"""media_provider —— SMTC 监听 + 写 8a5c0008 + 媒体键 NOTIFY 8a5c000d。

ESP 端屏上"上一首/播放暂停/下一首"按钮 → media_button_event_t (4B)
PC 收到后调 winsdk 模拟键盘媒体键。

每 10s 兜底全量同步一次（防 winsdk 事件遗漏）。
"""

from __future__ import annotations

import asyncio
import logging
import struct
import sys

from ..constants import (
    MEDIA_BTN_STRUCT, MEDIA_BUTTON_CHAR_UUID, MEDIA_CHAR_UUID,
    MEDIA_PERIODIC_RESYNC_S,
)
from ..shared.packers import EMPTY_MEDIA_PAYLOAD, pack_media
from ..shared.smtc import MediaState, SmtcMonitor, send_media_key
from .base import Provider, ProviderContext

logger = logging.getLogger(__name__)


MEDIA_BTN_NAMES = {0: "prev", 1: "playpause", 2: "next"}


class MediaProvider(Provider):
    name = "media"

    def __init__(self) -> None:
        self._unsubs: list = []
        self._monitor: SmtcMonitor | None = None
        self._last_payload: bytes | None = None
        self._last_btn_seq: int | None = None
        self._resync_task: asyncio.Task | None = None
        self._push_lock = asyncio.Lock()

    def subscriptions(self) -> list[str]:
        return [MEDIA_BUTTON_CHAR_UUID]

    async def on_start(self, ctx: ProviderContext) -> None:
        def _on_btn(payload: object) -> None:
            data = payload if isinstance(payload, (bytes, bytearray)) else b""
            self._handle_button(ctx, bytes(data))
        self._unsubs.append(
            ctx.bus.on(f"notify:{MEDIA_BUTTON_CHAR_UUID.lower()}", _on_btn))

        if sys.platform == "win32":
            loop = asyncio.get_running_loop()
            async def _on_change(state: MediaState) -> None:
                await self._push(ctx, state)
            self._monitor = SmtcMonitor(_on_change, loop)
            try:
                await self._monitor.start()
            except Exception as e:
                ctx.bus.emit("log", ("warn", self.name, f"smtc start: {e}"))
        else:
            ctx.bus.emit("log", ("info", self.name, "smtc unavailable; empty payload"))
            try:
                await ctx.write(MEDIA_CHAR_UUID, EMPTY_MEDIA_PAYLOAD, response=True)
            except Exception:
                pass

        self._resync_task = asyncio.create_task(self._resync_loop(ctx))

    async def on_stop(self, ctx: ProviderContext) -> None:
        for u in self._unsubs:
            try: u()
            except Exception: pass
        self._unsubs.clear()
        if self._resync_task:
            self._resync_task.cancel()
            try: await self._resync_task
            except (asyncio.CancelledError, Exception): pass
            self._resync_task = None
        if self._monitor is not None:
            try: await self._monitor.stop()
            except Exception: pass
            self._monitor = None

    async def _resync_loop(self, ctx: ProviderContext) -> None:
        while True:
            try:
                await asyncio.sleep(MEDIA_PERIODIC_RESYNC_S)
            except asyncio.CancelledError:
                return
            if ctx.quiesce_during_upload():
                continue
            if self._monitor is None:
                continue
            try:
                state = await self._monitor.fetch_state()
                await self._push(ctx, state)
            except Exception as e:
                ctx.bus.emit("log", ("warn", self.name, f"resync: {e}"))

    async def _push(self, ctx: ProviderContext, state: MediaState) -> None:
        if ctx.quiesce_during_upload():
            return
        async with self._push_lock:
            data = pack_media(state.playing, state.position_sec, state.duration_sec,
                               state.title, state.artist)
            if data == self._last_payload:
                return
            try:
                await ctx.write(MEDIA_CHAR_UUID, data, response=True)
                self._last_payload = data
                ctx.bus.emit("log", ("info", self.name,
                    f"{'PLAY' if state.playing else 'PAUSE'} \"{state.title}\""))
            except Exception as e:
                ctx.bus.emit("log", ("warn", self.name, f"push: {e}"))

    def _handle_button(self, ctx: ProviderContext, data: bytes) -> None:
        if len(data) != struct.calcsize(MEDIA_BTN_STRUCT):
            return
        btn_id, action, seq = struct.unpack(MEDIA_BTN_STRUCT, data)
        if action != 0:
            return
        if seq == self._last_btn_seq:
            return
        self._last_btn_seq = seq
        name = MEDIA_BTN_NAMES.get(btn_id)
        if name is None:
            return
        ctx.bus.emit("log", ("info", self.name, f"btn {name} seq={seq}"))
        try:
            send_media_key(name)
        except Exception as e:
            ctx.bus.emit("log", ("warn", self.name, f"key: {e}"))
