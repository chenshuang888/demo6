"""ESP32 桌面伴侣 —— BLE 多通道服务端。

单一进程连接 ESP32 BLE，在同一 BleakClient 上同时：
  - 订阅 media_button_char notify（屏上音乐页按钮 → Windows 媒体键）
      id=0 Prev / id=1 PlayPause / id=2 Next
  - 订阅 Windows 媒体会话 + 写 media_char
      (title / artist / playing / position / duration) → ESP 屏
  - 轮询 Windows 通知中心 + 写 notify_char
      (微信 / QQ / Teams 白名单 toast) → ESP 屏
  - psutil 定时采集系统状态 + 写 system_char
      (CPU / MEM / DISK / BAT / NET / TEMP / Uptime) → ESP 屏
  - ESP 订阅 CTS / weather-req / system-req 时被动响应推数据

依赖:
    pip install -r requirements.txt

运行:
    python desktop_companion.py              # 实际执行 Windows 动作
    python desktop_companion.py --dry-run    # 只打印媒体键事件，不触发系统动作

Ctrl+C 退出。连接断开 3s 后自动重连。
"""

import argparse
import asyncio
import collections
import ctypes
import struct
import sys
import time as _time
from dataclasses import dataclass
from datetime import datetime
from typing import Optional

import psutil
import requests
from bleak import BleakClient, BleakScanner

try:
    from winsdk.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as SessionManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
    )
    from winsdk.windows.ui.notifications import NotificationKinds
    from winsdk.windows.ui.notifications.management import (
        UserNotificationListener,
        UserNotificationListenerAccessStatus,
    )
except ImportError:
    print("winsdk 未安装或不在 Windows 上：pip install winsdk", file=sys.stderr)
    raise


# ---------------------------------------------------------------------------
# 常量（与固件端严格对齐）
# ---------------------------------------------------------------------------

DEVICE_NAME         = "ESP32-S3-DEMO"
MEDIA_CHAR_UUID     = "8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP)
CTS_CHAR_UUID       = "00002a2b-0000-1000-8000-00805f9b34fb"   # Write+Notify (PC 写 CTS / ESP 发 notify 请求时间同步)
WEATHER_CHAR_UUID   = "8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP, 天气推送)
WEATHER_REQ_CHAR_UUID = "8a5c000b-0000-4aef-b87e-4fa1e0c7e0f6" # Notify (ESP → PC, 请求天气)
NOTIFY_CHAR_UUID    = "8a5c0004-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP, 通知推送)
SYSTEM_CHAR_UUID    = "8a5c000a-0000-4aef-b87e-4fa1e0c7e0f6"   # Write  (PC → ESP, 系统监控)
SYSTEM_REQ_CHAR_UUID = "8a5c000c-0000-4aef-b87e-4fa1e0c7e0f6"  # Notify (ESP → PC, 请求系统数据)
MEDIA_BUTTON_CHAR_UUID = "8a5c000d-0000-4aef-b87e-4fa1e0c7e0f6" # Notify (ESP → PC, 媒体键按钮)

# media_button_event_t: id u8, action u8, seq u16  → 4 B (严格对齐 services/media_service.h)
MEDIA_BTN_STRUCT = "<BBH"
MEDIA_BTN_SIZE   = struct.calcsize(MEDIA_BTN_STRUCT)

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

# notification_payload_t: timestamp u32, category u8, priority u8, _r 2x,
#                         title 32s, body 96s  → 136 B（严格对齐 services/notify_manager.h）
NOTIFY_STRUCT     = "<IBB2x32s96s"
NOTIFY_SIZE       = struct.calcsize(NOTIFY_STRUCT)
NOTIFY_TITLE_MAX  = 32
NOTIFY_BODY_MAX   = 96

# notify_category_t（与 services/notify_manager.h 对齐）
NOTIFY_CAT_GENERIC  = 0
NOTIFY_CAT_MESSAGE  = 1
NOTIFY_CAT_EMAIL    = 2
NOTIFY_CAT_CALL     = 3
NOTIFY_CAT_CALENDAR = 4
NOTIFY_CAT_SOCIAL   = 5
NOTIFY_CAT_NEWS     = 6
NOTIFY_CAT_ALERT    = 7

