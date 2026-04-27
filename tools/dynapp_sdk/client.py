"""DynappClient —— 连接 ESP / 路由消息 / 发送消息的主类。

典型用法见 dynapp_sdk/__init__.py 的 docstring。

线程模型:
  - 全异步，依赖 bleak 的 asyncio 事件循环
  - send 内部加锁，多个 provider 并发调用安全
  - 自动重连：掉线后 backoff 1→2→4→8→16→30s 重试

错误处理:
  - 连接失败：WARN 日志 + 退避重试，不抛
  - JSON 解析失败：DEBUG 日志，不抛
  - send payload 过长：raise ValueError（开发者编程错误，应当立即看到）
  - handler 抛异常：router 内部 catch + ERROR 日志
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Awaitable, Callable, Optional, Union

from bleak import BleakClient, BleakScanner

from .constants import (
    DEFAULT_DEVICE_NAME_HINT,
    MAX_PAYLOAD,
    RX_UUID,
    SVC_UUID,
    TX_UUID,
)
from .router import Handler, Router

logger = logging.getLogger(__name__)

DecoratorTarget = Callable[[Handler], Handler]


class DynappClient:
    """ESP32 dynamic-app 连接客户端。

    Args:
        device_name: 扫描时的设备名子串匹配（不区分大小写）。默认 "ESP32"。
        address:     如果你已经知道 MAC，可以直接传，跳过扫描。
        scan_timeout: 扫描超时秒数，默认 10。
        auto_reconnect: 掉线是否自动重连，默认 True。
    """

    def __init__(
        self,
        *,
        device_name: str = DEFAULT_DEVICE_NAME_HINT,
        address: Optional[str] = None,
        scan_timeout: float = 10.0,
        auto_reconnect: bool = True,
    ) -> None:
        self._device_name = device_name
        self._address = address
        self._scan_timeout = scan_timeout
        self._auto_reconnect = auto_reconnect

        self._client: Optional[BleakClient] = None
        self._router = Router()
        self._send_lock = asyncio.Lock()
        self._stopped = asyncio.Event()
        self._reconnect_task: Optional[asyncio.Task] = None

    # =========================================================================
    # 上下文管理 / 启停
    # =========================================================================

    async def __aenter__(self) -> "DynappClient":
        await self._connect_with_retry()
        if self._auto_reconnect:
            self._reconnect_task = asyncio.create_task(self._watchdog())
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        self._stopped.set()
        if self._reconnect_task:
            self._reconnect_task.cancel()
        await self._teardown()

    async def run_forever(self) -> None:
        """阻塞直到 stop() 被调用或 Ctrl+C。"""
        try:
            await self._stopped.wait()
        except asyncio.CancelledError:
            pass

    def stop(self) -> None:
        self._stopped.set()

    # =========================================================================
    # 注册 handler
    # =========================================================================

    def on(
        self,
        from_app: Optional[str] = None,
        msg_type: Optional[str] = None,
    ) -> DecoratorTarget:
        """装饰器形式注册消息回调。

        用法:
            @client.on("weather", "req")
            async def _(msg):
                ...

            @client.on("weather")          # weather 任意 type
            @client.on(msg_type="ping")    # 任意 app 的 ping
            @client.on()                   # 全部消息（catch-all）
        """
        def deco(fn: Handler) -> Handler:
            self._router.register(from_app, msg_type, fn)
            return fn
        return deco

    # =========================================================================
    # 发送
    # =========================================================================

    async def send(
        self,
        to_app: str,
        msg_type: str,
        body: Any = None,
    ) -> bool:
        """发一条消息给指定 app。返回 True=成功 enqueue。"""
        return await self._send_raw({"to": to_app, "type": msg_type, "body": body}
                                    if body is not None
                                    else {"to": to_app, "type": msg_type})

    async def broadcast(self, msg_type: str, body: Any = None) -> bool:
        """to=* 广播给所有 app（实际上同一时刻只有一个在跑）。"""
        return await self.send("*", msg_type, body)

    async def _send_raw(self, msg: dict) -> bool:
        if self._client is None or not self._client.is_connected:
            logger.warning("send dropped: not connected")
            return False

        payload = json.dumps(msg, ensure_ascii=False).encode("utf-8")
        if len(payload) > MAX_PAYLOAD:
            raise ValueError(
                f"payload {len(payload)}B exceeds MAX_PAYLOAD={MAX_PAYLOAD}; "
                f"split it manually or shrink fields"
            )

        async with self._send_lock:
            try:
                await self._client.write_gatt_char(RX_UUID, payload, response=False)
                logger.debug("sent (%dB) %s", len(payload), msg)
                return True
            except Exception as e:
                logger.warning("send failed: %s", e)
                return False

    # =========================================================================
    # 状态查询
    # =========================================================================

    @property
    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    # =========================================================================
    # 内部：连接 / 重连 / notify 接收
    # =========================================================================

    async def _scan_for_device(self) -> Optional[str]:
        if self._address:
            return self._address
        logger.info("scanning %.1fs for '%s' ...", self._scan_timeout, self._device_name)
        devices = await BleakScanner.discover(timeout=self._scan_timeout)
        for d in devices:
            if d.name and self._device_name.lower() in d.name.lower():
                logger.info("found %s @ %s", d.name, d.address)
                return d.address
        logger.warning("no device matched '%s'", self._device_name)
        return None

    async def _connect_with_retry(self) -> None:
        backoff = 1.0
        while not self._stopped.is_set():
            addr = await self._scan_for_device()
            if addr:
                try:
                    cli = BleakClient(addr)
                    await cli.connect()
                    if cli.is_connected:
                        await cli.start_notify(TX_UUID, self._on_notify)
                        self._client = cli
                        logger.info("connected to %s", addr)
                        return
                except Exception as e:
                    logger.warning("connect failed: %s", e)
            # 退避
            backoff = min(backoff * 2, 30.0)
            logger.info("retry in %.1fs", backoff)
            try:
                await asyncio.wait_for(self._stopped.wait(), timeout=backoff)
                return   # stop 期间被叫醒，直接退
            except asyncio.TimeoutError:
                pass

    async def _watchdog(self) -> None:
        """定期检查连接，断了就重连。"""
        while not self._stopped.is_set():
            await asyncio.sleep(3.0)
            if self._client is not None and not self._client.is_connected:
                logger.warning("connection lost, attempting reconnect ...")
                await self._teardown()
                await self._connect_with_retry()

    async def _teardown(self) -> None:
        if self._client is None:
            return
        try:
            if self._client.is_connected:
                await self._client.stop_notify(TX_UUID)
                await self._client.disconnect()
        except Exception as e:
            logger.debug("teardown ignore: %s", e)
        self._client = None

    def _on_notify(self, _handle: int, data: bytearray) -> None:
        # bleak 在 asyncio 线程里调这个 cb，可以安全 schedule task
        try:
            text = bytes(data).decode("utf-8")
        except UnicodeDecodeError:
            logger.debug("recv non-utf8 (%dB), drop", len(data))
            return
        try:
            msg = json.loads(text)
        except json.JSONDecodeError:
            logger.debug("recv non-json: %s", text[:80])
            return
        if not isinstance(msg, dict):
            logger.debug("recv non-object json, drop: %s", text[:80])
            return
        # router.dispatch 是 async，丢到事件循环
        asyncio.create_task(self._router.dispatch(msg))
