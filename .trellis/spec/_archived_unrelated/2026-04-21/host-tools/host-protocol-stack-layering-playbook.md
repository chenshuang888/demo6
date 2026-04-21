# Playbook：主机端协议栈分层（schema/codec/transport/protocol）以消除“帧构建重复”

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）“PC 端分层解耦重构方案”。
>
> 目标：让主机端（PC/Web/Node）对 MCU 通信时具备统一的封包/解包与并发控制，避免“每个业务函数手写帧结构”“waitResponse 轮询 + 竞态”。

---
## 上下文签名（Context Signature，必填）

> 目的：避免“场景相似但背景不同”时照搬实现细节。

- 目标平台：ESP32/ESP32‑S3/STM32/…（按项目填写）
- SDK/版本：ESP-IDF x.y / LVGL 9.x / STM32 HAL / …（按项目填写）
- 外设/链路：UART/BLE/Wi‑Fi/TCP/UDP/屏幕/触摸/外置 Flash/…（按项目填写）
- 资源约束：Flash/RAM/PSRAM，是否允许双缓冲/大缓存（按项目填写）
- 并发模型：回调在哪个线程/任务触发；谁是 owner（UI/协议/存储）（按项目填写）

---

## 不变式（可直接复用）

- 先跑通最小闭环（冒烟）再叠功能/优化，避免不可定位
- 只直接复用“原则与边界”，实现细节必须参数化
- 必须可观测：日志/计数/错误码/抓包等至少一种证据链

---

## 参数清单（必须由当前项目提供/确认）

> 关键参数缺失时必须停手，先做 Fit Check：`spec/iot/guides/spec-reuse-safety-playbook.md`

- 常量/边界：magic、分辨率、slot 大小、最大 payload、buffer 大小等（按项目填写）
- 时序策略：超时/重试/节奏/窗口/幂等（按项目填写）
- 存储语义：写入位置、校验策略、激活/回滚策略（如适用）（按项目填写）

---

## 可替换点（示例映射，不得照搬）

- 本文若出现文件名/目录名/参数示例值：一律视为“示例”，必须先做角色映射再落地
- 角色映射建议：transport/codec/protocol/ui/persist 的 owner 边界先明确

---

## 停手规则（Stop Rules）

- 上下文签名关键项不匹配（平台/版本/外设/资源/并发模型）时，禁止照搬实施步骤
- 关键参数缺失（端序/magic/分辨率/slot/payload_size/超时重试等）时，禁止给最终实现
- 缺少可执行的最小冒烟闭环（无法验收）时，禁止继续叠功能

---


## 现状症状（高概率导致后续重复踩坑）

- **帧构建重复多处**：同一个命令（如 `APP_START`/`OTA_START`）在多个函数里手动拼字节
- `waitResponse()` 轮询式等待，且缺少“单飞行请求”约束 → 并发时响应错配
- MCU 主动请求的解析逻辑散落在“传输层/业务层”之间 → 耦合与改漏风险
- 日志函数（如 `_log`）重复定义，不同模块各说各话

---

## 推荐目标架构

```
schema.js   # 声明：每条命令的字段、长度、可变长度规则
codec.js    # 编解码：frame <-> {cmd, fields...}，只做纯转换
transport.js# 传输：串口读写、断帧、队列、超时、waitResponse
protocol.js # 业务：upload/querySlots/store/ota 等高层 API（薄包装）
```

核心约束：

- `codec` 不访问串口、不 sleep、不等响应（纯函数）
- `transport` 不理解业务语义，只提供“发命令/等响应/分发 MCU 主动请求事件”
- `protocol` 只组合调用，不再手写任何帧字节数组

---

## schema.js（契约层）

设计原则：

- 每条命令在 schema 中只有一个定义（单一事实来源）
- 支持：
  - 固定长度命令（例如 PING）
  - 变长命令（例如 `*_DATA`、商店列表等）
- 字段类型系统建议包含：`u8/u16le/u32le/bytes/string_fixed` 等

---

## codec.js（编解码层）

职责：

- `encode(cmd, fields) -> Uint8Array/Buffer`
- `tryDecode(buffer) -> { ok, consumed, frame }`（或等价形式）
- 对变长命令：由 codec 负责读取长度字段并决定 `consumed`，避免上层重复逻辑

---

## transport.js（传输层）

职责：

- 串口打开/关闭、RX 缓冲、超时断帧
- **并发保护**：推荐“单飞行请求”或队列化发送
- `sendAndWait(cmd, payload, timeoutMs)`：
  - 只对“响应类帧”解锁等待器
  - 与 MCU 主动请求（`STORE_*` 等）分流

---

## protocol.js（业务层：薄包装）

职责：

- `uploadApp(slot, fileBuffer, ...)`
- `uploadOta(...)`
- `queryAppSlots()/queryOtaSlots()`
- 商店逻辑（如需要先查已安装 ID 再过滤列表）：在这里组织流程，但底层帧仍来自 codec/schema

---

## 实施步骤（建议顺序）

1. 先落 `schema.js`（无依赖，可独立验收）
2. 落 `codec.js`（对比现有封包/解包结果，保证一致）
3. 落 `transport.js`（先把“发送 + 收包 + 超时 + 单飞行”跑通）
4. 重写 `protocol.js`（让业务函数全部改为调用 transport/codec）
5. 删除/合并旧的重复模块（如旧 `serial.js` / 重复拼帧代码）

---

## 验收标准

- 任意命令的帧格式变更只需要改 `schema.js`（不再全仓库搜 `0x7E` 手改）
- 并发情况下不出现响应错配/死锁（单飞行或队列化生效）
- MCU 主动请求仍能被正确解析与分发（不会被 waitResponse 吞掉）

