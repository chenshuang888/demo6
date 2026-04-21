# Contract：无线 KVM（PC ⇄ ESP32）TCP 帧传输 + 触摸回传

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo1.md`）中的协议草案与实现约束。

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

- 参与方：
  - PC 端：TCP Server（推送 JPEG 帧；接收触摸事件）
  - 设备端（ESP32-S3 等）：TCP Client（接收 JPEG 帧；发送触摸事件）
- 版本：v1（固定头部 + little-endian；不含 CRC/压缩协商）

---

## 一句话原则

**TCP 上必须“自描述分帧”：magic + length，然后按 length 精确读满；不要依赖 `recv()` 一次就拿到完整帧。**

---

## 数据结构（Schema）

### 1) 下行：JPEG 帧（PC → ESP32）

每帧结构：

| 字段 | 类型 | 单位/范围 | 必填 | 语义 | 备注 |
|---|---|---|---|---|---|
| `magic` | `uint32` | 固定 | 是 | 帧起始标记 | 小端，值为 `0x4B564D46`（ASCII: `"KVMF"`） |
| `jpeg_len` | `uint32` | 0..N | 是 | JPEG 字节长度 | 小端 |
| `jpeg_bytes` | `uint8[jpeg_len]` | bytes | 是 | JPEG 数据 | 紧随其后 |

字节布局（连续）：

- Bytes `0..3`：`"KVMF"`（little-endian 数值为 `0x4B564D46`）
- Bytes `4..7`：`jpeg_len`（`uint32_t` little-endian）
- Bytes `8..(8+jpeg_len-1)`：JPEG payload

### 2) 上行：触摸事件（ESP32 → PC）

固定 12 字节：

| 字段 | 类型 | 单位/范围 | 必填 | 语义 | 备注 |
|---|---|---|---|---|---|
| `magic` | `uint32` | 固定 | 是 | 触摸报文标记 | 小端，值为 `0x4B564D54`（ASCII: `"KVMT"`） |
| `x` | `uint16` | 0..319 | 是 | 触摸 x 坐标 | 小端；如果分辨率可变，应先归一化再映射 |
| `y` | `uint16` | 0..239 | 是 | 触摸 y 坐标 | 小端 |
| `event` | `uint32` | 0..2 | 是 | 触摸事件类型 | 小端：`0=TOUCH_DOWN`，`1=TOUCH_MOVE`，`2=TOUCH_UP` |

---

## 交互流程（状态机）

### 连接关系

- **ESP32 作为 TCP Client** 主动连接 PC 的 TCP Server。
- 同一个 socket 上可全双工：
  - ESP32 持续阻塞读帧（下行）
  - ESP32 随时写触摸事件（上行）

### 读帧规则（ESP32）

1. `read_exact(8)` 读头
2. 校验 `magic == "KVMF"`
3. 解析 `jpeg_len`
4. `read_exact(jpeg_len)` 读满 payload
5. 交给 JPEG 解码 / 显示链路

---

## 错误处理约定

- magic 不匹配：
  - v1 推荐策略：**直接断开 socket 并重连**（简化实现，避免复杂的流内重同步）
- `jpeg_len` 非法（过大/为 0）：
  - 断开并重连；并在日志里打印 `jpeg_len` 与最近一次 magic

---

## 兼容策略（版本演进）

- v1 固定 `magic + len`，后续如果需要版本号/时间戳/压缩协商，建议在头部扩展为：
  - `magic(uint32) + version(uint16) + flags(uint16) + len(uint32)`（仍保持对齐与小端）

---

## 验收标准

- PC 端持续发送 JPEG 帧（例如 10fps），ESP32 端持续显示且无花屏/错帧
- ESP32 端触摸事件在 PC 上可观察到鼠标移动/点击（并可验证 `DOWN/MOVE/UP` 顺序）

