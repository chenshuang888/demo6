# Playbook：STM32 上传工具（Web）前后端分离重构（Node.js + serialport）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）“STM32 上传工具 -- 前后端分离重构方案”。
>
> 目标：把浏览器侧“串口读写 + 帧解析 + 业务状态机”迁移到 Node.js 后端，让前端只负责渲染与发起动作，降低共享脚本耦合与页面崩溃风险。

---

## 上下文签名（Context Signature，必填）

- 目标：把“串口读写 + 协议解析 + 业务状态机”从浏览器侧迁移到 Node 后端，前端只做渲染与触发动作
- 参与方：前端页面（Web/Electron）+ Node 后端（serialport）+ MCU（协议端）
- 风险等级：中到高（并发/重入会导致协议错配、waitResponse 被误触发、上传/OTA 卡死）
- 依赖：需要明确协议契约（命令表、ACK/seq、帧格式、超时重试）并保持单一事实来源

---

## 不变式（可直接复用）

- 串口会话必须串行化：一个连接、一条解析循环、一个 response 等待器（避免前端并发打乱协议）
- 协议栈分层：schema/codec/transport/protocol 分层能显著降低“拼帧重复/并发错配”
- 事件可观测：日志/进度/状态通过 WS 推送，前端不应依赖隐式全局变量

---

## 参数清单（必须由当前项目提供/确认）

- 协议：命令表、ACK 语义（是否带 seq）、超时/重试策略、最大帧/最大 payload
- 串口：波特率、端口选择策略、断连重连策略
- 并发边界：哪些请求允许并行（通常不允许），如何队列化
- 文件大小与槽位：APP/OTA slot 大小、上传限制（`limits.fileSize`）与设备端一致

---

## 可替换点（示例映射，不得照搬）

- `tools/server/server.js|serial.js|protocol.js|config.js` 是一种最小结构示例；你的工程可用不同目录名/模块名，但应保留相同角色边界
- REST/WS 的路由与事件名（`/api/...`、`type` 字段）属于接口设计示例，需要按你的产品/UI/鉴权要求调整

---

## 停手规则（Stop Rules）

- 你还没把协议契约写清楚（命令表/帧格式/ACK(seq)/超时重试）就开始写后端业务编排
- 你没有“单会话串行化”的机制（队列/互斥/单飞行），禁止引入多个上传/OTA 并发入口
- 无法观测到“请求→响应配对链路”（日志/trace/seq），禁止继续加复杂业务（先补可观测性）

---

## 推荐最小后端结构（奥卡姆剃刀）

```
tools/
  server/
    server.js    # Express + WebSocket + 静态文件服务 + REST 路由
    serial.js    # 串口层：serialport 打开/关闭/读写 + RX 缓冲 + 超时断帧
    protocol.js  # 协议层：封包/解包 + waitResponse + 业务处理（upload/slots/store/ota）
    config.js    # 常量/路径/配置（波特率、目录等）
```

原则：

- 前端不再直接操作串口与协议细节（减少 DOM/全局函数耦合）
- 后端维护“单一串口会话 + 单一解析循环 + 单一响应等待器”，避免前端并发把协议打乱

---

## REST API（前端主动操作）

统一返回：

```json
{ "success": true, "data": {...} }
{ "success": false, "error": "错误描述" }
```

### 串口管理

- `GET /api/serial/ports`：列举串口
- `POST /api/serial/connect`：`{ port: "COM3" }` 打开串口 + PING 测试
- `POST /api/serial/disconnect`：关闭串口
- `GET /api/serial/status`：查询 `{ connected, port }`

### APP 管理

- `POST /api/app/upload`：`multipart/form-data`（`file` bin + `slot`）
- `GET /api/app/slots`：返回 `[{ slotId, status, name, dataSize, appId }, ...]`
- `DELETE /api/app/slots/:slotId`：删除槽位

### OTA 管理

