# Contract：STM32 单外置 Flash 的 OTA 分区与职责划分（Bootloader + W25Q64）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-----.md`、`C--Users-ChenShuang-Desktop-OLED-----.md`）中关于 STM32 OTA、外置 Flash 分区规划与“应用商店/动态 App + OTA”组合架构的总结。
>
> 目标：在内部 Flash 紧张的 STM32 上，用外置 SPI Flash（如 W25Q64）实现可落地、可回滚、可验收的 OTA 更新闭环。

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


## 一句话原则

**内部 Flash 只放“Bootloader + 当前运行固件”，新固件放外置 Flash；升级时由 Bootloader 搬运、校验并切换。**

---

## 适用范围

- MCU：STM32F103C8/F103RB 等内部 Flash 紧张的系列
- 外置存储：SPI Flash（W25Q64：8MB）
- 形态：
  - 纯 OTA（外置 Flash 只存固件候选）
  - OTA + 动态 App/应用商店（外置 Flash 同时承载 App slots）

---

## 典型布局 A：最小 OTA-only（示意）

### 内部 Flash（示例：64KB 量级）

- Bootloader：`0x08000000 - 0x08001FFF`（8KB）
- Application：`0x08002000 - 0x0800FBFF`（56KB）
- OTA Flag/配置：`0x0800FC00 - 0x0800FFFF`（1KB，示例）

### 外置 Flash（W25Q64：8MB）

- 固件元信息：`0x000000 - 0x000FFF`（4KB）
- 固件数据：`0x001000 - ...`（长度按固件大小）

说明：

- 地址/大小必须以你的 linker/擦除粒度为准，上面只是示意。
- 不变的是职责分工：内部“当前运行”，外置“候选/下载/校验”。

---

## 典型布局 B：OTA + 动态 App/应用商店（更完整示例）

对话里在 STM32F103 + 128×64 OLED 项目给出了更完整的外置 Flash 规划（8MB = `0x800000`），适合“应用商店 + OTA”的组合场景：

```text
W25Q64 (8MB)
├─ [0x000000 - 0x03FFFF] App 存储区 (256KB)
│  ├─ Slot 0-7（每个 8KB）
│  │  ├─ 元数据区（例如 256B）：magic/app_id/version/size...
│  │  └─ 数据区（剩余）：App 机器码/资源
│  └─（可继续扩展更多 slots/更大 slot）
├─ [0x100000 - 0x1FFFFF] OTA 固件区 (128KB)
│  ├─ Slot 0（64KB）：运行固件备份
│  └─ Slot 1（64KB）：待激活新固件
└─ [0x7FFC00 - 0x7FFFFF] Bootloader 配置区（1KB）
   ├─ OTA flag（示例：0xAA55AA55）
   ├─ active slot id
   ├─ rollback 标志
   └─ retry 计数
```

要点：

- “动态 App 区”与“OTA 区”建议物理隔离，避免相互污染与擦写冲突。
- Bootloader 配置区放在末尾小块，便于单独擦写/更新（注意擦除粒度与掉电一致性）。

---

## 验收标准

- 外置 Flash 中存在完整候选固件与元信息（大小/校验可验证）
- 触发升级后，Bootloader 能稳定完成搬运、校验、切换并启动新固件
- 失败路径明确：校验失败/写入失败/掉电中断时，系统能回到可运行状态（不进入“半升级”）

