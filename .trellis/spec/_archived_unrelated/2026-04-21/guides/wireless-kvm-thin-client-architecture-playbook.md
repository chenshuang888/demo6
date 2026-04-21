# Playbook：无线 KVM 瘦客户端（ESP32-S3）— JPEG 帧流 → 解码 → LVGL Canvas → 触摸回传

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中 demo1 的实际使用方式描述：ESP32-S3 作为 WiFi 端的 KVM 客户端，从 PC 端接收 JPEG 压缩画面并显示，同时把触摸事件回传到 PC。
>
> 目标：把“端到端数据流 + 任务划分 + 关键瓶颈”说清楚，方便后续扩展（帧率/延迟/重连/协议分块），也方便把复杂度留在正确的层里。

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

**把它当成两个方向的流水线：PC→ESP（视频下行）与 ESP→PC（触摸上行）。下行走 JPEG 压缩，ESP 侧用 `esp_jpeg` 解码到 LVGL canvas；上行走触摸事件回传。**

协议格式见：`spec/iot/protocol/kvm-tcp-jpeg-frame-and-touch-contract.md`。

---

## 数据流（对话中的描述抽象）

下行（视频）：

1) PC 端采集屏幕 → JPEG 压缩
2) ESP32-S3 通过 WiFi/TCP 接收帧
3) `esp_jpeg` 软解码
4) 写入 LVGL canvas / draw buffer
5) LVGL 刷新到 LCD（ST7789）

上行（触摸）：

1) FT6336U 触摸事件进入 LVGL（或触摸驱动）
2) 将触摸坐标/手势通过 TCP 回传到 PC
3) PC 端把它映射为鼠标/触控输入

---

## 任务划分（对话中的典型做法）

对话提到的经验性拆分：

- Core 1：帧接收（网络 IO）/ 解码等重任务
- Core 0：LVGL 刷新/触摸发送（UI 主循环）

这样做的动机：

- 避免网络抖动/解码抖动拖垮 UI 刷新节奏
- 让显示与触摸的“交互响应”更稳定

---

## 依赖组件（对话中点名）

- 显示/触摸：
  - `espressif/esp_lcd_touch_ft5x06`（FT6336U 通常兼容）
- 解码：
  - `espressif/esp_jpeg`
- UI：
  - LVGL 9.x（component manager）

---

## 验收标准

- 连上 WiFi 后能持续显示来自 PC 的画面（哪怕帧率不高）
- 触摸点击能回传并在 PC 侧产生可观察的输入效果
- 断线后能重连（至少“不会崩溃”）
