"""协议常量。改这里就能改协议（但只在固件配套修改时才动）。"""

# GATT —— 与固件 services/dynapp_bridge_service.c 严格对齐
SVC_UUID = "a3a30001-0000-4aef-b87e-4fa1e0c7e0f6"
RX_UUID  = "a3a30002-0000-4aef-b87e-4fa1e0c7e0f6"   # PC → ESP, WRITE
TX_UUID  = "a3a30003-0000-4aef-b87e-4fa1e0c7e0f6"   # ESP → PC, READ + NOTIFY

# 单条消息最大字节数（utf-8 编码后），与固件 DYNAPP_BRIDGE_MAX_PAYLOAD 对齐
MAX_PAYLOAD = 200

# 默认扫描时使用的设备名片段（不区分大小写、子串匹配）
DEFAULT_DEVICE_NAME_HINT = "ESP32"

# 协议保留 type（SDK 内置处理，业务别用作自己的语义）
RESERVED_TYPES_INBOUND_TO_APP = {
    "ping": "ESP 应自动回 pong（JS helper 内置）；PC 收到 ping 不会自动回",
}
