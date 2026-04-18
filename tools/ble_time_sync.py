"""ESP32 BLE Companion (CustomTkinter GUI).

PC 端配套工具，功能：
  1. 时间同步 — Current Time Service (UUID 0x2A2B)
  2. 天气推送 — 自定义服务 (UUID 8a5c0002-...)
  3. 通知推送 — 自定义服务 (UUID 8a5c0004-...)

风格：深紫 + 青绿，与 ESP32 端 LVGL 屏幕配色一致。
布局：左侧边栏导航 + 右侧内容区。

依赖安装:
    pip install -r requirements.txt

运行:
    python ble_time_sync.py
"""

import asyncio
import struct
import sys
import threading
import time as _time
from dataclasses import dataclass
from datetime import datetime

import customtkinter as ctk
import requests
from bleak import BleakClient, BleakScanner


def _enable_windows_dpi_awareness() -> None:
    """声明进程自己处理 DPI，避免 Windows 二次放大窗口。"""
    if sys.platform != "win32":
        return
    import ctypes
    for fn in (
        lambda: ctypes.windll.shcore.SetProcessDpiAwareness(2),
        lambda: ctypes.windll.shcore.SetProcessDpiAwareness(1),
        lambda: ctypes.windll.user32.SetProcessDPIAware(),
    ):
        try:
            fn()
            return
        except Exception:
            continue


# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

DEVICE_NAME = "ESP32-S3-DEMO"
CTS_CHAR_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"
WEATHER_CHAR_UUID = "8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6"
NOTIFY_CHAR_UUID  = "8a5c0004-0000-4aef-b87e-4fa1e0c7e0f6"

SCAN_TIMEOUT = 10.0
CTS_STRUCT = "<HBBBBBBBB"                    # 10 字节, ble_cts_current_time_t
WEATHER_STRUCT = "<hhhBBI24s32s"             # 68 字节, weather_payload_t
NOTIFY_STRUCT  = "<IBB2x32s96s"              # 136 字节, notification_payload_t

AUTO_PUSH_INTERVAL_SEC = 600                 # 10 分钟

# 天气编码（与 services/weather_manager.h 对齐）
WC_UNKNOWN, WC_CLEAR, WC_CLOUDY, WC_OVERCAST = 0, 1, 2, 3
WC_RAIN, WC_SNOW, WC_FOG, WC_THUNDER = 4, 5, 6, 7

# 通知类别（与 services/notify_manager.h 对齐）
NOTIFY_CATEGORIES = [
    ("Generic",  0),
    ("Message",  1),
    ("Email",    2),
    ("Call",     3),
    ("Calendar", 4),
    ("Social",   5),
    ("News",     6),
    ("Alert",    7),
]
NOTIFY_PRIORITIES = [("Low", 0), ("Normal", 1), ("High", 2)]


# ---------------------------------------------------------------------------
# 配色（与 ESP32 LVGL 页面完全一致）
# ---------------------------------------------------------------------------

COLOR_BG        = "#1E1B2E"
COLOR_SIDEBAR   = "#2D2640"
COLOR_CARD      = "#2D2640"
COLOR_CARD_ALT  = "#3A3354"
COLOR_ACCENT    = "#06B6D4"
COLOR_ACCENT_H  = "#0891B2"
COLOR_TEXT      = "#F1ECFF"
COLOR_MUTED     = "#9B94B5"
COLOR_SUCCESS   = "#10B981"
COLOR_DANGER    = "#EF4444"
COLOR_WARN      = "#F97316"

FONT_FAMILY = "Microsoft YaHei UI"


# ---------------------------------------------------------------------------
# 天气数据 + 打包
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

