# Playbook：LVGL 9 Screen 切换（动画/事件）+ `top_layer` 状态栏的“手机式”框架化落地

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中“手机操作系统式应用框架”研究报告：LVGL 9 的 screen 管理 API、load 动画事件顺序、以及“状态栏应放在 `top_layer` 才能跨 screen 常驻”的关键洞察。
>
> 目标：在 LVGL 9 上实现“Launcher 常驻 + App 进出销毁 + 可选动画”，并把**资源释放时机**设计清楚，避免 `auto_del`/动画中途切屏带来的 double-free/泄漏。

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

**状态栏/导航栏放 `lv_layer_top()`；App UI 放独立 `screen`；切屏用 `lv_screen_load_anim()` 时不要依赖 `auto_del=true`，而是在 `LV_EVENT_SCREEN_UNLOADED` 做 `on_destroy` 与资源清理。**

---

## LVGL 9 的关键事实（对话中归纳）

### 1) Screen 切换事件顺序（4 个）

按时间顺序：

- `LV_EVENT_SCREEN_UNLOAD_START`：旧 screen 开始退场准备
- `LV_EVENT_SCREEN_LOAD_START`：新 screen 开始进场
- `LV_EVENT_SCREEN_LOADED`：新 screen 就绪
- `LV_EVENT_SCREEN_UNLOADED`：旧 screen 完全移除（**适合做最终清理**）

### 2) 4 层体系（从底到顶）

- `bottom_layer`
- `act_scr`（活动 screen，App 主内容）
- `top_layer`（**状态栏/导航栏应该放这里**）
- `sys_layer`

关键洞察：把状态栏放 `top_layer`，切换 App screen 时它不会消失。

---

## 为什么不建议 `auto_del=true`

对话给出的工程理由是：你往往需要在旧 screen 完全卸载时，执行 App 的 `on_destroy`（释放私有数据/stop timer/释放 style 等）。

如果 `auto_del=true` 由 LVGL 自动删旧 screen，你就很容易：

- “来不及”跑完自己的销毁逻辑
- 或把销毁逻辑塞到不合适的时机，导致资源仍被引用

更稳的策略：

- `auto_del=false`
- 在 `LV_EVENT_SCREEN_UNLOADED` 中触发 `on_destroy`，然后显式 `lv_obj_delete_async(old_scr)`（或你自己的延迟销毁策略）

---

## 动画安全边界（对话点名的坑）

- **避免动画中途切换 screen**：等当前 `lv_screen_load_anim` 完成再发起下一次切换，否则可能出现 double-free/状态错乱。
- 若需要连续导航：用“队列化的导航请求”，在 `LV_EVENT_SCREEN_LOADED` 后再处理下一条。

---

## 内存与稳定性要点（对话中给的 checklist）

- **Style**：`lv_style_init()` 的样式不会因为删对象自动释放，退出 App 时要 `lv_style_reset()`（或集中管理 style 生命周期）
- **删除时机**：事件回调中删除对象优先用 `lv_obj_delete_async()`，减少“当前对象/当前 screen 正在使用”导致的崩溃风险
- **更新优先**：数据变化时用 setter 更新 widget（如 `lv_label_set_text()`），不要频繁删/重建整棵 UI 树

---

## 验收标准

- 状态栏跨 screen 切换保持常驻（不会跟着 App 消失）
- App 退出后 screen 与私有数据都能释放（重复进入/退出不涨内存）
- 连续切屏不会出现偶发崩溃（double-free / use-after-free）

