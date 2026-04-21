# Pitfall：Windows 上 `sdkconfig.defaults` 被按 GBK 读取导致 `UnicodeDecodeError`

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中首次创建 `sdkconfig.defaults` 后，构建阶段直接报 `UnicodeDecodeError: 'gbk' codec can't decode ...` 的真实问题定位与修复。
>
> 目标：避免因为“在 defaults 里写了中文注释/UTF-8 内容”导致 Kconfig/生成脚本在 Windows 下直接炸掉，把注意力从业务代码误导到构建系统。

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

构建/配置阶段报错类似：

- `UnicodeDecodeError: 'gbk' codec can't decode byte ... illegal multibyte sequence`

---

## 根因（对话中的结论）

- ESP-IDF 的配置/生成链路在 Windows 上**可能会用 GBK** 去读取 `sdkconfig.defaults`
- 如果 `sdkconfig.defaults` 实际是 UTF-8（尤其包含中文注释/中文字符），就会出现 GBK 解码失败

这类问题的特点是：你明明改的是“注释”，但构建会直接失败，看起来像工程/工具链坏了。

---

## 标准修复

对话中的可复用修法（优先级从高到低）：

1) **把 `sdkconfig.defaults` 里的中文全部移除**（只用英文注释 / 纯 ASCII）
2) 让 `sdkconfig.defaults` 保持“足够朴素”：只放必要的 Kconfig 行，不写花哨注释

> 实战建议：工程文档可以中文，但 `sdkconfig.defaults` / `partitions.csv` 这类“工具链要读的配置文件”优先保持 ASCII，减少跨环境不确定性。

---

## 验收标准

- 不再出现 `UnicodeDecodeError`（能进入正常的 cmake/kconfgen 生成流程）
- `sdkconfig.defaults` 的配置项仍然能在实际 `sdkconfig` 中生效（下一步仍需验证“defaults 生效方式”，见 `spec/iot/firmware/sdkconfig-defaults-playbook.md`）

