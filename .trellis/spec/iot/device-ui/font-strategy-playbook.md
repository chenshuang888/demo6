# Playbook：小屏字体策略（中文/子集/Tiny TTF/位图）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`、`C--Users-ChenShuang-Desktop-esp32-demo6.md`）的字体方案讨论与落地步骤。
>
> 目标：在 240×320 等小屏上“清晰可读 + 体积可控 + 可迭代”，并且不因为字体把 Flash/RAM/帧率拖垮。

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

- **不显示中文**：优先把默认字体改小（Montserrat 10/12）或使用像素风 `UNSCII_8`，成本最低。
- **要显示中文**：默认原则是 **只做子集**，不要整包；更推荐 **Tiny TTF + 子集 TTF 嵌入固件**，而不是把大位图字库硬塞进 Flash。

> 补充：如果你已经在走 `lv_binfont_create()` + binfont 的路线，小屏中文通常也可以优先“位图字体（binfont）”，但要做好 **bpp/容量/清晰度** 的权衡，并把字体按 role 拆开（正文/标题/数字/图标）。

---

## 为什么小屏会觉得“发糊”（常见根因）

对话里提到的一个非常典型的主观感受是：默认字体在小屏上看起来不够锐利。

常见原因是组合效应：

- 低 DPI 小屏 + 小字号
- RGB565（色深有限）
- SPI 刷新屏（像素边缘与刷新观感更敏感）
- 字体自带灰阶边缘/抗锯齿策略（在小字号上更容易“发虚”）

这不是你“用错了 LVGL”，而是显示条件决定了“更锐利的字体方案”更合适。

---

## 先问 3 个问题（决定路线）

1. 你要不要中文？（最大分水岭）
2. 你的 UI 主要是“菜单/状态字”还是“长文本阅读/聊天”？（决定字符集规模）
3. 你对 Flash 体积有没有硬线（例如 1.5MB 字库上限）？（决定字体源与子集策略）

---

## 方案总览

### 方案 0：只用英文/数字/符号（最省心）

- 默认字体改小：`LV_FONT_DEFAULT_MONTSERRAT_12` → `10`
- 或像素风：`UNSCII_8`（适合英文数字/符号，不适合中文）

适用：系统面板、媒体控制器、简单菜单。

---

## 最低成本：不导入外部字库，只改 LVGL 配置

当你“就是想更清晰一点，但懒得自己做字库”时，优先用 LVGL 自带可选字体：

- 把默认字体改小：例如 Montserrat 10/12
- 或改成像素风：`UNSCII_8`（很硬、很清楚，但偏终端风格）

落地通常只涉及两类动作：

1. 在 LVGL 配置里启用相应字体（Kconfig 或 `lv_conf.h`，取决于你的接入方式）
2. 调整 `LV_FONT_DEFAULT` 指向你要的默认字体

注意：如果你要显示中文，`UNSCII_8` 基本不适合；此时只能考虑 CJK 字体或 Tiny TTF 子集路线。

### 对话中给出的“优先级试错”顺序（可直接照抄）

- 主要是英文/数字：
  1. `LV_FONT_DEFAULT_UNSCII_8`
  2. `LV_FONT_DEFAULT_MONTSERRAT_12`
  3. `LV_FONT_DEFAULT_MONTSERRAT_10`
- 主要是中文：
  1. `LV_FONT_DEFAULT_SIMSUN_14_CJK`（通常比 16 更可控）
  2. `LV_FONT_DEFAULT_SIMSUN_16_CJK`

### 方案 1：用 LVGL 内置 CJK（快速起步）

对话中提到 LVGL 内置可选的 CJK 字体（例如 `SIMSUN_14_CJK/16_CJK`）适合作为“先跑通”的阶段性方案，但字号可能偏大/偏糊。

适用：验证流程、功能先跑通。

#### demo6 实战补充：LVGL 9.5 内置 `Source Han Sans SC`（14/16）可以省掉 lv_font_conv

对话里出现的“重大简化点”：

- LVGL（在某些版本/配置下）内置 `Source Han Sans SC` 的 CJK 字体（14/16 两档）
- 只要在 `sdkconfig.defaults` 勾选对应 Kconfig 即可启用
- 不需要 `lv_font_conv`、不需要 Node.js、也不需要下载 TTF

约束与落地要点（对话中的坑位）：

- **只有 14/16 两档**：原本标题用 20 的，需要做 role 映射（例如 TITLE 退到 16）。
- **字体对象是 `const`**：不能原地改字段（例如直接挂 fallback）；需要“值拷贝到非 const 全局，再挂 fallback”。
- **符号/图标兼容性**：
  - 对话里发现：内置 CJK 字体的生成参数 `-r` 已包含 ASCII `0x20-0x7F`，并包含“大部分” FontAwesome symbol 区段。
  - 但仍可能漏掉少数符号（对话点名 `LV_SYMBOL_BLUETOOTH` 可能不在列表里），需要 fallback 到 Montserrat 或在个别控件上指定字体。

推荐的 UI 字体 role（对话中的落地方式）：

- `APP_FONT_TEXT`：CJK 14（正文/列表/通知 body）
- `APP_FONT_TITLE`：CJK 16（标题/歌名）
- `APP_FONT_LARGE`：Montserrat 24（大号数字：温度/时间等纯数字）


### 方案 2：位图字体（可控但容易膨胀）

特点：渲染快、实现直观，但字符集一大就很容易爆 Flash。

适用：字符集非常有限（菜单固定字）、或需要极致性能。

