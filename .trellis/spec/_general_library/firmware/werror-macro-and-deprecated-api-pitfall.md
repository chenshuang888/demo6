# Pitfall：`-Werror` 下宏/弃用 API 会直接炸编译（`ESP_RETURN_ON_ERROR` / `esp_lcd_touch_get_*`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中一次真实报错链路：驱动拆分后在 `drivers/lcd_panel.c` 与 `drivers/touch_ft5x06.c` 触发 `-Werror`，同时出现“缺头文件导致宏被当函数”和“弃用 API 警告”。
>
> 目标：在严格告警策略（`-Werror=all`）下，把这类“细小但高频”的编译失败一次性规避。

---
## 上下文签名（Context Signature）

> 这是“坑点复盘（Pitfall）”。症状相似不代表根因相同。
> 如果无法对齐本节上下文与证据：**禁止**直接给“最终修复实现”，只能给排查路径与最小验证闭环。

- 目标平台：ESP32/ESP32-S3/STM32/…
- SDK/版本：ESP-IDF x.y / LVGL x.y / HAL 版本 / …
- 关键外设：LCD/Touch/I2C/SPI/UART/Wi‑Fi/BLE/…
- 资源约束：Flash/RAM/是否有 PSRAM / heap 策略
- 并发模型：谁是 single-writer？哪些回调/中断上下文？

---

## 证据最小集（必须补齐，否则只给排查清单）

- 复现步骤：最短 3~5 步
- 关键日志：至少 10 行（含时间戳/线程/错误码）
- 关键配置：`sdkconfig`/分区表/LVGL 配置/驱动配置（只列与问题相关的）
- 边界条件：是否“只在某分辨率/某字体/某 MTU/某波特率/某温度/某电源”下发生？

---

## 停手规则（Stop Rules）

- 无复现、无日志、无法确认平台/版本：不要输出最终修复，只输出“要补齐的信息 + 排查清单”。
- 修复涉及写 flash/修改分区/改并发 owner：先给最小冒烟闭环与回滚方案，再进入实现细节。
- 多个根因都解释得通：先加观测点（日志/计数器/抓包）缩小假设空间，再改代码。

---


## 一句话结论

**当工程使用 `-Werror` 时：缺头文件导致的宏不可用、以及 `deprecated` 警告，都会被当成“必须立刻修”的错误。**

---

## 典型症状

### 1) 宏没生效 → 被当成函数调用

- 报错：
  - `error: implicit declaration of function 'ESP_RETURN_ON_ERROR' [-Werror=implicit-function-declaration]`
- 现象本质：
  - `ESP_RETURN_ON_ERROR` 是宏；缺少头文件时，预处理没有展开，编译器就当成“未声明函数”

### 2) 弃用 API 触发告警（在 `-Werror` 下会卡死）

- 例如：
  - `warning: 'esp_lcd_touch_get_coordinates' is deprecated ... [-Wdeprecated-declarations]`
- 在 `-Werror`/严格策略下，这类告警常常会导致构建失败或阻塞合入。

---

## 修复要点（对话中已验证）

### 1) 使用 `ESP_RETURN_ON_ERROR` 必须包含对应头文件

- 添加：
  - `#include "esp_check.h"`

> 也可以选择不用该宏，改用 `ESP_ERROR_CHECK` 等更常见写法，但关键是：宏/函数必须有来源。

### 2) 触摸层弃用 API 替换

- 从：
  - `esp_lcd_touch_get_coordinates(...)`
- 替换为：
  - `esp_lcd_touch_get_data(...)`

---

## 预防清单

- [ ] 只要看到 `implicit declaration of function`，优先检查：是不是宏缺头文件导致没展开
- [ ] 引入第三方/managed component 后，优先用“新 API”而不是“虽能用但已弃用”的旧接口
- [ ] 在 `-Werror` 项目里，**不要让 deprecation warning 留到最后再处理**

---

## 验收标准

- `drivers/lcd_panel.c` / `drivers/touch_ft5x06.c` 在 `-Werror` 下能通过编译
- 不再出现 `ESP_RETURN_ON_ERROR` 的 implicit declaration
- 不再出现 `esp_lcd_touch_get_coordinates` 的弃用告警

