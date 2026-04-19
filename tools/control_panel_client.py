"""ESP32 Control Panel Subscriber.

订阅 ESP32 的 control characteristic，把屏上按钮事件映射成 Windows 动作：
  id=0  → 锁屏
  id=1  → 静音切换
  id=2  → 上一首
  id=3  → 下一首

依赖 (已包含在 requirements.txt):
    pip install -r requirements.txt

运行:
    python control_panel_client.py              # 实际执行 Windows 动作
    python control_panel_client.py --dry-run    # 只打印事件，不触发系统动作（联调用）

Ctrl+C 退出。连接断开会自动重连。
"""

import argparse
import asyncio
import ctypes
import struct
import sys
import time as _time

from bleak import BleakClient, BleakScanner

# ---------------------------------------------------------------------------
# 常量（与 services/control_service.h 严格对齐）
# ---------------------------------------------------------------------------

DEVICE_NAME = "ESP32-S3-DEMO"
CONTROL_CHAR_UUID = "8a5c0006-0000-4aef-b87e-4fa1e0c7e0f6"

# control_event_t: type u8, id u8, action u8, _reserved u8, value i16, seq u16
EVENT_STRUCT = "<BBBBhH"
EVENT_SIZE = struct.calcsize(EVENT_STRUCT)   # = 8

SCAN_TIMEOUT_S = 10.0
RECONNECT_DELAY_S = 3.0

# ---------------------------------------------------------------------------
# Windows 动作
# ---------------------------------------------------------------------------

VK_VOLUME_MUTE      = 0xAD
VK_MEDIA_PREV_TRACK = 0xB1
VK_MEDIA_NEXT_TRACK = 0xB0
KEYEVENTF_KEYUP     = 0x0002


def _send_key(vk: int) -> None:
    """发一个标准虚拟键（press + release）。media/volume 键的标准做法。"""
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)


def _lock_screen() -> None:
    ctypes.windll.user32.LockWorkStation()


# id → (显示名, 动作函数)
BUTTON_ACTIONS = {
    0: ("Lock",  _lock_screen),
    1: ("Mute",  lambda: _send_key(VK_VOLUME_MUTE)),
    2: ("Prev",  lambda: _send_key(VK_MEDIA_PREV_TRACK)),
    3: ("Next",  lambda: _send_key(VK_MEDIA_NEXT_TRACK)),
}


# ---------------------------------------------------------------------------
# 事件回调
# ---------------------------------------------------------------------------

class Handler:
    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        # 上一次已处理的 seq；同一 seq 重复到达则忽略。
        # ESP32 重启后 seq 会从 1 重新计数，小概率与 _last_seq 相同被误丢；
        # 下一次按钮就能解锁，可以接受。
        self._last_seq: int | None = None

    def on_notify(self, _handle: int, data: bytearray) -> None:
        if len(data) != EVENT_SIZE:
            self._log(f"[warn] 非预期长度 {len(data)} B: {bytes(data).hex()}")
            return

        type_, btn_id, action, _r, value, seq = struct.unpack(
            EVENT_STRUCT, bytes(data))

        # 只处理 button press 事件
        if type_ != 0 or action != 0:
            self._log(f"[info] 忽略 type={type_} action={action} id={btn_id} seq={seq}")
            return

        if seq == self._last_seq:
            self._log(f"[dup]  seq={seq} 重复，忽略")
            return
        self._last_seq = seq

        entry = BUTTON_ACTIONS.get(btn_id)
        if entry is None:
            self._log(f"[warn] 未知按钮 id={btn_id} seq={seq}")
            return

        name, fn = entry
        prefix = "[dry]" if self.dry_run else "[exec]"
        self._log(f"{prefix} id={btn_id} seq={seq} value={value}  → {name}")

        if self.dry_run:
            return

        try:
            fn()
        except Exception as e:
            self._log(f"[err]  动作执行失败: {e}")

    @staticmethod
    def _log(msg: str) -> None:
        print(f"{_time.strftime('%H:%M:%S')}  {msg}", flush=True)


# ---------------------------------------------------------------------------
# 连接生命周期
# ---------------------------------------------------------------------------

async def run_session(handler: Handler) -> None:
    """单次完整会话：扫描 → 连接 → 订阅 → 等待断开。出错直接抛。"""
    print(f"{_time.strftime('%H:%M:%S')}  扫描 {DEVICE_NAME}...", flush=True)
    device = await BleakScanner.find_device_by_name(
        DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"{SCAN_TIMEOUT_S:.0f}s 内未发现 {DEVICE_NAME}")

    print(f"{_time.strftime('%H:%M:%S')}  命中 {device.address}，连接中...", flush=True)
    async with BleakClient(device) as client:
        await client.start_notify(CONTROL_CHAR_UUID, handler.on_notify)
        print(f"{_time.strftime('%H:%M:%S')}  已订阅 {CONTROL_CHAR_UUID}，等待事件\n", flush=True)
        while client.is_connected:
            await asyncio.sleep(0.5)
        print(f"\n{_time.strftime('%H:%M:%S')}  连接断开", flush=True)


async def main(handler: Handler) -> None:
    while True:
        try:
            await run_session(handler)
        except asyncio.CancelledError:
            return
        except Exception as e:
            print(f"{_time.strftime('%H:%M:%S')}  会话结束: {e}", flush=True)
        print(f"{_time.strftime('%H:%M:%S')}  {RECONNECT_DELAY_S:.0f}s 后重试\n", flush=True)
        await asyncio.sleep(RECONNECT_DELAY_S)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dry-run", action="store_true",
                   help="只打印事件、不实际执行 Windows 动作（联调用）")
    return p.parse_args()


if __name__ == "__main__":
    if sys.platform != "win32":
        print("warning: 当前脚本的动作映射基于 Windows user32 API，"
              "其他平台只能用 --dry-run 看事件", file=sys.stderr)

    args = parse_args()
    handler = Handler(dry_run=args.dry_run)
    try:
        asyncio.run(main(handler))
    except KeyboardInterrupt:
        print("\nbye")
