# Contract：自定义 BLE Control Service（NOTIFY + 8 字节 control_event）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“ESP32 屏上点按钮 → 通过 GATT Notify 主动推送事件到 PC”的 MVP 协议约定（固定 8 字节事件 + seq 去重）。
>
> 目标：把“触摸按钮 → PC 动作”做成稳定可验收的最小闭环：不引入 HID 复杂度，先用自定义 GATT 私有协议跑通。

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


## 一句话结论

**Control 面板用自定义 GATT 服务 + 单个 NOTIFY Characteristic；每次按钮触发推送一个 8 字节 packed 事件，PC 端订阅并映射到本地动作。**

---

## UUID 约定（对话中的实现/验收示例）

- Service UUID：`8a5c0005-0000-4aef-b87e-4fa1e0c7e0f6`
- Characteristic UUID：`8a5c0006-0000-4aef-b87e-4fa1e0c7e0f6`
  - 属性：NOTIFY（ESP32 → PC）

---

## Payload：`control_event_t`（8 bytes，必须 packed）

```c
typedef struct {
    uint8_t  type;      // 0=button（先只做 button）
    uint8_t  id;        // 按钮编号（MVP：0..3 对应 2x2 的四个按钮）
    uint8_t  action;    // 0=press（后续可扩 press/long/release）
    uint8_t  _reserved; // 置 0
    int16_t  value;     // 给 slider 预留（button 时置 0）
    uint16_t seq;       // 小端递增序号：防丢/去重/验收
} __attribute__((packed)) control_event_t;  // 8 bytes
```

---

## 端到端验收（推荐顺序）

1. PC 侧订阅该 Characteristic（Enable Notifications / `start_notify`）。
2. 屏上点按钮，PC 端应收到 8 字节 payload，且 `seq` 连续递增。
3. 断开连接时：设备侧应丢弃发送（不崩溃），并在 UI 上显示 `Off`（或灰态）。

---

## 设计取舍（对话中的关键解释）

- 先做 **自定义 GATT**：开发快、与既有 time/weather/notify 风格一致；缺点是 PC 端程序必须常驻。
- HID 是后续进阶：系统级输入设备，成本更高、调试路径不同。

