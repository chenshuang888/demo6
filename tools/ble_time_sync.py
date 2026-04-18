"""ESP32 BLE 时间同步工具 (Tkinter GUI).

依赖安装:
    pip install bleak>=0.22

运行:
    python ble_time_sync.py

功能: 与 ESP32-S3-DEMO 固件 (services/time_service.c) 中的
Current Time Service (UUID 0x1805, 特征 0x2A2B) 通信,
一键读取 ESP32 时间或将电脑本地时间同步过去.
"""

import asyncio
import struct
import threading
import tkinter as tk
from datetime import datetime
from tkinter import ttk

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "ESP32-S3-DEMO"
CTS_CHAR_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"
SCAN_TIMEOUT = 10.0
CTS_STRUCT = "<HBBBBBBBB"  # 10 字节, 对应 ble_cts_current_time_t


class BleTimeClient:
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


class App:
    def __init__(self):
        self._ble = BleTimeClient()
        self._loop = asyncio.new_event_loop()
        self._loop_thread = threading.Thread(target=self._run_loop, daemon=True)
        self._loop_thread.start()
        self._build_ui()

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def _build_ui(self) -> None:
        self.root = tk.Tk()
        self.root.title("ESP32 BLE 时间同步")
        self.root.geometry("320x210")
        self.root.resizable(False, False)

        self.status_var = tk.StringVar(value="● 未连接")
        self.status_label = tk.Label(self.root, textvariable=self.status_var,
                                     fg="gray", font=("Microsoft YaHei UI", 11))
        self.status_label.pack(padx=12, pady=(14, 6))

        self.connect_btn = ttk.Button(self.root, text=f"连接 {DEVICE_NAME}",
                                      command=self._on_connect_click)
        self.connect_btn.pack(fill="x", padx=12, pady=6)

        self.time_var = tk.StringVar(value="ESP32 时间: --")
        tk.Label(self.root, textvariable=self.time_var,
                 font=("Consolas", 11)).pack(padx=12, pady=6)

        btn_frame = tk.Frame(self.root)
        btn_frame.pack(fill="x", padx=12, pady=(6, 12))
        self.read_btn = ttk.Button(btn_frame, text="读取时间",
                                   command=self._on_read_click, state="disabled")
        self.read_btn.pack(side="left", expand=True, fill="x", padx=(0, 5))
        self.sync_btn = ttk.Button(btn_frame, text="同步电脑时间",
                                   command=self._on_sync_click, state="disabled")
        self.sync_btn.pack(side="right", expand=True, fill="x", padx=(5, 0))

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ----- 状态与按钮管理 -----

    def _set_status(self, text: str, color: str) -> None:
        self.status_var.set(text)
        self.status_label.config(fg=color)

    def _refresh_buttons(self, busy: bool) -> None:
        self.connect_btn.config(state="disabled" if busy else "normal")
        can_use = (not busy) and self._ble.connected
        self.read_btn.config(state="normal" if can_use else "disabled")
        self.sync_btn.config(state="normal" if can_use else "disabled")

    # ----- 异步任务桥接 -----

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

    # ----- 按钮回调 -----

    def _on_connect_click(self) -> None:
        if self._ble.connected:
            self._set_status("● 断开中...", "orange")

            def done(exc, _result):
                if exc is None:
                    self._set_status("● 未连接", "gray")
                    self.connect_btn.config(text=f"连接 {DEVICE_NAME}")
                    self.time_var.set("ESP32 时间: --")
                else:
                    self._set_status(f"● 断开失败: {exc}", "red")

            self._submit(self._ble.disconnect(), done)
        else:
            self._set_status("● 连接中...", "orange")

            def done(exc, _result):
                if exc is None:
                    self._set_status("● 已连接", "green")
                    self.connect_btn.config(text="断开")
                else:
                    self._set_status(f"● 连接失败: {exc}", "red")

            self._submit(self._ble.connect(), done)

    def _on_read_click(self) -> None:
        self._set_status("● 读取中...", "orange")

        def done(exc, result):
            if exc is None:
                self.time_var.set(f"ESP32 时间: {result:%Y-%m-%d %H:%M:%S}")
                self._set_status("● 已连接", "green")
            else:
                self._set_status(f"● 读取失败: {exc}", "red")

        self._submit(self._ble.read_time(), done)

    def _on_sync_click(self) -> None:
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

    # ----- 关闭 -----

    def _on_close(self) -> None:
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
