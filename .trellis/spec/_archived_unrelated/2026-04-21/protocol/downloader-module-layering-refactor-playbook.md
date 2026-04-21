# Playbook：Downloader 通信模块分层重构（protocol/cmd_handler + 门面兼容）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）“通信模块分层架构重构方案”“Downloader 模块重构方案”等段落。
>
> 目标：把 `downloader.c` 的“帧解析 + 业务分发 + APP/OTA 下载会话 + 回调 + 主动请求”解耦，消除重复，同时 **页面层与对外 API 不改**。

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


## 适用范围

- MCU 侧存在一个巨型 `downloader.c`（动辄 1000+ 行）
- 典型症状：
  - `APP_*` 与 `OTA_*` 的 `Start/Data/End` 逻辑高度对称且重复
  - `Downloader_ParseFrame()` 巨型 `switch-case`，混杂“长度校验/字段提取/业务分发”
  - 一处改动需要同时改多个 case，容易改漏

---

## 一句话原则

**把“帧”与“命令语义”拆开：协议层只负责“把帧变成（cmd,payload）”，命令层只负责“处理语义并调用存储/回调”。**

---

## 推荐分层与文件拆分

### 1) 协议层：`protocol.c/.h`

- 职责：
  - 帧头扫描（如 `0x7E`）
  - 基础完整性校验（长度够不够）
  - 查表获取命令负载长度（固定/可变）
  - 取出 `payload` 指针与长度并分发到 handler
  - 统一封包：
    - `Protocol_SendResponse(status, data, len)`
    - `Protocol_SendRequest(cmd, payload, len)`（MCU 主动请求场景）

### 2) 命令处理层：`cmd_handler.c/.h`

- 职责：
  - 每个命令的语义处理（Ping / GetSlots / DownloadStart/Data/End 等）
  - **统一下载会话管理**（一份代码同时服务 APP 与 OTA）
  - 回调指针管理与触发（进度/完成/版本检查等）
  - 主动请求 API（AppStore_RequestList/Detail/Download、OTA_RequestVersionCheck/Update）

当命令/业务继续增长时，可以在“命令处理层”内部再拆出更高内聚的子模块（保持协议层不变）：

- `app_store_client.c`：商店请求/回调/过滤逻辑
- `ota_update_client.c`：版本检查/请求更新/回滚策略编排
- `download_session.c`：纯传输会话（Start/Data/End）抽象，供 APP/OTA 复用

### 3) 门面头文件：`downloader.h`（对外兼容）

- **对页面层/`main.c`/`uart.c` 保持“签名完全不变”**
- 内部再 include `protocol.h`/`cmd_handler.h`
- 页面层不直接 include 内部头文件

---

## 消除重复的核心：通用下载会话 + 存储适配器

### `DownloadSession_t`（概念）

- 把 APP 与 OTA 的会话变量统一为同一结构：
  - 当前状态、期望序号、已收字节、总字节
  - 256 字节写缓冲（或等效机制）
  - 进度/完成回调

### `StorageOps_t`（概念）

- 把“写入目标不同”抽成适配器：
  - `erase(slot)`
  - `write(slot, offset, data, len)`
  - `set_meta(slot, meta)`（可选）
- APP 与 OTA 各提供一个 ops 实例（`app_storage_ops` / `ota_storage_ops`）

---

## 帧解析从 switch 到表驱动（建议）

- 定义命令分发表 `CmdEntry_t[]`：
  - `cmd`
  - `fixed_len` 或 `is_variable_len`
  - `handler(payload, len)`
- `Downloader_ParseFrame()` 只做：
  - 解析 cmd
  - 查表
  - 校验长度
  - 调 handler

对新增命令的影响：

- switch-case：新增命令需复制一坨“校验/取字段/分发”模板，易错
- 表驱动：只需在表里加一行 + 实现 handler

---

## 可变长度命令的“消耗字节数”约定（避免断帧/粘包时解析错位）

在“UART 超时断帧 + 0x7E 扫描”的实现里，`Downloader_ParseFrame(buf,len)` 往往需要在同一缓冲区内循环解析多条命令（或在数据不完整时自恢复）。

推荐约定：

- handler 接口形如 `handler(payload, remaining)`，其中：
  - `payload` 指向 `CMD` 后的负载起始
  - `remaining` 为从 `payload` 到缓冲区末尾的可用字节数
- handler 返回：
  - `>= 0`：本命令实际消耗的 **payload 字节数**（不含帧头与 CMD）
  - `-1`：数据不完整（例如只收到 header 未收到完整 data），协议层应当“滑动扫描/等待更多数据”

协议层推进策略（语义示意）：

- 成功解析：`i += 2 + consumed`（2 表示 `0x7E + CMD`）
- 不完整：`i += 1`（跳过一个字节继续找下一个 `0x7E`，避免死循环）

这能避免把“可变长度命令”（如 `*_DATA`、`STORE_GET_LIST`）强行当成固定长度而导致错位解析。

---

## TransferSession 设计注意点（从对话中抽取的易错点）

- `StorageOps_t` 建议以 **`const` 指针**方式注入：APP/OTA 各定义一个静态表，会话里只保存指针（避免复制、便于扩展）。
- `on_progress` 回调直接放在 session 结构体里：
  - `Data` 包每次落盘/累积后自动触发进度回调
  - 若两端回调签名相同（如 `void(uint32_t received, uint32_t total)`），可直接复用，无需额外包装
- `Finish` 阶段不要急着清除 `slot_id/received_size`：
  - `*_End` handler 往往需要这些字段去构造元数据并写入
  - 建议：业务层写完元数据后再把 session 状态置回 idle
- 元数据写入通常 **不适合强行统一**：
  - APP/OTA 元数据字段不同（APP 有 `app_id/app_version` 等，OTA 有 `fw_name/version/fw_size` 等）
  - 更现实的做法是“会话统一 + 元数据在各自业务层落地”

---

## 分发表实现选型（从简单到强大）

- **静态数组表驱动（最推荐作为起点）**：编译期确定、实现简单、适合命令集合相对稳定的设备侧协议。
- **运行时注册表**：适合插件化/模块可选加载（需要注册 API、去重、排序等机制）。
- **链接器段聚合（linker section）**：适合大型固件“分散定义、自动汇总”，但对工具链/链接脚本有更强依赖（STM32/Keil 环境要谨慎评估）。

---

## 风险与对策

- 风险：可变长度命令的长度校验被“固定长度表”误伤
  - 对策：对 `*_DATA` / `STORE_*` 等命令标记为可变长度，由 handler 自行从 payload 读取长度字段并做二次校验
- 风险：拆分后 include/链接关系变复杂（尤其 Keil 工程）
  - 对策：保持 `downloader.h` 作为页面层唯一入口；工程文件里只新增 `protocol.c`/`cmd_handler.c` 并移除旧 `downloader.c`

---

## 验收标准

- 页面层、`main.c`、`uart.c` **零修改**即可通过编译（公共 API 签名不变）
- APP/OTA 下载流程行为一致：
  - 分包序号校验、ACK 语义、进度回调触发点不变
- 新增命令不再触碰巨型 switch（只增表项 + handler）
