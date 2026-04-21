# Playbook：用 `sdkconfig.defaults` 固化关键配置（并确保实际生效）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）关于 `sdkconfig.defaults`、分区表、PSRAM、LVGL malloc、性能开关的配置与验证。
>
> 目标：避免每次清理/换环境导致配置漂移；同时保证“defaults 写了 ≠ 实际生效了”。

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

**把“必须一致”的配置写进 `sdkconfig.defaults`，把“实际生效的配置”用运行时日志/对比方式验证出来。**

---

## 适用范围

- ESP-IDF 项目（尤其是 Windows + VSCode 扩展环境）
- 项目依赖 PSRAM、LVGL、特定分区表/Flash 配置时

---

## 关键配置清单（示例）

对话中给出的 `sdkconfig.defaults` 关键项（示意）：

- 自定义分区表：
  - `CONFIG_PARTITION_TABLE_CUSTOM=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`
- Flash 容量（例如 16MB）
- PSRAM（例如 8MB Octal 模式、速度）
- LVGL 使用 libc malloc（让大块分配可落 PSRAM）：
  - `CONFIG_LV_USE_CLIB_MALLOC=y`
- WiFi 缓冲区（按需求调）

---

## “写了 defaults 但没生效”的检查方法

对话里强调了一个现实：

- `sdkconfig.defaults` 是默认值来源
- 但最终生效的 `sdkconfig` 可能因为环境/历史缓存/工具链选择而出现差异

建议的验证方式：

1. **对比 defaults 与实际 sdkconfig 的关键值**
2. **运行时打印关键配置**
   - Flash 模式/频率
   - PSRAM 模式/频率
   - LVGL color depth / 默认字体 / OS 模式

---

## 典型故障链路：分区表没生效导致 LittleFS 打包失败

对话里给了一个非常典型、也最容易让人误判的链路（尤其在你新增 `partitions.csv` 之后）：

1. 你新增了 `partitions.csv`
2. 你希望启用自定义分区表（写了 `CONFIG_PARTITION_TABLE_CUSTOM=y` 等）
3. 但构建实际仍在用旧的 `sdkconfig`（或缓存），继续走默认分区表
4. 默认分区表里没有你新增的 `resources` 分区
5. 构建阶段要生成 LittleFS 镜像时（例如 `littlefs_create_partition_image(...)`），就会因为找不到分区而失败

排查要点：

- 先确认 **实际生效** 的 `sdkconfig` 里是否真的启用了 custom partition table
- 不要被后续的 bootloader/ninja 输出误导：那通常只是并行步骤被前面的失败拖停
- 真正核心错误通常出现在“找不到分区/打包镜像失败”的那条日志附近

### 一眼识别的证据（对话中的例子）

如果你在 `sdkconfig` 里看到类似：

- `CONFIG_PARTITION_TABLE_SINGLE_APP=y`
- `# CONFIG_PARTITION_TABLE_CUSTOM is not set`

同时构建阶段又出现了类似目标失败：

- `FAILED: CMakeFiles/littlefs_resources_bin ...`

那基本可以直接把注意力从 UI/字体代码移开，先把“分区表切换”修正掉。

### 最直接的修法（只讲方向，不强行替你执行）

目标只有一个：让 **当前实际生效** 的 `sdkconfig` 里变成：

- `CONFIG_PARTITION_TABLE_CUSTOM=y`
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`

常见落地方式（任选一种按你团队工作流）：

1. 通过 `idf.py menuconfig` 勾选 custom partition table，并保存生成新的 `sdkconfig`
2. 如果你的项目把 `sdkconfig` 也纳入版本管理：同步更新/替换它，而不是只改 defaults
3. 必要时清理构建缓存后重配（避免旧配置残留）

---

## 常见工作流建议

如果你在 Windows 下构建环境容易漂：

- 优先使用 VSCode ESP-IDF 扩展的 Build 流程（一致性更好）
- 或使用 ESP-IDF 自带的 CMD/PowerShell 环境运行 `idf.py`

---

## 关键提醒：新增/修改 defaults 后，`sdkconfig` 不会自动“吸收”新项

对话里总结出的经验是：

- `sdkconfig.defaults` 更像“生成时的默认值来源”
- **但只要仓库里已经存在 `sdkconfig`，很多新加入的关键项（PSRAM/分区表等）不会自动出现**

可选做法（按你团队规则择一）：

1) **显式 reconfigure**：跑一次使 Kconfig 重新生成（通常是最可控的方式）
2) **删除/移走旧 `sdkconfig`**：让 `idf.py` 重新生成
   - PowerShell：`Remove-Item sdkconfig`
   - 或：`rm sdkconfig`（若环境支持）

并且当分区表/大块配置改变时，对话强烈建议：

- `idf.py fullclean`（避免旧缓存让你以为“我改了但没生效”）

---

## 额外提醒：Flash 容量告警不要忽略

对话里出现过一类很典型的运行时告警（示意）：

- `spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k). Using the size in the binary image header.`

含义：实际芯片可能是 16MB，但你的工程按 2MB 在编译/生成镜像头与分区布局。  
短期不一定立刻崩（尤其当你的分区总大小还没超过 2MB 范围），但它会在你后续扩大分区/上 OTA/加资源时变成隐患。

建议：把 Flash 容量相关配置也纳入“defaults + 实际生效验证”的关键项里，保证分区表与真实硬件一致。

---

## 验收标准

- 你能稳定复现相同的 Flash/PSRAM/LVGL 配置（清理 build 后也一致）
- defaults 与实际生效值的差异可解释、可定位
