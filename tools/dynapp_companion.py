"""dynapp_companion —— 给所有动态 app 跑后台 PC 服务。

用法:
    pip install -r requirements.txt
    python dynapp_companion.py

可选参数:
    --addr XX:XX:XX:XX:XX:XX   跳过扫描，直连指定 MAC
    --device-name 'ESP32'      扫描时匹配的设备名子串
    --log-level INFO|DEBUG     日志级别

新增 app 接入只需：
    1) 在 providers/ 下写 your_provider.py
    2) 在下方 main() 里加一行 register_your(client)
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys

from dynapp_sdk import DynappClient
from providers.weather_provider import register_weather
from providers.media_provider import register_media


async def amain(args: argparse.Namespace) -> None:
    async with DynappClient(
        device_name=args.device_name,
        address=args.addr,
    ) as client:
        # —— 注册所有 app 的 provider ——
        register_weather(client)
        register_media(client)

        print("[companion] running. Ctrl+C to quit.")
        await client.run_forever()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--addr", help="ESP32 BLE MAC; if omitted, auto scan")
    p.add_argument("--device-name", default="ESP32",
                   help="device name substring used for scanning (default: ESP32)")
    p.add_argument("--log-level", default="INFO",
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    args = p.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)-7s %(name)s %(message)s",
        datefmt="%H:%M:%S",
    )

    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\n[companion] bye")
        sys.exit(0)


if __name__ == "__main__":
    main()
