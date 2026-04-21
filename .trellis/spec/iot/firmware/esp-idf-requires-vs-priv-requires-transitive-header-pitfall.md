# Pitfall：ESP-IDF `REQUIRES` vs `PRIV_REQUIRES`（公共头暴露依赖导致 `nvs.h` 找不到）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中 NVS 分层落地后出现的真实编译错误：`persist.h` 公共头里 `#include "nvs.h"`，但组件把 `nvs_flash` 放在 `PRIV_REQUIRES`，导致依赖方编译失败。
>
> 目标：把“编译错误 → 根因 → 修复 → 预防”的路径固化，避免在新增模块时反复翻车。

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

编译到依赖该组件的目标时报错：

- `fatal error: nvs.h: No such file or directory`

---

## 根因（可复用的判断法）

在 ESP-IDF 组件系统里：

- `PRIV_REQUIRES`：**私有依赖**，只对本组件 `.c` 生效，不向上游传递 include 路径
- `REQUIRES`：**公共依赖**，任何 `REQUIRES <本组件>` 的组件都会自动拿到该依赖的 include 路径

因此：

- 如果你的 **公共头文件**（比如 `persist.h`）暴露了 `nvs.h` 中的类型/宏/错误码，那么 `nvs_flash` 必须是公共依赖（`REQUIRES`）。

---

## 标准修复（优先级从高到低）

### 方案 A（最直接）：把依赖提到 `REQUIRES`

当 `persist.h` 需要 `nvs.h` 时，把 `nvs_flash` 从 `PRIV_REQUIRES` 移到 `REQUIRES`。

### 方案 B（更“干净”）：公共头不暴露底层依赖

如果你不希望依赖向上传播：

- 让 `persist.h` 不再 `#include "nvs.h"`（把 NVS 相关类型/错误码隐藏在 `.c` 内）
- 公共 API 返回项目自己的错误码/状态码

---

## 验收标准

- 依赖该组件的 `main/app/drivers` 均能编译通过（无需额外手动 include 路径）。
- 公共头的 include 关系是“向下可见”，不会被 `PRIV_REQUIRES` 意外切断。

