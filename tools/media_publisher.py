"""ESP32 Now Playing 推送器。

订阅 Windows 当前媒体会话（Spotify / 网易云 / 浏览器 YouTube 等），
把曲目信息通过 BLE Write 到 ESP32 的 media characteristic：

  Service UUID: 8a5c0007-0000-4aef-b87e-4fa1e0c7e0f6
  Char UUID:    8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6 (WRITE)

推送时机:
  - 当前会话变化（CurrentSessionChanged）
  - 曲目属性变化（MediaPropertiesChanged）
  - 播放状态变化（PlaybackInfoChanged）
  - 时间轴属性变化（TimelinePropertiesChanged）
  - 兜底：每 10 秒全量同步一次

依赖:
    pip install -r requirements.txt   # 含 bleak 和 winsdk

运行:
    python media_publisher.py

Ctrl+C 退出。连接断开会自动重连。
"""

import argparse
import asyncio
import struct
import sys
import time as _time

from bleak import BleakClient, BleakScanner

try:
    from winsdk.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as SessionManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
    )
except ImportError as exc:  # pragma: no cover
    print("winsdk 未安装或不在 Windows 上：pip install winsdk", file=sys.stderr)
    raise

# ---------------------------------------------------------------------------
# 常量（与 services/media_manager.h 严格对齐）
# ---------------------------------------------------------------------------

DEVICE_NAME     = "ESP32-S3-DEMO"
MEDIA_CHAR_UUID = "8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6"

# media_payload_t: B playing, B _reserved, h position, h duration,
#                  H _pad, I sample_ts, 48s title, 32s artist  → 92 B
PAYLOAD_STRUCT = "<BBhhHI48s32s"
PAYLOAD_SIZE   = struct.calcsize(PAYLOAD_STRUCT)   # = 92

SCAN_TIMEOUT_S      = 10.0
RECONNECT_DELAY_S   = 3.0
PERIODIC_SYNC_S     = 10.0   # 兜底全量同步间隔

TITLE_MAX_BYTES  = 47         # 48 字段留 1 字节给 \0
ARTIST_MAX_BYTES = 31         # 32 字段留 1 字节给 \0


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"{_time.strftime('%H:%M:%S')}  {msg}", flush=True)


def _utf8_fixed(s: str, max_body_bytes: int, total_bytes: int) -> bytes:
    """UTF-8 编码，按字节截断到 max_body_bytes，末尾 \\0 填充到 total_bytes。"""
    b = (s or "").encode("utf-8", errors="replace")
    if len(b) > max_body_bytes:
        b = b[:max_body_bytes]
        # 回退到完整 UTF-8 边界
        while b and (b[-1] & 0xC0) == 0x80:
            b = b[:-1]
    return b.ljust(total_bytes, b"\0")


def _seconds_from_timedelta(td) -> int:
    """WinRT TimeSpan → int 秒；异常/零值返回 -1 以表示未知。"""
    if td is None:
        return -1
    try:
        s = int(td.total_seconds())
    except Exception:
        return -1
    return s if s >= 0 else -1


# ---------------------------------------------------------------------------
# Payload 构造
# ---------------------------------------------------------------------------

EMPTY_PAYLOAD = struct.pack(
    PAYLOAD_STRUCT,
    0, 0, -1, -1, 0, 0,
    b"".ljust(48, b"\0"),
    b"".ljust(32, b"\0"),
)


async def build_payload(session) -> bytes:
    """读取当前 session 的属性，打包成 92 B bytes。session 为 None → 空 payload。"""
    if session is None:
        return EMPTY_PAYLOAD

    try:
        props = await session.try_get_media_properties_async()
    except Exception as e:
        log(f"[warn] get_media_properties_async failed: {e}")
        return EMPTY_PAYLOAD

    info = session.get_playback_info()
    tl   = session.get_timeline_properties()

    playing  = 1 if info.playback_status == PlaybackStatus.PLAYING else 0
    position = _seconds_from_timedelta(getattr(tl, "position", None))

    # end_time 有时会是 0（未加载），同样视为未知
    duration = _seconds_from_timedelta(getattr(tl, "end_time", None))
    if duration == 0:
        duration = -1

    title_bytes  = _utf8_fixed(getattr(props, "title", "")  or "", TITLE_MAX_BYTES, 48)
    artist_bytes = _utf8_fixed(getattr(props, "artist", "") or "", ARTIST_MAX_BYTES, 32)

    return struct.pack(
        PAYLOAD_STRUCT,
        playing, 0,
        max(-1, min(position, 32767)),
        max(-1, min(duration, 32767)),
        0, int(_time.time()),
        title_bytes, artist_bytes,
    )


