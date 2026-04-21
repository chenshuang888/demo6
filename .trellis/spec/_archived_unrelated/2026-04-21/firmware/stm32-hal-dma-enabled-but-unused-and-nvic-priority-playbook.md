# Playbook：STM32（HAL）DMA 使能但未使用 + NVIC 优先级分层（避免“看似能跑、边界就炸”）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-----1.md`）中对 STM32F103C8T6 工程的 DMA/中断/引脚占用分析总结。

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


## 目标

- 让工程配置“所见即所得”：启用的 HAL 模块确实在用，不浪费 flash
- 把中断优先级分层写清楚，避免主链路被 UI tick/调试串口抢占导致抖动或丢包

---

## 前置条件

- 使用 STM32 HAL（CubeMX 生成或手工维护都可）
- 工程内同时存在：串口（主链路/调试）、定时器（UI tick）、可能的 DMA

---

## 关键结论 1：DMA 使能但未使用会白占空间

对话中的典型场景：

- `HAL_DMA_MODULE_ENABLED` 已启用
- 但 SPI/I2C/USART 实际都在用 bit-bang / 轮询 / 中断，没有用 DMA

建议：

- 如果短期确定不用 DMA：在 `stm32f1xx_hal_conf.h` 中禁用 DMA 模块，以节省 flash（对话中给出的量级约 10KB）
- 如果未来要用 DMA：就把 DMA 的使用点落地（至少 USART RX），否则“启用但不用”会让维护者误判系统能力

---

## 关键结论 2：NVIC 优先级建议分层（主链路优先）

对话中的参考分层（按工程实际调整）：

- 优先级 0（最高）：主链路串口（例如 `USART3`）
- 优先级 1：调试串口（例如 `USART1`）
- 优先级 2：UI/周期性 tick（例如 `TIM3`）
- 优先级 15（最低）：`SysTick`

示例（仅示意）：

```c
HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);  // 主链路
HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);  // 调试
HAL_NVIC_SetPriority(TIM3_IRQn,  2, 0);   // UI tick
```

---

## 验证顺序

1. 在“高负载串口收发”下观察是否丢包（主链路应稳定）
2. 打开 UI tick/刷新后，确认主链路抖动不明显
3. 需要 DMA 时再逐步引入（优先 UART RX），不要一口气全改

