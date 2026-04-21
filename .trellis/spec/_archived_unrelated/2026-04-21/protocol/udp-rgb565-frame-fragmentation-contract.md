# Contract：UDP 分片传输 RGB565 帧（PC → ESP32）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo1.md`）中的“最小端到端通路”规划与初版分片头设计。

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
  - PC 端：把图像转换为 RGB565，按 UDP 分片发送
  - 设备端（ESP32）：UDP 接收、按 `frame_id` 重组、整帧显示
- 典型屏幕：320×240，RGB565（16bit）
- 版本：v0（最小头部；不含 CRC/ACK/重传）

---

## 一句话原则

**UDP 只做“尽力而为”：每个 datagram 自带最小可重组信息；收端必须能丢弃半帧并快速切到新帧。**

---

## 数据结构（Schema）

### 帧数据

- 像素格式：RGB565，逐行排列（row-major）
- 单帧字节数：`frame_bytes = width * height * 2`
  - 320×240：`153600` bytes

### UDP 分片头（固定 8 字节）

| 字段 | 类型 | 必填 | 语义 | 备注 |
|---|---|---|---|---|
| `magic` | `uint16` | 是 | 分片识别 | 小端；**必须在 PC/设备两端统一**（建议在同一个头文件/脚本常量里维护） |
| `frame_id` | `uint16` | 是 | 帧号 | 小端；单调递增，回绕允许 |
| `chunk_idx` | `uint16` | 是 | 当前分片序号 | 小端；从 0 开始 |
| `chunk_total` | `uint16` | 是 | 分片总数 | 小端；`>= 1` |

字节布局：

- Bytes `0..1`：`magic(uint16_le)`
- Bytes `2..3`：`frame_id(uint16_le)`
- Bytes `4..5`：`chunk_idx(uint16_le)`
- Bytes `6..7`：`chunk_total(uint16_le)`
- Bytes `8..`：payload

> 备注：对话中的初版头还曾讨论“offset”，但最终给出的 v0 头为上述 8 字节结构；payload 的放置按 `chunk_idx * payload_size` 计算即可。

---

## 交互流程（状态机）

### 发送端（PC）

1. 生成一帧 RGB565 `frame_bytes`
2. 选择 `payload_size`（建议不超过 1400 bytes，避免逼近 MTU）
3. 计算 `chunk_total = ceil(frame_bytes / payload_size)`
4. 对每个 `chunk_idx`：
   - 构造 8 字节头
   - 追加 payload（最后一片可短）
   - `sendto()` 发出

### 接收端（ESP32）

1. `recvfrom()` 收到 datagram
2. 校验 `magic`
3. 解析 `frame_id/chunk_idx/chunk_total`
4. 若 `frame_id` 变化：
   - 丢弃旧帧未完成数据
   - 重置 bitset/计数器
5. 将 payload 拷贝到 `frame_buf + chunk_idx * payload_size`
6. 当所有 chunk 收齐：
   - 整帧提交显示（建议优先 `esp_lcd_panel_draw_bitmap`）
   - 进入下一个 `frame_id`

---

## 错误处理约定

- 丢包/乱序：允许；收端应支持乱序到达（靠 `chunk_idx` 放回正确位置）
- 半帧：超时或帧切换时直接丢弃（不要卡在“等齐”）
- `chunk_total` 变化：视为协议不一致，直接丢弃该帧

---

## 验收标准

- PC 端发送静态 PNG→RGB565→UDP 分片，ESP32 端能稳定显示同一张图（无明显撕裂/错位）
- 把 WiFi 环境切到弱信号（丢包增加）时：允许掉帧，但不允许“卡死在半帧”

