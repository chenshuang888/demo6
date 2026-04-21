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
from dataclasses import dataclass
from datetime import datetime
from typing import Awaitable, Callable, Optional

import requests
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
CTS_CHAR_UUID       = "00002a2b-0000-1000-8000-00805f9b34fb"   # Write  (PC → ESP, 时间同步)
WEATHER_CHAR_UUID   = "8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP, 天气推送)

# control_event_t: type u8, id u8, action u8, _r u8, value i16, seq u16  → 8 B
EVENT_STRUCT = "<BBBBhH"
EVENT_SIZE   = struct.calcsize(EVENT_STRUCT)

# control_event_t.type 枚举（与固件 control_service.h 对齐）
EVENT_TYPE_BUTTON  = 0
EVENT_TYPE_REQUEST = 2

# REQUEST 事件的 id 语义
REQUEST_TIME_SYNC  = 0
REQUEST_WEATHER    = 1

# CTS (Current Time Service) 结构：与 services/time_service.c 的 ble_cts_current_time_t 对齐
# year u16, month u8, day u8, hour u8, minute u8, second u8,
# day_of_week u8 (1=Mon..7=Sun), fractions256 u8, adjust_reason u8  → 10 B
CTS_STRUCT = "<HBBBBBBBB"

# 天气 payload（与 services/weather_manager.h 对齐，68 B）
WEATHER_STRUCT = "<hhhBBI24s32s"
WEATHER_CACHE_TTL_S = 600   # 10 分钟缓存：防反复打开天气页把 API 打爆

# 天气编码（与 weather_manager.h WEATHER_CODE_* 对齐）
WC_UNKNOWN, WC_CLEAR, WC_CLOUDY, WC_OVERCAST = 0, 1, 2, 3
WC_RAIN, WC_SNOW, WC_FOG, WC_THUNDER = 4, 5, 6, 7

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
# 天气拉取（搬自 ble_time_sync.py，服务于 ESP 反向请求）
# ---------------------------------------------------------------------------

def wmo_to_code(wmo: int) -> int:
    if wmo == 0:
        return WC_CLEAR
    if wmo in (1, 2):
        return WC_CLOUDY
    if wmo == 3:
        return WC_OVERCAST
    if wmo in (45, 48):
        return WC_FOG
    if wmo in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82):
        return WC_RAIN
    if wmo in (71, 73, 75, 77, 85, 86):
        return WC_SNOW
    if wmo in (95, 96, 99):
        return WC_THUNDER
    return WC_UNKNOWN


WMO_DESC = {
    0: "Clear",
    1: "Mainly Clear", 2: "Partly Cloudy", 3: "Overcast",
    45: "Fog", 48: "Fog",
    51: "Light Drizzle", 53: "Drizzle", 55: "Heavy Drizzle",
    56: "Freezing Drizzle", 57: "Freezing Drizzle",
    61: "Light Rain", 63: "Rain", 65: "Heavy Rain",
    66: "Freezing Rain", 67: "Freezing Rain",
    71: "Light Snow", 73: "Snow", 75: "Heavy Snow",
    77: "Snow Grains",
    80: "Light Showers", 81: "Showers", 82: "Heavy Showers",
    85: "Snow Showers", 86: "Heavy Snow Showers",
    95: "Thunderstorm", 96: "Thunder w/ Hail", 99: "Thunder w/ Hail",
}


@dataclass
class Weather:
    temp: float
    temp_min: float
    temp_max: float
    humidity: int
    wmo: int
    city: str

    def desc(self) -> str:
        return WMO_DESC.get(self.wmo, "Unknown")

    def pack(self) -> bytes:
        city_b = self.city.encode("utf-8")[:23].ljust(24, b"\0")
        desc_b = self.desc().encode("utf-8")[:31].ljust(32, b"\0")
        return struct.pack(
            WEATHER_STRUCT,
            int(round(self.temp * 10)),
            int(round(self.temp_min * 10)),
            int(round(self.temp_max * 10)),
            max(0, min(100, int(self.humidity))),
            wmo_to_code(self.wmo),
            int(_time.time()),
            city_b,
            desc_b,
        )


def locate_by_ip() -> tuple[float, float, str]:
    r = requests.get("http://ip-api.com/json/", timeout=10)
    r.raise_for_status()
    j = r.json()
    if j.get("status") != "success":
        raise RuntimeError(f"ip-api: {j}")
    return float(j["lat"]), float(j["lon"]), j.get("city", "Unknown")