# ---------------------------------------------------------------------------
# Publisher
# ---------------------------------------------------------------------------

class Publisher:
    """订阅 winsdk 媒体事件 + 维护当前 session + BLE write。"""

    def __init__(self, client: BleakClient, loop: asyncio.AbstractEventLoop):
        self._client = client
        self._loop = loop
        self._mgr = None
        self._session = None
        self._session_tokens: list[tuple[object, str, int]] = []
        self._mgr_token: int | None = None
        self._push_lock = asyncio.Lock()

    async def start(self) -> None:
        self._mgr = await SessionManager.request_async()
        # 注册 session 切换事件
        self._mgr_token = self._mgr.add_current_session_changed(self._on_session_changed)
        await self._rebind_session()

    async def stop(self) -> None:
        self._unbind_session_events()
        if self._mgr is not None and self._mgr_token is not None:
            try:
                self._mgr.remove_current_session_changed(self._mgr_token)
            except Exception:
                pass

    # ---- session 生命周期 ----

    def _unbind_session_events(self) -> None:
        for obj, event, token in self._session_tokens:
            try:
                getattr(obj, f"remove_{event}")(token)
            except Exception:
                pass
        self._session_tokens.clear()

    async def _rebind_session(self) -> None:
        self._unbind_session_events()
        self._session = self._mgr.get_current_session()

        if self._session is not None:
            name = getattr(self._session, "source_app_user_model_id", "?")
            log(f"session bound: {name}")
            for event_name in (
                "media_properties_changed",
                "playback_info_changed",
                "timeline_properties_changed",
            ):
                try:
                    add_fn = getattr(self._session, f"add_{event_name}")
                    token = add_fn(self._on_any_changed)
                    self._session_tokens.append((self._session, event_name, token))
                except Exception as e:
                    log(f"[warn] subscribe {event_name} failed: {e}")
        else:
            log("session bound: None (nothing playing)")

        await self.push_now()

    # ---- COM 线程回调 → asyncio ----

    def _on_session_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self._rebind_session(), self._loop)

    def _on_any_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self.push_now(), self._loop)

    # ---- 实际推送 ----

    async def push_now(self) -> None:
        async with self._push_lock:
            try:
                data = await build_payload(self._session)
                await self._client.write_gatt_char(MEDIA_CHAR_UUID, data, response=True)
            except Exception as e:
                log(f"[err] push failed: {e}")
                return

            # 简单解析一下 log 内容便于调试
            playing, _, pos, dur, _, _ = struct.unpack("<BBhhHI", data[:12])
            title  = data[12:12+48].split(b"\0", 1)[0].decode("utf-8", errors="replace")
            artist = data[12+48:12+48+32].split(b"\0", 1)[0].decode("utf-8", errors="replace")
            log(f"[push] {'PLAY' if playing else 'PAUSE'} "
                f"{pos}s/{dur}s  \"{title}\" - \"{artist}\"")


# ---------------------------------------------------------------------------
# 连接生命周期
# ---------------------------------------------------------------------------

async def run_session() -> None:
    log(f"扫描 {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"{SCAN_TIMEOUT_S:.0f}s 内未发现 {DEVICE_NAME}")

    log(f"命中 {device.address}，连接中...")
    async with BleakClient(device) as client:
        loop = asyncio.get_running_loop()
        pub = Publisher(client, loop)
        await pub.start()
        log(f"已就绪，监听媒体事件 + 每 {PERIODIC_SYNC_S:.0f}s 兜底同步\n")
        try:
            while client.is_connected:
                await asyncio.sleep(PERIODIC_SYNC_S)
                await pub.push_now()
        finally:
            await pub.stop()
        log("连接断开")


async def main() -> None:
    while True:
        try:
            await run_session()
        except asyncio.CancelledError:
            return
        except Exception as e:
            log(f"会话结束: {e}")
        log(f"{RECONNECT_DELAY_S:.0f}s 后重试\n")
        await asyncio.sleep(RECONNECT_DELAY_S)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    return p.parse_args()


if __name__ == "__main__":
    if sys.platform != "win32":
        print("windows only（winsdk 仅支持 Windows）", file=sys.stderr)
        sys.exit(1)
    parse_args()
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nbye")
