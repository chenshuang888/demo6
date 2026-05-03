"""plugin_manager —— 扫描 / 加载 / 路由插件。

职责：
  1. 从两个目录扫 plugins/<name>/plugin.py
       - 仓库内置：tools/plugins/
       - 用户级：%APPDATA%/esp32_companion/plugins/  或  ~/.esp32_companion/plugins/
  2. importlib 加载，搜集 Plugin 子类，实例化，注入 bus/log/tx
  3. 路由消息：bridge 收到 JSON → dispatch_message → 按 bind_app 派发给插件
  4. 路由生命周期：on_connect / on_disconnect 广播给所有插件
  5. 收集带 GUI 的插件，给 gui/app.py 用

不做：
  - 修改老插件后 hot reload（importlib.reload 在复杂场景容易出问题）
  - 远程下载 / 在线市场
  - 沙箱权限隔离
"""

from __future__ import annotations

import asyncio
import importlib
import importlib.util
import inspect
import logging
import os
import sys
from pathlib import Path
from typing import Any, Callable, Optional

from .bus import EventBus
from .plugin_sdk import Plugin

logger = logging.getLogger(__name__)


def _user_plugins_dir() -> Path:
    """跨平台用户级插件目录。"""
    if sys.platform == "win32":
        base = os.environ.get("APPDATA")
        if base:
            return Path(base) / "esp32_companion" / "plugins"
    return Path.home() / ".esp32_companion" / "plugins"


def _builtin_plugins_dir() -> Path:
    """tools/plugins/ —— 仓库内置插件目录。

    定位规则：plugin_manager.py 在 tools/companion/，往上两级到 tools/plugins/。
    PyInstaller 打包后 sys._MEIPASS 是临时解压目录，同样按相对路径找。
    """
    if hasattr(sys, "_MEIPASS"):
        return Path(sys._MEIPASS) / "plugins"   # type: ignore[attr-defined]
    return Path(__file__).resolve().parent.parent / "plugins"


def _ensure_tools_in_syspath() -> None:
    """让插件能 from companion.X import ... —— tools/ 必须在 sys.path。"""
    tools_dir = str(Path(__file__).resolve().parent.parent)
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)