def fetch_weather(lat: float, lon: float, city: str) -> Weather:
    url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={lat}&longitude={lon}"
        "&current=temperature_2m,relative_humidity_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min"
        "&timezone=auto&forecast_days=1"
    )
    r = requests.get(url, timeout=15)
    r.raise_for_status()
    j = r.json()
    cur = j["current"]
    daily = j["daily"]
    return Weather(
        temp=float(cur["temperature_2m"]),
        temp_min=float(daily["temperature_2m_min"][0]),
        temp_max=float(daily["temperature_2m_max"][0]),
        humidity=int(cur["relative_humidity_2m"]),
        wmo=int(cur["weather_code"]),
        city=city,
    )


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
        # ESP 反向请求 PC 推送时间/天气的回调，由 run_session 在连接建立后注入
        self._time_sync_cb: Optional[Callable[[], Awaitable[None]]] = None
        self._weather_cb: Optional[Callable[[], Awaitable[None]]] = None

    def set_time_sync_cb(self, cb: Optional[Callable[[], Awaitable[None]]]) -> None:
        """连接期间把 push_cts 闭包挂到 handler；断开时传 None 清除。"""
        self._time_sync_cb = cb

    def set_weather_cb(self, cb: Optional[Callable[[], Awaitable[None]]]) -> None:
        """连接期间把 push_weather 闭包挂到 handler；断开时传 None 清除。"""
        self._weather_cb = cb

    def on_notify(self, _handle: int, data: bytearray) -> None:
        if len(data) != EVENT_SIZE:
            log(f"[warn] ctrl 长度异常 {len(data)}B: {bytes(data).hex()}")
            return

        type_, btn_id, action, _r, value, seq = struct.unpack(
            EVENT_STRUCT, bytes(data))

        # ESP → PC 请求事件路由
        if type_ == EVENT_TYPE_REQUEST:
            if btn_id == REQUEST_TIME_SYNC:
                log(f"[req] time_sync seq={seq}")
                if self._time_sync_cb is not None:
                    asyncio.create_task(self._time_sync_cb())
                else:
                    log("[warn] time_sync 请求到达但未安装回调")
            elif btn_id == REQUEST_WEATHER:
                log(f"[req] weather seq={seq}")
                if self._weather_cb is not None:
                    asyncio.create_task(self._weather_cb())
                else:
                    log("[warn] weather 请求到达但未安装回调")
            else:
                log(f"[warn] 未知 request id={btn_id} seq={seq}")
            return

        if type_ != EVENT_TYPE_BUTTON or action != 0:
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

async def push_cts(client: BleakClient) -> None:
    """把 PC 当前本地时间打包成 CTS 结构写给 ESP。

    ESP 在连上 PC 订阅 control_char 后会主动请求一次，
    以解决设备上电时间不准的问题（无 WiFi / SNTP 可用）。
    """
    now = datetime.now()
    data = struct.pack(
        CTS_STRUCT,
        now.year, now.month, now.day,
        now.hour, now.minute, now.second,
        now.isoweekday(),   # 1=Mon..7=Sun，与 ble_cts_current_time_t.day_of_week 对齐
        0,                  # fractions256：未知
        0,                  # adjust_reason：无
    )
    try:
        await client.write_gatt_char(CTS_CHAR_UUID, data, response=True)
        log(f"[cts] pushed {now:%Y-%m-%d %H:%M:%S}")
    except Exception as e:
        log(f"[err] cts 推送失败: {e}")


# 模块级天气缓存：避免反复进出天气页时打爆 open-meteo / ip-api 免费 API
_weather_cache: Optional[tuple[float, Weather]] = None
_weather_location: Optional[tuple[float, float, str]] = None


async def push_weather(client: BleakClient) -> None:
    """响应 ESP 的 REQUEST_WEATHER：拉一次天气写到 weather_char。

    10 分钟内的重复请求直接回缓存数据；IP 定位结果全局保留一次，因为
    桌面机短时间内地理位置不会变。fetch/locate 都是同步 requests，
    放到 loop.run_in_executor 避免阻塞事件循环。
    """
    global _weather_cache, _weather_location

    loop = asyncio.get_running_loop()
    now_ts = _time.time()

    # 命中缓存直接推送（天气 5-10 分钟粒度足够精确）
    if _weather_cache is not None and now_ts - _weather_cache[0] < WEATHER_CACHE_TTL_S:
        w = _weather_cache[1]
        age = int(now_ts - _weather_cache[0])
        log(f"[weather] cache hit ({age}s old)")
    else:
        try:
            if _weather_location is None:
                _weather_location = await loop.run_in_executor(None, locate_by_ip)
                lat, lon, city = _weather_location
                log(f"[weather] located: {city} ({lat:.2f}, {lon:.2f})")
            lat, lon, city = _weather_location
            w = await loop.run_in_executor(None, fetch_weather, lat, lon, city)
            _weather_cache = (now_ts, w)
            log(f"[weather] fetched: {w.desc()} {w.temp:.1f}°C "
                f"[{w.temp_min:.0f}/{w.temp_max:.0f}] humid={w.humidity}%")
        except Exception as e:
            log(f"[err] weather fetch 失败: {e}")
            return

    try:
        await client.write_gatt_char(WEATHER_CHAR_UUID, w.pack(), response=True)
        log(f"[weather] pushed {w.city}")
    except Exception as e:
        log(f"[err] weather 推送失败: {e}")


async def run_session(ctrl: ControlHandler) -> None:
    log(f"扫描 {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"{SCAN_TIMEOUT_S:.0f}s 内未发现 {DEVICE_NAME}")

    log(f"命中 {device.address}，连接中...")
    async with BleakClient(device) as client:
        loop = asyncio.get_running_loop()

        # 注入反向请求回调：ESP 订阅 control_char 或进入天气页时分别触发
        ctrl.set_time_sync_cb(lambda: push_cts(client))
        ctrl.set_weather_cb(lambda: push_weather(client))

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
            ctrl.set_time_sync_cb(None)
            ctrl.set_weather_cb(None)
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
