"""music_proxy plugin —— 把 Windows SMTC 媒体状态推给动态 music app。

通用服务（bind_app=None）：任何动态 app 想看 PC 端正在播什么，发 from=music/type=req
即可拿到 state；想控制播放发 from=music/type=btn body={id:"play"/"pause"/"next"/"prev"}。

只在 Windows 平台有效。其它平台静默 no-op。
"""

from __future__ import annotations

import asyncio
import sys
import time
from typing import Optional

from companion.plugin_sdk import Plugin


class MusicProxyPlugin(Plugin):
    plugin_id = "music_proxy"
    title     = ""
    bind_app  = None

    def __init__(self) -> None:
        super().__init__()
        self._smtc = None  # type: Optional[object]

    async def on_connect(self, addr: str) -> None:
        if sys.platform != "win32":
            return
        if self._smtc is not None:
            return
        from companion.shared.smtc import MediaState, SmtcMonitor

        loop = asyncio.get_running_loop()

        async def _on_change(state: MediaState) -> None:
            self.tx_to("music", "state", body=self._media_to_body(state))

        self._smtc = SmtcMonitor(_on_change, loop)
        try:
            await self._smtc.start()
            self.log.info("smtc monitor started")
        except Exception as e:
            self.log.warning("smtc start: %s", e)
            self._smtc = None

    async def on_disconnect(self) -> None:
        if self._smtc is None:
            return
        try:
            await self._smtc.stop()
        except Exception:
            pass
        self._smtc = None

    async def on_message(self, msg: dict) -> None:
        if msg.get("from") != "music":
            return
        mtype = msg.get("type")
        if mtype == "req":
            await self._handle_req()
        elif mtype == "btn":
            self._handle_btn(msg)

    async def _handle_req(self) -> None:
        if self._smtc is None:
            self.tx_to("music", "no_session")
            return
        state = await self._smtc.fetch_state()
        if not state.title and not state.artist:
            self.tx_to("music", "no_session")
            return
        self.tx_to("music", "state", body=self._media_to_body(state))

    def _handle_btn(self, msg: dict) -> None:
        from companion.shared.smtc import send_media_key
        body = msg.get("body") if isinstance(msg.get("body"), dict) else {}
        action = body.get("id") or body.get("action") or ""
        try:
            send_media_key(action)
            self.log.info("media key %s", action)
        except Exception as e:
            self.log.warning("media key: %s", e)

    @staticmethod
    def _media_to_body(state) -> dict:
        return {
            "playing":  bool(state.playing),
            "position": max(0, state.position_sec),
            "duration": max(0, state.duration_sec),
            "title":    (state.title or "")[:48],
            "artist":   (state.artist or "")[:32],
            "ts":       int(time.time()),
        }