NOTIFY_PRIO_NORMAL  = 1

# system_payload_t: cpu u8, mem u8, disk u8, bat u8, charging u8, _r u8,
#                   cpu_temp_cx10 i16, uptime_sec u32, net_down_kbps u16,
#                   net_up_kbps u16  → 16 B（严格对齐 services/system_manager.h）
SYSTEM_STRUCT = "<BBBBBBhIHH"
SYSTEM_SIZE   = struct.calcsize(SYSTEM_STRUCT)

# 系统监控常量
SYSTEM_PUSH_INTERVAL_S   = 2.0
SYSTEM_BATTERY_ABSENT    = 255
SYSTEM_CHARGING_ABSENT   = 255
SYSTEM_CPU_TEMP_INVALID  = -32768

# Windows Toast 白名单：DisplayName（中英文兼容）→ (category, 标准化 app 名)
# 标准化名供日志辨识；category 决定 ESP 屏上图标和颜色
TOAST_APP_WHITELIST: dict[str, tuple[int, str]] = {
    "微信":            (NOTIFY_CAT_MESSAGE, "WeChat"),
    "WeChat":          (NOTIFY_CAT_MESSAGE, "WeChat"),
    "Weixin":          (NOTIFY_CAT_MESSAGE, "WeChat"),
    "QQ":              (NOTIFY_CAT_SOCIAL,  "QQ"),
    "腾讯QQ":          (NOTIFY_CAT_SOCIAL,  "QQ"),
    "Tencent QQ":      (NOTIFY_CAT_SOCIAL,  "QQ"),
    "TIM":             (NOTIFY_CAT_SOCIAL,  "TIM"),
    "Microsoft Teams": (NOTIFY_CAT_MESSAGE, "Teams"),
    "Teams":           (NOTIFY_CAT_MESSAGE, "Teams"),
}

TOAST_DEDUP_WINDOW = 50   # Windows 偶尔重复触发 ADDED，LRU 足以覆盖
TOAST_POLL_INTERVAL_S = 2.0   # 轮询通知中心间隔（UWP 事件订阅在 Win32 不可用）

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

VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PLAY_PAUSE = 0xB3
KEYEVENTF_KEYUP     = 0x0002


def _send_key(vk: int) -> None:
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)


# media_button_event_t.id 枚举（与 services/media_service.h 对齐）
MEDIA_BTN_ACTIONS = {
    0: ("Prev",      lambda: _send_key(VK_MEDIA_PREV_TRACK)),
    1: ("PlayPause", lambda: _send_key(VK_MEDIA_PLAY_PAUSE)),
    2: ("Next",      lambda: _send_key(VK_MEDIA_NEXT_TRACK)),
}


class MediaButtonHandler:
    """解析 media_button char 的 4 字节事件，做去重 + 触发 Windows 媒体键。

    通道 UUID: 0x8a5c000d (ESP → PC NOTIFY)
    """

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self._last_seq: int | None = None

    def on_notify(self, _handle: int, data: bytearray) -> None:
        if len(data) != MEDIA_BTN_SIZE:
            log(f"[warn] media-btn 长度异常 {len(data)}B: {bytes(data).hex()}")
            return

        btn_id, action, seq = struct.unpack(MEDIA_BTN_STRUCT, bytes(data))

        if action != 0:
            log(f"[info] 忽略 media-btn action={action} id={btn_id} seq={seq}")
            return

        if seq == self._last_seq:
            log(f"[dup] media-btn seq={seq} 重复，忽略")
            return
        self._last_seq = seq

        entry = MEDIA_BTN_ACTIONS.get(btn_id)
        if entry is None:
            log(f"[warn] media-btn 未知 id={btn_id} seq={seq}")
            return

        name, fn = entry
        prefix = "[dry]" if self.dry_run else "[exec]"
        log(f"{prefix} media-btn id={btn_id} seq={seq}  → {name}")

        if self.dry_run:
            return
        try:
            fn()
        except Exception as e:
            log(f"[err] media-btn 动作失败: {e}")