- `POST /api/ota/upload`：`multipart/form-data`（`file` bin + `version`）；实现上可固定 slot
- `GET /api/ota/slots`：返回 `{ slots: [...], activeSlot }`
- `DELETE /api/ota/slots/:slotId`
- `PUT /api/ota/active/:slotId`：设置/取消激活（可约定 `0xFF` 为取消）
- `POST /api/ota/scan`：扫描固件目录，返回 `{ name, version, size }` 或 `null`

### APP 商店（仅给前端列表展示）

- `GET /api/store/apps`：读取本地 `apps.json`（`binFile` 存文件名，后端用 `path.join` 拼路径）

> 说明：MCU 主动请求（`STORE_GET_LIST/DETAIL/DOWNLOAD`）不走 REST，而由后端协议解析后直接处理。

---

## WebSocket（后端推送事件）

统一格式：

```json
{ "type": "xxx", "data": { ... } }
```

常见 type（示例）：

- `log`：`{ message, level }`
- `progress`：`{ percent, current, total }`
- `status`：`{ text }`
- `connected` / `disconnected`
- `upload_complete` / `upload_failed`
- `slots_updated`：`{ mode, slots }`
- `mcu_request`：`{ cmd, detail }`
- `ota_scan_result`
- `serial_rx`（调试用）

---

## 接口形态选型：REST + WS vs WS-only

对话中还出现过一种“全 WebSocket”方案：前端用 `type: namespace:action` 的消息发起请求，后端用事件推送回包，并可选用 `requestId` 做请求-响应配对。

两种方案各自适用：

- **REST + WS（本文件默认）**
  - 优点：文件上传天然适配（`multipart/form-data`），语义清晰
  - 缺点：需要维护两套通道（REST 触发 + WS 推送）
- **WS-only**
  - 优点：统一单通道；请求/响应/事件都走 WS
  - 风险：若把 bin 直接 base64 放在 WS payload，会带来体积膨胀与内存峰值（尤其多并发时）

推荐：如果必须通过 WS 传文件，严格限制文件大小（例如 64KB）并做单飞行/队列化。

---

## 复杂度上升后的模块拆分（可选）

当功能从“手动上传”增长到“商店/OTA/扫描/回滚”等时，建议在保持边界清晰的前提下进一步拆：

- `serial_manager.js`：串口生命周期、收包缓冲、断帧与基础发送
- `protocol.js`：编解码与 `waitResponse()`
- `upload_service.js`：统一上传流程（用 `mode=app|ota` 复用一套 Start/Data/End 状态机）
- `ota_service.js` / `store_service.js`：把“业务编排”从 protocol 中拆出去，避免 `protocol.js` 变成第二个巨型文件

验收点：上传流程不应在“手动上传/商店下载/OTA 自动更新”里复制三份。

---

## 关键坑：MCU 请求处理中的“嵌套通信”

某些 MCU 主动请求的 handler 会在处理过程中再次向 MCU 发命令并等待响应（例如：
先查询已安装 ID → 再返回过滤后的商店列表）。

要点：

- 依赖“请求命令字节”和“响应命令字节”可区分（例如响应用 `0x00/0x01`）
- `waitResponse()` 必须只对“响应类帧”置位，不被 `STORE_*` 请求帧误触发
- 推荐把串口写入串行化（队列/互斥），避免并发发送造成响应错配

---

## serialport 迁移要点（WebSerial → Node serialport）

- 读取模型从“reader while 循环”变为 **事件驱动**：
  - `port.on('data', chunk => rxBuffer.push(...chunk))`
- 仍然需要“解析/断帧”定时任务：
  - 例如 `setInterval(checkFrameTimeout, 10)` + 50ms 帧超时（按原协议约定）

---

## 文件上传建议（Node.js）

- `multer.memoryStorage()` 直接存内存，避免落盘复杂度
- 设置 `limits.fileSize`（例如 64KB），与设备侧槽位/协议上限保持一致

---

## 验收标准

- 前端页面不包含任何串口/协议状态机代码，仅发 REST 请求 + 接收 WS 推送
- MCU 主动请求（商店/OTA 检查）不会卡死 `waitResponse()`（嵌套通信可闭环）
- 后端串口读写与解析稳定：断连、异常可观测并能恢复
