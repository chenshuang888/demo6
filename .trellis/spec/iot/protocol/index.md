# 协议（帧格式 / 命令表 / 状态机 / 兼容）

> 本目录聚焦：设备与主机（PC/Web 工具）之间的协议契约，避免"解析各写一套""语义漂移"。

## 复用安全（必读）

- 复用任何条目前先做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（端序/magic/长度上限不对齐就会"看似能跑但随机炸"）

## 文档

- `./ble-cts-current-time-binary-contract.md`：BLE CTS（0x1805/0x2A2B）当前时间特征的 10 字节 packed 二进制契约（PC/ESP32 对齐）。
- `./ble-weather-service-payload-contract.md`：自定义 BLE Weather Service（UUID + 68 字节 packed payload + Python `struct.pack` 对齐）。
- `./ble-notification-service-payload-contract.md`：自定义 BLE Notification Service（WRITE + 136 字节 packed payload + category/priority）。
- `./ble-control-service-event-contract.md`：自定义 BLE Control Service（NOTIFY + 8 字节 control_event + seq 去重）。
- `./ble-media-service-payload-contract.md`：自定义 BLE Media Service（WRITE + 92 字节 media_payload + 设备端本地插值进度条）。

### 跨 Service 模式与分配规则（本项目专属）

- `./esp-to-pc-notify-request-pattern-playbook.md`：ESP 主动向 PC 请求数据：每个业务 service 自管一个 NOTIFY char（time 复用 CTS 的 NOTIFY flag；weather/system 分配独立 req char），触发端与响应端同在一个 service 内。
- `./ble-custom-uuid-allocation-decision-record.md`：5 个自定义服务的 UUID 分配方案（全局 base `8a5c0000...` + 奇偶短码配对），以及扩展规则。
