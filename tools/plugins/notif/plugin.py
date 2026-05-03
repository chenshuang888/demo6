"""notif plugin —— 把 Windows 系统通知 push 给动态 notif_pkg。

bind_app="notif_pkg"：notif_pkg 自己不发消息（消费者纯接收），所以 on_message 不实现。
本插件主要在 on_connect 时启动 WinNotificationMonitor，每条新通知 → tx('add', body)。

私有 helper：
  win_notifications.py（同目录）—— 原 companion/shared/win_notifications.py
"""

from __future__ import annotations

import asyncio
import sys
import time
from typing import Optional

from companion.plugin_sdk import Plugin


class NotifPlugin(Plugin):
    plugin_id = "notif"
    title     = ""
    bind_app  = "notif_pkg"

    def __init__(self) -> None:
        super().__init__()
        self._mon = None    # type: Optional[object]

    async def on_connect(self, addr: str) -> None:
        if sys.platform != "win32":
            return
        if self._mon is not None:
            return
        # 相对 import：plugin 目录已在 sys.path（plugin_manager 加进去的）
        from win_notifications import WinNotificationMonitor

        loop = asyncio.get_running_loop()

        async def _on_change(item: dict) -> None:
            self.log.info("notif/add app=%r title=%r",
                           item.get("app"), item.get("title"))
            self.tx("add", body={
                "title": (item.get("title") or "")[:31],
                "body":  (item.get("body")  or "")[:95],
                "ts":    int(item.get("ts") or time.time()),
                "cat":   item.get("cat") or "msg",
            })

        self._mon = WinNotificationMonitor(_on_change, loop)
        try:
            await self._mon.start()
            self.log.info("win notifications watcher started")
        except Exception as e:
            self.log.warning("winnotif start: %s", e)
            self._mon = None

    async def on_disconnect(self) -> None:
        if self._mon is None:
            return
        try:
            await self._mon.stop()
        except Exception:
            pass
        self._mon = None
