"""ESP32 桌面伴侣 —— 音乐副屏 + 控制面板合并版。

单一进程连接 ESP32 BLE，在同一 BleakClient 上同时：
  - 订阅 control_char notify（屏上按钮 → Windows 动作）
      id=0 Lock / id=1 Mute / id=2 Prev / id=3 Next / id=4 PlayPause
  - 订阅 Windows 媒体会话 + 写 media_char
      (title / artist / playing / position / duration) → ESP 屏

等价于 control_panel_client.py + media_publisher.py 的功能，合并避免
两个进程抢 BLE 连接导致的不稳定。

依赖:
    pip install -r requirements.txt

运行:
    python desktop_companion.py              # 实际执行 Windows 动作
    python desktop_companion.py --dry-run    # 只打印事件，不触发系统动作

Ctrl+C 退出。连接断开 3s 后自动重连。
"""

import argparse
import asyncio
import ctypes
import struct
import sys
import time as _time

from bleak import BleakClient, BleakScanner

try:
    from winsdk.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as SessionManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
    )
except ImportError:
    print("winsdk 未安装或不在 Windows 上：pip install winsdk", file=sys.stderr)
    raise


# ---------------------------------------------------------------------------
# 常量（与固件端严格对齐）
# ---------------------------------------------------------------------------

DEVICE_NAME         = "ESP32-S3-DEMO"
CONTROL_CHAR_UUID   = "8a5c0006-0000-4aef-b87e-4fa1e0c7e0f6"   # Notify (ESP → PC)
MEDIA_CHAR_UUID     = "8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP)

# control_event_t: type u8, id u8, action u8, _r u8, value i16, seq u16  → 8 B
EVENT_STRUCT = "<BBBBhH"
EVENT_SIZE   = struct.calcsize(EVENT_STRUCT)

# media_payload_t: playing u8, _r u8, pos i16, dur i16, _pad u16,
#                  sample_ts u32, title 48s, artist 32s  → 92 B
PAYLOAD_STRUCT = "<BBhhHI48s32s"
PAYLOAD_SIZE   = struct.calcsize(PAYLOAD_STRUCT)

SCAN_TIMEOUT_S    = 10.0
RECONNECT_DELAY_S = 3.0
PERIODIC_SYNC_S   = 10.0

TITLE_MAX_BYTES  = 47
ARTIST_MAX_BYTES = 31


# ---------------------------------------------------------------------------
# 通用工具
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"{_time.strftime('%H:%M:%S')}  {msg}", flush=True)


# ---------------------------------------------------------------------------
# Windows 动作（接收屏上按钮事件后调用）
# ---------------------------------------------------------------------------

VK_VOLUME_MUTE      = 0xAD
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PLAY_PAUSE = 0xB3
KEYEVENTF_KEYUP     = 0x0002


def _send_key(vk: int) -> None:
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)


def _lock_screen() -> None:
    ctypes.windll.user32.LockWorkStation()


BUTTON_ACTIONS = {
    0: ("Lock",      _lock_screen),
    1: ("Mute",      lambda: _send_key(VK_VOLUME_MUTE)),
    2: ("Prev",      lambda: _send_key(VK_MEDIA_PREV_TRACK)),
    3: ("Next",      lambda: _send_key(VK_MEDIA_NEXT_TRACK)),
    4: ("PlayPause", lambda: _send_key(VK_MEDIA_PLAY_PAUSE)),
}


class ControlHandler:
    """解析 control_char 的 8 字节事件，做去重 + 执行动作。"""

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self._last_seq: int | None = None

    def on_notify(self, _handle: int, data: bytearray) -> None:
        if len(data) != EVENT_SIZE:
            log(f"[warn] ctrl 长度异常 {len(data)}B: {bytes(data).hex()}")
            return

        type_, btn_id, action, _r, value, seq = struct.unpack(
            EVENT_STRUCT, bytes(data))

        if type_ != 0 or action != 0:
            log(f"[info] 忽略 ctrl type={type_} action={action} id={btn_id} seq={seq}")
            return

        if seq == self._last_seq:
            log(f"[dup] ctrl seq={seq} 重复，忽略")
            return
        self._last_seq = seq

        entry = BUTTON_ACTIONS.get(btn_id)
        if entry is None:
            log(f"[warn] ctrl 未知 id={btn_id} seq={seq}")
            return

        name, fn = entry
        prefix = "[dry]" if self.dry_run else "[exec]"
        log(f"{prefix} ctrl id={btn_id} seq={seq}  → {name}")

        if self.dry_run:
            return
        try:
            fn()
        except Exception as e:
            log(f"[err] ctrl 动作失败: {e}")


# ---------------------------------------------------------------------------
# Media publisher（winsdk 事件 + 写 media_char）
# ---------------------------------------------------------------------------

def _utf8_fixed(s: str, max_body: int, total: int) -> bytes:
    """UTF-8 编码；超长时回退到最近的合法字符边界，避免截半个多字节字符。"""
    b = (s or "").encode("utf-8", errors="replace")
    if len(b) > max_body:
        b = b[:max_body]
        # 往回找到能完整解码的边界（最多退 3 字节，UTF-8 最长 4 字节字符）
        while b:
            try:
                b.decode("utf-8")
                break
            except UnicodeDecodeError:
                b = b[:-1]
    return b.ljust(total, b"\0")


