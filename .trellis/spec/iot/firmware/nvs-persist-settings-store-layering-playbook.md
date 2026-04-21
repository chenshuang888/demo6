# Playbook：NVS 持久化分层（persist / settings_store / notify_manager 增强）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“为什么用 NVS、怎么做分层、哪些数据该持久化、如何避免 UI 掉帧、以及 REQUIRES/PRIV_REQUIRES 踩坑”的总结。
>
> 目标：把“掉电即失忆”的痛点用最小代价修掉，同时保持线程模型清晰（single-writer）与构建依赖可控。

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

**把 NVS 访问集中在 `persist`（唯一直接调用 NVS API 的模块），上层用 `settings_store` 提供业务语义；列表类数据（通知 ring）由 manager 自己做 dirty + 防抖落盘。**

---

## 什么时候 NVS 足够（什么时候才需要文件系统）

对话给出的盘点结论：

- 本项目要持久化的数据量 < 2KB（背光 1B、时间戳 8B、通知 ring ~1.36KB、未来 SSID/密码 ~70B）。
- 这类“少量 KV/小 blob”是 NVS 的主场；上文件系统（SPIFFS/LittleFS/FATFS）只会增加实体与复杂度。

文件系统更适合：

- 大量独立文件（日志/图片/音频）
- MB 级流式读写
- 用户上传下载 + 目录结构

---

## 推荐分层

对话里建议的三层：

1) `services/persist.{h,c}`

- 只做 NVS 的薄封装：`init/get/set/erase_namespace`
- 提供 `u8/i64/blob` 这类 typed API

2) `services/settings_store.{h,c}`

- 只管“业务语义”：背光、最近一次系统时间
- 内部调用 persist
- UI 事件回调可直接调用（仍在 UI 线程）

3) `services/notify_manager.c`（增强）

- `load_from_nvs()`：启动时读 blob 恢复 ring
- `dirty + tick_flush()`：运行期防抖写入（对话示例为 2 秒）

---

## 持久化策略（对话中的推荐）

| 数据 | 写入时机 | 备注 |
|---|---|---|
| 背光 | 菜单切档立即写 | 体感收益最大 |
| 系统时间 | UI 线程每 5 分钟写一次 | ESP32 无 RTC 备份电池，保存“最后一次时间”只能减轻默认值问题 |
| 通知 ring | dirty 后 2 秒防抖批量写；clear 立即写 | 降低写放大与卡顿概率 |

---

## 线程模型（必须与 single-writer 一致）

对话里的关键结论：

- **启动期（`app_main`）**：`persist_init` / `settings_store_init` / `load_last_time` / `notify_manager_init(load)`（只执行一次）
- **运行期（`ui_task`）**：`notify_manager_tick_flush`、`settings_store_tick_save_time`（周期调用）+ 背光点击回调（同 UI 线程）
- **BLE host 线程不直接碰 NVS**：只做 `xQueueSend`（入队），由 UI 线程落盘

性能风险提示（对话也提到）：

- UI 线程既画 LVGL 又写 NVS：若将来出现可见卡顿，可拆一个低优先级 `persist_task` 专门落盘，但仍要保持“单写者语义”（例如通过 `xTaskNotify` 触发）。

---

## 验收标准

- 背光调到某档位后断电重启：仍保持该档位。
- 推送通知后等待防抖落盘，再断电重启：通知仍在（且数量/顺序正确）。
- `idf.py erase-flash` 后：全部回到默认值且不崩溃。

