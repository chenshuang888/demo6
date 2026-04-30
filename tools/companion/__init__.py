"""tools/companion —— 统一的 ESP32 BLE 桌面伴侣。

入口：python -m companion           （GUI 模式）
      python -m companion --no-gui   （后台 daemon）

合并自原来的 4 个进程：
  - desktop_companion.py     （CTS / weather / notify / system / media）
  - ble_time_sync.py         （手动通知输入）
  - dynapp_companion.py      （动态 app JSON 通道）
  - dynapp_push_gui.py       （动态 app 上传）

一个 BleakClient 同时点亮 7 个 ESP service（time / weather / notify / system /
media / dynapp_bridge / dynapp_upload），不再需要在工具之间切换。
"""

__version__ = "0.1.0"
