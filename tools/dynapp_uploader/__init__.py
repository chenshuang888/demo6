"""dynapp_uploader —— ESP32 dynamic app 上传/运维 SDK。

GUI 入口：tools/dynapp_push_gui.py

也可以独立用：

    import asyncio
    from dynapp_uploader import UploaderClient

    async def main():
        async with UploaderClient(device_name="ESP32") as c:
            await c.upload_file("echo2", "echo2.js")
            print(await c.list_apps())
            await c.delete_app("echo2")

    asyncio.run(main())
"""

from .client import UploaderClient, UploadError
from .constants import (
    DEFAULT_DEVICE_NAME_HINT,
    MAX_CHUNK,
    MAX_SCRIPT_BYTES,
    NAME_LEN,
    RESULT_NAMES,
)

__all__ = [
    "UploaderClient",
    "UploadError",
    "DEFAULT_DEVICE_NAME_HINT",
    "MAX_CHUNK",
    "MAX_SCRIPT_BYTES",
    "NAME_LEN",
    "RESULT_NAMES",
]
