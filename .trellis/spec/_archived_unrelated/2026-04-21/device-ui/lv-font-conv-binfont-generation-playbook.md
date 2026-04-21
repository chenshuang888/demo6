# Playbook：用 `lv_font_conv` 把 TTF 转成 LVGL binfont（用于 `/res/fonts/*.bin`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中“先把资源字体链路跑通”的实战：从工程内现成 TTF 找字体源 → 用 `lv_font_conv` 生成 4 个 binfont → 放进 `resources/fonts/` → 让 `font_manager` 在设备上加载并生效。
>
> 目标：快速、可重复地生成可用的 `.bin` 字体文件，优先服务“链路验证”，再逐步走向“正式字体资源”。

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

第一阶段别纠结“最终 UI 字体”，先用工程里现成的 TTF（例如 LVGL managed component 自带测试字体）把链路跑通：  
`TTF -> lv_font_conv -> resources/fonts/*.bin -> 打包到 LittleFS -> /res/fonts/*.bin -> font_manager -> UI 生效`。

---

## 1) 字体源怎么选（第一阶段：只为验证链路）

对话里采用的策略是：**先用项目里现成就有的字体源**，避免引入不明外部来源。

示例字体源（来自 LVGL managed component 的测试文件）：

- `managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf`

优点：

- 项目里现成就有（不额外下载）
- 支持中文（能验证“中文文案是否能显示”）
- 适合做“技术验证字体源”

注意：

- 这类字体更适合做 bring-up 验证，不一定是最终产品 UI 字体
- 链路稳定后再替换成你选定的正式字体源

---

## 2) 生成哪些 `.bin`（按 role 拆分）

对话里建议第一版就按 role 拆开，避免“一套字体包打天下”：

- `resources/fonts/ui_sans_16.bin`：正文/常规文本
- `resources/fonts/ui_sans_24.bin`：标题
- `resources/fonts/ui_num_24.bin`：数字（只含 `0123456789`）
- `resources/fonts/ui_icon_16.bin`：图标字体（第一阶段可先用数字占位验证链路，后续再换真 icon 字库）

---

## 3) `lv_font_conv` 的“验证链路”参数建议（先稳）

对话里验证过一组“优先稳定可显示”的组合：

- `--format bin`
- `--no-compress`
- `--no-prefilter`
- `--bpp 4`

含义：

- **先不压缩、不预过滤**：优先排除“资源格式/解码兼容性”变量，先把显示跑通
- `--bpp 4`：对小字号可读性通常更友好（但也更占空间）；后续可以按容量与观感再收敛

> 如果你使用了压缩/预过滤，出现“能加载、能读到 metrics，但屏幕不显示”的情况，优先回到不压缩版本做对照验证（见相关 pitfall）。

---

## 4) 字符集怎么收敛（让验证更快）

对话里给了一个很实用的原则：**先做最小可验证字符集**，别一上来全量中文字库。

示例：

- `ui_sans_16.bin` / `ui_sans_24.bin`：只覆盖你当前页面要展示的字符串（例如 `资源字体已加载2026`）
- `ui_num_24.bin`：只覆盖数字
- `ui_icon_16.bin`：第一阶段先用数字占位

这样做的收益：

- 字体文件体积小、生成快
- 失败时更容易定位问题在“链路”还是“字体容量/内存”

---

## 5) 设备端验收点（看日志就能判断）

对话里建议用日志一眼区分：

- 成功：出现类似 `loaded font: /res/fonts/ui_sans_16.bin`
- 失败：仍然是 `use fallback font for /res/fonts/...`

并且要区分两类状态：

- 挂载失败：`/res` 路径不可用（先修 `resources_fs` / 分区表/挂载）
- 挂载成功但字体 fallback：通常是文件缺失/格式不兼容/字符集不包含（下一步再修字体）

---

## 相关文档

- `spec/iot/device-ui/resource-filesystem-playbook.md`：`/res` 挂载与 LVGL FS 的关键约定
- `spec/iot/device-ui/binfont-load-success-but-no-render-pitfall.md`：binfont “加载成功但不显示”的定位与修法

