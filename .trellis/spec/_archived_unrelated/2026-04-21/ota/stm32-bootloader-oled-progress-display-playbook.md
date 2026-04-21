# Playbook：在 STM32 Bootloader 上用 OLED 显示升级状态与进度（最小可用）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中对 Bootloader 工程目录与 OLED 状态显示写法的总结。
>
> 目标：Bootloader 做 OTA/搬运/校验时，用户可观测（不卡死、不会因 UI 逻辑影响升级主链路）。

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

Bootloader 的 OLED UI 只做两件事：**显示当前阶段 + 显示进度**；不要在 Bootloader UI 里引入复杂页面栈/动画/耗时逻辑。

---

## 推荐显示内容（固定模板）

- 第一行：`Bootloader`
- 第二行：阶段（Initializing / Checking / Copying / Verifying / Booting / Error）
- 第三行：百分比（例如 `50%`）
- 可选：最后一行显示错误码/重试次数

---

## 最小调用序列（示意）

1. 初始化与清屏：
   - `OLED_Init()` → `OLED_Clear()` → `OLED_Update()`
2. 显示阶段与进度（每次更新都先清掉相关区域或整屏刷新）：
   - `OLED_ShowString(...)`
   - `OLED_ShowNum(...)` + `%`
   - `OLED_Update()`

---

## 刷新频率建议

- 进度刷新不要过密：例如“进度变化才刷新”或“每 100ms 刷一次”
- 避免在搬运/写 Flash 的临界区里做 OLED 刷新（减少时序干扰）

---

## 验收标准

- 触发升级时：能看到阶段变化与进度递增
- 升级失败时：能看到明确错误阶段（至少能区分“校验失败/写入失败/找不到固件”）
- UI 不影响升级主链路（不因刷新导致超时/丢包/写入异常）

