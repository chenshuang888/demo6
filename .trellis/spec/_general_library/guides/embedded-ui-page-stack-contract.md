# Contract：嵌入式 UI 的页面栈（Page Stack）与生命周期（非 LVGL 也适用）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中 STM32F103 + 128×64 OLED 项目对 `ui_framework.*` 的页面栈实现总结（`UI_SwitchPage/UI_ReplacePage/UI_BackPage/UI_BackHome` + `init/enter/update/draw/key_event/exit`）。
>
> 目标：把“页面切换/返回/主页”的语义收口到一套稳定契约，避免每个页面各写各的切换逻辑导致失控；并为后续从 OLED 自绘迁移到 LVGL/其他 GUI 预留一致的心智模型。

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

把 UI 组织成“**页面栈 + 明确生命周期**”：页面只能通过 `UI_SwitchPage/Replace/Back/BackHome` 切换；每个页面只实现 `init/enter/update/draw/key_event/exit`，不要直接操作别的页面的内部状态。

---

## 核心数据结构（示意）

- `page_stack[PAGE_STACK_SIZE]`：页面栈（建议固定深度，例如 8 层）
- `stack_top`：栈顶
- `current_page_id`：当前页面 ID

约束：

- 栈深必须是常量上限（避免动态分配导致内存不确定）
- 栈操作必须检查上溢/下溢，并返回错误码（不要 silent fail）

---

## 页面切换 API（对外唯一入口）

推荐最小集合：

- `UI_SwitchPage(page_id, anim)`：压栈进入新页面（push）
- `UI_ReplacePage(page_id, anim)`：替换当前页面（replace top）
- `UI_BackPage(anim)`：返回上一页（pop）
- `UI_BackHome(anim)`：回到主页（清栈到 home）

原则：

- 页面内部禁止直接“改栈/改 current_page_id”，只能调用上述 API
- 所有“返回”语义必须走同一套 `Back`/`BackHome`，避免出现多套返回逻辑

---

## 页面生命周期契约（每个页面都必须遵守）

建议统一 6 个钩子（名字可调整，语义别变）：

- `init()`：首次进入时调用一次（只做一次性初始化）
- `enter()`：每次进入页面时调用（重置/刷新页面状态）
- `update()`：每帧/定期更新逻辑（轻量）
- `draw()`：每帧绘制/刷新 UI（或触发渲染）
- `key_event(evt)`：按键/输入事件处理（只做轻量逻辑与状态切换）
- `exit()`：离开页面时调用（释放页面占用的资源/停止定时器）

关键规则：

- `init()` 与 `enter()` 必须分开：前者只做一次，后者每次进入都可重入
- `exit()` 必须与 `enter()` 对称（哪些资源在 enter 里启用，就在 exit 里停掉）

---

## 输入模型建议（4 键设备的最小可用）

典型按键集（示例）：

- `UP/DOWN`：选择移动
- `OK`：确认
- `BACK`：返回（统一走 `UI_BackPage` 或 `UI_BackHome`）

建议把“短按/长按”做成明确事件枚举（例如 `KEY_EVENT_OK_LONG`），不要在页面里到处用计数器猜测。

---

## 调度与 tick（UI 框架常见实现）

如果采用“1ms tick + 任务表”调度，建议使用“差值比较 + 累加 last_run”防漂移/防溢出：

- 判断：`(now - last_run) >= rate_ms`
- 更新：`last_run += rate_ms`（不要 `last_run = now`，否则会漂移/抖动）

---

## 验收标准

- 项目里所有页面切换都只通过 `UI_SwitchPage/Replace/Back/BackHome`
- 任意页面可以被“多次进入/退出”而不出现状态残留或资源泄漏
- 栈上溢/下溢有明确错误处理（不会导致随机跳页）

