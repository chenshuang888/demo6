"""UploaderClient —— BLE 上传/运维客户端。

短生命周期：连接 → 操作 → 断开。每次 GUI 操作开一个新 client。

线程模型：bleak 是 asyncio。GUI 用 threading 跑一个独立 event loop，
通过 asyncio.run_coroutine_threadsafe 提交协程。

不复用 dynapp_sdk.DynappClient，因为：
  - GATT service 不同（a3a4 vs a3a3）
  - 协议是请求-响应（每帧等 status），不是事件流
  - bleak 一次只能 connect 一个 ESP，跟 bridge client 互斥
"""

from __future__ import annotations

import asyncio
import logging
import os
from dataclasses import dataclass
from typing import Awaitable, Callable, Optional

from bleak import BleakClient, BleakScanner

from .constants import (
    DEFAULT_DEVICE_NAME_HINT,
    MAX_CHUNK, MAX_SCRIPT_BYTES, NAME_LEN,
    OP_CHUNK, OP_END, OP_START, OP_DELETE, OP_LIST,
    RESULT_OK, RESULT_NAMES,
    RX_UUID, STATUS_UUID, SVC_UUID,
)
from .protocol import (
    StatusFrame, chunk_iter, crc32_of,
    pack_chunk, pack_delete, pack_end, pack_list, pack_start,
    parse_status,
)

logger = logging.getLogger(__name__)


class UploadError(Exception):
    """上传/运维 操作失败。message 里带 result 码。"""


# =============================================================================
# Client
# =============================================================================

ProgressCb = Callable[[int, int], None]   # (sent_bytes, total_bytes)