def make_request_handler(name: str, action):
    """把 BLE notify callback 桥接到一个 async action。

    ESP 端每次 subscribe 上升沿或页面进入都会发一帧 1B seq。
    body[0] 仅做日志观察，不去重（重复触发幂等：push_cts/push_weather 都安全，
    sys_pub.push_now 自带 asyncio.Lock）。
    """
    def handler(_sender, data: bytearray) -> None:
        seq = data[0] if data else None
        log(f"[req] {name} seq={seq}")
        asyncio.create_task(action())
    return handler


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
# Toast publisher（winsdk UserNotificationListener + 写 notify_char）
# ---------------------------------------------------------------------------

def _extract_toast_text(notif) -> tuple[str, str]:
    """从 UserNotification 提取 (title, body)。

    Windows toast 通常用 ToastGeneric 模板，文本元素数组 lines[0] 是标题，
    lines[1:] 合并为正文。某些非标准 toast 可能没有 ToastGeneric 绑定，
    退化到取第一个可用绑定。
    """
    try:
        visual = notif.notification.visual
    except Exception:
        return "", ""
    if visual is None:
        return "", ""

    binding = None
    try:
        binding = visual.get_binding("ToastGeneric")
    except Exception:
        binding = None

    if binding is None:
        # 兜底：遍历所有 binding 取第一个有文本的
        try:
            for b in visual.bindings:
                binding = b
                break
        except Exception:
            return "", ""

    if binding is None:
        return "", ""

    try:
        texts = list(binding.get_text_elements())
    except Exception:
        return "", ""

    lines: list[str] = []
    for t in texts:
        try:
            s = t.text or ""
        except Exception:
            s = ""
        if s:
            lines.append(s)

    if not lines:
        return "", ""
    title = lines[0]
    body = "\n".join(lines[1:]) if len(lines) > 1 else ""
    return title, body


class ToastPublisher:
    """订阅 Windows 通知中心（UserNotificationListener），捕获白名单应用的 toast
    并通过 BLE write 推送到 ESP notify_service。

    Win32 桌面进程无法注册 NotificationChanged 事件（该事件仅对注册了
    background task 的 UWP 应用可用），因此采用轮询 GetNotificationsAsync
    每 TOAST_POLL_INTERVAL_S 秒拉一次当前通知中心，按 notification.id 做
    增量 diff + LRU 去重。
    """

    def __init__(self, client: BleakClient, loop: asyncio.AbstractEventLoop):
        self._client = client
        self._loop = loop
        self._listener = None
        self._seen_ids: "collections.deque[int]" = collections.deque(
            maxlen=TOAST_DEDUP_WINDOW)
        self._poll_task: asyncio.Task | None = None
        self._first_scan = True   # 首次扫描只标记已见，不推送历史 toast
        self._enabled = False

    async def start(self) -> None:
        try:
            self._listener = UserNotificationListener.current
        except Exception as e:
            log(f"[toast] listener 不可用: {e}")
            return

        # 首次调用会弹 Windows 权限对话框；已授权则立即返回
        try:
            status = await self._listener.request_access_async()
        except Exception as e:
            log(f"[toast] 权限申请失败: {e}")
            return

        if status != UserNotificationListenerAccessStatus.ALLOWED:
            log(f"[toast] 权限被拒 (status={status})，listener 已禁用")
            log("[toast] 可在 Windows 设置 → 隐私与安全性 → 通知 中开启")
            return

        self._enabled = True
        self._poll_task = asyncio.create_task(self._poll_loop())
        log(f"[toast] listener access granted, polling every {TOAST_POLL_INTERVAL_S}s")

    async def stop(self) -> None:
        self._enabled = False
        if self._poll_task is not None:
            self._poll_task.cancel()
            try:
                await self._poll_task
            except (asyncio.CancelledError, Exception):
                pass
            self._poll_task = None
        self._listener = None

    async def _poll_loop(self) -> None:
        while self._enabled:
            try:
                await self._scan_once()
            except asyncio.CancelledError:
                raise
            except Exception as e:
                log(f"[toast] 轮询异常: {e}")
            try:
                await asyncio.sleep(TOAST_POLL_INTERVAL_S)
            except asyncio.CancelledError:
                raise

    async def _scan_once(self) -> None:
        if self._listener is None:
            return
        try:
            toasts = await self._listener.get_notifications_async(
                NotificationKinds.TOAST)
        except Exception as e:
            log(f"[toast] get_notifications_async 失败: {e}")
            return

        # 收集本轮未见过的 (id, notif)
        new_entries: list[tuple[int, object]] = []
        for notif in toasts:
            try:
                nid = int(notif.id)
            except Exception:
                continue
            if nid in self._seen_ids:
                continue
            new_entries.append((nid, notif))

        # 首次扫描：把通知中心里已有的 toast 全部标记已见但不推送，
        # 避免启动时把十几分钟前的旧消息一次性灌给 ESP
        if self._first_scan:
            for nid, _ in new_entries:
                self._seen_ids.append(nid)
            self._first_scan = False
            if new_entries:
                log(f"[toast] 初始扫描跳过 {len(new_entries)} 条已有 toast")
            return

        for nid, notif in new_entries:
            self._seen_ids.append(nid)
            await self._process_notification(nid, notif)

    async def _process_notification(self, notif_id: int, notif) -> None:
        try:
            app_name = notif.app_info.display_info.display_name or ""
        except Exception:
            app_name = ""

        hit = TOAST_APP_WHITELIST.get(app_name)
        if hit is None:
            log(f"[toast] skip app={app_name!r} id={notif_id}")
            return
        category, std_name = hit

        title, body = _extract_toast_text(notif)
        if not title and not body:
            log(f"[toast] {std_name} 空文本，跳过 id={notif_id}")
            return

        # UTF-8 安全截断（复用 _utf8_fixed，留 1B 给末尾 \0）
        title_b = _utf8_fixed(title, NOTIFY_TITLE_MAX - 1, NOTIFY_TITLE_MAX)
        body_b  = _utf8_fixed(body,  NOTIFY_BODY_MAX - 1,  NOTIFY_BODY_MAX)
        data = struct.pack(
            NOTIFY_STRUCT,
            int(_time.time()), category, NOTIFY_PRIO_NORMAL,
            title_b, body_b,
        )

        try:
            await self._client.write_gatt_char(
                NOTIFY_CHAR_UUID, data, response=True)
        except Exception as e:
            log(f"[toast] 推送失败 {std_name}: {e}")
            return

        body_preview = body.replace("\n", " ")[:40]
        log(f"[toast] {std_name} \"{title}\" \"{body_preview}\"")


