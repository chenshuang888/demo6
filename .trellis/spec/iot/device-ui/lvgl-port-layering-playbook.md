# Playbook：LVGL 接入的驱动分层（底层硬件层 + LVGL 对接层）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于 `drivers/` 分层与“不过度设计”的最终建议。
>
> 目标：把“能跑起来的 LVGL glue”整理成可重复落地的工程结构，后续扩展不返工。

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

**建议拆，但只拆成“底层硬件层 + LVGL 对接层”两层就够了；不要继续细分太多。**

---

## 现状如何理解（避免误判）

如果你当前的 `drivers/`：

- 明确只服务于“把 LCD/触摸接到 LVGL 上”，没有做成通用驱动库的诉求
- 接口围绕 LVGL（例如需要 `lv_display_t*`、内部注册 `flush_cb/read_cb`）

那么它更像是 **板级 BSP + LVGL glue 层** 的组合，这个定位本身没有问题；本 Playbook 的目标是把边界再收敛，降低后续演进的返工成本。

---

## 目标

- 显示/触摸底层驱动不依赖 LVGL（不包含 LVGL 语义）
- `lvgl_port.*` 作为唯一的 LVGL glue：负责把底层能力接到 LVGL（display/indev）
- 坐标系、旋转、mirror/swap_xy 等“显示系统策略”集中在 LVGL 对接层，避免职责漂移

---

## 推荐目录形态（最小集合）

对话中给出的“更合理的未来形态”可落为：

- `lcd_panel.*`：纯显示底层（初始化、背光、绘制、开关、旋转能力等）
- `touch_ft5x06.*`：纯触摸底层（I2C 初始化、读取原始坐标）
- `lvgl_port.*`：专门对接 LVGL（display flush、touch read、坐标映射）
- `board_config.h`：板级参数（引脚、方向、分辨率、背光参数等）

---

## 分层与职责（必须写清）

### 1) 底层硬件层（`lcd_panel.*` / `touch_ft5x06.*`）

底层更适合提供“能力”而不是“框架语义”，典型能力清单：

- `init`
- `draw_bitmap`
- `set_backlight`
- `set_panel_on`
- `set_rotation` / `set_mirror`（能力接口，是否使用由上层决定）

底层触摸驱动应只返回 **原始坐标**（不做屏幕方向映射）。

#### 底层典型包含什么（从对话抽出来的“混层点”）

- 显示侧：SPI/I80 总线初始化、屏幕控制器（如 ST7789）初始化、背光 PWM/LEDC 初始化、面板方向/反色/开关等“面板能力”
- 触摸侧：I2C 总线初始化、触摸芯片（如 FT5x06）驱动创建、读取 raw 坐标

### 2) LVGL 对接层（`lvgl_port.*`）

- 创建/配置 LVGL display、注册 flush_cb
- 创建/配置 LVGL indev、注册 read_cb
- **集中处理坐标映射**：方向、mirror、swap_xy、坐标翻转等

#### LVGL 对接层典型包含什么（从对话抽出来的“混层点”）

- 显示侧：`flush_cb`、flush 完成通知、`lv_display_set_*` 的注册、LVGL draw buffer 的分配与大小策略（例如 40 行局部缓冲/partial render/RGB565）
- 触摸侧：`lv_indev_create()`、`lv_indev_set_read_cb()`、`touch_read_cb()` 这类 LVGL 输入回调与坐标映射

> 蒸馏原则：坐标映射属于“显示系统怎么解释坐标”，是上层策略，不是芯片驱动职责。

---

## 关键边界原则（强约束）

### 原则 1：坐标变换最好放在 `lvgl_port.*`

落地规则：

- `touch_ft5x06.*`：返回 raw 坐标
- `lvgl_port.*`：根据屏幕方向做映射（包括 mirror/swap_xy/翻转）

### 原则 2：底层尽量提供“能力”，不要直接提供“框架语义”

落地规则：

- 底层 API 命名与参数保持硬件语义（bitmap、rotation、on/off）
- LVGL 回调/对象（如 `lv_display_t*`）尽量不要穿透到底层
- 底层文件尽量不要 `#include <lvgl.h>`（一旦出现通常意味着混层；例外情况要写明原因）

---

## 最小依赖思路（避免一开始写太重）

对话中给出的落地建议是“先最小集合，缺啥再补啥”：

- 先保留核心：`lvgl`、`esp_lcd`、`esp_lcd_touch_ft5x06`
- 如果 `lvgl_port.c` 直接用到 `esp_timer_get_time()` 再补：`esp_timer`
- 先不急着显式写：`esp_driver_spi` / `esp_driver_i2c` / `esp_driver_ledc`

---

## 常见误解：拆分后“依赖变多”≠“工程变重”

对话里出现过一个典型疑问：为什么拆分后看起来要引入很多底层库？

这里的关键是：你看到的往往是 **ESP-IDF 组件依赖声明被显式化**（`idf_component_register` 的 `REQUIRES/PRIV_REQUIRES`），不一定是新增了很多源码文件。

拆成 `lcd_panel.* / touch_ft5x06.* / lvgl_port.*` 之后，更容易把依赖关系想清楚：

- 谁直接用 SPI/I2C/LEDC
- 谁直接用 LVGL
- 谁直接用 `esp_timer`

落地建议：

- 以“**源文件里实际 include/调用了什么**”为准，逐层补齐依赖
- 组件内部使用优先写到 `PRIV_REQUIRES`，避免把依赖扩散到上层

---

## 实施步骤（建议顺序）

1. **先定边界**：明确哪些文件属于底层、哪些属于 `lvgl_port.*`，并写到 README/注释（哪怕一句话）。
2. **触摸先归位**：保证触摸底层只输出 raw 坐标；把翻转/映射迁移到 `lvgl_port.*`。
3. **显示能力接口化**：把显示底层收敛为 `init/draw_bitmap/backlight/onoff/rotation` 等能力接口。
4. **LVGL glue 收敛**：所有 LVGL 的创建与回调注册只留在 `lvgl_port.*`。
5. **收敛配置来源**：把方向/分辨率/引脚等板级参数统一到 `board_config.h`（避免多处重复定义）。

---

## 验证顺序（必须可执行）

1. **显示冒烟**：只跑 display flush（无触摸），确认刷屏正常、方向正确。
2. **触摸冒烟**：只验证 raw 坐标能读到（日志/简单点阵），再开启 `lvgl_port.*` 映射。
3. **整体验证**：LVGL demo 可操作，点击区域与显示一致（不出现“触摸方向反了/轴对不上”）。

---

## 常见问题（快速排障）

- 触摸点位与显示错位
  - 优先检查：坐标映射是否分散在多个层（应只在 `lvgl_port.*`）
- 底层 API 里出现 `lv_*` 类型
  - 说明：边界被破坏，后续更难迁移/替换 GUI 框架