# 天气编码 → 颜色（与 page_weather.c 对齐）
CODE_COLOR = {
    WC_CLEAR:    "#FBBF24",
    WC_CLOUDY:   "#94A3B8",
    WC_OVERCAST: "#6B7280",
    WC_RAIN:     "#3B82F6",
    WC_SNOW:     "#BAE6FD",
    WC_FOG:      "#A78BFA",
    WC_THUNDER:  COLOR_WARN,
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


def _utf8_fixed(s: str, size: int) -> bytes:
    """按 UTF-8 编码并在合法边界截断到 size 字节，末尾 \\0 填充。"""
    b = s.encode("utf-8")
    if len(b) > size - 1:
        b = b[:size - 1]
        while b and (b[-1] & 0xC0) == 0x80:
            b = b[:-1]
    return b.ljust(size, b"\0")


# ---------------------------------------------------------------------------
# BLE 客户端
# ---------------------------------------------------------------------------

class BleClient:
    def __init__(self):
        self._client: BleakClient | None = None

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    @property
    def address(self) -> str | None:
        return self._client.address if self._client else None

    async def connect(self) -> None:
        device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT)
        if device is None:
            raise RuntimeError(f"未找到设备 {DEVICE_NAME} (扫描 {SCAN_TIMEOUT:.0f}s)")
        client = BleakClient(device)
        await client.connect()
        self._client = client

    async def disconnect(self) -> None:
        if self._client is not None:
            try:
                await self._client.disconnect()
            finally:
                self._client = None

    async def read_time(self) -> datetime:
        if not self.connected:
            raise RuntimeError("未连接")
        data = await self._client.read_gatt_char(CTS_CHAR_UUID)
        expected = struct.calcsize(CTS_STRUCT)
        if len(data) != expected:
            raise RuntimeError(f"数据长度异常: {len(data)} (期望 {expected})")
        year, month, day, hour, minute, second, _dow, _frac, _adj = struct.unpack(CTS_STRUCT, data)
        return datetime(year, month, day, hour, minute, second)

    async def write_time(self, dt: datetime) -> None:
        if not self.connected:
            raise RuntimeError("未连接")
        dow = dt.weekday() + 1
        fractions256 = (dt.microsecond * 256) // 1_000_000
        data = struct.pack(
            CTS_STRUCT,
            dt.year, dt.month, dt.day,
            dt.hour, dt.minute, dt.second,
            dow, fractions256, 0,
        )
        await self._client.write_gatt_char(CTS_CHAR_UUID, data, response=True)

    async def write_weather(self, w: Weather) -> None:
        if not self.connected:
            raise RuntimeError("未连接")
        await self._client.write_gatt_char(WEATHER_CHAR_UUID, w.pack(), response=True)

    async def write_notification(self, category: int, priority: int,
                                 title: str, body: str) -> None:
        if not self.connected:
            raise RuntimeError("未连接")
        data = struct.pack(
            NOTIFY_STRUCT,
            int(_time.time()),
            category, priority,
            _utf8_fixed(title, 32),
            _utf8_fixed(body, 96),
        )
        await self._client.write_gatt_char(NOTIFY_CHAR_UUID, data, response=True)


# ---------------------------------------------------------------------------
# GUI 辅助
# ---------------------------------------------------------------------------

def f(size: int, weight: str = "normal") -> ctk.CTkFont:
    return ctk.CTkFont(family=FONT_FAMILY, size=size, weight=weight)


def fmono(size: int, weight: str = "normal") -> ctk.CTkFont:
    return ctk.CTkFont(family="Consolas", size=size, weight=weight)


# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------