# ---------------------------------------------------------------------------
# System publisher（psutil 采集 CPU/MEM/DISK/BAT/NET/温度 → 写 system_char）
# ---------------------------------------------------------------------------

def _read_cpu_temp_cx10() -> int:
    """Windows 上 psutil.sensors_temperatures 多数虚拟机/家用机读不到，
    读不到就返回 SYSTEM_CPU_TEMP_INVALID 让 ESP 端显示 "--"。"""
    getter = getattr(psutil, "sensors_temperatures", None)
    if getter is None:
        return SYSTEM_CPU_TEMP_INVALID
    try:
        temps = getter()
    except Exception:
        return SYSTEM_CPU_TEMP_INVALID
    if not temps:
        return SYSTEM_CPU_TEMP_INVALID

    # 优先找常见 CPU 芯片组；否则取任意第一个传感器
    for key in ("coretemp", "cpu_thermal", "k10temp", "acpitz"):
        entries = temps.get(key)
        if entries:
            cur = entries[0].current
            if cur is not None:
                return max(-32767, min(32767, int(round(float(cur) * 10))))
    for entries in temps.values():
        if entries:
            cur = entries[0].current
            if cur is not None:
                return max(-32767, min(32767, int(round(float(cur) * 10))))
    return SYSTEM_CPU_TEMP_INVALID


