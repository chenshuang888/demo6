# Contract：`app_manager` 的生命周期模型（launcher 常驻、普通 App 离开即销毁）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中对 demo5 当前资源/内存结构的调研与演进建议：现有 `font_manager/image_manager/resources_fs` 的可复用点、当前缺失的生命周期挂点、以及一套“最小可落地”的 launcher↔app 切换状态机草案。
>
> 目标：把 UI 从“单页验证”演进到“可切换多 app/page”的结构时，先把生命周期与资源释放顺序立住，避免出现字体/图片释放早于 LVGL 对象销毁导致的悬空指针与随机崩溃。

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

对话里最推荐的最小生命周期策略是：

- **launcher 常驻**
- **普通 App 离开即销毁（destroy on leave）**

这样能把复杂度集中在“screen/widget 生命周期”，而不是一上来就把资源系统做重。

---

## 现状可复用点（对话明确认可）

### 1) UI 不直接碰路径

- 字体：`font_manager_get(FONT_ROLE_...)`
- 图片：`image_manager_apply(image, IMAGE_ROLE_...)` / `image_manager_get_path(...)`

这条边界非常适合延伸到 `app_manager`：让 app_manager 继续站在“语义角色”层协调资源，而不是把 `/res/...` 细节泄漏到页面。

### 2) role-based manager 易升级成 app-scope manifest

现有 manager 都是 `enum role + 静态 binding 表 + 隐藏真实路径`，天然适合演进为：

- 公共资源 role
- 每个 app 私有 role/manifest
- app_manager 根据 app 描述去 preload/release 一组 role

### 3) fallback 思路要保留

字体侧现在是“资源加载失败 → fallback 到内置字体”，不 panic。  
对 app 切换时代仍然关键：资源缺失时不应直接崩溃。

---

## 当前缺失的关键模式（不补齐就谈不上安全释放）

对话里指出的缺口包括：

- 没有 `enter/leave/destroy` 生命周期（只有 `ui_app_init()`）
- 没有 screen/object teardown 流程（缺少明确 `lv_obj_del` / screen 切换与销毁协议）
- manager 侧没有 unload/destroy API、没有引用计数/所有权模型

结论：要支持“按 app 生命周期回收资源”，第一步不是先改 manager，而是先把 **app 生命周期挂点** 补齐。

---

## 核心约束（实现时不能忽略）

1. **字体释放一定要晚于使用它的 LVGL 对象销毁**
   - LVGL 对象持有的是 `lv_font_t*` 指针；先释放字体会导致悬空指针
2. **图片释放更复杂**
   - 当前图片是“路径绑定到 LVGL 对象”，解码/缓存更多由 LVGL 侧决定
3. **`resources_fs` 建议全局常驻**
   - 不建议随 app 切换反复 mount/unmount
4. **所有 `app_manager_*` 调用必须发生在 UI/LVGL 线程**
   - 避免从其他 FreeRTOS task 直接调用 LVGL API
5. **切换要防重入**
   - 增加 `APPMGR_STATE_SWITCHING` 避免切换过程中重复触发

---

## 推荐的最小状态机与 API（草案级，但足够落地）

### App 描述对象（示意）

- `app_id` / `kind`（launcher vs normal）
- `destroy_on_leave`（普通 app 建议 true）
- 生命周期回调：
  - `create(ctx)`
  - `enter(ctx)`
  - `leave(ctx)`
  - `destroy(ctx)`

`ctx` 至少包含：

- `lv_obj_t *screen`

### 切换流程（关键点）

- `app_manager_start()`：创建并进入 launcher
- `app_manager_open(app_id)`：从 launcher 打开普通 app
  - `launcher.leave()` → `target.create()`（若未创建）→ `lv_screen_load(target.screen)` → `target.enter()`
- `app_manager_back_to_launcher()`：
  - `current.leave()` → `lv_screen_load(launcher.screen)` → `launcher.enter()` →（若 `destroy_on_leave`）`current.destroy()` + 删除 screen

对话里强调的顺序约束：

- 不要“先删当前 active screen 再切换”
- 先 load 到目标（例如 launcher），再销毁旧 normal app screen

---

## 约束补充：`app_manager_t` 采用“opaque struct”封装（头文件只暴露指针）

对话里解释了一个对长期维护很重要的 C 语言封装手法：

- 在 `app_manager.h` 里只做前向声明并 typedef：
  - `typedef struct app_manager app_manager_t;`
- 在 `.c` 文件里才真正定义 `struct app_manager { ... }`

这样做的收益：

- 外部模块能持有 `app_manager_t *` 指针，但**看不到成员**
- 外部模块无法写 `manager->current` 这种访问（编译期直接阻止）
- 强制外部通过 `app_manager_*` API 交互，避免“所有模块随手改内部状态”的结构腐化

这在嵌入式项目里尤其有价值：模块边界清晰、可控演进、减少耦合。

---

## `ui_app` 的最小改造建议（把它降级为 launcher 实现）

对话里建议把 `ui_app.c` 从“整个 UI 系统入口”改成“launcher 的实现”：

- `ui_app_init()` 作为兼容入口保留，但内部改为初始化/启动 `app_manager`
- 现有 UI 创建逻辑迁移到 `launcher_create(ctx)`：
  - 创建独立 `ctx->screen`
  - 所有 widget 挂到 `ctx->screen`
  - 不再直接用 `lv_screen_active()`

为了验证状态机链路，launcher 页面至少要有一个可点击入口，触发 `app_manager_open(...)` 打开一个极简普通 app。

---

## 验收标准

- app 切换过程中不出现悬空指针/随机崩溃
- launcher 返回稳定、普通 app 离开后内存不持续增长
- 资源加载失败时只降级 fallback，不会引发崩溃
