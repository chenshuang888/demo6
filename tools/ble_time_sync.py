"""ESP32 BLE Companion (Tkinter GUI).

统一的 PC 端配套工具，整合两块功能：
  1. 时间同步 — Current Time Service (UUID 0x2A2B)
  2. 天气推送 — 自定义天气服务 (UUID 8a5c0002-...)

天气数据来源：
  - IP 定位: ip-api.com (免费无需 Key)
  - 天气数据: open-meteo.com (免费无需 Key)

依赖安装:
    pip install -r requirements.txt

运行:
    python ble_time_sync.py
"""

import asyncio
import struct
import threading
import time as _time
import tkinter as tk
from dataclasses import dataclass
from datetime import datetime
from tkinter import ttk

import requests
from bleak import BleakClient, BleakScanner

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

DEVICE_NAME = "ESP32-S3-DEMO"
CTS_CHAR_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"
WEATHER_CHAR_UUID = "8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6"

SCAN_TIMEOUT = 10.0
CTS_STRUCT = "<HBBBBBBBB"                    # 10 字节, ble_cts_current_time_t
WEATHER_STRUCT = "<hhhBBI24s32s"             # 68 字节, weather_payload_t

AUTO_PUSH_INTERVAL_SEC = 600                 # 10 分钟

# 天气编码（与 services/weather_manager.h 对齐）
WC_UNKNOWN, WC_CLEAR, WC_CLOUDY, WC_OVERCAST = 0, 1, 2, 3
WC_RAIN, WC_SNOW, WC_FOG, WC_THUNDER = 4, 5, 6, 7


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


# ---------------------------------------------------------------------------
# 天气数据 + 打包
# ---------------------------------------------------------------------------

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
# BLE 客户端
# ---------------------------------------------------------------------------

class BleClient:
    def __init__(self):
        self._client: BleakClient | None = None

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.is_connected

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
        # Python weekday(): 0=周一..6=周日 -> 固件: 1=周一..7=周日
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


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

