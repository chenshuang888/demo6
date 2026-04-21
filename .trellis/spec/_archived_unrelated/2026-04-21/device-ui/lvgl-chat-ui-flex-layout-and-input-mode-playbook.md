# Playbook：LVGL 聊天页 UI（flex column）与输入模式切换（候选栏 + `lv_btnmatrix`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里对 `app_chat.c` 的端到端设计讲解：布局、模式切换、键盘事件与候选栏交互。
>
> 目标：在 240×320 这类小屏上做“能用且可扩展”的聊天 UI，不把页面写成一坨 hardcode。

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

用 **flex column** 做三段式布局（聊天区/输入栏/输入扩展区），通过切换 `HIDDEN` 让聊天区自动伸缩；键盘用 `lv_btnmatrix`，候选栏用横向容器。

---

## 推荐布局结构（flex column 自动伸缩）

> 前提：App screen 已经预留状态栏/导航栏空间（系统 UI 不由 App 管）。

- `chat_cont`（`flex_grow=1`）：聊天气泡列表，可滚动
  - user bubble（绿色）
  - ai bubble（灰色）
- `input_bar`（固定高度，flex row）
  - `input_lbl`（`flex_grow=1`）：输入框内容
  - `send_btn`（固定宽高）：发送按钮
- `pinyin_lbl`（默认隐藏）：拼音/中间态提示（可选）
- `cand_cont`（默认隐藏，固定高度，flex row）：候选字/联想词横条
- `kb_cont`（默认隐藏，固定高度）：键盘容器
  - `kb`：`lv_btnmatrix`

关键点：
- **聊天模式**：`pinyin_lbl/cand_cont/kb_cont` 全部 `HIDDEN`，`chat_cont` 吃满剩余空间
- **输入模式**：三者 `CLEAR_HIDDEN`，flex 自动把 `chat_cont` 压缩到剩余空间

---

## `lv_btnmatrix` 键盘：为何适合小屏

- 自带按键布局、事件、相对宽度控制（`ctrl_map` 低 4bit）
- 适合做：
  - 英文 QWERTY（可分页/缩排）
  - 功能键（退格/空格/发送/收起）

对话里提到的经验点：
- 空格键通常需要更宽（例如 5 倍宽），否则难按

---

## 键盘事件处理（`LV_EVENT_VALUE_CHANGED` 的最小状态机）

建议把输入分成两条“缓冲”：

- `pinyin_buf`：尚未确认的拼音（或中间态）
- `input_text`：已经确认进入输入框的内容

典型按键语义（对话版逻辑）：

- 字母键：append 到 `pinyin_buf` → 更新显示 → 发起 `/pinyin` 候选请求
- 退格：
  - `pinyin_buf` 非空：删拼音最后一个字母 → 重发 `/pinyin`
  - `pinyin_buf` 空：从 `input_text` 删除最后一个 UTF-8 字符
- 空格：
  - 有候选：默认选第 1 个候选（降低点按成本）
  - 无候选：输入框追加空格
- 发送：
  - 还有未确认拼音：先选第 1 个候选
  - 否则：退出输入模式 → 发起 `/chat`
- 收起：退出输入模式

---

## 验收标准

- 输入模式 ↔ 聊天模式切换时：
  - 页面不会抖动/错位（flex 自动伸缩）
  - 聊天区始终可滚动且能“滚到底”
- 键盘体验：
  - 退格/空格/发送可盲按（不需要很准的点）
  - 候选栏刷新稳定（不出现旧响应覆盖新输入）

