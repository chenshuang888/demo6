# Playbook：LVGL 多 App 架构分层（App Manager + UI Shell + HAL）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里关于“桌面/设置/时钟等多 App UI”的框架分层总结与代码审查结论。
>
> 目标：让设备 UI 能像“手机”一样演进（不断加 App/换屏），而不会因为耦合、切屏时序、资源释放不一致导致反复踩坑。

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

把 UI 切成四层：**HAL（硬件）→ UI Shell（系统 UI）→ App Manager（导航栈）→ Apps（具体业务页）**，并且按这个顺序落地。

---

## 推荐分层（从底到顶）

```
+-------------------------------------------------------+
|                    App 层                              |
|   Launcher App | Settings App | Clock App | ...       |
+-------------------------------------------------------+
|                  App Manager（导航栈）                  |
|   app_create() | app_destroy() | app_switch()         |
+-------------------------------------------------------+
|                  UI Shell（系统 UI）                    |
|   status_bar (top_layer) | nav_bar (top_layer)        |
+-------------------------------------------------------+
|                  HAL 层（已完成）                       |
|   Display | Touch | Backlight | LVGL init             |
+-------------------------------------------------------+
```

### HAL 层（Display/Touch/Backlight/LVGL init）

- 目标：把“硬件差异”封装掉，别让上层知道 SPI/I2C、旋转、亮度 PWM、DMA、flush ready 之类细节。
- 边界：HAL 不应该知道“页面/状态栏/应用”。

### UI Shell（系统 UI：常驻状态栏/导航栏）

- 目标：把“常驻组件”固定住（例如蓝牙/Wi-Fi/电量/时间、返回/主页按钮），**所有 App 都不应重复造轮子**。
- 推荐做法：用 `lv_layer_top()` / `top_layer` 放常驻 UI，让它不随切屏被销毁。
- 约束：App 禁止 delete/top_layer 的对象；Shell 的生命周期由系统统一管理。

### App Manager（导航栈：创建/销毁/切换/返回）

- 目标：把“切屏时序 + 生命周期”变成一个统一的机制，避免每个 App 自己 `lv_scr_load` 造成风格不一致和泄漏。
- 典型能力：
  - `register_app(app_info_t)`
  - `launch(app_id)`
  - `go_back()`
  - `go_home()`
  - `current_app()` / `current_screen()`

### Apps（业务页：只关心自己的 UI 与业务）

- 目标：新增一个 App 的成本要低（只写 create/destroy + UI 逻辑即可）。
- 推荐：App 只操作自己创建的 screen/root，不直接碰 status/nav。

---

## 落地优先级（对话里明确给的顺序）

1. 先抽象 **App Manager**（导航栈 + 生命周期）
2. 再做 **UI Shell**（状态栏 + Launcher 桌面 Grid）
3. 最后逐个开发具体 App

原因：导航栈/生命周期是“系统级约束”，越晚做越难统一，后期容易返工。

---

## 最小接口建议（保持简洁、可扩展）

对话中的经验是：接口越少越不容易烂尾。

- `app_info_t`
  - `id`（建议 enum）
  - `name`
  - `icon`（可选）
  - `on_create()`：创建并返回当前 App 的 `lv_obj_t* screen`（或 root）
  - `on_destroy(screen)`：销毁 App 资源（包括 screen、timer、异步任务句柄等）

注意：如果 App 内部起了 `lv_timer`、注册了事件回调、持有外部资源（例如 BLE notify manager 句柄），`on_destroy()` 必须负责解绑/停止。

---

## 反复踩坑的约束（写进代码规范/Checklist）

- **切屏必须走 App Manager**：不要每个 App 自己 `lv_scr_load()`。
- **删除对象避免在事件回调里直接 `lv_obj_delete()`**：优先 `lv_obj_delete_async()`，降低时序风险。
- **屏幕对象谁创建谁销毁**：不要“只清指针不删对象”。
- **共享 UI 指标集中定义**（状态栏高度、导航栏高度、padding 等）：不要散落魔法数字。

---

## 验收标准（建议）

- 新增一个 App 只需要：
  - 注册 `app_info_t`
  - 实现 `on_create/on_destroy`
  - 不改系统其它文件（或仅改 Launcher 的入口列表）
- 任意 App 反复进入/退出 10 次，堆内存能回落或保持稳定（无缓慢增长）。

