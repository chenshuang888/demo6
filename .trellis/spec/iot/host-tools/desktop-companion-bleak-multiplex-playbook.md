# Playbook：`desktop_companion.py`（一个 BleakClient 同时“订阅 control + 推送 media”）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“control_panel_client + media_publisher 两脚本无法稳定同时连接同一台 ESP32”的真实问题与最终收敛：合并为 `desktop_companion.py`，在同一个连接里完成双向闭环。
>
> 目标：把 PC 端工具做到“一个脚本就够用”，并且具备可控的联调模式（`--dry-run`）、自动重连与可观测日志。

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

**同一台 BLE peripheral（ESP32）在 bleak 层很难被两个独立进程稳定共享连接；把订阅（Notify）和推送（Write）合并到一个 `BleakClient` 是最稳的工程解。**

---

## 典型症状（对话中的现场）

- `control_panel_client.py` 单独运行 OK
- `media_publisher.py` 单独运行 OK
- 两个一起跑：扫描/连接/订阅会互相打架（或表现为不稳定/间歇失效）

对话结论：合并脚本是“本来就是同一个业务闭环”的自然做法。

---

## 运行模式（必须保留）

### 1) `--dry-run`：只打印，不触发系统动作

用途：

- 先验证 ESP→PC notify 通路（事件格式、seq 递增、订阅稳定）
- 避免 Lock/Mute 等动作影响当前操作

### 2) 正式模式：执行动作 + 推送媒体信息

用途：

- 端到端闭环体验：点按钮 → PC 执行动作 → 媒体状态变化 → 再推回屏幕

---

## 启动前检查（避免扫描/连接冲突）

对话明确提醒：如果 `ble_time_sync.py` 之类 GUI 还连着 ESP32，先断开或关闭，否则会出现扫描冲突/连接抢占。

---

## 推荐的主循环结构（高可用）

最小目标是三件事同时跑：

1) 订阅 `control_service` 的 NOTIFY characteristic（接收按钮事件）
2) 监听 Windows 媒体会话变化（SMTC），变化时推 `media_payload`
3) 自动重连 + 周期兜底推送（例如每 10 秒一次）

对话中的稳定性要点：

- **自动重连**：断线后 3 秒重试
- **去重 push**：完全相同的 payload 直接跳过，避免同一次切歌触发多个事件导致“推 7 遍”

---

## 验收标准

- 同一个脚本运行 30 分钟以上：无明显断连风暴、无内存泄漏式日志刷屏。
- 关闭播放器/无音乐：脚本仍稳定，能打印 `media session: None` 并继续等待。
- 点按钮：必定触发一条 `[exec]`（或 `[dry]`）日志，并且 seq 单调递增。

