# Playbook：HTTP 流式文本（AI 回复）——UTF-8 边界处理 + LVGL 线程安全更新

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里 `app_chat.c` 的"流式 AI 回复"设计：后台 HTTP task 持续 append，UI 线程定时刷新，并避免 UTF-8 多字节被截断导致显示异常。

> **[本项目适配说明]**（demo6）：
> - 本项目**不使用 HTTP**（所有 PC 交互走 BLE），"HTTP streaming + cJSON" 章节不适用
> - 但核心"**后台任务 → LVGL UI 线程桥接**"思路完全适用本项目：
>   - BLE GATT 回调（NimBLE host 线程）只做 push 入队，不直接改 LVGL 对象树
>   - UI 任务在 `lv_timer_handler` 循环中消费队列（见 `services/*_manager.c` 的 `process_pending`）
>   - 与 `../guides/nimble-ui-thread-communication-contract.md` 的不变式相同
> - **UTF-8 边界处理**对 BLE payload 仍有意义：weather/notify/media 的定长字符串字段（24s/32s/48s/96s）要避免 UTF-8 字符被截断成半字节（`�`），见 `../host-tools/utf8-fixed-size-truncate-pitfall.md`
> - **复用时**：HTTP 分段接收 / `esp_http_client` 相关 API 跳过；UI 线程桥接 / UTF-8 合法边界探测 / buffer 溢出保护 这些不变式直接用

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

流式文本不要在 HTTP 回调里直接改 LVGL；用 **“后台缓冲 + UI 线程轮询/队列”**，并且在渲染前检查 **UTF-8 是否完整**（避免中文 3 字节被分片截断）。

---

## 关键约束

- **LVGL 线程安全**：UI 更新应在 LVGL 线程（或持有 LVGL mutex 的上下文）执行
- **流式分片**：网络分片可能打断一个 UTF-8 字符
- **设备内存**：响应缓冲建议放 PSRAM（对话示例为 4KB 起步）

---

## 推荐落地结构（对话版）

### 1) 进入聊天后：分配响应缓冲 + 启动 UI 轮询

- App create：
  - `s_resp_buf`（PSRAM，4KB 起步，可扩展）
  - `poll_timer`（例如 100ms）

### 2) 发送消息：先创建“AI 占位气泡”，再启动 HTTP task

发送 `chat` 时：

- 先把用户消息气泡加到列表
- 再创建 AI 占位气泡（label 初始为 `"..."`）
- 设置：
  - `s_streaming=true`
  - `s_ai_bubble_lbl=<占位 label>`
- `xTaskCreate(chat_task)`

### 3) HTTP task：只负责 append + 打标记

HTTP task 做：

- `esp_http_client` POST `/chat`
- 在接收回调里把 chunk append 到 `s_resp_buf`
- 设置：
  - `s_new_chunk=true`
- 流结束时：
  - `s_streaming=false`

注意：HTTP task **不直接** `lv_label_set_text`。

### 4) UI 线程（timer）：检查 UTF-8 完整再渲染

对话里的做法是：

- `poll_timer_cb` 每 100ms 运行一次
- 如果 `s_new_chunk && s_ai_bubble_lbl`：
  - 若 `is_utf8_complete(s_resp_buf, s_resp_len)` 为 false：跳过本轮（等待下一个 chunk 补齐）
  - 否则：
    - 更新 AI 气泡 label 文本
    - 滚动聊天容器到底部
    - 清 `s_new_chunk`
  - 若 `!s_streaming`：
    - `s_ai_bubble_lbl=NULL`（解除引用，避免后续误写）

核心目的是：**不渲染半个 UTF-8 字符**，否则中文会出现 `�` 或 label 显示错乱。

---

## `is_utf8_complete` 的最低要求

你不需要实现完整 UTF-8 解码器，最低要求是：

- 检查末尾 1～3 个字节，看最后一个 codepoint 是否被截断
- 常见情况：
  - ASCII：1 字节
  - 中文：3 字节（最常见）

如果你已经有“固定长度字段 UTF-8 安全截断”的工具/逻辑，也可以复用同一套验证思路。

---

## 常见坑

- **直接在 HTTP 回调里改 LVGL**：轻则花屏，重则随机崩溃（跨线程/时序问题）
- **不做 UTF-8 边界处理**：中文流式输出容易出 `�`，或出现“某些字符丢失”
- **缓冲过小**：长回复会截断（后续在“HTTP buf 截断 → JSON 不完整 → 白屏”坑位里单独处理）

---

## 验收标准

- AI 流式输出包含中文时：
  - 不出现 `�`
  - 不出现随机断行/乱码
- 运行 10 次对话：
  - 不崩溃、不白屏
  - UI 更新稳定（气泡能持续增长并保持滚动到底部）
