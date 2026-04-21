# Playbook：`app_manager` 最小可用实现（接口 + 数据结构 + 切换流程）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中对 demo5 当前 `app/app_manager.c` 的结构性解读：它做什么/不做什么、对外 API、内部数据结构、状态机与切换流程。
>
> 目标：把 `app_manager` 的“最小闭环”写清楚，便于你后续扩展（资源 manifest、stop/resume、多 app 跳转）时不推翻现有结构。

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

当前 `app_manager` 可以理解成：

> **应用注册表 + screen 生命周期切换器 + 简单状态机**  
> 负责注册 app、懒创建 app screen、在 launcher 和普通 app 之间切换，并在返回桌面时按策略销毁普通 app 实例。

---

## 它现在做什么 / 不做什么

### 做什么（两件核心事）

1. **管理已注册的 app**（固定注册表）
2. **管理 app 之间的切换生命周期**（create/enter/leave/destroy + screen load）

### 不做什么（对话明确强调）

- 不负责字体加载/释放（`font_manager`）
- 不负责图片缓存管理（`image_manager` 目前也不做缓存）
- 不负责文件系统分区切换（例如 `/res/apps/[app_id]/`）
- 不做多任务调度（只在 UI/LVGL 线程内驱动）

> 现阶段资源层仍建议全局常驻，`app_manager` 先把 screen 生命周期跑通就够了。

---

## 对外 API（建议保持稳定）

以对话中提到的接口为主线：

- `app_manager_init()`
  - 初始化内部全局状态，进入 `APPMGR_STATE_READY`
- `app_manager_register(const app_desc_t *app)`
  - 注册 app 到内部表
  - 做必要检查：非空、必须有 id/必须有 create、禁止重复 id、launcher 只能一个、数量不超上限
- `app_manager_start()`
  - 启动默认 app（launcher）：ensure created → `lv_screen_load` → `enter` → 设置 current=launcher
- `app_manager_open(const char *app_id)`
  - 从当前 app 切到某个普通 app：`leave(current)` → ensure created(target) → `lv_screen_load` → `enter(target)`
- `app_manager_back_to_launcher()`
  - 从普通 app 返回 launcher：`leave(current)` → ensure created(launcher) → `lv_screen_load(launcher)` → `enter(launcher)` →（若 `destroy_on_leave`）销毁当前 app
- `app_manager_get_current()`
  - 返回当前激活 app 的描述对象（用于上层显示状态/扩展预留）

---

## 内部核心数据结构（理解它才能正确扩展）

### `app_desc_t`（静态描述）

对话里把它定义为“app 的说明书”，通常是 `static const`：

- `id` / `title`
- `kind`（launcher vs normal）
- `destroy_on_leave`
- 生命周期回调（cb）

### `app_context_t`（运行时上下文）

对话里强调最关键字段是：

- `lv_obj_t *screen`：每个 app 自己的独立 screen

其余常见字段：

- `manager`（指回 `app_manager_t*`）
- `app`（指回 `app_desc_t*`）
- `user_data`

#### `ctx->manager` 的意义（现在用得少，但不奇怪）

对话里给出的解释是：`ctx->manager` 更多是**为未来预留的上下文入口**，让 app 回调知道自己属于哪个 manager。

即使现在暂时没用到它，未来如果要在回调里实现：

- 查询当前 app / launcher
- 获取 manager 状态
- 做更复杂的 app 资源协调

都会需要一个“回到 manager”的入口指针。

如果你非常追求“这版更干净”，也可以删掉它；但保留它是标准写法，扩展更方便。

#### 为什么框架层需要 `ctx->screen`

对话里强调：`ctx->screen` 不只是为了“销毁”，它至少支撑三类框架行为：

1. **首次进入 app**：`lv_screen_load(ctx->screen)`
2. **从别的 app 切回来**：如果该 app 没销毁，需要再次 `lv_screen_load(ctx->screen)`
3. **离开时销毁**：需要明确删除目标 screen（例如 `lv_obj_delete(ctx->screen)` 或等价 API）

结论：`screen` 是“切换显示”这条主链路的必要上下文，不是可有可无的字段。

### `app_record_t`（内部记录：把“静态 + 运行态”合一）

对话里指出它把两类信息放在一起：

- `desc`（静态定义）
- `ctx/state`（运行时实例信息与状态）

### `app_manager_t`（manager 本体，建议 opaque）

通常内部包含：

- 当前 manager state
- `apps[]` 注册表数组 + `app_count`
- `launcher` 指针
- `current` 指针

并建议用 `opaque struct` 封装（见 `spec/iot/guides/app-manager-lifecycle-contract.md`）。

---

## 状态机（最小可落地）

### app 自己的状态（示意）

- `APP_STATE_EMPTY`
- `APP_STATE_REGISTERED`
- `APP_STATE_CREATED`
- `APP_STATE_ACTIVE`
- `APP_STATE_HIDDEN`

### manager 的状态（示意）

至少需要：

- `APPMGR_STATE_READY`
- `APPMGR_STATE_SWITCHING`（防重入）

---

## 切换流程的关键约束（避免悬空指针/随机崩溃）

- 所有 `app_manager_*` 调用必须发生在 UI/LVGL 线程
- 不要“先删 active screen 再切换”
  - 推荐：先 `lv_screen_load` 到目标（例如 launcher），再销毁旧 normal app screen
- 若 `destroy_on_leave=true`：
  - `destroy` 必须负责清理 screen/widget
  - 资源释放要晚于 LVGL 对象销毁（字体指针悬空是最危险的）

---

## 边界原则：应用层“表达意图”，框架层“执行切换”

对话里把分工说得很直白：

- 应用层负责：
  - 创建自己的 screen 与控件
  - 在合适的时候调用 `app_manager_open(...)` / `app_manager_back_to_launcher()`（发起切换请求）
- 框架层负责：
  - 当前 active app 是谁
  - 何时 `lv_screen_load(...)`
  - destroy_on_leave 的执行
  - 切换顺序一致性与状态机防重入

不要让 app 自己去直接调用 `lv_screen_load()` / `lv_obj_delete()` 做“全局切换决策”，否则切换逻辑会散落在每个 app 内部，后续很难统一演进。

---

## 后续优化建议（按“值不值得现在做”排序）

对话里给的优化点很实用，这里按落地优先级整理：

1. **让 launcher 不再硬编码 app_id（更值得优先做）**
   - 增加查询接口（示例）：
     - `app_manager_get_app_count()`
     - `app_manager_get_app_at(index)`
     - `app_manager_get_launcher()`
   - launcher 根据注册表自动生成 app 列表，才更像“平台桌面”
2. **可选：给 app 层再薄一层 runtime/navi API（不是刚需）**
   - 目的：app 不直接 include `app_manager.h`
   - 形态示例：`app_runtime_open("demo")`、`app_runtime_back_to_launcher()` 或基于 `ctx` 的 `app_context_open(ctx, "...")`
3. **可选：`ctx->manager` 现在可删可留**
   - 当前贡献不大；保留是为了未来扩展，删除能让结构更朴素
4. **状态机别提前加重**
   - `REGISTERED/CREATED/ACTIVE/HIDDEN` + manager 的 `READY/SWITCHING` 对当前已足够
   - `PAUSED/STOPPED/RESUMED` 等放到“确实需要”再加
5. **导航语义可以后置**
   - 当前命令式 `open()/back_to_launcher()` 非常合理
   - app 多了再考虑更通用的“返回上一个/关闭当前”等导航抽象
