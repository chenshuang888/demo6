# Playbook：把工程配置迁移到“ESP32-S3 16MB Flash + 8MB PSRAM（Octal）”基线（以 demo3 为参考）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中围绕“resources 分区要 4MB”“日志提示实际 flash=16MB 但工程按 2MB 编译”“参考 demo3 的 `sdkconfig(.defaults)`”的一整段核对与迁移建议。
>
> 目标：当你已经有一个“跑得稳”的参考工程（demo3），要把另一个工程（demo5）迁移到同一硬件基线时，按可控步骤完成 flash/psram/分区表相关配置，避免盲抄导致不开机。

---
## 上下文签名（Context Signature，必填）

> 目的：避免“场景相似但背景不同”时照搬实现细节。

- 目标平台：ESP32/ESP32‑S3/STM32/…（按项目填写）
- SDK/版本：ESP-IDF x.y / LVGL 9.x / STM32 HAL / …（按项目填写）
- 外设/链路：UART/BLE/Wi‑Fi/TCP/UDP/屏幕/触摸/外置 Flash/…（按项目填写）
- 资源约束：Flash/RAM/PSRAM，是否允许双缓冲/大缓存（按项目填写）
- 并发模型：回调在哪个线程/任务触发；谁是 owner（UI/协议/存储）（按项目填写）

---

## 不变式（可直接复用）

- 先跑通最小闭环（冒烟）再叠功能/优化，避免不可定位
- 只直接复用“原则与边界”，实现细节必须参数化
- 必须可观测：日志/计数/错误码/抓包等至少一种证据链

---

## 参数清单（必须由当前项目提供/确认）

> 关键参数缺失时必须停手，先做 Fit Check：`spec/iot/guides/spec-reuse-safety-playbook.md`

- 常量/边界：magic、分辨率、slot 大小、最大 payload、buffer 大小等（按项目填写）
- 时序策略：超时/重试/节奏/窗口/幂等（按项目填写）
- 存储语义：写入位置、校验策略、激活/回滚策略（如适用）（按项目填写）

---

## 可替换点（示例映射，不得照搬）

- 本文若出现文件名/目录名/参数示例值：一律视为“示例”，必须先做角色映射再落地
- 角色映射建议：transport/codec/protocol/ui/persist 的 owner 边界先明确

---

## 停手规则（Stop Rules）

- 上下文签名关键项不匹配（平台/版本/外设/资源/并发模型）时，禁止照搬实施步骤
- 关键参数缺失（端序/magic/分辨率/slot/payload_size/超时重试等）时，禁止给最终实现
- 缺少可执行的最小冒烟闭环（无法验收）时，禁止继续叠功能

---


## 一句话结论

迁移顺序建议是：

1. **先让 demo5 的实际 `sdkconfig` 进入 16MB Flash 基线**
2. **再启用/对齐 8MB PSRAM（Octal + 80MHz + malloc 策略）**
3. **保持自定义分区表始终一致生效**
4. **最后再处理 flash mode（QIO/DIO）这种“配错就不开机”的高风险项**

关键原则：**别只改 `sdkconfig.defaults`，最终一定要看 demo5 的真实 `sdkconfig` 是否已经生效。**

---

## 1) 先区分：哪些是“已实际生效”，哪些只是 defaults 模板

对话里明确区分了两类文件的可信度：

- `sdkconfig`：当前工程 **实际生效** 的配置（你现在编译用的就是它）
- `sdkconfig.defaults`：默认模板（更像“新工程初始值”，不会自动覆盖已有 `sdkconfig`）

因此迁移时的验收必须看：

- demo5 的 `sdkconfig` 是否已经体现你要的 16MB/PSRAM/分区表

---

## 2) demo3 的“已实际生效”关键项（可作为参考目标）

参考文件：`C:/Users/ChenShuang/Desktop/esp32/demo3/sdkconfig`

### Flash 容量与频率

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`
- `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`

### 自定义分区表

- `CONFIG_PARTITION_TABLE_CUSTOM=y`
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`

### PSRAM（Octal + 80MHz + malloc 策略）

- `CONFIG_SPIRAM=y`
- `CONFIG_SPIRAM_MODE_OCT=y`
- `CONFIG_SPIRAM_SPEED_80M=y`
- `CONFIG_SPIRAM_BOOT_INIT=y`
- `CONFIG_SPIRAM_TYPE_AUTO=y`
- `CONFIG_SPIRAM_USE_MALLOC=y`
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`
- `CONFIG_SPIRAM_RODATA=y`
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`

