# Contract：Parser 串口帧格式 + 注册式命令分发（多端口、多服务）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）“STM32 UART 双模式通信方案深度分析”后半部分对 `parser.c`/`protocol.c` 的结构与调用点统计。
>
> 目标：把 MCU 端“统一收包解析 + 多业务服务注册 handler + 统一回包”的边界写成契约，避免新增命令时注册漏、回包格式漂移、或误用上下文导致回错端口/命令。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 适用范围

- MCU 端存在一个公共 `Parser` 模块：
  - 负责从某个 `port` 的 RX 流中识别帧
  - 按 `cmd` 分发到业务 handler
  - 支持多个端口（示例：`TP_PORT_USART3`、`TP_PORT_USART1`）
- 业务模块以“注册表”方式挂载（Downloader/App/OTA/ASR 等）

---

## 帧格式（必须一致）

### 通用帧（MCU 主动发起请求：`Parser_SendFrame`）

```
| 0x55 | 0xAA | CMD (1B) | payload (N Bytes) | 0xA5 | 0x5A |
```

### 响应帧（MCU 回复 PC 请求：`Parser_SendResponse`）

```
| 0x55 | 0xAA | CMD (1B) | STATUS (1B) | data (N Bytes) | 0xA5 | 0x5A |
```

状态码约定：

```c
#define STATUS_OK    0x00
#define STATUS_ERROR 0x01
```

容量约束（对话中的示例）：

- 最大 payload：256B
- 帧缓冲：`PARSER_FRAME_BUF = 280`（约等于头尾 + cmd + status + 256）

---

## 命令分发（注册式）

### 注册 API（概念）

```c
int Parser_Register(uint8_t port, uint8_t cmd, ServiceCallback_t cb);
```

推荐约定：

- 所有业务模块在各自 `*_Init()` 中完成注册
- 一个 `cmd` 在同一 `port` 上只能有一个 handler（冲突要显式报错或覆盖策略固定）

对话中的典型分布（示意）：

- `TP_PORT_USART3`：Downloader/App/OTA 系列命令（PING、APP_*、OTA_*、STORE_*…）
- `TP_PORT_USART1`：ASR/语音控制命令（UP/DOWN/OK/BACK 等）

---

## 回包 API 分层（避免业务层重复拼帧）

### `Parser_SendFrame` vs `Parser_SendResponse`

- `Parser_SendFrame(port, cmd, payload, len)`：不带 `STATUS`，用于 MCU 主动请求（上行）
- `Parser_SendResponse(port, cmd, status, data, len)`：带 `STATUS`，用于应答（下行）

### `Protocol_SendResponse` 的“上下文自动取值”模式（强约束）

对话中的典型实现（语义）：

```c
void Protocol_SendResponse(uint8_t status, const uint8_t *data, uint16_t len) {
    Parser_SendResponse(Parser_GetCurrentPort(),
                        Parser_GetCurrentCmd(),
                        status, data, len);
}
```

含义：

- 业务 handler 在被 `Parser_Task`/分发器调用时，Parser 内部保存了 `current_port/current_cmd`
- 业务层只传 `status/data` 即可回包，降低重复与漏参风险

**危险点（必须写进契约）**：

- 这种模式依赖“当前上下文”：
  - 只能在 handler 同步调用栈内使用
  - 不能在异步回调/延迟任务里直接调用（否则 `current_cmd` 已变化，回错包）
- 若系统存在多任务并发处理不同端口/不同帧，需要保证 Parser 的 `current_*` 具备隔离（或改为显式传递 context）

---

## 验收标准

- 新增命令时：
  - `CMD` 常量定义、handler 实现、`Parser_Register` 三者齐全
  - PC 端能按帧格式解析到 `STATUS`
- handler 内回包：
  - 同步路径使用 `Protocol_SendResponse` 不回错端口/命令
  - 异步路径不允许使用隐式上下文（或改为显式 context 版本）

