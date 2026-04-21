# Playbook：用 `lv_binfont_create()` 从文件系统加载字体（LVGL binfont）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于 `lv_binfont_create(path)`、LittleFS 挂载到 `/res`、以及“字体文件必须是 LVGL binfont，不是 TTF”的一组关键问答。
>
> 目标：把“读文件”“解析字体”“UI 解耦”和“文件格式要求”讲清楚，避免下一次又在字体加载上绕圈。

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

`lv_binfont_create(path)` 做的是：**读取字体文件 + 解析成 `lv_font_t*`**；它只识别 **LVGL binary font（binfont）**，不识别 `.ttf/.otf`。  
因此：**UI 层不要直接调用它**，而应由 `font_manager` 按 role 管理加载/缓存/回退。

---

## 为什么“加载字体文件”要用 LVGL 的 API

对话里最关键的拆解是：读取字体文件本质分两步：

1. **把文件内容读出来**：`fopen/fread` 等（这一步只是拿到 bytes）
2. **把内容解析成 `lv_font_t`**：这一步需要 LVGL 的解析逻辑

`lv_binfont_create()` 就是把第 1 + 2 步串起来，并把结果变成可直接给 `lv_label_set_text` 等 UI 组件使用的 `lv_font_t*`。

---

## “这些文件必须是 LVGL binfont，不是 TTF”是什么意思

含义很直接：如果你走的是 `lv_binfont_create()` 路线，那么字体资源就必须是 LVGL 能解析的 binfont 文件（通常扩展名用 `.bin`）。

- **不能直接用 `.ttf/.otf`**：因为 `lv_binfont_create()` 不会去解析 TTF/OTF
- **binfont 怎么来**：来自 LVGL 的字体转换流程（离线转换/生成），再把 `.bin` 放进 `resources/fonts/`

如果你想直接使用 `.ttf`，通常要换路线（例如 Tiny TTF / 把 TTF embed 到固件内存中再解析），不要强行把 `.ttf` 喂给 `lv_binfont_create()`。

---

## 路径与文件系统：你到底在“打开”哪一种路径

在 ESP-IDF 场景里，一个常见、清晰的组合是：

- ESP-IDF 把 LittleFS 分区挂载到 `/res`
- LVGL 打开 `CONFIG_LV_USE_FS_STDIO`，让 LVGL 走 `fopen/fread`
- `font_manager` 在内部尝试加载（示例）：`/res/fonts/ui_sans_16.bin`

注意：LVGL 有自己的 FS 路径语法（可能涉及 drive letter，例如 `'R'`）。**不要猜**：

- 用一个最小冒烟验证：`lv_fs_open()` 能否打开你预期的路径
- 或者直接用 `fopen("/res/xxx")` 先确认底层 VFS/LittleFS 已经挂上且文件存在

相关“资源从设备文件系统提供给 LVGL”的整体链路，见：

- `spec/iot/device-ui/resource-filesystem-playbook.md`

---

## 失败模式与策略（建议默认就有）

### 常见失败模式

- 文件系统没挂载成功（`/res` 不存在 / register 返回错误）
- 文件存在但路径不对（目录结构/命名不一致）
- 文件格式不对（不是 binfont）
- 内存不足导致创建失败

### 建议策略

- `font_manager_init()` 里：先初始化/确认资源文件系统 ready，再逐个加载字体
- 任意加载失败：**回退到内置字体**（例如 Montserrat），保证 UI 起码能跑起来
- UI 层仍然只按 role 调 `font_manager_get(role)`，不传播路径/格式细节

---

## 验收标准

- 在资源文件存在时：`font_manager` 能加载并让 UI 生效（至少 1~2 个 role）
- 在资源缺失/挂载失败时：UI 仍能显示（fallback 生效），并且日志能明确区分“挂载失败/文件缺失/格式错误”

