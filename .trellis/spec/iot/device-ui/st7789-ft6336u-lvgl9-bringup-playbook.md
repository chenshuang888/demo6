# Playbook：ESP32-S3 + ST7789（SPI）+ FT6336U/FT5x06（I2C）+ LVGL 9 的最小点屏/触摸 bring-up

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中 demo1 的已跑通配置盘点：ST7789V 320×240 RGB565 + FT6336U 触摸（兼容 FT5x06），并给出初始化顺序、关键 SPI/I2C 参数、以及 swap/mirror 的典型组合。
>
> 目标：把"硬件起来"的关键项固化成可复用清单：**先点亮屏，再让触摸坐标对**，避免上来就写复杂 UI/框架导致定位困难。

> **[本项目适配说明]**（demo6）：
> - 本项目触摸 IC 是 **FT5x06**（不是 FT6336U），通过 `espressif/esp_lcd_touch_ft5x06` managed component 接入；I2C 初始化与地址见 `drivers/touch_ft5x06.c`
> - 屏幕 240×320 竖屏（H_RES=240, V_RES=320，与 playbook 里 320×240 横屏相反）
> - pinmap 集中在 `drivers/board_config.h`（LCD: SCK12/MOSI11/CS10/DC9/RST8/BL14；Touch: SCL18/SDA17/RST15/INT16）
> - SPI 频率 40MHz，I2C 400kHz
> - 方向参数：`BOARD_LCD_MIRROR_X=1 / MIRROR_Y=1 / SWAP_XY=0`，触摸侧坐标在 `lvgl_port.c` 的 `lvgl_touch_read_cb` 里镜像
> - **复用时**：本 playbook 的不变式（init 顺序、先点屏再对触摸、方向排查走全局旋转）可直接用；FT6336U 特有的寄存器细节不要照搬，按 esp_lcd_touch_ft5x06 的 API 走
> - 相关：`../firmware/esp-idf-lvgl-drivers-bootstrap-playbook.md`（驱动层迁入）、`./display-touch-rotation-debug-playbook.md`（方向排查）

---

## 上下文签名（Context Signature，必填）

- 目标平台：ESP32‑S3（ESP‑IDF 5.x）+ LVGL 9.x
- 显示：ST7789（SPI）RGB565（常见 240×320/320×240）
- 触摸：FT6336U/FT5x06（I2C）
- 风险等级：高（点屏/触摸是后续所有 UI 的地基；方向/坐标系错了后面会全错）
- 并发模型：flush 回调/触摸 read_cb 的调用链必须清楚（避免误以为“驱动没跑”）

---

## 不变式（可直接复用）

- 先点亮屏幕，再对齐触摸坐标（不要一开始就写复杂 UI）
- 方向校准优先靠 `swap_xy/mirror` 组合，避免乱改触摸回调内部坐标逻辑
- bring‑up 要求可观测：四角标记 + 触摸回显（最快定位旋转/镜像问题）

---

## 参数清单（必须由当前项目提供/确认）

- 分辨率与方向：240×320 竖屏？320×240 横屏？是否需要 invert_color
- SPI：host、PCLK、IO 队列深度、DMA/PSRAM 相关配置
- I2C：端口号、频率、INT/RST 引脚（若有）
- pinmap：GPIO 表必须来自当前硬件（严禁照搬示例表）

---

## 停手规则（Stop Rules）

- pinmap 未确认时，禁止做“方向/坐标系”层面的结论（先把线对上）
- flush 完成通知链路不清楚时，禁止上双缓冲/动画（先把单缓冲跑稳）
- 触摸 read_cb 不进时，优先检查 indev/display 绑定链路，不要直接判定“触摸驱动坏了”

---

## 一句话结论

**先按固定顺序初始化：`NVS → LVGL → SPI 总线 → LCD panel io/panel → 背光 → LVGL draw buffer → I2C → touch`；显示方向靠 `swap_xy/mirror` 组合校准，别乱改坐标回调。**

---

## 参考引脚表（对话中的 demo1 配置）

> 不同板子会变；但“把 pinmap 写成表并统一管理”的习惯值得保留。

| 功能 | GPIO |
|---|---:|
| SCK | 12 |
| MOSI | 11 |
| CS | 10 |
| DC | 9 |
| RST | 8 |
| 背光 | 14 |
| 触摸 SCL | 18 |
| 触摸 SDA | 17 |
| 触摸 RST | 15 |
| 触摸 INT | 16 |

---

## 显示初始化关键参数（对话提到的典型值）

- SPI Host：`SPI2_HOST`
- PCLK：40MHz（示例）
- 颜色格式：RGB565（16-bit）
- IO 队列深度：10（示例）
- 面板方向校准：
  - `swap_xy(true)` + `mirror(false, true)`（示例）
  - 是否需要 `invert_color` 视屏幕实际而定

> 方向/镜像的判断方法：优先用“画四角标记 + 触摸点回显”的最小页面验证，见 `spec/iot/device-ui/display-touch-rotation-debug-playbook.md`。

---

## LVGL draw buffer 策略（与 PSRAM 强相关）

对话中的 demo1 使用：

- 全屏双缓冲（两张 full frame buffer）
- 放在 PSRAM
- full render 模式

账本与取舍见：

- `spec/iot/device-ui/lvgl-fullscreen-double-buffer-psram-render-mode-full-playbook.md`

---

## 触摸初始化关键参数（对话提到的典型值）

- I2C：`I2C_NUM_0`
- 速率：400kHz
- `x_max/y_max` 与屏幕分辨率对齐
- `swap_xy/mirror_*` 与显示方向联动（先让显示方向对，再对齐触摸）
- 可用触摸驱动：`esp_lcd_touch_ft5x06`（FT6336U 通常兼容）

---

## 验收标准（bring-up 最小闭环）

- 屏幕可稳定显示（无花屏/无方向乱）
- 背光可控（PWM 生效）
- 触摸坐标与屏幕一致（点哪里响应哪里；边缘不漂移）
