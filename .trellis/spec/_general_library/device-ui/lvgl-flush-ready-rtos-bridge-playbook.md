# Playbook：LVGL 刷新完成通知的“RTOS 桥接”写法（回调直达 vs 通知 UI 任务）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中围绕 `lv_display_flush_ready(display)` 的讨论：为什么它像“中断完成通知”，以及在 RTOS 里如何让分层更顺。
>
> 目标：把 LVGL 的 flush 机制（开始由 LVGL 主动、结束由驱动通知）落到一个可维护的 FreeRTOS 结构上，避免“底层回调直接碰 LVGL”带来的别扭与耦合。

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

**`flush_cb` 是 LVGL 主动调用；“刷完通知”是底层完成事件被动触发。为了分层更顺，可以把完成事件先转成 RTOS 通知，再由 UI 任务调用 `lv_display_flush_ready(display)`。**

---

## 先分清两类东西：状态 vs 事件

对话里用“触摸轮询 vs 刷屏完成通知”做了区分：

- 触摸：**持续变化的状态** → 适合周期性读取（轮询）
- 刷屏完成：**一次性完成事件** → 更适合“发生时立刻通知”（回调/完成事件）

因此：

- 不是不能轮询刷屏完成
- 而是对刷屏这种“天然异步完成”的事，直接用完成通知通常更自然、更准

---

## LVGL 刷新机制的关键点（必须知道）

对话里强调了一个容易误解的点：  
**不是 LVGL 自己去轮询 LCD 驱动是否刷完，而是你必须显式告诉 LVGL：刷完了。**

常见链路是：

1. LVGL 调用你的 `flush_cb`（让你开始刷）
2. 驱动启动 SPI/DMA 传输
3. 传输完成时触发“完成事件”
4. 你（通过某条路径）调用 `lv_display_flush_ready(display)`

---

## 两种落地方式对比

### 方式 1：完成回调里直接调用 `lv_display_flush_ready(display)`（最短路径）

链路：

```
底层完成事件 -> 回调 -> 直接 lv_display_flush_ready(display)
```

优点：

- 代码短、实现快、能跑

缺点（对话中的“不舒服点”）：

- 底层事件与 LVGL 贴得太近
- `display`（上层对象）被一路塞到底层，看起来“语义不纯”

适合：快速验证阶段、你能接受这层耦合的项目。

### 方式 2：完成事件先转成 RTOS 通知，再由 UI 任务调用 `lv_display_flush_ready(display)`（更 RTOS 风格）

链路：

```
底层完成事件 -> 发 RTOS 通知 -> UI 任务收到 -> lv_display_flush_ready(display)
```

可选通知手段（对话列举）：

- task notification
- semaphore
- queue
- event group

优点：

- 分层更清楚：底层只做“通知”，LVGL 操作都留在 UI 侧（`lvgl_port.*`）
- 更符合“策略由交接层决定”的直觉

缺点：

- 多一层通知，代码略复杂

---

## 你真正可选的是什么

对话里的关键结论是：

- 最终都要回到 `lv_display_flush_ready(display)`
- 真正可选的是：**谁来通知 LVGL、通过什么路径通知**

---

## 验收标准

- 能清晰回答：“开始刷新是谁调用谁”“结束通知是谁触发谁”
- 不把 `lv_display_flush_ready(display)` 写进纯底层驱动（尽量留在 `lvgl_port.*` 或 UI 任务路径里）
- 未来需要换 GUI/裸刷图时，底层仍可复用（完成事件仍可用于帧同步/统计等非 LVGL 场景）