class PluginManager:
    def __init__(
        self,
        bus: EventBus,
        tx_func:        Callable[[str, str, Any], None],
        is_connected:   Callable[[], bool],
    ) -> None:
        self._bus = bus
        self._tx = tx_func
        self._is_connected = is_connected

        # discovered: plugin_id → Plugin instance
        self._plugins: dict[str, Plugin] = {}
        # 已加载的 module 名（防止重复 import）
        self._loaded_modules: set[str] = set()

    # ------------------------------------------------------------------
    # 发现 + 加载
    # ------------------------------------------------------------------

    def discover_and_load(self) -> int:
        """扫所有插件目录，加载新发现的；已加载的跳过。返回新加载的插件数。"""
        _ensure_tools_in_syspath()
        new_count = 0
        for root in [_builtin_plugins_dir(), _user_plugins_dir()]:
            if not root.is_dir():
                continue
            for entry in sorted(root.iterdir()):
                if not entry.is_dir():
                    continue
                plugin_py = entry / "plugin.py"
                if not plugin_py.is_file():
                    continue
                new_count += self._load_one(entry.name, plugin_py)
        return new_count

    def _load_one(self, dir_name: str, plugin_py: Path) -> int:
        """加载单个 plugin.py。返回新增的插件实例数（0 或 1+）。"""
        # 模块名用绝对路径生成，避免不同根下同名 dir 冲突
        mod_name = f"_companion_plugin_{dir_name}"
        if mod_name in self._loaded_modules:
            return 0

        try:
            spec = importlib.util.spec_from_file_location(mod_name, plugin_py)
            if spec is None or spec.loader is None:
                logger.warning("plugin %s: cannot create spec", dir_name)
                return 0
            module = importlib.util.module_from_spec(spec)
            # 让插件包内可以相对 import 同目录文件
            sys.modules[mod_name] = module
            # 保证 plugin.py 同目录在 sys.path（让 from .x import y 能用）
            plugin_dir = str(plugin_py.parent)
            if plugin_dir not in sys.path:
                sys.path.insert(0, plugin_dir)
            spec.loader.exec_module(module)
        except Exception as e:
            logger.exception("plugin %s: load failed: %s", dir_name, e)
            return 0

        self._loaded_modules.add(mod_name)

        # 收集 module 里所有 Plugin 子类（不含基类本身）
        added = 0
        for _, obj in inspect.getmembers(module, inspect.isclass):
            if not issubclass(obj, Plugin) or obj is Plugin:
                continue
            # 防止从其它包 import 进来的 Plugin 子类被重复实例化
            if obj.__module__ != mod_name:
                continue
            if not obj.plugin_id:
                logger.warning("plugin class %s missing plugin_id, skip",
                                obj.__name__)
                continue
            if obj.plugin_id in self._plugins:
                logger.warning("plugin_id %s already loaded, skip duplicate",
                                obj.plugin_id)
                continue
            try:
                inst = obj()
            except Exception as e:
                logger.exception("plugin %s: instantiate failed: %s",
                                  obj.plugin_id, e)
                continue
            # 平台注入
            inst.bus = self._bus
            inst.log = logging.getLogger(f"plugin.{obj.plugin_id}")
            inst._tx_to = self._tx
            inst._is_connected_fn = self._is_connected
            try:
                inst.on_load()
            except Exception as e:
                logger.exception("plugin %s: on_load failed: %s",
                                  obj.plugin_id, e)
                continue
            self._plugins[obj.plugin_id] = inst
            logger.info("plugin loaded: %s (%s)", obj.plugin_id,
                         obj.__name__)
            added += 1
        return added

    # ------------------------------------------------------------------
    # 调度（被 bridge / core 调用）
    # ------------------------------------------------------------------

    def dispatch_message(self, msg: dict) -> None:
        """bridge 收到 JSON 解码后调本方法。按 bind_app 派发。"""
        from_app = msg.get("from")
        for p in self._plugins.values():
            if p.bind_app and p.bind_app != from_app:
                continue
            # bind_app=None 收所有；bind_app=X 只收 from=X
            try:
                coro = p.on_message(msg)
                if asyncio.iscoroutine(coro):
                    asyncio.create_task(coro)
            except Exception as e:
                p.log.exception("on_message failed: %s", e)

    def dispatch_connect(self, addr: str) -> None:
        for p in self._plugins.values():
            try:
                coro = p.on_connect(addr)
                if asyncio.iscoroutine(coro):
                    asyncio.create_task(coro)
            except Exception as e:
                p.log.exception("on_connect failed: %s", e)

    def dispatch_disconnect(self) -> None:
        for p in self._plugins.values():
            try:
                coro = p.on_disconnect()
                if asyncio.iscoroutine(coro):
                    asyncio.create_task(coro)
            except Exception as e:
                p.log.exception("on_disconnect failed: %s", e)

    # ------------------------------------------------------------------
    # GUI 集成
    # ------------------------------------------------------------------

    def get_gui_pages(self) -> list[tuple[str, str, Plugin]]:
        """返回 [(plugin_id, title, plugin_instance), ...]，
        title 非空且 make_gui_page 返回非 None 的才算（实际是否返回 None 调用时才知道）。"""
        out = []
        for p in self._plugins.values():
            if p.title:
                out.append((p.plugin_id, p.title, p))
        return out

    def get_all(self) -> dict[str, Plugin]:
        return dict(self._plugins)

    # ------------------------------------------------------------------
    # 卸载（退出时清理）
    # ------------------------------------------------------------------

    def unload_all(self) -> None:
        for p in list(self._plugins.values()):
            try:
                p._cancel_all_tasks()
                p.on_unload()
            except Exception as e:
                logger.warning("plugin %s on_unload: %s", p.plugin_id, e)
        self._plugins.clear()
