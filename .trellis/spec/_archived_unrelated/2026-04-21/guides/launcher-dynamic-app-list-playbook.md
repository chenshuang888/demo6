# Playbook：让 `launcher_app` 从注册表生成应用入口（不再硬编码 `"demo"`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中 demo5 关于 `launcher_app.c` 与 `app_manager` 的演进建议。
>
> 目标：下一次再做“多 app 框架 + launcher”时，不会重复犯“入口写死/路径写死/生命周期错乱”的错误；新增 app 只需要注册一次即可自动出现在 launcher。

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

launcher 不应该“手写按钮打开某个 app”，而应该 **只读枚举 app 注册表**，并基于注册表自动生成入口（列表或网格）。

---

## 最小落地方案（推荐）

### 1) `app_manager` 只暴露“只读枚举”

- 目的：让 launcher 能“看到有哪些 app”，但 **不能修改注册表**（边界干净、风险低）。
- 推荐只读接口（示例命名）：
  - `app_manager_get_app_count()`
  - `app_manager_get_app_by_index(size_t index)` → 返回 `const app_desc_t *`

约束：

- 枚举返回的 `app_desc_t` 必须是稳定生命周期（通常是 `static const`）。
- 禁止返回可变指针让上层改写注册表内容。

### 2) launcher 从注册表生成入口

生成逻辑（最小闭环）：

1. 枚举所有 `app_desc_t`
2. 过滤掉 `APP_KIND_LAUNCHER`（避免“自己点自己”）
3. 为每个普通 app 创建一个入口控件（按钮/卡片）
4. 入口显示使用 `app->title`（不要在 launcher 里硬编码 app 名称）
5. 点击时调用 `app_manager_open(desc->id)`

### 3) 点击回调不要传字符串常量，传 `app_desc_t *`

推荐把 `const app_desc_t *` 直接挂在控件 `user_data`：

- 点击回调里取出 `desc->id`
- 再调用 `app_manager_open(desc->id)`

原因：避免字符串散落硬编码、避免后续改 id/标题时到处改代码；同时 `static const app_desc_t` 生命周期稳定，指针传递安全。

---

## UI 形态建议（按复杂度递增）

- V1（复杂度最低）：可滚动纵向列表（app 多了也不会挤爆屏幕）。
- V2（符合“桌面”观感）：`240x320` 上做 3×3 网格（最多 9 个）。
- 超过 9 个的扩展策略（先选一种，别提前过度设计）：
  - 纵向分页/滚动
  - 左右翻页桌面
  - 第 9 格做“更多”

关键原则：先把“基于注册表自动生成入口”跑通，再逐步优化视觉与分页。

---

## 不做清单（防止一上来就复杂）

- 不做运行时动态增删 app 后自动刷新（先“启动时生成列表”即可）
- 不做复杂分组/权限/隐藏策略
- 不强行引入 icon 资源体系（先占位，后续再补）

---

## 验收标准

- `launcher_app` 中不再出现硬编码 `"demo"` / `"clock"` 这类 app id 字符串（除非是测试临时代码）。
- 在 `apps_entry`（或统一注册入口）新增 app 后：
  - launcher 自动出现新入口
  - 点击能打开正确 app
- launcher 不会把自己显示成一个入口（已过滤 `APP_KIND_LAUNCHER`）。