class UploaderClient:
    """ESP32 dynapp uploader BLE 客户端。

    用法：
        async with UploaderClient(device_name="ESP32") as c:
            await c.upload_file("echo2", "echo2.js", on_progress=lambda s,t: ...)
            names = await c.list_apps()
            await c.delete_app("echo2")
    """

    DEFAULT_TIMEOUT = 5.0       # 单帧 status 等待秒数

    def __init__(
        self,
        *,
        device_name: str = DEFAULT_DEVICE_NAME_HINT,
        address: Optional[str] = None,
        scan_timeout: float = 10.0,
    ) -> None:
        self._device_name = device_name
        self._address = address
        self._scan_timeout = scan_timeout

        self._client: Optional[BleakClient] = None
        self._connected_addr: Optional[str] = None

        # seq → Future[StatusFrame]
        self._pending: dict[int, asyncio.Future[StatusFrame]] = {}
        self._seq = 0
        self._send_lock = asyncio.Lock()

    # ------------------------------------------------------------------
    # 连接 / 断开
    # ------------------------------------------------------------------

    async def __aenter__(self) -> "UploaderClient":
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.disconnect()

    async def connect(self) -> str:
        """扫描并连接，返回 MAC。"""
        addr = self._address
        if not addr:
            logger.info("scanning for device matching %r ...", self._device_name)
            devices = await BleakScanner.discover(timeout=self._scan_timeout)
            hint = self._device_name.lower()
            for d in devices:
                if d.name and hint in d.name.lower():
                    addr = d.address
                    logger.info("found %s @ %s", d.name, addr)
                    break
            if not addr:
                raise UploadError(f"no device found matching {self._device_name!r}")

        self._client = BleakClient(addr)
        await self._client.connect()
        self._connected_addr = addr

        # subscribe status
        await self._client.start_notify(STATUS_UUID, self._on_status)
        logger.info("connected and subscribed to status")
        return addr

    async def disconnect(self) -> None:
        if self._client:
            try:
                await self._client.stop_notify(STATUS_UUID)
            except Exception:
                pass
            try:
                await self._client.disconnect()
            except Exception:
                pass
            self._client = None
        # 取消所有未完成的 future
        for fut in self._pending.values():
            if not fut.done():
                fut.cancel()
        self._pending.clear()

    @property
    def connected_address(self) -> Optional[str]:
        return self._connected_addr

    # ------------------------------------------------------------------
    # 高层 API
    # ------------------------------------------------------------------

    async def upload_file(
        self,
        name: str,
        path: str,
        *,
        on_progress: Optional[ProgressCb] = None,
    ) -> None:
        with open(path, "rb") as f:
            data = f.read()
        await self.upload_bytes(name, data, on_progress=on_progress)

    async def upload_bytes(
        self,
        name: str,
        data: bytes,
        *,
        on_progress: Optional[ProgressCb] = None,
    ) -> None:
        if len(name.encode("ascii")) > NAME_LEN:
            raise ValueError(f"name too long (max {NAME_LEN}): {name}")
        if not data:
            raise ValueError("empty payload")
        if len(data) > MAX_SCRIPT_BYTES:
            raise ValueError(f"data too large: {len(data)} > {MAX_SCRIPT_BYTES}")

        total = len(data)
        crc = crc32_of(data)
        logger.info("upload %s: %d B, crc=0x%08x", name, total, crc)

        # START
        st = await self._send_and_wait(pack_start(name, total, crc, self._next_seq()))
        self._raise_if_bad(st, "START")

        # CHUNKs
        sent = 0
        for offset, chunk in chunk_iter(data, MAX_CHUNK):
            st = await self._send_and_wait(pack_chunk(offset, chunk, self._next_seq()))
            self._raise_if_bad(st, f"CHUNK off={offset}")
            sent = offset + len(chunk)
            if on_progress:
                on_progress(sent, total)

        # END
        st = await self._send_and_wait(pack_end(self._next_seq()))
        self._raise_if_bad(st, "END")
        logger.info("upload %s: done", name)

    async def list_apps(self) -> list[str]:
        st = await self._send_and_wait(pack_list(self._next_seq()))
        self._raise_if_bad(st, "LIST")
        return st.names

    async def delete_app(self, name: str) -> None:
        st = await self._send_and_wait(pack_delete(name, self._next_seq()))
        self._raise_if_bad(st, "DELETE")
        logger.info("deleted %s", name)

    # ------------------------------------------------------------------
    # 底层
    # ------------------------------------------------------------------

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFF
        # 避免 0（status_cb 用 0 不容易区分初始态，无影响但更显眼）
        if self._seq == 0:
            self._seq = 1
        return self._seq

    async def _send_and_wait(
        self, frame: bytes, timeout: Optional[float] = None
    ) -> StatusFrame:
        if not self._client or not self._client.is_connected:
            raise UploadError("not connected")
        seq = frame[1]   # header 第二字节就是 seq
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[StatusFrame] = loop.create_future()
        self._pending[seq] = fut

        async with self._send_lock:
            try:
                await self._client.write_gatt_char(RX_UUID, frame, response=False)
            except Exception:
                self._pending.pop(seq, None)
                raise

        try:
            return await asyncio.wait_for(fut, timeout or self.DEFAULT_TIMEOUT)
        except asyncio.TimeoutError:
            self._pending.pop(seq, None)
            raise UploadError(f"timeout waiting status for seq={seq}")

    def _on_status(self, _char, data: bytearray) -> None:
        """bleak notify 回调，跑在 bleak 内部的 asyncio task。"""
        try:
            st = parse_status(bytes(data))
        except ValueError as e:
            logger.warning("bad status frame: %s", e)
            return

        fut = self._pending.pop(st.seq, None)
        if fut is None:
            logger.debug("status with no waiter: op=0x%02x seq=%d", st.op, st.seq)
            return
        if not fut.done():
            fut.set_result(st)

    def _raise_if_bad(self, st: StatusFrame, label: str) -> None:
        if st.result != RESULT_OK:
            name = RESULT_NAMES.get(st.result, f"0x{st.result:02x}")
            raise UploadError(f"{label} failed: {name}")
