# Playbook：16MB Flash 的分区表规划（`factory` 3MB + `resources` 4MB + `storage`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中围绕“resources 分区给 4MB”“不要留未定义空洞”“重排分区后旧 LittleFS 内容会失效”“resources label 不要改名”的一整段讨论。
>
> 目标：给出一份清晰、可扩展、可解释的 `partitions.csv` 规划模板，并明确哪些约束是“代码契约”不能随手改。

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

对话里最推荐、最清晰的布局是：

- `factory` 给足（3MB）保证后续 app 体积增长空间
- `resources`（LittleFS）直接给 4MB 满足字体/图片资源诉求
- 剩余空间显式给 `storage`（FAT）而不是留一大段未定义空洞

---

## 推荐布局（示例）

对话中给出的示例（重点看容量分配思路）：

```csv
# Name,      Type, SubType,  Offset,    Size,      Flags
nvs,         data, nvs,      0x9000,    0x6000,
phy_init,    data, phy,      0xf000,    0x1000,
factory,     app,  factory,  0x10000,   0x300000,
resources,   data, littlefs, 0x310000,  0x400000,
storage,     data, fat,      0x710000,  0x8F0000,
```

解释要点：

- `factory=3MB`：参考“同类工程”的稳妥上限；UI/LVGL/网络等后续会涨体积
- `resources=4MB`：满足字体（尤其中文）+ 图标 + 图片的增长空间
- `storage`：为“可变数据”留落点（缓存、下载资源、日志、动态包等）

---

## 如果你只想绝对最小改动（不推荐但可跑）

对话里也给了一个“只满足 resources=4MB，但留空洞”的最小版本：

```csv
# Name,      Type, SubType,  Offset,    Size,      Flags
nvs,         data, nvs,      0x9000,    0x6000,
phy_init,    data, phy,      0xf000,    0x1000,
factory,     app,  factory,  0x10000,   0x300000,
resources,   data, littlefs, 0x310000,  0x400000,
```

问题：

- 后面会留下一大段未定义空间（能跑但不清晰）
- 一旦你后续需要“下载/缓存/日志/动态资源”，还是要回来补 `storage`

---

## 不能随手改的约束（契约）

### 1) `resources` 分区 label 不要改名

对话里强调：label 被这些地方“契约化”依赖：

- 构建打包：根 `CMakeLists.txt` 的 `littlefs_create_partition_image(...)`
- 挂载：`app/resources_fs.c` 的 `partition_label`

如果你改了 label，必须同步修改这些地方；否则典型症状是：镜像生成失败、或运行时挂载失败。

### 2) 重排分区后，旧 LittleFS 内容要视为失效

`resources` 是文件系统分区：

- 一旦你修改分区的 offset/size，本质上就是“底层地址变了”
- 旧数据不能指望原地保留：必须按新分区重新生成并重新烧录资源镜像

---

## 验收标准

- 启动日志 `boot: Partition Table:` 中能看到 `resources`（且 offset/size 与预期一致）
- `resources_fs` 能稳定挂载到 `/res`
- 资源镜像生成目标（例如 `littlefs_resources_bin`）不再报“找不到分区”
- 扩大资源后仍能稳定启动（Flash size / header / 分区布局一致）

