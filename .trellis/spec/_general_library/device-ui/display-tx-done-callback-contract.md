# Contract：显示底层用 `tx_done_cb + user_ctx` 解耦 LVGL（flush 完成通知）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中对接口的解释：`lcd_panel_init(lcd_panel_tx_done_cb_t tx_done_cb, void *user_ctx)` 为什么要这样设计。
>
> 目标：把“底层知道完成时机、上层决定完成后动作”的模式固化为契约，避免把 LVGL 写死进底层驱动。

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

**底层只负责在“颜色传输完成”时触发回调；LVGL 相关的 `lv_display_flush_ready()` 只能放在 `lvgl_port.*` 里。**

---

## 契约内容

### 1) 底层（例如 `lcd_panel.*`）职责

- 负责 SPI/LCD/背光等底层初始化
- 在“颜色传输完成”回调点，把事件转发给上层提供的 `tx_done_cb`
- **底层不直接依赖 LVGL**
  - 不应 `#include "lvgl.h"`
  - 不应出现 `lv_display_t`、`lv_display_flush_ready(...)` 等 LVGL 语义

### 2) 上层（例如 `lvgl_port.*`）职责

- 把 `lv_display_t*` 等 LVGL 对象作为 `user_ctx` 传入底层
- 在 `tx_done_cb(user_ctx)` 里调用 `lv_display_flush_ready(display)`
- 这样才能满足 LVGL flush 生命周期：“开始刷”与“刷完通知”严格配对

---

## 这类回调“是谁调用”的（避免和 LVGL 回调混淆）

对话里专门澄清过一点：`tx_done_cb` **不是 LVGL 主动调用的回调**。

- `lv_display_set_flush_cb(display, flush_cb)` / `lv_indev_set_read_cb(indev, read_cb)`
  - 这类回调是 **LVGL 主动调用**
- `tx_done_cb(user_ctx)`（颜色传输完成）
  - 这类回调是 **ESP-IDF / `esp_lcd` 在底层传输完成后被动触发**

它属于“底层事件通知”，只是被用来满足 LVGL 的 flush 生命周期而已。

---

## 为什么需要 `user_ctx`

对话里给出的核心点是：只有函数指针不够，上层回调还需要知道“该通知哪个对象”。

- 同一份回调逻辑可以复用
- 通过 `user_ctx` 携带具体对象指针（例如某个 `lv_display_t*`）

---

## 实现建议（避免 `void *数组` 混塞类型）

对话里指出过一个可维护性风险：把不同类型硬塞进 `void *callback_ctx[2]` 再强转取出，可读性与类型安全都较差。

更推荐的落地方式是用结构体承载回调上下文：

- `cb`：`tx_done_cb`
- `user_ctx`：上层对象指针

这样读代码的人无需猜测数组下标含义，后续扩展也更安全。

---

## 接口形状建议：`init()` 与 `set_cb()` 分开（更顺眼、更符合分层直觉）

对话里指出的“别扭点”是：把“回调注册策略”塞进 `init(...)` 参数里，会让 `init` 的语义变得不纯。

更推荐的形态是：

- `lcd_panel_init()`：只做硬件初始化
- `lcd_panel_set_tx_done_cb(cb, user_ctx)`（或 `lcd_panel_register_tx_done_callback(...)`）：单独绑定完成回调

原因：

- 谁创建底层硬件对象（例如 `panel_io`），谁负责接住硬件事件（例如 `on_color_trans_done`）
- 但“完成后通知谁”可以由交接层决定；因此把“通知策略绑定”拆成单独 API，会更清晰

### 谁调用、谁实现（对话中的关键澄清）

- `lcd_panel_set_tx_done_cb(...)` **应该由交接层调用**（例如 `lvgl_port.*` 决定通知 LVGL）
- 但这个函数**通常仍由底层实现**（例如 `lcd_panel.*` 持有 `panel_io`/保存回调上下文，最方便接住硬件事件并触发）

一句话：**策略由交接层决定，存储与触发由底层负责。**

---

## 没有 LVGL 时，这个回调要不要保留？

对话里的结论是：

- 多数情况下：**可以不写/不启用**（你只是 `draw_bitmap`，不需要“刷完通知”）
- 但以下场景仍然可能有价值：
  - 双缓冲切换
  - 帧结束通知其它任务
  - 刷新耗时统计
  - 刷完置标志位/触发下一帧

所以更准确的说法是：它不是 LCD 驱动“必需”，而是“有人需要知道刷屏何时完成”时才需要。

---

## 验收标准

- `lcd_panel.*` 不包含任何 LVGL 头文件或 LVGL API
- `lvgl_port.*` 里能清晰看到“注册回调 → 回调里 flush_ready”的链路
- 回调上下文具备明确类型（优先结构体），避免隐式强转堆叠
