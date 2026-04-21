# 协议（BLE 契约 / 跨端对齐）

> 本目录聚焦：**本项目 5 个 BLE 自定义 service + 标准 CTS** 的 payload 契约、UUID 分配、跨端语义对齐。

## 复用安全（必读）

- 复用前做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（端序/长度/对齐不对就会"看似能跑但随机炸"）

## Payload 契约（每个 service 一条）

- `./ble-cts-current-time-binary-contract.md`：标准 CTS（0x1805/0x2A2B）10 字节 packed（PC 端 `<HBBBBBBBB`）
- `./ble-weather-service-payload-contract.md`：weather service（0x8a5c0001/2）68 字节 packed
- `./ble-notification-service-payload-contract.md`：notification service（0x8a5c0003/4）136 字节 packed + category/priority
- `./ble-media-service-payload-contract.md`：media service（0x8a5c0007/8 + 0x8a5c000d）92 字节 media_payload + 4 字节媒体键按钮 NOTIFY

## 跨 Service 模式与 UUID 分配

- `./ble-custom-uuid-allocation-decision-record.md`：全局 base `8a5c0000...` + 奇偶短码配对规则 + 末尾追加例外 + RETIRED UUID 规则
- `./esp-to-pc-notify-request-pattern-playbook.md`：ESP 主动向 PC 请求数据的模式 v3 —— 每业务 service 自管 NOTIFY char，触发端与响应端同 service
