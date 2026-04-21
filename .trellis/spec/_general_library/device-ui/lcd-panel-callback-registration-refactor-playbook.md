# Playbook：把 `lcd_panel_init(cb, user_ctx)` 改成“init + register”（最小改动、更顺眼）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于 `lcd_panel_init(lvgl_flush_ready_cb, s_display)` “语义不纯/看着难受”的讨论，以及推荐的最小接口改造方案（私有 context + 显式注册，不引入公开 `lcd_panel_t`）。
>
> 目标：让接口语义更干净、分层更顺，同时保持“不过度设计”，避免把模块重构成大对象体系。

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


## 一句话结论

**推荐把“初始化硬件”和“绑定完成通知策略”拆开：`lcd_panel_init()` 只做硬件初始化；`lcd_panel_register_tx_done_cb(cb, user_ctx)` 由 `lvgl_port.*` 调用来决定“刷完后通知谁”。**

---

## 背景：为什么 `lcd_panel_init(lvgl_flush_ready_cb, s_display)` 看起来别扭

对话里把原因说得很清楚：

- `lv_display_set_flush_cb(...)` 一看就是“绑定回调”，所以顺
- `lcd_panel_init(...)` 名字承诺的是“初始化”，但实际还承担了“绑定回调策略”，所以语义不纯

并且要分清两类回调：

- LVGL 主动调用：`flush_cb/read_cb`
- 底层被动触发：SPI 传输完成事件（需要你最终调用 `lv_display_flush_ready(display)`）

---

## 推荐接口形态（最小集合）

在 `drivers/lcd_panel.h` 推荐落成：

- `esp_err_t lcd_panel_init(void);`
- `void lcd_panel_register_tx_done_cb(lcd_panel_tx_done_cb_t cb, void *user_ctx);`

语义对应：

- `lcd_panel_init()`：初始化 SPI/LCD/背光、创建 `panel_io/panel_handle`
- `lcd_panel_register_tx_done_cb(...)`：注册“传输完成事件”的上层处理策略（由交接层决定）

> 备注：函数名也可以用 `lcd_panel_set_tx_done_cb(...)`，核心是“显式绑定”而不是“塞进 init 参数”。

---

## 内部实现建议：私有 context（不要引入公开 `lcd_panel_t`）

对话里的取舍结论是：

- **当前阶段不要引入公开的 `lcd_panel_t` 结构体**
- **用私有静态 context 保存回调与 user_ctx** 就够了

一个朴素但类型安全的做法：

- 在 `lcd_panel.c` 内定义 `static` 的 `context`（含 `panel_io`、回调函数指针、`user_ctx`）
- `on_color_trans_done` 只负责触发：如果设置了回调就调用回调，把 `user_ctx` 原样带回去

这样达成：

- 策略由 `lvgl_port.*` 决定（谁来通知 LVGL）
- 存储与触发由 `lcd_panel.*` 负责（谁持有 `panel_io`，谁能接住底层事件）

---

## 风险点与验证点（对话中明确提到要关注）

### 风险点

- 回调触发时机与上下文：底层完成事件可能发生在驱动回调上下文中，不应把“复杂 UI 逻辑”塞进去
- 注册时序：确保 `register_tx_done_cb(...)` 在首次刷新前完成
- 未来扩展：如果后续要支持多屏/多实例，私有静态 context 可能需要升级（但现在阶段先别为未来过度设计）

### 验证点（最小）

- `lv_demo_widgets()` 或最小页面能正常刷新，不出现“flush 卡死”
- 屏幕刷新完成后 `lv_display_flush_ready(display)` 能被触发到（不重复、不丢失）
- 不需要在 `lcd_panel.*` include LVGL 头文件

---

## 建议实施顺序（最小改动路线）

1. 在 `lcd_panel.h/.c` 增加 `lcd_panel_register_tx_done_cb(...)`
2. 把原来 `lcd_panel_init(cb, user_ctx)` 的参数移除，保留纯硬件初始化
3. 在 `lvgl_port_init()` 里：
   - 先 `lcd_panel_init()`
   - 再 `lcd_panel_register_tx_done_cb(lvgl_flush_ready_cb, s_display)`
4. 如果你希望更“RTOS 风格”：把完成回调改成“发通知”，由 UI 任务调用 `lv_display_flush_ready(display)`

---

## 什么时候需要升级到“公开对象”？

对话给出的判断逻辑是：

- 现在先把边界理顺（init 与 register 分开），**不要一上来全量对象化**
- 真正需要 `lcd_panel_t` 这类公开对象，通常发生在：
  - 多实例（多屏/多通道）
  - 需要在不同模块之间传递同一个面板对象
  - 生命周期管理要更精细（创建/销毁/热插拔）

---

## 验收标准

- 代码阅读体验：初始化与回调绑定语义分离（不再“init 里塞策略”）
- 分层体验：`lvgl_port.*` 决定“通知 LVGL 的策略”；`lcd_panel.*` 只暴露底层事件能力
- 无新增“重量级对象体系”，保持不过度设计

