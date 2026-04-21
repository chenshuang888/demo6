# Pitfall：ESP32-S3 MSPI Timing 表缺失（Flash/PSRAM 频率组合不受支持导致编译失败）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中编译 `esp_hw_support/mspi_timing_by_mspi_delay.c` 失败：缺少某个 `MSPI_TIMING_*` 配置表，根因是 Flash 与 PSRAM 的时钟组合不被 ESP-IDF 支持。
>
> 目标：遇到 `mspi_timing_*` 相关报错时，不要在应用层盲调；先把 Flash/PSRAM 的频率与模式组合拉回到“IDF 明确支持”的集合。

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


## 典型症状

- 构建失败在：`esp_hw_support/mspi_timing_by_mspi_delay.c`
- 错误指向 `mspi_timing_tuning_configs.h` 一类的宏展开
- 对话中总结的关键信息：
  - 报错语义是：`FLASH and PSRAM Mode configuration are not supported`
  - 缺少类似 `MSPI_TIMING_PSRAM_CONFIG_TABLE_CORE_CLK_240M_MODULE_CLK_80M_DTR_MODE` 的表项

---

## 根因（对话中的结论）

**Flash 频率与 PSRAM 频率组合不在 ESP-IDF 支持表里。**

对话里的具体案例是：

- Flash 设成 120MHz
- PSRAM 设成 80MHz

该组合会触发“找不到 MSPI timing 表”的编译期失败。

---

## 标准修复（工程上更稳的优先）

对话给出的可用组合建议：

- ✅ **Flash 80MHz + PSRAM 80MHz**（更稳，优先）
- ⚠️ Flash 120MHz + PSRAM 120MHz（理论可行但可能更挑板子/稳定性，需要自行验证）

因此修复路径通常是：

1) 把 Flash 频率降到 80MHz（先把链路跑通）
2) 需要更高性能再整体抬到 120/120（并单独评估稳定性）

---

## 验收标准

- `idf.py build` 不再卡在 `esp_hw_support` 的 MSPI timing 文件
- 运行时日志能正常初始化 Flash/PSRAM（不出现“频率/模式不支持”一类告警）

