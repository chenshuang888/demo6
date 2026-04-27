"""dynapp_sdk —— PC 端开发动态 app 配套服务的 SDK。

典型用法:

    import asyncio
    from dynapp_sdk import DynappClient

    async def main():
        async with DynappClient(device_name="ESP32") as client:
            @client.on("weather", "req")
            async def on_req(msg):
                await client.send("weather", "data", body={"temp_c": 23})

            await client.run_forever()

    asyncio.run(main())

详见 docs/动态app双端通信协议.md 与 docs/动态app开发指南.md。
"""

from .client import DynappClient
from .constants import (
    SVC_UUID, RX_UUID, TX_UUID,
    MAX_PAYLOAD,
    DEFAULT_DEVICE_NAME_HINT,
)
from .router import Router

__all__ = [
    "DynappClient",
    "Router",
    "SVC_UUID", "RX_UUID", "TX_UUID",
    "MAX_PAYLOAD",
    "DEFAULT_DEVICE_NAME_HINT",
]
