# Contract：`driver/` 目录的语义边界（什么能进 driver，什么不该进）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中 demo5 关于是否把 `resources_fs.*` 放进 `driver/` 的讨论。
>
> 目标：避免“把带业务语义/系统语义的模块塞进 driver，导致目录语义混乱”；并保证后续移植时“可移植层”足够薄且清晰。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 一句话结论

**`driver/` 默认指“硬件外设驱动层”。像 `resources_fs` 这种“分区挂载 + VFS 接入 + `/res` 语义路径”的模块，不是硬件驱动，不建议放进 `driver/`。**

---

## 判断准则：什么才更像 `driver`

通常更像 `driver/` 的模块：

- lcd / touch
- i2c / spi / uart
- gpio / pwm / backlight
- 传感器

共同点：直接驱动某个硬件外设，基本不携带“业务语义/系统语义”。

---

## 反例：为什么 `resources_fs` 不像 driver

`resources_fs` 做的事情更像：

- 组 LittleFS 挂载参数
- 挂载 `resources` 分区
- 暴露 `/res` 这个资源语义路径

它是“存储基础设施/资源系统底层支撑”，而不是硬件外设驱动。

并且它通常会绑定语义常量（例如 `RESOURCES_FS_PARTITION_LABEL`、`RESOURCES_FS_BASE_PATH`），说明它并非通用文件系统驱动，而是资源框架语义的一部分。

---

## 推荐归位方式（按收口目标选择）

### 方案 A（更推荐）：并入 `resource_manager`

如果目标是尽量收口边界：

- 直接并入 `resource_manager.*`
- 不再保留 `resources_fs.*` 作为独立公共概念

### 方案 B：独立一层，但放在“平台/系统/存储”类目录

如果你希望先保留独立层，也建议放在更符合语义的位置：

- `platform/`
- `system/`
- `storage/`
- `fs/`

例如：

- `platform/resources_fs.c`
- `storage/resources_fs.c`
- `fs/resources_fs.c`

---

## 验收标准

- 读目录的人不会被误导：看到 `driver/` 就默认理解为“硬件外设驱动”。
- “带资源语义/系统语义”的模块不会混进 `driver/`，避免后续移植/重构时职责打架。

