# Playbook：LVGL 9 输入设备（`lv_indev_create`）接入要点（timer / `read_cb` / display 绑定）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo1.md`）中对 LVGL 9.x `lv_indev_create` 源码行为的排查结论。

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


## 目标

把触摸/按键等输入设备以 LVGL 9 的“indev”方式稳定接入，避免：

- `read_cb` 不被调用（以为是驱动没跑，其实是 display 没绑定/没默认显示器）
- 误以为需要手动创建轮询 timer，导致重复/乱序调用
- `set_type` / `set_read_cb` 先后顺序争论与误用

---

## 前置条件

- LVGL 9.x（对话中排查的源码版本为 **LVGL 9.5.0**，但这些行为对 9.x 通常成立）
- 已创建并初始化 display（至少有一个 display，且最好已设置为 default display）

---

## 关键事实（必须记住）

1) `lv_indev_create()` **会自动创建一个定时器**，周期为 `LV_DEF_REFR_PERIOD`，默认模式为 `LV_INDEV_MODE_TIMER`。

2) `lv_indev_set_type()` 与 `lv_indev_set_read_cb()` **没有强制的调用顺序依赖**；但在 indev 开始被 LVGL 轮询前，这两项必须都设置完成。

3) `lv_indev_set_display()` **存在且通常需要**：在 `lv_indev_read()` 内部会检查 `indev->disp`，没有绑定 display 会直接 return，导致你永远看不到 `read_cb` 被调用。

> 备注：`lv_indev_create()` 可能会尝试绑定 default display；但如果工程里没有 default display、或你想绑定到指定 display，就必须显式调用 `lv_indev_set_display()`。

---

## 实施步骤（建议顺序）

1) 先创建并就绪 display
   - 确保 display 已完成 `flush_cb` 等基础接入
   - 如果你的工程有多个 display，明确哪个是默认/主显示

2) 创建 indev
   - `lv_indev_t * indev = lv_indev_create();`

3) 设置类型与读回调（顺序随意）
   - `lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);`（触摸）
   - `lv_indev_set_read_cb(indev, my_touch_read_cb);`

4) 绑定 display（强烈建议显式写出来）
   - `lv_indev_set_display(indev, disp);`

---

## 验收标准

- `read_cb` 以 `LV_DEF_REFR_PERIOD` 的节奏被周期性调用（可在 `read_cb` 内加计数或日志）
- 断开/重连触摸驱动时，系统不会因为 indev 指针/disp 为空而进入静默失败

---

## 常见问题（快速排障）

- 现象：`read_cb` 完全不进
  - 优先检查：是否绑定了 display（`lv_indev_set_display`），以及是否存在 default display
  - 次级检查：是否确实调用了 `lv_indev_set_read_cb`（未设置会有 warning，但在裁剪日志时可能看不到）

- 现象：`read_cb` 调用频率不符合预期
  - 检查：`LV_DEF_REFR_PERIOD` 实际配置值（以及 LVGL tick 是否正常推进）

