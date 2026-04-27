"""media_provider —— 给 music.js 提供 SMTC 状态 + 接收按钮命令。

依赖（仅 Windows）:
    pip install winsdk

协议:
  PC ─→ ESP   {to: "music", type: "state", body: {playing, position, duration,
                                                   title, artist, ts}}
                按需推送（曲目变化、播放/暂停切换、用户拖拽进度）
  PC ─→ ESP   {to: "music", type: "no_session"}     当无任何媒体会话时
  PC ←─ ESP   {to: "music", type: "btn", body: {id: "prev"|"play"|"next"}}
                ESP 端用户按按钮时上报，PC 收到后调 Windows 媒体键

body 字段:
  playing   bool      当前是否在播放
  position  int       当前进度秒；不可用时给 0
  duration  int       总时长秒；直播 / 不可用时给 0
  title     string    曲目名（utf-8）
  artist    string    艺术家
  ts        int       PC 采样时刻 unix sec（JS 端用来插值）

进度条:
  PC 不必每秒推。JS 收到 state 后用 sys.time.uptimeMs() 插值。
  曲目/play_pause/seek 时 PC 主动 push 一次即可。
"""

from __future__ import annotations

import asyncio
import ctypes
import logging
import sys
import time
from typing import Any, Optional

from dynapp_sdk import DynappClient

logger = logging.getLogger(__name__)

# ----------------------------------------------------------------------------
# Windows-only：winsdk + 模拟媒体键
# ----------------------------------------------------------------------------

_WIN = sys.platform == "win32"

try:
    if _WIN:
        from winsdk.windows.media.control import (
            GlobalSystemMediaTransportControlsSessionManager as SessionManager,
            GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
        )
    else:
        SessionManager = None
        PlaybackStatus = None
except ImportError:
    logger.warning("winsdk 未安装；media provider 将禁用。pip install winsdk")
    SessionManager = None
    PlaybackStatus = None


VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PLAY_PAUSE = 0xB3
KEYEVENTF_KEYUP     = 0x0002


def _send_media_key(vk: int) -> None:
    if not _WIN:
        logger.warning("media key only on windows")
        return
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)


_BTN_TO_VK = {
    "prev": VK_MEDIA_PREV_TRACK,
    "play": VK_MEDIA_PLAY_PAUSE,
    "next": VK_MEDIA_NEXT_TRACK,
}


# ----------------------------------------------------------------------------
# SMTC 监听 + 推送
# ----------------------------------------------------------------------------

def _seconds_from_timedelta(td) -> int:
    if td is None:
        return 0
    try:
        # winsdk 的 TimeSpan 暴露 .duration（100ns 单位）
        return int(getattr(td, "duration", 0) // 10_000_000)
    except Exception:
        return 0


async def _build_state(session) -> dict[str, Any]:
    """读 SMTC session → 拼成 JSON state body。"""
    if session is None:
        return {
            "playing": False, "position": 0, "duration": 0,
            "title": "", "artist": "", "ts": int(time.time()),
        }
    try:
        props = await session.try_get_media_properties_async()
    except Exception as e:
        logger.debug("media props fetch failed: %s", e)
        return {
            "playing": False, "position": 0, "duration": 0,
            "title": "", "artist": "", "ts": int(time.time()),
        }
    info = session.get_playback_info()
    tl   = session.get_timeline_properties()

    playing = bool(info.playback_status == PlaybackStatus.PLAYING) if PlaybackStatus else False
    pos = max(0, _seconds_from_timedelta(getattr(tl, "position", None)))
    dur = max(0, _seconds_from_timedelta(getattr(tl, "end_time", None)))

    return {
        "playing":  playing,
        "position": pos,
        "duration": dur,
        "title":    (getattr(props, "title", "")  or "")[:48],
        "artist":   (getattr(props, "artist", "") or "")[:32],
        "ts":       int(time.time()),
    }


class _MediaWatcher:
    """订阅 SMTC 事件 → 状态变化时通过 client 推 JSON。"""

    def __init__(self, client: DynappClient, loop: asyncio.AbstractEventLoop):
        self._client = client
        self._loop = loop
        self._mgr = None
        self._session = None
        self._tokens: list[tuple[object, str, int]] = []
        self._mgr_token: Optional[int] = None
        self._push_lock = asyncio.Lock()
        self._last_state: Optional[dict[str, Any]] = None

    async def start(self) -> None:
        if SessionManager is None:
            logger.warning("media: SessionManager 不可用（winsdk 缺失或非 win），watcher 不启动")
            return
        self._mgr = await SessionManager.request_async()
        self._mgr_token = self._mgr.add_current_session_changed(self._on_sess_changed)
        await self._rebind()

    def _on_sess_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self._rebind(), self._loop)

    def _on_any_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self._push(), self._loop)

    def _unbind(self) -> None:
        for obj, ev, tok in self._tokens:
            try: getattr(obj, f"remove_{ev}")(tok)
            except Exception: pass
        self._tokens.clear()

    async def _rebind(self) -> None:
        self._unbind()
        self._session = self._mgr.get_current_session()
        if self._session is not None:
            for ev in ("media_properties_changed",
                       "playback_info_changed",
                       "timeline_properties_changed"):
                try:
                    add = getattr(self._session, f"add_{ev}")
                    self._tokens.append((self._session, ev, add(self._on_any_changed)))
                except Exception as e:
                    logger.debug("subscribe %s failed: %s", ev, e)
            logger.info("media: bound session %s",
                        getattr(self._session, "source_app_user_model_id", "?"))
        else:
            logger.info("media: no session (nothing playing)")
        await self._push(force=True)

    async def _push(self, force: bool = False) -> None:
        async with self._push_lock:
            if self._session is None:
                await self._client.send("music", "no_session")
                self._last_state = None
                return
            state = await _build_state(self._session)
            # 去重：title/artist/playing/duration 相同且 position 差 < 2s 时跳过
            if not force and self._is_dup(state):
                return
            await self._client.send("music", "state", body=state)
            self._last_state = state
            logger.debug("media: pushed %s",
                         {k: state[k] for k in ("playing", "position", "duration")})

    def _is_dup(self, s: dict[str, Any]) -> bool:
        last = self._last_state
        if not last:
            return False
        for k in ("playing", "duration", "title", "artist"):
            if last.get(k) != s.get(k):
                return False
        # position 小幅变化不算新事件（避免 winsdk 频繁触发）
        if abs(int(last.get("position", 0)) - int(s.get("position", 0))) > 1:
            return False
        return True


# ----------------------------------------------------------------------------
# 注册到 client
# ----------------------------------------------------------------------------

def register_media(client: DynappClient) -> None:
    loop = asyncio.get_event_loop()
    watcher = _MediaWatcher(client, loop)

    @client.on("music", "btn")
    async def _on_btn(msg: dict) -> None:
        body = msg.get("body") or {}
        btn = body.get("id")
        vk = _BTN_TO_VK.get(btn)
        if vk is None:
            logger.warning("music: unknown btn id=%r", btn)
            return
        _send_media_key(vk)
        logger.info("music: pressed %s", btn)
        # 略等一会让 SMTC 状态更新，再推一次
        await asyncio.sleep(0.3)
        await watcher._push(force=True)

    # 收到 ESP 主动 req → 立即推一次（用户进 app 时主动取）
    @client.on("music", "req")
    async def _on_req(_msg: dict) -> None:
        await watcher._push(force=True)

    # 后台启动 watcher
    asyncio.create_task(watcher.start())
