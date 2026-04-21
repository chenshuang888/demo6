# Playbook：notify_manager（10 条环形缓冲 + version 去重 + BLE→UI 入队）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“通知服务（PC WRITE 推送）+ 通知页 UI”的落地方案讨论与验证要点。
>
> 目标：在 ESP32-S3 + LVGL 项目里，用最少同步原语实现稳定的通知列表：BLE 回调不阻塞、UI 不掉帧、容量受控、验收可量化。

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

**BLE 回调只负责把 136B payload 非阻塞入队；UI 线程消费队列并写入 10 条环形缓冲，同时维护 `version`（数据变更计数器）让页面按需刷新。**

---

## 适用场景

- “只保留最新快照”不够用（天气只要最新一条；通知需要列表）。
- 容量固定：最多 10 条（满了丢最旧）。
- UI 更新频率高（例如 10ms 一帧），但通知到达频率低/突发，需要 **去重/增量刷新**。

---

## 推荐接口形态（与对话一致）

- `notify_manager_init()`
- `notify_manager_push(const notification_payload_t *n)`：BLE host 线程调用，必须非阻塞
- `notify_manager_process_pending()`：UI 线程调用，把队列中的消息写入环形缓冲
- `notify_manager_count()`
- `notify_manager_get_at(index)`：约定 `index=0` 是最新一条
- `notify_manager_clear()`：清空（若做持久化，清空应立即落盘）
- `notify_manager_version()`：返回 `uint32_t`，用于页面去重

---

## 关键实现点（设计精华）

### 1) “队列是跨线程边界”，环形缓冲是“UI 线程所有权”

- BLE 回调：只做长度校验 + `xQueueSend`（或等价） → 立即返回
- UI 线程：`process_pending()` 里循环 `xQueueReceive`，把消息写入静态数组

这样能保证：

- BLE 线程不阻塞，不会拖慢协议栈
- LVGL 对象树只有 UI 线程改动（线程安全）

### 2) `version` 去重，避免页面每帧重建 UI

对话强调的风险：UI 线程 10ms 跑一次 `update()`，如果每次都“清空 list + 重建 item”，会掉帧。

推荐：manager 内维护 `static uint32_t s_version;`

- 每当 `process_pending()` 实际写入 ring buffer 时，`s_version++`
- 页面侧缓存 `last_version`：不变则跳过 UI 更新

---

## 验收标准

- 连推 15 条：`count==10`，并且最新 10 条顺序正确（index 0 最新）。
- 快速连推（突发）：不崩溃、不死锁、BLE 不掉链。
- UI 无明显掉帧：`version` 不变时页面不重建列表。