注意：

- 对话里指出：demo3 的 `sdkconfig` 里可能看不到“显式写死 8MB”的固定容量项，更像依赖硬件/自动探测；因此“8MB”要用运行时日志/探测结果来确认，不要只靠某一条 Kconfig 推断。

---

## 3) demo5 迁移清单（按风险分级）

> 对话里把迁移分成“必改/推荐/条件必改”，这个分法很实用。

### A. 必改：Flash 容量切到 16MB

目标：消除“实际检测到 16MB，但镜像头/分区按 2MB”的长期隐患，并为更大资源分区留空间。

关键项（示意）：

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`

### B. 必改：启用 PSRAM 并切到 Octal 模式

目标：给 LVGL/资源/缓存留足 RAM 缓冲空间（尤其是后续字体/图片加大后）。

关键项参考 demo3（见上）。

### C. 必改：自定义分区表保持一致生效

关键项：

- `CONFIG_PARTITION_TABLE_CUSTOM=y`
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`

> 对话里已经踩过坑：仓库里如果已有 `sdkconfig`，真正生效的是 `sdkconfig`，不是 defaults。

### D. 推荐同步：Flash 频率 80MHz

- `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`

### E. 条件必改（高风险）：Flash mode（QIO/DIO）必须按硬件确认

对话里强调：flash mode 是“配错就可能直接不开机”的项，不能盲抄参考工程。

策略建议：

- 如果 demo5 硬件与 demo3 完全一致（例如同类 N16R8 模组），可以考虑对齐 demo3 的 QIO
- 如果硬件来源不明：先保持现有 mode，先完成 16MB + PSRAM bring-up，再单独验证 QIO

此外对话还提示了一个现实：`sdkconfig` 里可能同时存在“布尔选项”和“字符串值”不一致的情况（例如开启 AUTO_DETECT 时），因此最终还是要以**运行时行为/日志**作为验收依据。

---

## 3.1) “哪些项要双改 defaults + sdkconfig”（落地时很关键）

对话里给了一个很实用的划分：由于 demo5 仓库里已经存在 `sdkconfig`，第一次迁移时建议把“硬件基线项”双改，避免出现“defaults 看起来对，但实际仍按旧 sdkconfig 编译”的情况。

### 建议双改的（硬件基线项）

- 分区表选择：
  - `CONFIG_PARTITION_TABLE_CUSTOM=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`
- Flash 容量：
  - `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
  - `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`
- Flash 频率：
  - `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`
- Flash mode（高风险，按硬件确认）：
  - `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` 或 `...DIO=y`
  - 并确保最终 `sdkconfig` 里对应的 `CONFIG_FLASHMODE_QIO/DIO`
- PSRAM 开关/模式/速度：
  - `CONFIG_SPIRAM=y`
  - `CONFIG_SPIRAM_MODE_OCT=y`
  - `CONFIG_SPIRAM_SPEED_80M=y`
- PSRAM 分配策略：
  - `CONFIG_SPIRAM_USE_MALLOC=y`
  - `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`
  - `CONFIG_SPIRAM_RODATA=y`
  - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`
  - `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`
  - `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`

### 可以主要维护在 defaults，让 sdkconfig 通过 menuconfig 派生/生成的

对话里建议不要手工追着抄的衍生项（示例）：

- `CONFIG_PARTITION_TABLE_FILENAME`
- `CONFIG_PARTITION_TABLE_OFFSET`
- `CONFIG_SPIRAM_BOOT_INIT`
- `CONFIG_SPIRAM_TYPE_AUTO`
- `CONFIG_SPIRAM_SPEED=80`
- 各种字符串形式标准化结果

---

## 4) 最小验收标准（迁移是否成功）

- 启动日志不再出现“Detected size(16384k) larger than ... header(2048k)”这类 flash size 不匹配告警（或你能解释并确认已对齐）
- `boot: Partition Table:` 中的分区布局符合你的 `partitions.csv`
- PSRAM 在运行时被识别并初始化（并且 heap 策略符合预期）
- 在扩大 `resources` 分区/资源量后，仍能稳定启动与运行
