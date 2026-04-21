# Pitfall：LVGL 字体未启用导致 `lv_font_montserrat_xx` 未定义；`sdkconfig.defaults` 改了却不生效

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中 `lv_font_montserrat_20/24/48` 反复触发未定义错误，以及 `sdkconfig.defaults` 不覆盖已有 `sdkconfig` 的现实问题。
>
> 目标：避免“UI 写完了，卡在字体编译错误”以及避免“以为 defaults 生效，其实没生效”。

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

1) **不要假设 LVGL 默认启用了你想用的字体。**先确认，再引用。  
2) **`sdkconfig.defaults` 不会自动覆盖已有 `sdkconfig`。**改 defaults 后要 `idf.py reconfigure`（或直接改 `sdkconfig`）。

---

## 典型现象

- 编译错误：
  - `error: 'lv_font_montserrat_24' undeclared`
  - `error: 'lv_font_montserrat_20' undeclared`
  - 类似地，`48` 更常见（默认通常不启用）

---

## 根因

### 1) 字体是否存在由 Kconfig 决定

LVGL 的 `lv_conf_kconfig.h` 会根据 `sdkconfig` 生成宏，未启用的字体不会生成对应的 `lv_font_montserrat_xx` 符号。

### 2) 只改 `sdkconfig.defaults` 不等于“当前工程配置已变”

如果工程里已经有 `sdkconfig`：

- `sdkconfig.defaults` 只是“默认值来源”
- 现有 `sdkconfig` 不会被自动覆盖

---

## 修复（推荐顺序）

### 方案 A（推荐）：启用需要的字体，并 reconfigure

1. 在 `sdkconfig.defaults` 或 `menuconfig` 启用字体（对话中启用了 20/24）：
   - `CONFIG_LV_FONT_MONTSERRAT_20=y`
   - `CONFIG_LV_FONT_MONTSERRAT_24=y`
2. 执行 `idf.py reconfigure`
3. 再 `idf.py build`

### 方案 B：临时回退到已启用字体（先让它编过）

如果你不想现在改配置：

- UI 先只用已启用的字体（例如 `montserrat_14`）
- 等框架跑通后，再决定是否增加字体体积

### 方案 C：defaults 已改但急着验证 —— 直接改 `sdkconfig`

当你已经确认 defaults 不生效（例如没跑 reconfigure），又需要快速验证：

- 直接在 `sdkconfig` 里把需要的 `CONFIG_LV_FONT_MONTSERRAT_xx` 打开
- 后续再把它“正规化”迁回 `sdkconfig.defaults`

---

## 验收标准

- `build/config/sdkconfig.h` 中能看到目标字体宏已启用
- 工程不再因 `lv_font_montserrat_xx` 未定义而失败
- 重新 `idf.py reconfigure` 后配置一致（不是“改了但没生效”）

