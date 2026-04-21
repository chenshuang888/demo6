# Playbook：主机端 AI 工具函数（SSE 流式转发 + JSON 数组提取 + 标点归一化）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里 `server.py` 的关键实现细节：把 AI 的 SSE 流式输出转成设备端易消费的 `text/plain` 分片，并用通用 helper 稳定提取 `["..."]` 这类 JSON 数组结果。

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

主机端要做“协议适配层”：

- 上游（AI API）：SSE/JSON/可能带多余解释文本
- 下游（ESP32）：**尽量简单**（`text/plain` 分片、或纯 JSON 数组）

---

## 1) `/chat`：SSE 流式转发为 `text/plain`

对话里的推荐思路：

1. ESP32 `POST /chat {"message":"你好"}`
2. `server.py` 调 AI API（`stream=True`）
3. 逐行读取 SSE：
   - `data: {"choices":[{"delta":{"content":"你"}}]}`
   - `data: {"choices":[{"delta":{"content":"好"}}]}`
   - `data: [DONE]`
4. 每次拿到 `delta.content`：
   - 做一次 **标点归一化**（见下节）
   - 直接 `yield` 给 ESP32（Flask `Response(generate(), content_type='text/plain')`）

下游收益：

- ESP32 不需要理解 SSE/JSON
- 直接把文本 chunk append 到 label，就能得到“打字机效果”

---

## 2) PUNCT_MAP：标点归一化（为设备字体/显示服务）

对话里提到一个非常实用的细节：

- 上游可能输出全角标点：`"！"`、`"，"`、`"。"`
- 设备端字体/符号集可能更适配半角：`! , .`

因此可以在主机端做一个 `PUNCT_MAP`：

- `！` → `!`
- `，` → `,`
- `。` → `.`
- （按你的字体/符号集再补充）

原则：**显示端承受不了的字符，尽量在主机端归一化**，把协议层保持简单。

---

## 3) `ai_query_json_array`：从 AI 文本里稳定提取 `[...]`

场景：

- `/suggest` 或拼音缩写兜底时，让 AI 返回 JSON 数组
- 但 AI 有时会在数组前后加解释文字

对话中的工程化做法：

- 让 AI 返回“尽量只有数组”
- 仍然准备一个兜底提取：
  - `re.search(r'\\[.*?\\]', content)` 抓第一段 `[...]`
  - 再 `json.loads()` 得到列表
  - 失败则返回 `[]`

建议：

- `temperature` 低一些（例如 0.3 或更低），输出更稳定
- `timeout` 要有上限（例如 3–5s），避免卡住设备交互

---

## 验收标准

- `/chat`：
  - ESP32 端能稳定逐段显示（无需解析 SSE）
  - 标点显示稳定（不出现“设备字体缺符号导致方块/乱码”）
- `ai_query_json_array`：
  - 大部分情况下直接 `json.loads` 成功
  - 少量带解释文本时也能提取到数组
  - 超时/失败可控地返回 `[]`