class App:
    def __init__(self):
        self._ble = BleClient()
        self._loop = asyncio.new_event_loop()
        self._loop_thread = threading.Thread(target=self._run_loop, daemon=True)
        self._loop_thread.start()

        self._location: tuple[float, float, str] | None = None
        self._latest_weather: Weather | None = None
        self._auto_timer: str | None = None

        self._build_ui()

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    # ----- UI 布局 -----

    def _build_ui(self) -> None:
        self.root = tk.Tk()
        self.root.title("ESP32 BLE Companion")
        self.root.geometry("400x480")
        self.root.resizable(False, False)

        FONT_UI = ("Microsoft YaHei UI", 10)
        FONT_SECTION = ("Microsoft YaHei UI", 10, "bold")
        FONT_STATUS = ("Microsoft YaHei UI", 11)

        # ===== 连接区 =====
        self.status_var = tk.StringVar(value="● 未连接")
        self.status_label = tk.Label(self.root, textvariable=self.status_var,
                                     fg="gray", font=FONT_STATUS)
        self.status_label.pack(padx=12, pady=(14, 6))

        self.connect_btn = ttk.Button(self.root, text=f"连接 {DEVICE_NAME}",
                                      command=self._on_connect_click)
        self.connect_btn.pack(fill="x", padx=12, pady=6)

        # ===== 时间区 =====
        ttk.Separator(self.root, orient="horizontal").pack(fill="x", padx=12, pady=(12, 4))
        ttk.Label(self.root, text="时间同步", font=FONT_SECTION).pack(anchor="w", padx=12)

        self.time_var = tk.StringVar(value="ESP32 时间: --")
        tk.Label(self.root, textvariable=self.time_var,
                 font=("Consolas", 11)).pack(padx=12, pady=4)

        time_btn_frame = tk.Frame(self.root)
        time_btn_frame.pack(fill="x", padx=12, pady=(0, 6))
        self.read_btn = ttk.Button(time_btn_frame, text="读取时间",
                                   command=self._on_read_time, state="disabled")
        self.read_btn.pack(side="left", expand=True, fill="x", padx=(0, 5))
        self.sync_btn = ttk.Button(time_btn_frame, text="同步电脑时间",
                                   command=self._on_sync_time, state="disabled")
        self.sync_btn.pack(side="right", expand=True, fill="x", padx=(5, 0))

        # ===== 天气区 =====
        ttk.Separator(self.root, orient="horizontal").pack(fill="x", padx=12, pady=(12, 4))
        ttk.Label(self.root, text="天气推送", font=FONT_SECTION).pack(anchor="w", padx=12)

        self.location_var = tk.StringVar(value="位置: --")
        tk.Label(self.root, textvariable=self.location_var,
                 font=FONT_UI, anchor="w").pack(fill="x", padx=12, pady=(4, 2))

        self.weather_var = tk.StringVar(value="天气: --")
        tk.Label(self.root, textvariable=self.weather_var,
                 font=FONT_UI, anchor="w", justify="left", wraplength=370).pack(fill="x", padx=12, pady=2)

        weather_btn_frame = tk.Frame(self.root)
        weather_btn_frame.pack(fill="x", padx=12, pady=(6, 4))
        self.refresh_weather_btn = ttk.Button(weather_btn_frame, text="刷新天气",
                                              command=self._on_refresh_weather)
        self.refresh_weather_btn.pack(side="left", expand=True, fill="x", padx=(0, 5))
        self.push_weather_btn = ttk.Button(weather_btn_frame, text="推送到 ESP32",
                                           command=self._on_push_weather, state="disabled")
        self.push_weather_btn.pack(side="right", expand=True, fill="x", padx=(5, 0))

        self.auto_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(self.root,
                        text=f"自动推送（每 {AUTO_PUSH_INTERVAL_SEC // 60} 分钟）",
                        variable=self.auto_var,
                        command=self._on_auto_toggle).pack(anchor="w", padx=12, pady=(6, 2))

        self.auto_status_var = tk.StringVar(value="")
        tk.Label(self.root, textvariable=self.auto_status_var,
                 fg="gray", font=("Microsoft YaHei UI", 9)).pack(anchor="w", padx=12, pady=(0, 10))

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ----- 状态 / 按钮 -----

    def _set_status(self, text: str, color: str) -> None:
        self.status_var.set(text)
        self.status_label.config(fg=color)

    def _refresh_buttons(self, busy: bool) -> None:
        self.connect_btn.config(state="disabled" if busy else "normal")
        can_ble = (not busy) and self._ble.connected
        self.read_btn.config(state="normal" if can_ble else "disabled")
        self.sync_btn.config(state="normal" if can_ble else "disabled")
        self.refresh_weather_btn.config(state="disabled" if busy else "normal")
        can_push = can_ble and self._latest_weather is not None
        self.push_weather_btn.config(state="normal" if can_push else "disabled")

    # ----- 异步桥接 -----

    def _submit(self, coro, done_callback) -> None:
        """提交协程到后台 loop; done_callback(exc, result) 在 Tk 主线程执行."""
        self._refresh_buttons(busy=True)
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)

        def on_done(fut):
            exc = fut.exception()
            result = None if exc else fut.result()
            def finish():
                done_callback(exc, result)
                self._refresh_buttons(busy=False)
            self.root.after(0, finish)

        future.add_done_callback(on_done)

    # ----- 连接 -----

    def _on_connect_click(self) -> None:
        if self._ble.connected:
            self._set_status("● 断开中...", "orange")

            def done(exc, _r):
                if exc is None:
                    self._set_status("● 未连接", "gray")
                    self.connect_btn.config(text=f"连接 {DEVICE_NAME}")
                    self.time_var.set("ESP32 时间: --")
                    # 连接断开后自动停用自动推送
                    if self.auto_var.get():
                        self.auto_var.set(False)
                        self._stop_auto_timer()
                        self.auto_status_var.set("断开后已关闭自动推送")
                else:
                    self._set_status(f"● 断开失败: {exc}", "red")

            self._submit(self._ble.disconnect(), done)
        else:
            self._set_status("● 连接中...", "orange")

            def done(exc, _r):
                if exc is None:
                    self._set_status("● 已连接", "green")
                    self.connect_btn.config(text="断开")
                else:
                    self._set_status(f"● 连接失败: {exc}", "red")

            self._submit(self._ble.connect(), done)

    # ----- 时间 -----

    def _on_read_time(self) -> None:
        self._set_status("● 读取中...", "orange")

        def done(exc, result):
            if exc is None:
                self.time_var.set(f"ESP32 时间: {result:%Y-%m-%d %H:%M:%S}")
                self._set_status("● 已连接", "green")
            else:
                self._set_status(f"● 读取失败: {exc}", "red")

        self._submit(self._ble.read_time(), done)

    def _on_sync_time(self) -> None:
        self._set_status("● 同步中...", "orange")
        now = datetime.now()

        async def task():
            await self._ble.write_time(now)
            return await self._ble.read_time()

        def done(exc, result):
            if exc is None:
                self.time_var.set(f"ESP32 时间: {result:%Y-%m-%d %H:%M:%S}")
                self._set_status("● 同步成功", "green")
            else:
                self._set_status(f"● 同步失败: {exc}", "red")

        self._submit(task(), done)

    # ----- 天气 -----

    async def _fetch_weather_task(self) -> Weather:
        """定位 + 拉取天气（同步 API 放在 executor 里，避免阻塞 event loop）"""
        loop = asyncio.get_running_loop()
        if self._location is None:
            self._location = await loop.run_in_executor(None, locate_by_ip)
        lat, lon, city = self._location
        return await loop.run_in_executor(None, fetch_weather, lat, lon, city)

    def _render_weather(self, w: Weather) -> None:
        lat, lon, city = self._location
        self.location_var.set(f"位置: {city} ({lat:.4f}, {lon:.4f})")
        self.weather_var.set(
            f"天气: {w.temp:.1f}°C {w.desc()}, 湿度 {w.humidity}%  "
            f"(min {w.temp_min:.0f}° / max {w.temp_max:.0f}°)"
        )

    def _on_refresh_weather(self) -> None:
        self._set_status("● 拉取天气...", "orange")

        def done(exc, w):
            if exc is None:
                self._latest_weather = w
                self._render_weather(w)
                self._set_status("● 已连接" if self._ble.connected else "● 未连接",
                                 "green" if self._ble.connected else "gray")
            else:
                self._set_status(f"● 拉取失败: {exc}", "red")

        self._submit(self._fetch_weather_task(), done)

    def _on_push_weather(self) -> None:
        if self._latest_weather is None:
            return
        self._set_status("● 推送中...", "orange")

        def done(exc, _r):
            if exc is None:
                self._set_status("● 已推送", "green")
                self.auto_status_var.set(f"上次推送: {datetime.now():%H:%M:%S}")
            else:
                self._set_status(f"● 推送失败: {exc}", "red")

        self._submit(self._ble.write_weather(self._latest_weather), done)

    # ----- 自动推送 -----

    def _on_auto_toggle(self) -> None:
        if self.auto_var.get():
            if not self._ble.connected:
                self.auto_var.set(False)
                self._set_status("● 请先连接再开启自动推送", "red")
                return
            self.auto_status_var.set("自动推送已开启")
            self._run_auto_cycle()
        else:
            self._stop_auto_timer()
            self.auto_status_var.set("自动推送已关闭")

    def _run_auto_cycle(self) -> None:
        """立即拉取 + 推送，然后调度下一次"""
        async def task():
            w = await self._fetch_weather_task()
            self._latest_weather = w
            await self._ble.write_weather(w)
            return w

        def done(exc, w):
            if exc is None:
                self._render_weather(w)
                self.auto_status_var.set(
                    f"上次推送: {datetime.now():%H:%M:%S}  "
                    f"下次: ~{AUTO_PUSH_INTERVAL_SEC // 60} 分钟后"
                )
            else:
                self.auto_status_var.set(f"推送失败: {exc}")

            # 下次调度；即使失败也继续尝试
            if self.auto_var.get():
                self._auto_timer = self.root.after(
                    AUTO_PUSH_INTERVAL_SEC * 1000, self._run_auto_cycle
                )

        self._submit(task(), done)

    def _stop_auto_timer(self) -> None:
        if self._auto_timer is not None:
            self.root.after_cancel(self._auto_timer)
            self._auto_timer = None

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
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    App().run()
