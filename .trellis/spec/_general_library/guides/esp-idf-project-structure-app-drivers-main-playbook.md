# Playbook：从 LVGL 官方 demo 迁移到“main + drivers + app”的工程结构（ESP-IDF）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中从“直接跑 `lv_demo_widgets()`”迁移到项目自有 UI 的落地过程：新增 `app/`、收敛 `main/`、保持 `drivers/` 分层干净、并用最小页面验证调用链。
>
> 目标：把“能持续演进的目录/组件边界”先立住，避免 UI/资源逻辑下沉到驱动层，也避免 `main` 变成大杂烩。

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

把工程拆成三块最顺：

- `drivers/`：硬件驱动 + LVGL port glue（显示/触摸/总线/回调桥接）
- `app/`：UI 入口（`ui_app`）、资源/字体等管理层（`font_manager` 等）
- `main/`：系统启动与装配（init 顺序），不要放业务逻辑

并用一个“最小页面”替换官方 demo，先把 `main -> ui_app -> font_manager -> lv_font_t*` 主链路跑通。

---

## 目录与职责边界（建议第一版就定死）

### `drivers/` 应该放什么

- 显示/触摸/背光等外设驱动
- LVGL port：把硬件事件/回调转成 LVGL 需要的接口（例如 flush ready 通知）

### `app/` 应该放什么

- UI 入口：`ui_app_init()`
- 字体/资源管理：`font_manager_*`、`asset_manager_*`（如果有）
- 页面框架/页面注册（如果你的 UI 规模已经需要）

### `main/` 只做什么

- `app_main()` 的初始化顺序与错误处理
- 组件初始化的装配（谁先、谁后）
- 日志与最小冒烟（别把 UI/资源细节塞进 `main`）

---

## CMake 组件接入（最小改动范式）

对话里落地的最小接法要点是：

- 根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 同时包含 `drivers` 与 `app`
- `main/CMakeLists.txt` 显式依赖 `drivers lvgl app`，保证 include/链接关系清晰

> 约束：不要让 `app` 反向依赖 `main`；`drivers` 也不要依赖 `app`。依赖方向应该从上到下。

---

## `main/main.c` 初始化顺序（推荐）

对话里建议的顺序是：

1. 初始化 `lvgl_port`（让显示/触摸 ready）
2. 初始化 `font_manager`（先用内置字体也可以，目的是把“UI 不碰实现细节”立住）
3. 初始化 `ui_app`（创建最小页面）

### 关于 `main_task: Returned from app_main()` 的日志

对话里特别澄清过：如果你的 `app_main()` 里创建了长期运行的任务（例如 `ui_task`），然后 `app_main()` 自己返回，那么日志里看到 `main_task: Returned from app_main()` 是**正常现象**，不代表程序退出。

---

## 最小页面：用来替换 `lv_demo_widgets()` 的验收点

最小页面的目的不是“好看”，而是验收主链路是否正确：

- 页面能显示一个标题/文本/数字
- 文本分别使用 `font_manager_get(role)` 返回的字体
- 运行时不再出现官方 demo（证明入口已切换到项目自有 UI）

---

## 验收标准

- `main/` 只剩启动与装配逻辑，没有 UI/资源细节
- `drivers/` 不包含 UI 资源/字体管理代码
- `app/` 能独立演进（未来接入文件系统字体、资源分区等时，UI 层调用边界不被推翻）