def _seconds_from_timedelta(td) -> int:
    if td is None:
        return -1
    try:
        s = int(td.total_seconds())
    except Exception:
        return -1
    return s if s >= 0 else -1


EMPTY_MEDIA_PAYLOAD = struct.pack(
    PAYLOAD_STRUCT,
    0, 0, -1, -1, 0, 0,
    b"".ljust(48, b"\0"),
    b"".ljust(32, b"\0"),
)


async def build_media_payload(session) -> bytes:
    if session is None:
        return EMPTY_MEDIA_PAYLOAD
    try:
        props = await session.try_get_media_properties_async()
    except Exception as e:
        log(f"[warn] media props 获取失败: {e}")
        return EMPTY_MEDIA_PAYLOAD

    info = session.get_playback_info()
    tl   = session.get_timeline_properties()

    playing  = 1 if info.playback_status == PlaybackStatus.PLAYING else 0
    position = _seconds_from_timedelta(getattr(tl, "position", None))
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


class MediaPublisher:
    """订阅 winsdk 媒体事件，状态变化时通过 BLE write 推送到 ESP。"""

    def __init__(self, client: BleakClient, loop: asyncio.AbstractEventLoop):
        self._client = client
        self._loop = loop
        self._mgr = None
        self._session = None
        self._session_tokens: list[tuple[object, str, int]] = []
        self._mgr_token: int | None = None
        self._push_lock = asyncio.Lock()
        self._last_payload: bytes | None = None   # 去重：完全相同的 payload 不再推送

    async def start(self) -> None:
        self._mgr = await SessionManager.request_async()
        self._mgr_token = self._mgr.add_current_session_changed(self._on_session_changed)
        await self._rebind_session()

    async def stop(self) -> None:
        self._unbind_session_events()
        if self._mgr is not None and self._mgr_token is not None:
            try:
                self._mgr.remove_current_session_changed(self._mgr_token)
            except Exception:
                pass

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
            log(f"media session: {name}")
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
                    log(f"[warn] subscribe {event_name} 失败: {e}")
        else:
            log("media session: None (nothing playing)")

        await self.push_now()

    # winsdk 事件回调在 COM 线程，必须桥接回 asyncio loop
    def _on_session_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self._rebind_session(), self._loop)

    def _on_any_changed(self, *_):
        asyncio.run_coroutine_threadsafe(self.push_now(), self._loop)

    async def push_now(self) -> None:
        async with self._push_lock:
            try:
                data = await build_media_payload(self._session)
            except Exception as e:
                log(f"[err] media 构造失败: {e}")
                return

            if data == self._last_payload:
                return   # 与上次完全相同则跳过，避免 winsdk 事件洪水

            try:
                await self._client.write_gatt_char(MEDIA_CHAR_UUID, data, response=True)
            except Exception as e:
                log(f"[err] media 推送失败: {e}")
                return
            self._last_payload = data

            playing, _, pos, dur, _, _ = struct.unpack("<BBhhHI", data[:12])
            title  = data[12:12+48].split(b"\0", 1)[0].decode("utf-8", errors="replace")
            artist = data[12+48:12+48+32].split(b"\0", 1)[0].decode("utf-8", errors="replace")
            log(f"[push] {'PLAY' if playing else 'PAUSE'} "
                f"{pos}s/{dur}s  \"{title}\" - \"{artist}\"")


# ---------------------------------------------------------------------------
# 会话生命周期（一个 BleakClient 同时承载 notify + write）
# ---------------------------------------------------------------------------

async def run_session(ctrl: ControlHandler) -> None:
    log(f"扫描 {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"{SCAN_TIMEOUT_S:.0f}s 内未发现 {DEVICE_NAME}")

    log(f"命中 {device.address}，连接中...")
    async with BleakClient(device) as client:
        loop = asyncio.get_running_loop()

        # 1) 订阅控制按钮
        await client.start_notify(CONTROL_CHAR_UUID, ctrl.on_notify)
        log(f"已订阅 control {CONTROL_CHAR_UUID}")

        # 2) 启动媒体推送
        pub = MediaPublisher(client, loop)
        await pub.start()
        log("已就绪\n")

        try:
            while client.is_connected:
                await asyncio.sleep(PERIODIC_SYNC_S)
                # 兜底：每 10s 全量同步一次媒体状态，防 winsdk 事件遗漏
                await pub.push_now()
        finally:
            await pub.stop()
            try:
                await client.stop_notify(CONTROL_CHAR_UUID)
            except Exception:
                pass
        log("连接断开")


async def main(ctrl: ControlHandler) -> None:
    while True:
        try:
            await run_session(ctrl)
        except asyncio.CancelledError:
            return
        except Exception as e:
            log(f"会话结束: {e}")
        log(f"{RECONNECT_DELAY_S:.0f}s 后重试\n")
        await asyncio.sleep(RECONNECT_DELAY_S)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dry-run", action="store_true",
                   help="控制按钮事件只打印、不触发 Windows 动作（联调用）")
    return p.parse_args()


if __name__ == "__main__":
    if sys.platform != "win32":
        print("windows only（winsdk 仅支持 Windows）", file=sys.stderr)
        sys.exit(1)
    args = parse_args()
    ctrl = ControlHandler(dry_run=args.dry_run)
    try:
        asyncio.run(main(ctrl))
    except KeyboardInterrupt:
        print("\nbye")
