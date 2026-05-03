"""主窗口 + 侧边栏 + page 路由 + 关闭拦截。"""

from __future__ import annotations

import logging
from typing import Optional

import customtkinter as ctk

from .. import config as cfg
from ..bus import EventBus
from ..runner import AsyncRunner
from .pages.home import HomePage
from .pages.log import LogPage
from .pages.music import MusicPage
from .pages.notify import NotifyPage
from .pages.upload import UploadPage
from .theme import (
    COLOR_ACCENT, COLOR_BG, COLOR_MUTED, COLOR_PANEL, COLOR_PANEL_HI,
    COLOR_SIDEBAR, COLOR_TEXT, CTK_APPEARANCE, CTK_THEME, SIDEBAR_W,
    WINDOW_H, WINDOW_W, enable_windows_dpi_awareness,
)
from .widgets import StatusDot

logger = logging.getLogger(__name__)


PAGE_DEFS = [
    ("home",   "首页",   HomePage),
    ("music",  "音乐",   MusicPage),
    ("upload", "上传",   UploadPage),
    ("notify", "通知",   NotifyPage),
    ("log",    "日志",   LogPage),
]


class CompanionApp:
    def __init__(self, bus: EventBus, runner: AsyncRunner,
                  cfg_data: dict, on_quit_request,
                  plugin_manager=None) -> None:
        self.bus = bus
        self.runner = runner
        self.cfg = cfg_data
        self._on_quit_request = on_quit_request
        self._pm = plugin_manager

        enable_windows_dpi_awareness()
        ctk.set_appearance_mode(CTK_APPEARANCE)
        ctk.set_default_color_theme(CTK_THEME)

        self.root = ctk.CTk()
        self.root.title("ESP32 Companion")
        self.root.geometry(f"{WINDOW_W}x{WINDOW_H}")
        self.root.configure(fg_color=COLOR_BG)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self._pages: dict[str, ctk.CTkFrame] = {}
        self._buttons: dict[str, ctk.CTkButton] = {}
        self._build()
        self._show("home")

        bus.on("connect",    lambda a: self.root.after(0, self._update_conn, True, a))
        bus.on("disconnect", lambda _: self.root.after(0, self._update_conn, False, ""))

    # ------------------------------------------------------------------
    # build
    # ------------------------------------------------------------------

    def _collect_page_defs(self) -> list[tuple[str, str, object]]:
        """合并内置 + 插件页。第三个元素是 callable (master, app) → frame。"""
        out: list[tuple[str, str, object]] = []
        for key, label, cls in PAGE_DEFS:
            out.append((key, label, cls))
        if self._pm is None:
            return out
        for pid, title, plugin in self._pm.get_gui_pages():
            def make(master, app, p=plugin):
                return p.make_gui_page(master, app)
            out.append((pid, title, make))
        return out

    def _build(self) -> None:
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_rowconfigure(0, weight=1)

        # 侧边栏
        side = ctk.CTkFrame(self.root, fg_color=COLOR_SIDEBAR,
                              width=SIDEBAR_W, corner_radius=0)
        side.grid(row=0, column=0, sticky="nsw")
        side.grid_propagate(False)
        side.grid_rowconfigure(99, weight=1)
        self._sidebar = side

        ctk.CTkLabel(side, text="Companion",
                      text_color=COLOR_ACCENT,
                      font=ctk.CTkFont(size=20, weight="bold")) \
            .grid(row=0, column=0, padx=20, pady=(20, 4), sticky="w")
        ctk.CTkLabel(side, text="ESP32 桌面伴侣",
                      text_color=COLOR_MUTED,
                      font=ctk.CTkFont(size=11)) \
            .grid(row=1, column=0, padx=20, pady=(0, 16), sticky="w")

        # 内容区（先建好，给 _populate 用）
        self._content = ctk.CTkFrame(self.root, fg_color=COLOR_BG, corner_radius=0)
        self._content.grid(row=0, column=1, sticky="nsew")
        self._content.grid_rowconfigure(0, weight=1)
        self._content.grid_columnconfigure(0, weight=1)

        # 底部连接状态
        self._side_status = StatusDot(side, text="未连接")
        self._side_status.grid(row=98, column=0, padx=20, pady=(8, 4), sticky="sw")

        # 刷新插件按钮（在状态点上方）
        self._refresh_btn = ctk.CTkButton(side, text="🔄 刷新插件",
                                           fg_color="transparent",
                                           hover_color=COLOR_PANEL_HI,
                                           text_color=COLOR_MUTED,
                                           corner_radius=8, height=28,
                                           font=ctk.CTkFont(size=11),
                                           command=self._on_refresh_plugins)
        self._refresh_btn.grid(row=97, column=0, padx=12, pady=(2, 0), sticky="ew")

        self._populate_pages()

    def _populate_pages(self) -> None:
        """根据 _collect_page_defs 填充侧边栏按钮 + content 页实例。"""
        # 先清掉旧按钮 + 旧 page
        for btn in self._buttons.values():
            try: btn.destroy()
            except Exception: pass
        self._buttons.clear()
        for page in self._pages.values():
            try:
                page.grid_forget()
                page.destroy()
            except Exception: pass
        self._pages.clear()

        for i, (key, label, factory) in enumerate(self._collect_page_defs()):
            btn = ctk.CTkButton(self._sidebar, text=label, anchor="w",
                                  fg_color="transparent",
                                  hover_color=COLOR_PANEL_HI,
                                  text_color=COLOR_TEXT,
                                  corner_radius=8, height=36,
                                  command=lambda k=key: self._show(k))
            btn.grid(row=2 + i, column=0, padx=12, pady=4, sticky="ew")
            self._buttons[key] = btn

            try:
                page = factory(self._content, self)
            except Exception as e:
                logger.warning("page %s build failed: %s", key, e)
                continue
            self._pages[key] = page

    def _on_refresh_plugins(self) -> None:
        if self._pm is None:
            return
        n = self._pm.discover_and_load()
        # 重新连接生命周期事件（对新插件的 on_connect 现在是 stale 的——
        # 当前已连接时，新插件错过了 connect。简单处理：让它们至少看到一次 connect）
        if n > 0:
            self._populate_pages()
            current = self._side_status.cget("text") if hasattr(self._side_status, "cget") else ""
            self.bus.emit("log", ("info", "gui", f"loaded {n} new plugin(s)"))
        else:
            self.bus.emit("log", ("info", "gui", "no new plugins found"))

    def _show(self, key: str) -> None:
        for k, page in self._pages.items():
            if k == key:
                page.grid(row=0, column=0, sticky="nsew")
            else:
                page.grid_forget()
        for k, btn in self._buttons.items():
            btn.configure(fg_color=COLOR_PANEL if k == key else "transparent")

    def _update_conn(self, ok: bool, addr: str) -> None:
        if ok:
            self._side_status.set(f"已连接 {addr[:12]}", COLOR_ACCENT)
        else:
            self._side_status.set("未连接")

    # ------------------------------------------------------------------
    # lifecycle
    # ------------------------------------------------------------------

    def _on_close(self) -> None:
        # GUI 关闭后核心继续：仅 withdraw + 触发可选托盘
        self.root.withdraw()
        self._on_quit_request("hide")

    def show_window(self) -> None:
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def quit_app(self) -> None:
        try:
            self.root.destroy()
        except Exception:
            pass

    def mainloop(self) -> None:
        self.root.mainloop()