class SystemPublisher:
    """每 SYSTEM_PUSH_INTERVAL_S 秒采集一次 PC 系统状态并 write 到 ESP。

    - CPU%：用 cpu_percent(interval=None)，相对上次调用的差值（首帧 0）
    - 磁盘：只看 C 盘 fixed，避免 disk_partitions() 遍历慢
    - 网速：用 net_io_counters 的字节差 / 时间差换算 KB/s
    - 温度：psutil.sensors_temperatures() 读不到就填哨兵
    - 电池：无电池填 255/255
    ESP 侧 system-req notify 触发立即推送一帧（打破 2s 常规节奏）。
    """

    def __init__(self, client: BleakClient, loop: asyncio.AbstractEventLoop):
        self._client = client
        self._loop = loop
        self._push_lock = asyncio.Lock()
        self._loop_task: asyncio.Task | None = None
        self._running = False

        # 网速差分基准；首帧无法算速率，填 0
        self._last_net_ts: float | None = None
        self._last_net_recv: int = 0
        self._last_net_sent: int = 0

        # 预热 cpu_percent，第一次调用会返回 0.0 但会记录采样点
        try:
            psutil.cpu_percent(interval=None)
        except Exception:
            pass

    async def start(self) -> None:
        self._running = True
        self._loop_task = asyncio.create_task(self._run_loop())

    async def stop(self) -> None:
        self._running = False
        if self._loop_task is not None:
            self._loop_task.cancel()
            try:
                await self._loop_task
            except (asyncio.CancelledError, Exception):
                pass
            self._loop_task = None

    async def _run_loop(self) -> None:
        # 首帧等一个间隔让 cpu_percent 有采样窗，以免第一次推送全 0
        try:
            await asyncio.sleep(SYSTEM_PUSH_INTERVAL_S)
        except asyncio.CancelledError:
            return
        while self._running:
            try:
                await self.push_now()
            except asyncio.CancelledError:
                raise
            except Exception as e:
                log(f"[system] 循环异常: {e}")
            try:
                await asyncio.sleep(SYSTEM_PUSH_INTERVAL_S)
            except asyncio.CancelledError:
                raise

    def _sample(self) -> bytes:
        cpu_pct  = int(round(psutil.cpu_percent(interval=None)))
        mem_pct  = int(round(psutil.virtual_memory().percent))

        try:
            disk_pct = int(round(psutil.disk_usage("C:\\").percent))
        except Exception:
            disk_pct = 0

        bat_pct    = SYSTEM_BATTERY_ABSENT
        charging   = SYSTEM_CHARGING_ABSENT
        try:
            bat = psutil.sensors_battery()
        except Exception:
            bat = None
        if bat is not None:
            bat_pct  = max(0, min(100, int(round(bat.percent))))
            charging = 1 if bat.power_plugged else 0

        cpu_temp = _read_cpu_temp_cx10()

        try:
            uptime = int(_time.time() - psutil.boot_time())
        except Exception:
            uptime = 0
        uptime = max(0, min(0xFFFFFFFF, uptime))

        # 网速：差分字节 / 差分秒 → KB/s
        down_kbps = 0
        up_kbps   = 0
        try:
            io = psutil.net_io_counters()
            now = _time.time()
            if self._last_net_ts is not None:
                dt = now - self._last_net_ts
                if dt > 0.1:
                    d_recv = max(0, io.bytes_recv - self._last_net_recv)
                    d_sent = max(0, io.bytes_sent - self._last_net_sent)
                    down_kbps = min(0xFFFF, int(d_recv / dt / 1024))
                    up_kbps   = min(0xFFFF, int(d_sent / dt / 1024))
            self._last_net_ts = now
            self._last_net_recv = io.bytes_recv
            self._last_net_sent = io.bytes_sent
        except Exception:
            pass

        return struct.pack(
            SYSTEM_STRUCT,
            max(0, min(100, cpu_pct)),
            max(0, min(100, mem_pct)),
            max(0, min(100, disk_pct)),
            bat_pct, charging, 0,
            cpu_temp,
            uptime,
            down_kbps, up_kbps,
        )

    async def push_now(self) -> None:
        async with self._push_lock:
            try:
                data = await self._loop.run_in_executor(None, self._sample)
            except Exception as e:
                log(f"[system] 采集失败: {e}")
                return
            try:
                await self._client.write_gatt_char(
                    SYSTEM_CHAR_UUID, data, response=True)
            except Exception as e:
                log(f"[system] 推送失败: {e}")
                return

            cpu, mem, disk, bat, chg, _, temp, up, dn, upk = struct.unpack(
                SYSTEM_STRUCT, data)
            bat_str  = "--" if bat == SYSTEM_BATTERY_ABSENT else f"{bat}%"
            chg_str  = "" if chg == SYSTEM_CHARGING_ABSENT else ("⚡" if chg else "")
            temp_str = "--" if temp == SYSTEM_CPU_TEMP_INVALID else f"{temp/10:.1f}°C"
            log(f"[system] CPU={cpu}% MEM={mem}% DISK={disk}% "
                f"BAT={bat_str}{chg_str} TEMP={temp_str} "
                f"UP={up}s ↓{dn}KB/s ↑{upk}KB/s")


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
    """响应 ESP 的 weather-req notify：拉一次天气写到 weather_char。

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


async def run_session(dry_run: bool) -> None:
    log(f"扫描 {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"{SCAN_TIMEOUT_S:.0f}s 内未发现 {DEVICE_NAME}")

    log(f"命中 {device.address}，连接中...")
    async with BleakClient(device) as client:
        loop = asyncio.get_running_loop()

        # 1) 媒体键按钮（page_music 上/下/播暂）
        media_btn = MediaButtonHandler(dry_run=dry_run)
        await client.start_notify(MEDIA_BUTTON_CHAR_UUID, media_btn.on_notify)
        log(f"已订阅 media-btn {MEDIA_BUTTON_CHAR_UUID}")

        # 2) 时间同步请求（ESP 订阅 CTS 的 NOTIFY → PC 立即 push_cts）
        await client.start_notify(
            CTS_CHAR_UUID,
            make_request_handler("time", lambda: push_cts(client)),
        )
        log(f"已订阅 time-req {CTS_CHAR_UUID}")

        # 3) 天气请求（进入天气页或订阅时触发）
        await client.start_notify(
            WEATHER_REQ_CHAR_UUID,
            make_request_handler("weather", lambda: push_weather(client)),
        )
        log(f"已订阅 weather-req {WEATHER_REQ_CHAR_UUID}")

        # 4) 启动媒体推送
        pub = MediaPublisher(client, loop)
        await pub.start()

        # 5) 启动 Toast 推送（权限被拒等失败不影响媒体/控制）
        toast_pub = ToastPublisher(client, loop)
        try:
            await toast_pub.start()
        except Exception as e:
            log(f"[toast] 启动失败，已跳过: {e}")

        # 6) 启动系统监控推送（psutil 采集失败不影响其他通道）
        #    sys_pub 起来后再订阅 system-req，确保 handler 拿得到 push_now
        sys_pub = SystemPublisher(client, loop)
        sys_req_subscribed = False
        try:
            await sys_pub.start()
            await client.start_notify(
                SYSTEM_REQ_CHAR_UUID,
                make_request_handler("system", sys_pub.push_now),
            )
            sys_req_subscribed = True
            log(f"已订阅 system-req {SYSTEM_REQ_CHAR_UUID}")
        except Exception as e:
            log(f"[system] 启动失败，已跳过: {e}")

        log("已就绪\n")

        try:
            while client.is_connected:
                await asyncio.sleep(PERIODIC_SYNC_S)
                # 兜底：每 10s 全量同步一次媒体状态，防 winsdk 事件遗漏
                await pub.push_now()
        finally:
            await sys_pub.stop()
            await toast_pub.stop()
            await pub.stop()
            for uuid in (MEDIA_BUTTON_CHAR_UUID, CTS_CHAR_UUID, WEATHER_REQ_CHAR_UUID):
                try:
                    await client.stop_notify(uuid)
                except Exception:
                    pass
            if sys_req_subscribed:
                try:
                    await client.stop_notify(SYSTEM_REQ_CHAR_UUID)
                except Exception:
                    pass
        log("连接断开")


async def main(dry_run: bool) -> None:
    while True:
        try:
            await run_session(dry_run)
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
                   help="媒体键事件只打印、不触发 Windows 动作（联调用）")
    return p.parse_args()


if __name__ == "__main__":
    if sys.platform != "win32":
        print("windows only（winsdk 仅支持 Windows）", file=sys.stderr)
        sys.exit(1)
    args = parse_args()
    try:
        asyncio.run(main(args.dry_run))
    except KeyboardInterrupt:
        print("\nbye")
