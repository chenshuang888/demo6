# Contract：STM32 动态 App 链接脚本与装载内存边界（app.ld）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中 `tools/DynamicLink/config/app.ld` 与动态 App 编译流程整理。
>
> 目标：把“动态 App 的运行地址/大小/入口/导出表位置”写成不可漂移的契约，避免后续 App 编译出来能生成 bin 但运行时崩溃。

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

**动态 App 必须被链接到固定 RAM 地址 `0x20002000`，总大小上限 `8KB`，并保证导出表段 `(.app_vectors)` 位于镜像最前。**

---

## 内存契约（必须与宿主固件一致）

- 装载内存：RAM
- 固定起始地址：`0x20002000`（对话说明：在栈之后）
- 长度上限：`8K`
- 入口点：`ENTRY(app_init)`

> 风险：如果宿主固件的栈/堆/全局区布局变化导致占用到 `0x20002000`，动态 App 会与宿主内存冲突，表现为随机 HardFault。

---

## 段布局关键点

链接脚本的核心约束：

- `.text` 段最前面必须 `KEEP(*(.app_vectors))`
  - 目的：确保“函数导出表/跳转表”在镜像起始，装载器可按固定偏移解析
- `.text/.rodata/.data/.bss` 全部放入同一段 RAM 区间（rwx）
- 丢弃不需要段：
  - `*(.ARM.exidx*)` / `*(.ARM.extab*)` / `*(.comment)`

---

## 编译产物生成流程（.c → .elf → .bin）

工具链：

- `arm-none-eabi-gcc`
- `arm-none-eabi-objcopy`
- `arm-none-eabi-size`

典型命令（语义示意，参数来自对话）：

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -Wall \
  -ffunction-sections -fdata-sections -nostdlib -nostartfiles \
  -T config/app.ld -o bin/xxx.elf src/xxx.c

arm-none-eabi-objcopy -O binary bin/xxx.elf bin/xxx.bin

arm-none-eabi-size bin/xxx.elf
```

---

## 验收标准

- `arm-none-eabi-size` 显示的 `.text+.data+.bss` 总和 < 8KB
- 生成的 `bin` 运行时能被宿主装载并正常调用 `app_init`
- 导出表位于镜像起始（`.app_vectors` 未被链接器丢弃/未被放到末尾）

