# Pitfall：ESP-IDF 组件名随版本变化，`REQUIRES` 写错会直接 CMake 失败

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）在 ESP-IDF 5.2.6/5.4.x 间来回对齐依赖名（`esp_driver_i2c`、`esp_lcd_touch` 等）的踩坑过程。
>
> 目标：避免“代码没问题，但 CMake 配不出来”，以及避免在旧版本 IDF 上引用新版本才存在的组件名。

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

**先确认 ESP-IDF 版本，再写 `REQUIRES`/`idf_component.yml`。不要把“新版本组件拆分后的名字”硬塞给旧版本。**

---

## 典型现象

- CMake 直接失败（还没到编译）：
  - `Failed to resolve component 'esp_driver_i2c'`
  - `Unknown/Not found component 'esp_lcd_touch'`（或等价报错）

---

## 根因（对话中的真实例子）

### 1) `esp_driver_i2c / esp_driver_spi / esp_driver_ledc` 在 IDF 5.2.x 不存在

- IDF 5.2.x 时代，I2C/SPI/LEDC 等驱动仍归在一个大组件：`driver`
- 因此 `REQUIRES esp_driver_i2c` 会直接 “resolve 失败”

**修复**：

- 在 `drivers/CMakeLists.txt` 里把 `REQUIRES esp_driver_i2c ...` 改为 `REQUIRES driver`

### 2) `esp_lcd_touch` 在部分 IDF 版本不是“自带组件”

对话里遇到的情况是：

- IDF 5.2.6 环境下，没有自带 `esp_lcd_touch`
- 需要用组件管理器显式拉取：
  - `espressif/esp_lcd_touch`
  - `espressif/esp_lcd_touch_ft5x06`

---

## 推荐做法（可执行）

### 1) 先定位当前 IDF 版本

最简单：看编译日志里的 `IDF_VER="vX.Y.Z"`，或 `idf.py --version`。

### 2) `REQUIRES` 写“最兼容”的集合

当你要兼容 5.2.x 时：

- 优先用 `driver`（而不是 `esp_driver_i2c/esp_driver_spi/...`）
- 其他常见依赖按实际 include 头文件补齐：
  - 用到了 `esp_timer.h` 就要 `REQUIRES esp_timer`

### 3) 触摸/LVGL 等外部依赖：用 `idf_component.yml` 显式声明

对话里用于兼容旧 IDF 的示例（语义）：

```yml
dependencies:
  lvgl/lvgl: "^9.0.0"
  espressif/esp_lcd_touch: "^1.2.1"
  espressif/esp_lcd_touch_ft5x06: "^1.1.0"
```

> 若你必须离线编译：把这些组件源码放进 `components/`，不要依赖 component manager。

---

## 验收标准

- `idf.py reconfigure` / `idf.py build` 不再出现 “Failed to resolve component …”
- `drivers` 组件能被扫描到并参与构建（不再是“目录存在但未编译”）

