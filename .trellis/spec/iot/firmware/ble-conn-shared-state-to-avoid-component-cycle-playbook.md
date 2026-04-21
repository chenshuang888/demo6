# Playbook：用 `ble_conn` 共享连接状态，避免 `services` 反向依赖 `drivers`（组件循环依赖）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“control_service 要发 notify，但 `services` 不能 include `ble_driver.h`，否则 drivers↔services 组件循环依赖”的真实落地方案。
>
> 目标：为“ESP32 侧主动 Notify”提供一个可复用的连接状态获取路径，同时保持 ESP-IDF 组件依赖单向、可扩展。

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

**把 `conn_handle`/connected 状态从 `ble_driver.c` 的 static 变量抽出来，下沉到 `services/ble_conn.{h,c}`；drivers 负责 set，services 负责 get，从而避免循环依赖。**

---

## 为什么 Notify 会需要 `conn_handle`

对话里给出的核心解释：

- Write/Read（PC → ESP）是“被动应答”，回调签名里 NimBLE 会把 `conn_handle` 作为入参“白送”。
- Notify（ESP → PC）是“主动发起”，API 必须显式传 `conn_handle`，协议栈无法猜你要发给哪个连接。

因此 `control_service`（或未来任何主动推送的 service）都需要一个可靠的“当前连接句柄”来源。

---

## 组件依赖冲突的典型形态

现状通常是：

- `drivers` 组件 `REQUIRES services`（因为 `ble_driver.c` 里会调用 `weather_service_init()` 等）

如果让 `services/control_service.c` 反过来 `#include "ble_driver.h"` 取 `conn_handle`，就会形成：

- `drivers → services`
- `services → drivers`

这会导致 ESP-IDF/CMake 组件系统拒绝或架构混乱（循环依赖）。

---

## `ble_conn` 的角色（最小且可复用）

`services/ble_conn.{h,c}` 只做三件事（对话中称为“连接状态看板”）：

- `ble_conn_set(bool connected, uint16_t handle)`：由 `ble_driver` 在 GAP connect/disconnect 事件中调用
- `ble_conn_is_connected()`：供 UI/service 查询
- `ble_conn_get_handle(uint16_t *out)`：供主动 notify 的 service 获取句柄

实现要点：

- 内部用 `static volatile bool s_connected; static volatile uint16_t s_handle;`
- disconnect 时要把 `s_connected=false` 并把 handle 置为无效值（例如 `BLE_HS_CONN_HANDLE_NONE`）

---

## “为什么这次不需要 control_manager”（对话中的决策标准）

对话里给出的判断：

- manager 的价值在于跨线程数据搬运（queue）+ 防抖/去重/落盘
- control 事件是 UI 线程产生，且 `ble_gatts_notify_custom` 本身会进入 NimBLE 内部发送队列（等价于“内部已有 queue”）
- 只要保证“单写者”（只有 UI 线程调用发送函数），额外再套一层 manager 反而是空队列、增加复杂度

适用边界：

- 若将来出现多个生产者线程、需要节流/重试/排序，或发送本身变成耗时操作，再引入 manager/queue 也不迟。

