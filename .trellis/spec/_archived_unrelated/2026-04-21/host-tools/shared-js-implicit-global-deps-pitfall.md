# Pitfall：共享 JS 的“隐式全局符号依赖”导致页面运行时报错

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中关于 `protocol.js`/`handlers.js` 的分析与 store 页面实施说明。
>
> 背景：为了复用，把 `serial.js/protocol.js/ui.js` 作为共享脚本；不同页面按需引入。

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

**共享脚本不仅要“DOM 不崩”，还要“符号不崩”：任何跨文件调用都必须显式依赖（引入顺序/注册表）或做 `typeof` 容错。**

---

## 触发条件

- `protocol.js` 的 `parseUnifiedData()` 在解析某些命令时，会直接调用 `handleStoreGetList/handleStoreGetDetail/handleStoreDownload` 等处理函数
- 这些处理函数定义在 `handlers.js`
- 某个页面（例如 store.html）没有引入 `handlers.js`，或引入顺序错误

---

## 现象（可观测信号）

- 控制台报错 `ReferenceError: handleStoreGetList is not defined`（或类似）
- 页面“看起来像串口/协议不通”，但根因是 JS 运行时崩溃导致后续逻辑中断

---

## 根因

- 共享模块采用“全局函数名直呼”的方式耦合（而非注册表/依赖注入）
- 页面按需加载脚本时，**缺少对“处理函数集合”这一依赖的显式约束**

---

## 修复策略（按推荐顺序）

### 方案 A（推荐，最低改动）：页面总是引入 `handlers.js`

- 只要页面引入了 `protocol.js` 且会走到 `parseUnifiedData()`，就把 `handlers.js` 作为必选依赖
- 同时固定脚本引入顺序（见下方“验收清单”）

### 方案 B：在 `protocol.js` 做容错（允许 handlers 缺席）

- 在调用前判断 `typeof handleStoreGetList === 'function'`
- 缺失时：记录日志并忽略该命令，避免整个页面崩溃

### 方案 C：重构为“注册表模式”（长期演进）

- `protocol.js` 不再直呼全局函数名
- 页面侧调用 `Protocol_RegisterHandlers({...})` 注入 handler 集合

---

## 验收清单

- 任何页面即使不支持“应用商店”功能，只要加载了共享协议脚本，也不因缺少 handlers 而崩溃
- 若采用方案 A：页面引入顺序稳定且无 `ReferenceError`
- 若采用方案 B/C：handlers 缺席时功能降级可观测（有日志），但页面不崩