NAV_ITEMS = [
    ("home",    "🏠  Home"),
    ("time",    "⏱  Time"),
    ("weather", "🌤  Weather"),
    ("notify",  "🔔  Notify"),
]


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        ctk.set_appearance_mode("dark")

        # 业务状态
        self._ble = BleClient()
        self._loop = asyncio.new_event_loop()
        self._loop_thread = threading.Thread(target=self._run_loop, daemon=True)
        self._loop_thread.start()

        self._location: tuple[float, float, str] | None = None
        self._latest_weather: Weather | None = None
        self._auto_timer: str | None = None
        self._last_read_time: datetime | None = None

        self._current_page: str = "home"
        self._busy: bool = False

        self._setup_window()
        self._build_layout()
        self._show_page("home")
        self._refresh_state()

    # ----- 窗口基础 -----

    def _setup_window(self) -> None:
        self.title("ESP32 BLE Companion")
        self.geometry("720x480")
        self.resizable(False, False)
        self.configure(fg_color=COLOR_BG)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_layout(self) -> None:
        self.grid_columnconfigure(0, weight=0)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self._build_sidebar()
        self._build_content()

    # ----- 侧边栏 -----

    def _build_sidebar(self) -> None:
        side = ctk.CTkFrame(self, width=180, corner_radius=0, fg_color=COLOR_SIDEBAR)
        side.grid(row=0, column=0, sticky="nsw")
        side.grid_propagate(False)
        side.grid_columnconfigure(0, weight=1)
        side.grid_rowconfigure(2, weight=1)  # 中间 nav 列表占据剩余空间

        # 顶部 Logo 区
        logo_box = ctk.CTkFrame(side, fg_color="transparent")
        logo_box.grid(row=0, column=0, sticky="ew", padx=16, pady=(20, 16))

        ctk.CTkLabel(logo_box, text="ESP32", font=f(20, "bold"),
                     text_color=COLOR_ACCENT).pack(anchor="w")
        ctk.CTkLabel(logo_box, text="BLE Companion", font=f(11),
                     text_color=COLOR_MUTED).pack(anchor="w")

        # 分割线
        ctk.CTkFrame(side, height=1, fg_color=COLOR_CARD_ALT).grid(
            row=1, column=0, sticky="ew", padx=12, pady=(0, 8))

        # 导航
        nav = ctk.CTkFrame(side, fg_color="transparent")
        nav.grid(row=2, column=0, sticky="new", padx=10)
        nav.grid_columnconfigure(0, weight=1)

        self._nav_buttons: dict[str, ctk.CTkButton] = {}
        for i, (key, label) in enumerate(NAV_ITEMS):
            btn = ctk.CTkButton(
                nav, text=label, anchor="w",
                font=f(13), height=40, corner_radius=8,
                fg_color="transparent",
                hover_color=COLOR_CARD_ALT,
                text_color=COLOR_TEXT,
                command=lambda k=key: self._show_page(k),
            )
            btn.grid(row=i, column=0, sticky="ew", pady=3)
            self._nav_buttons[key] = btn

        # 底部连接状态
        status_box = ctk.CTkFrame(side, fg_color="transparent")
        status_box.grid(row=3, column=0, sticky="sew", padx=16, pady=(12, 16))

        ctk.CTkFrame(status_box, height=1, fg_color=COLOR_CARD_ALT).pack(
            fill="x", pady=(0, 10))

        row = ctk.CTkFrame(status_box, fg_color="transparent")
        row.pack(fill="x")
        self._status_dot = ctk.CTkLabel(row, text="●", font=f(14),
                                         text_color=COLOR_MUTED)
        self._status_dot.pack(side="left")
        self._status_text = ctk.CTkLabel(row, text="未连接", font=f(12),
                                          text_color=COLOR_TEXT)
        self._status_text.pack(side="left", padx=(6, 0))

        self._status_addr = ctk.CTkLabel(status_box, text="", font=fmono(9),
                                          text_color=COLOR_MUTED)
        self._status_addr.pack(anchor="w", pady=(4, 0))

    def _update_nav_selection(self, active_key: str) -> None:
        for key, btn in self._nav_buttons.items():
            if key == active_key:
                btn.configure(fg_color=COLOR_ACCENT, text_color=COLOR_TEXT,
                              hover_color=COLOR_ACCENT_H)
            else:
                btn.configure(fg_color="transparent", text_color=COLOR_TEXT,
                              hover_color=COLOR_CARD_ALT)

    # ----- 内容区 -----

    def _build_content(self) -> None:
        container = ctk.CTkFrame(self, fg_color=COLOR_BG, corner_radius=0)
        container.grid(row=0, column=1, sticky="nsew")
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)
        self._content_container = container

        self._pages: dict[str, ctk.CTkFrame] = {}
        self._pages["home"]    = self._build_home()
        self._pages["time"]    = self._build_time()
        self._pages["weather"] = self._build_weather()
        self._pages["notify"]  = self._build_notify()
        for frame in self._pages.values():
            frame.grid(row=0, column=0, sticky="nsew", padx=24, pady=20)
            frame.grid_remove()

    def _show_page(self, key: str) -> None:
        if key not in self._pages:
            return
        if self._current_page and self._current_page in self._pages:
            self._pages[self._current_page].grid_remove()
        self._pages[key].grid()
        self._current_page = key
        self._update_nav_selection(key)

    # ----- Home 页 -----

    def _build_home(self) -> ctk.CTkFrame:
        page = ctk.CTkFrame(self._content_container, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(page, text="设备连接", font=f(22, "bold"),
                     text_color=COLOR_TEXT).grid(row=0, column=0, sticky="w")

        # 状态卡片
        card = ctk.CTkFrame(page, fg_color=COLOR_CARD, corner_radius=12)
        card.grid(row=1, column=0, sticky="ew", pady=(16, 16))

        ctk.CTkLabel(card, text=DEVICE_NAME, font=f(16, "bold"),
                     text_color=COLOR_ACCENT).pack(anchor="w", padx=20, pady=(18, 4))

        self._home_status_lbl = ctk.CTkLabel(card, text="未连接", font=f(13),
                                              text_color=COLOR_MUTED)
        self._home_status_lbl.pack(anchor="w", padx=20, pady=(0, 4))

        self._home_addr_lbl = ctk.CTkLabel(card, text="", font=fmono(10),
                                            text_color=COLOR_MUTED)
        self._home_addr_lbl.pack(anchor="w", padx=20, pady=(0, 18))

        # 主按钮
        self._connect_btn = ctk.CTkButton(
            page, text="连接设备", font=f(14, "bold"), height=44,
            corner_radius=10, fg_color=COLOR_ACCENT, hover_color=COLOR_ACCENT_H,
            text_color=COLOR_TEXT, command=self._on_connect_click,
        )
        self._connect_btn.grid(row=2, column=0, sticky="ew")

        ctk.CTkLabel(page,
            text="连接后可使用左侧菜单的时间同步、天气推送、通知推送功能",
            font=f(10), text_color=COLOR_MUTED).grid(row=3, column=0, sticky="w", pady=(14, 0))

        return page

    # ----- Time 页 -----

    def _build_time(self) -> ctk.CTkFrame:
        page = ctk.CTkFrame(self._content_container, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(page, text="时间同步", font=f(22, "bold"),
                     text_color=COLOR_TEXT).grid(row=0, column=0, sticky="w")

        card = ctk.CTkFrame(page, fg_color=COLOR_CARD, corner_radius=12)
        card.grid(row=1, column=0, sticky="ew", pady=(16, 16))

        ctk.CTkLabel(card, text="ESP32 时间", font=f(11),
                     text_color=COLOR_MUTED).pack(anchor="w", padx=20, pady=(18, 2))

        self._time_lbl = ctk.CTkLabel(card, text="--:--:--", font=fmono(28, "bold"),
                                       text_color=COLOR_ACCENT)
        self._time_lbl.pack(anchor="w", padx=20, pady=(0, 4))

        self._time_date_lbl = ctk.CTkLabel(card, text="--", font=f(12),
                                            text_color=COLOR_MUTED)
        self._time_date_lbl.pack(anchor="w", padx=20, pady=(0, 18))

        btns = ctk.CTkFrame(page, fg_color="transparent")
        btns.grid(row=2, column=0, sticky="ew")
        btns.grid_columnconfigure(0, weight=1)
        btns.grid_columnconfigure(1, weight=1)

        self._time_read_btn = ctk.CTkButton(
            btns, text="读取 ESP32 时间", font=f(13), height=40,
            corner_radius=10, fg_color=COLOR_CARD_ALT, hover_color=COLOR_CARD,
            text_color=COLOR_TEXT, command=self._on_read_time,
        )
        self._time_read_btn.grid(row=0, column=0, sticky="ew", padx=(0, 6))

        self._time_sync_btn = ctk.CTkButton(
            btns, text="同步电脑时间到 ESP32", font=f(13, "bold"), height=40,
            corner_radius=10, fg_color=COLOR_ACCENT, hover_color=COLOR_ACCENT_H,
            text_color=COLOR_TEXT, command=self._on_sync_time,
        )
        self._time_sync_btn.grid(row=0, column=1, sticky="ew", padx=(6, 0))

        return page

    # ----- Weather 页 -----

    def _build_weather(self) -> ctk.CTkFrame:
        page = ctk.CTkFrame(self._content_container, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(page, text="天气推送", font=f(22, "bold"),
                     text_color=COLOR_TEXT).grid(row=0, column=0, sticky="w")

        # 位置 + 天气合并为一个卡片
        card = ctk.CTkFrame(page, fg_color=COLOR_CARD, corner_radius=12)
        card.grid(row=1, column=0, sticky="ew", pady=(16, 12))

        self._weather_city_lbl = ctk.CTkLabel(card, text="位置: --", font=f(11),
                                                text_color=COLOR_MUTED)
        self._weather_city_lbl.pack(anchor="w", padx=20, pady=(16, 4))

        self._weather_temp_lbl = ctk.CTkLabel(card, text="--.-°", font=f(32, "bold"),
                                                text_color=COLOR_TEXT)
        self._weather_temp_lbl.pack(anchor="w", padx=20)

        self._weather_desc_lbl = ctk.CTkLabel(card, text="尚未拉取", font=f(14),
                                                text_color=COLOR_MUTED)
        self._weather_desc_lbl.pack(anchor="w", padx=20, pady=(2, 10))

        info_row = ctk.CTkFrame(card, fg_color="transparent")
        info_row.pack(fill="x", padx=20, pady=(0, 16))

        def mk_info(parent, title):
            box = ctk.CTkFrame(parent, fg_color=COLOR_CARD_ALT, corner_radius=8)
            box.pack(side="left", expand=True, fill="x", padx=4)
            ctk.CTkLabel(box, text=title, font=f(10),
                         text_color=COLOR_MUTED).pack(anchor="w", padx=10, pady=(6, 0))
            val = ctk.CTkLabel(box, text="--", font=f(14, "bold"),
                               text_color=COLOR_TEXT)
            val.pack(anchor="w", padx=10, pady=(0, 6))
            return val

        self._weather_low_lbl = mk_info(info_row, "最低")
        self._weather_high_lbl = mk_info(info_row, "最高")
        self._weather_hum_lbl = mk_info(info_row, "湿度")

        # 按钮
        btns = ctk.CTkFrame(page, fg_color="transparent")
        btns.grid(row=2, column=0, sticky="ew")
        btns.grid_columnconfigure(0, weight=1)
        btns.grid_columnconfigure(1, weight=1)

        self._weather_refresh_btn = ctk.CTkButton(
            btns, text="刷新天气", font=f(13), height=38,
            corner_radius=10, fg_color=COLOR_CARD_ALT, hover_color=COLOR_CARD,
            text_color=COLOR_TEXT, command=self._on_refresh_weather,
        )
        self._weather_refresh_btn.grid(row=0, column=0, sticky="ew", padx=(0, 6))

        self._weather_push_btn = ctk.CTkButton(
            btns, text="推送到 ESP32", font=f(13, "bold"), height=38,
            corner_radius=10, fg_color=COLOR_ACCENT, hover_color=COLOR_ACCENT_H,
            text_color=COLOR_TEXT, command=self._on_push_weather,
        )
        self._weather_push_btn.grid(row=0, column=1, sticky="ew", padx=(6, 0))

        # 自动推送
        auto_row = ctk.CTkFrame(page, fg_color="transparent")
        auto_row.grid(row=3, column=0, sticky="ew", pady=(12, 0))

        self._weather_auto_var = ctk.BooleanVar(value=False)
        self._weather_auto_switch = ctk.CTkSwitch(
            auto_row, text=f"自动推送 (每 {AUTO_PUSH_INTERVAL_SEC // 60} 分钟)",
            font=f(12), text_color=COLOR_TEXT,
            progress_color=COLOR_ACCENT,
            variable=self._weather_auto_var, command=self._on_auto_toggle,
        )
        self._weather_auto_switch.pack(side="left")

        self._weather_auto_status = ctk.CTkLabel(page, text="", font=f(10),
                                                   text_color=COLOR_MUTED)
        self._weather_auto_status.grid(row=4, column=0, sticky="w", pady=(4, 0))

        return page

    # ----- Notify 页 -----

    def _build_notify(self) -> ctk.CTkFrame:
        page = ctk.CTkFrame(self._content_container, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)
        page.grid_rowconfigure(3, weight=1)  # Textbox 行可扩展

        ctk.CTkLabel(page, text="通知推送", font=f(22, "bold"),
                     text_color=COLOR_TEXT).grid(row=0, column=0, sticky="w")

        # 类别 + 优先级
        top = ctk.CTkFrame(page, fg_color="transparent")
        top.grid(row=1, column=0, sticky="ew", pady=(16, 8))

        ctk.CTkLabel(top, text="类别", font=f(11), text_color=COLOR_MUTED
                     ).pack(side="left", padx=(0, 6))
        self._notify_cat_menu = ctk.CTkOptionMenu(
            top, values=[name for name, _ in NOTIFY_CATEGORIES],
            font=f(12), width=120,
            fg_color=COLOR_CARD, button_color=COLOR_CARD_ALT,
            button_hover_color=COLOR_ACCENT, text_color=COLOR_TEXT,
            dropdown_fg_color=COLOR_CARD, dropdown_text_color=COLOR_TEXT,
            dropdown_hover_color=COLOR_ACCENT_H,
        )
        self._notify_cat_menu.set("Message")
        self._notify_cat_menu.pack(side="left", padx=(0, 16))

        ctk.CTkLabel(top, text="优先级", font=f(11), text_color=COLOR_MUTED
                     ).pack(side="left", padx=(0, 6))
        self._notify_pri_menu = ctk.CTkOptionMenu(
            top, values=[name for name, _ in NOTIFY_PRIORITIES],
            font=f(12), width=100,
            fg_color=COLOR_CARD, button_color=COLOR_CARD_ALT,
            button_hover_color=COLOR_ACCENT, text_color=COLOR_TEXT,
            dropdown_fg_color=COLOR_CARD, dropdown_text_color=COLOR_TEXT,
            dropdown_hover_color=COLOR_ACCENT_H,
        )
        self._notify_pri_menu.set("Normal")
        self._notify_pri_menu.pack(side="left")

        # 标题
        ctk.CTkLabel(page, text="标题", font=f(11), text_color=COLOR_MUTED
                     ).grid(row=2, column=0, sticky="w", pady=(4, 2))
        self._notify_title = ctk.CTkEntry(
            page, font=f(12), height=32, fg_color=COLOR_CARD,
            border_color=COLOR_CARD_ALT, text_color=COLOR_TEXT,
            placeholder_text="Notification title",
        )
        self._notify_title.grid(row=2, column=0, sticky="ew", pady=(20, 0))

        # 内容
        body_box = ctk.CTkFrame(page, fg_color="transparent")
        body_box.grid(row=3, column=0, sticky="nsew", pady=(10, 0))
        body_box.grid_columnconfigure(0, weight=1)
        body_box.grid_rowconfigure(1, weight=1)

        ctk.CTkLabel(body_box, text="内容", font=f(11), text_color=COLOR_MUTED
                     ).grid(row=0, column=0, sticky="w", pady=(0, 2))
        self._notify_body = ctk.CTkTextbox(
            body_box, font=f(12), height=90, fg_color=COLOR_CARD,
            border_width=1, border_color=COLOR_CARD_ALT, text_color=COLOR_TEXT,
            wrap="word",
        )
        self._notify_body.grid(row=1, column=0, sticky="nsew")

        # 提示
        ctk.CTkLabel(page,
            text="提示: 固件字体仅支持 ASCII / 拉丁字符，中文会显示为方块。",
            font=f(10), text_color=COLOR_MUTED,
        ).grid(row=4, column=0, sticky="w", pady=(8, 0))

        # 推送按钮
        self._notify_push_btn = ctk.CTkButton(
            page, text="推送通知", font=f(13, "bold"), height=42,
            corner_radius=10, fg_color=COLOR_ACCENT, hover_color=COLOR_ACCENT_H,
            text_color=COLOR_TEXT, command=self._on_push_notify,
        )
        self._notify_push_btn.grid(row=5, column=0, sticky="ew", pady=(10, 0))

        return page

    # ----- 状态 / 按钮 -----

    def _set_status(self, text: str, kind: str = "info") -> None:
        """kind: info/success/error/busy"""
        color = {
            "info":    COLOR_MUTED,
            "success": COLOR_SUCCESS,
            "error":   COLOR_DANGER,
            "busy":    COLOR_WARN,
        }.get(kind, COLOR_MUTED)
        self._status_text.configure(text=text, text_color=color)

    def _refresh_state(self) -> None:
        """根据 busy + connected 同步所有按钮 state 及侧边栏状态灯"""
        connected = self._ble.connected
        can_ble = (not self._busy) and connected

        # 侧边栏底部
        if self._busy:
            dot_color = COLOR_WARN
        elif connected:
            dot_color = COLOR_SUCCESS
        else:
            dot_color = COLOR_DANGER
        self._status_dot.configure(text_color=dot_color)

        addr = self._ble.address if connected else ""
        self._status_addr.configure(text=addr)

        # Home 页
        self._home_status_lbl.configure(
            text="● 已连接" if connected else "● 未连接",
            text_color=COLOR_SUCCESS if connected else COLOR_MUTED,
        )
        self._home_addr_lbl.configure(text=f"Address: {addr}" if addr else "")
        self._connect_btn.configure(
            text="断开连接" if connected else "连接设备",
            fg_color=COLOR_DANGER if connected else COLOR_ACCENT,
            hover_color="#DC2626" if connected else COLOR_ACCENT_H,
            state="disabled" if self._busy else "normal",
        )

        # Time 页
        state_ble = "normal" if can_ble else "disabled"
        self._time_read_btn.configure(state=state_ble)
        self._time_sync_btn.configure(state=state_ble)

        # Weather 页
        self._weather_refresh_btn.configure(state="disabled" if self._busy else "normal")
        can_push_weather = can_ble and self._latest_weather is not None
        self._weather_push_btn.configure(state="normal" if can_push_weather else "disabled")

        # Notify 页
        self._notify_push_btn.configure(state=state_ble)

    # ----- 异步桥接 -----

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def _submit(self, coro, done_callback) -> None:
        self._busy = True
        self._refresh_state()
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)

        def on_done(fut):
            exc = fut.exception()
            result = None if exc else fut.result()
            def finish():
                done_callback(exc, result)
                self._busy = False
                self._refresh_state()
            self.after(0, finish)

        future.add_done_callback(on_done)

    # ----- 连接 -----

    def _on_connect_click(self) -> None:
        if self._ble.connected:
            self._set_status("断开中...", "busy")

            def done(exc, _r):
                if exc is None:
                    self._set_status("未连接", "info")
                    if self._weather_auto_var.get():
                        self._weather_auto_var.set(False)
                        self._stop_auto_timer()
                        self._weather_auto_status.configure(text="断开后已关闭自动推送")
                else:
                    self._set_status(f"断开失败: {exc}", "error")

            self._submit(self._ble.disconnect(), done)
        else:
            self._set_status("连接中...", "busy")

            def done(exc, _r):
                if exc is None:
                    self._set_status("已连接", "success")
                else:
                    self._set_status(f"连接失败: {exc}", "error")

            self._submit(self._ble.connect(), done)

    # ----- 时间 -----

    def _render_time(self, dt: datetime) -> None:
        self._time_lbl.configure(text=dt.strftime("%H:%M:%S"))
        self._time_date_lbl.configure(text=dt.strftime("%Y-%m-%d  %A"))

    def _on_read_time(self) -> None:
        self._set_status("读取时间...", "busy")

        def done(exc, result):
            if exc is None:
                self._render_time(result)
                self._set_status("已连接", "success")
            else:
                self._set_status(f"读取失败: {exc}", "error")

        self._submit(self._ble.read_time(), done)

    def _on_sync_time(self) -> None:
        self._set_status("同步时间...", "busy")
        now = datetime.now()

        async def task():
            await self._ble.write_time(now)
            return await self._ble.read_time()

        def done(exc, result):
            if exc is None:
                self._render_time(result)
                self._set_status("同步成功", "success")
            else:
                self._set_status(f"同步失败: {exc}", "error")

        self._submit(task(), done)

    # ----- 天气 -----

    async def _fetch_weather_task(self) -> Weather:
        loop = asyncio.get_running_loop()
        if self._location is None:
            self._location = await loop.run_in_executor(None, locate_by_ip)
        lat, lon, city = self._location
        return await loop.run_in_executor(None, fetch_weather, lat, lon, city)

    def _render_weather(self, w: Weather) -> None:
        lat, lon, city = self._location or (0.0, 0.0, "")
        self._weather_city_lbl.configure(text=f"位置: {city}  ({lat:.2f}, {lon:.2f})")
        self._weather_temp_lbl.configure(text=f"{w.temp:.1f}°")
        color = CODE_COLOR.get(wmo_to_code(w.wmo), COLOR_ACCENT)
        self._weather_desc_lbl.configure(text=w.desc(), text_color=color)
        self._weather_low_lbl.configure(text=f"{w.temp_min:.0f}°")
        self._weather_high_lbl.configure(text=f"{w.temp_max:.0f}°")
        self._weather_hum_lbl.configure(text=f"{w.humidity}%")

    def _on_refresh_weather(self) -> None:
        self._set_status("拉取天气...", "busy")

        def done(exc, w):
            if exc is None:
                self._latest_weather = w
                self._render_weather(w)
                self._set_status(
                    "已连接" if self._ble.connected else "未连接",
                    "success" if self._ble.connected else "info",
                )
            else:
                self._set_status(f"拉取失败: {exc}", "error")

        self._submit(self._fetch_weather_task(), done)

    def _on_push_weather(self) -> None:
        if self._latest_weather is None:
            return
        self._set_status("推送天气...", "busy")

        def done(exc, _r):
            if exc is None:
                self._set_status("天气已推送", "success")
                self._weather_auto_status.configure(
                    text=f"上次推送: {datetime.now():%H:%M:%S}")
            else:
                self._set_status(f"推送失败: {exc}", "error")

        self._submit(self._ble.write_weather(self._latest_weather), done)

    def _on_auto_toggle(self) -> None:
        if self._weather_auto_var.get():
            if not self._ble.connected:
                self._weather_auto_var.set(False)
                self._set_status("请先连接再开启自动推送", "error")
                return
            self._weather_auto_status.configure(text="自动推送已开启")
            self._run_auto_cycle()
        else:
            self._stop_auto_timer()
            self._weather_auto_status.configure(text="自动推送已关闭")

    def _run_auto_cycle(self) -> None:
        async def task():
            w = await self._fetch_weather_task()
            self._latest_weather = w
            await self._ble.write_weather(w)
            return w

        def done(exc, w):
            if exc is None:
                self._render_weather(w)
                self._weather_auto_status.configure(
                    text=f"上次推送: {datetime.now():%H:%M:%S}  "
                          f"下次: ~{AUTO_PUSH_INTERVAL_SEC // 60} 分钟后"
                )
            else:
                self._weather_auto_status.configure(text=f"推送失败: {exc}")

            if self._weather_auto_var.get():
                self._auto_timer = self.after(
                    AUTO_PUSH_INTERVAL_SEC * 1000, self._run_auto_cycle)

        self._submit(task(), done)

    def _stop_auto_timer(self) -> None:
        if self._auto_timer is not None:
            self.after_cancel(self._auto_timer)
            self._auto_timer = None

    # ----- 通知推送 -----

    def _on_push_notify(self) -> None:
        title = self._notify_title.get().strip()
        body = self._notify_body.get("1.0", "end-1c")
        if not title and not body:
            self._set_status("标题或内容至少填一个", "error")
            return

        cat = dict(NOTIFY_CATEGORIES)[self._notify_cat_menu.get()]
        pri = dict(NOTIFY_PRIORITIES)[self._notify_pri_menu.get()]

        self._set_status("推送通知...", "busy")

        def done(exc, _r):
            if exc is None:
                self._set_status("通知已推送", "success")
                self._notify_title.delete(0, "end")
                self._notify_body.delete("1.0", "end")
            else:
                self._set_status(f"推送失败: {exc}", "error")

        self._submit(self._ble.write_notification(cat, pri, title, body), done)

    # ----- 关闭 -----

    def _on_close(self) -> None:
        self._stop_auto_timer()
        try:
            if self._ble.connected:
                asyncio.run_coroutine_threadsafe(
                    self._ble.disconnect(), self._loop
                ).result(timeout=3)
        except Exception:
            pass
        self._loop.call_soon_threadsafe(self._loop.stop)
        self.destroy()

    def run(self) -> None:
        self.mainloop()


if __name__ == "__main__":
    _enable_windows_dpi_awareness()
    App().run()