### 方案 3：Tiny TTF + 子集 TTF（推荐）

对话中给出的推荐落地路线：

- 用 `pyftsubset` 做子集（例如 GB2312 常用字 + ASCII + 符号）
- 用 `EMBED_FILES` 把子集 `.ttf` 嵌入固件
- `app_fonts.c` 使用 `lv_tiny_ttf_create_data_ex()` 创建 `lv_font_t*`
- 通过宏 `APP_FONT_*` 保持上层页面代码不改

适用：中文 UI + 体积受限 + 不想把页面代码改得到处都是。

---

## 容易误判的一点：改了 `LV_FONT_DEFAULT`，demo 可能还是不变

你如果跑的是 `lv_demo_widgets()` 这类官方 demo，需要注意：

- demo 代码里**可能会直接点名使用某些字体**（例如 `montserrat_16/24` 之类）
- 这意味着：你即使把 `LV_FONT_DEFAULT` 改了，demo 仍可能继续使用它自己指定的字体组合

应对策略（按“省事优先”排序）：

1. 换一个更小、更贴近你实际 UI 的最小示例页面（自己画几个 label/button），验证默认字体是否生效
2. 接受 demo 的字体风格（它本来偏“桌面展示”而不是“极小屏锐利”）
3. 真想让 demo 变：去改 demo 里点名字体的地方（不推荐作为主路径）

---

## 推荐落地步骤（Tiny TTF 路线）

> 这是“低改动、不重构页面代码”的落地顺序。

1. **选字体源**
   - 优先选黑体类（小屏更清晰、轮廓更省空间）；对话中提到“同字数下楷体更难压缩到 1.5MB”
2. **生成字符子集**
   - 建议从“项目实际会显示的文本”反推字符集
   - 先做小子集跑通，缺字再补（避免一次性全量）
3. **嵌入固件**
   - `app/CMakeLists.txt` 增加 `EMBED_FILES "fonts/xxx_subset.ttf"`
4. **创建字体对象**
   - `app_fonts.c` 调 `lv_tiny_ttf_create_data_ex(data, size, size_px, kerning, cache_cnt)`
5. **配置开关**
   - `sdkconfig.defaults` 开 `LV_USE_TINY_TTF=y`
   - 视情况关闭内置大字库，回收 Flash（对话中提到可省回 ~800KB 量级）
6. **验证**
   - 冒烟：能显示中文/符号、不会崩溃
   - 缺字：以“缺字统计”驱动子集扩充，不要瞎加

---

## 小屏中文的另一条常见路线：binfont（位图）优先，按 role 拆分

对话里给出的更“嵌入式真实落地”的建议是：

- **不要一套字体包打天下**
- 把字体拆成 4 层（role）：
  - 正文
  - 标题
  - 数字
  - 图标

并且按用途做容量规划：

### 推荐组合（更平衡）

- **正文**：16px / **1bpp** / 常用字“全量”（例如 3500 常用字）
- **标题**：24px / **2bpp** / 只保留 UI 常用字子集（例如 300~800 字）
- **数字**：独立数字字体（只含 `0123456789`）
- **图标**：独立 icon font（后续替换为真正图标字库）

收益：

- 正文能覆盖足够的中文
- 标题观感更好
- 总体 flash 压力更可控

### 为什么不推荐“两个字号都全量”

对话里的判断非常直接：

- 一套小字号全量中文：可做
- 两套（16px + 24px）都全量：大概率会把 `resources` 分区推爆（尤其当你还要放图片/图标/其它资源）

---

## bpp 取舍：清晰度 vs 体积（小屏常见）

对话里提到的一个关键点是：你看到“糊”往往不是 LVGL “用错了”，而是：

- 小屏 + RGB565 + SPI 刷屏等条件下，对字体边缘更敏感
- bpp 越高观感可能更好，但体积也会明显变大

工程化建议：

1. bring-up 阶段可以先用更保守的参数验证链路（例如 `--bpp 4` 且不压缩）
2. 确认链路稳定后，再按容量目标逐步下调 bpp/扩大子集

> `lv_binfont_create()` 路线下，如果你使用了压缩/预过滤导致“能加载但不显示”，优先回到 `--no-compress --no-prefilter` 做对照验证。

相关落地与坑位见：

- `spec/iot/device-ui/lv-font-conv-binfont-generation-playbook.md`
- `spec/iot/device-ui/binfont-load-success-but-no-render-pitfall.md`

## 两个关键坑（写进脑子里）

### 坑 1：TTF vs OTF

对话中明确提醒：Tiny TTF 底层常基于 `stb_truetype`，**只解析 TrueType outlines（.ttf）**；如果是 OTF（CFF）可能无法解析。

### 坑 2：字体对象“结构体 vs 指针”的差异

对话中提到从位图字体切换到 Tiny TTF 时，常见差异是宏定义的写法：

- 位图字体可能是全局 `lv_font_t g_font;` → `#define APP_FONT_TEXT (&g_font)`
- Tiny TTF 返回的是 `lv_font_t*`（堆对象）→ `#define APP_FONT_TEXT (g_font_ptr)`

要点：**上层只要拿到 `lv_font_t*` 就行**，不要让页面层感知底层实现差异。

---

## 验收标准

- UI 主路径不会因为字体导致明显卡顿（可接受的范围内）
- 字库体积可控（有明确的子集策略与扩充流程）
- 上层页面代码不需要因为换字体而大面积改动（宏名/接口保持稳定）
