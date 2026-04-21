# Playbook：Web 串口工具新增页面接入清单（以 store.html 为例）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）“store.html 完整实施计划”与边界情况总结。
>
> 目标：新增页面时不再踩“共享脚本初始化即崩”“脚本缺依赖”“安装状态判断错误”等坑。

---

## 适用范围

- 一个 `tools/` 下存在多页面（如 uploader、store 等），并复用共享脚本：
  - `config.js`：常量 / `appLibrary` 等数据源
  - `ui.js`：日志与 UI 公共函数
  - `protocol.js`：协议封包/解包、`waitResponse()`、`parseSlotsData()` 等
  - `serial.js`：WebSerial 管理、读写循环、连接按钮绑定等
  - `handlers.js`：MCU 主动请求处理（商店列表/详情/下载等）

---

## 页面接入清单（从“必崩点”开始排）

### 1) 共享脚本的 DOM/元素契约

- 若加载 `serial.js`：
  - 必须提供 `#portSelect`（且在引入 `serial.js` 前已出现在 DOM 中）
  - 必须提供 `#connectBtn`
- `ui.js` 的 `log()`：
  - 页面可不提供 `#logContainer`，但 **必须确保 `log()` 做了容错**（不存在则 fallback 到 `console.log`）
  - 参见：`spec/iot/host-tools/shared-js-dom-dependency-pitfall.md`

### 2) 共享脚本的“符号/模块依赖”契约

- `protocol.js` 的 `parseUnifiedData()` 可能会调用 `handlers.js` 里的 `handleStoreGetList/Detail/Download`
- **结论：页面必须加载 `handlers.js`**（或对 `protocol.js` 做 `typeof` 容错）
  - 参见：`spec/iot/host-tools/shared-js-implicit-global-deps-pitfall.md`

### 3) 连接/断开 hook（页面侧必须掌控 UI 状态）

- 页面通常需要：
  - 自己的“连接状态显示”
  - 自己的“禁用按钮/防重复点击”逻辑
- 建议：页面侧提供 `isBusy` 标志位，并在关键操作前检查：
  - `port && port.readable`（避免 USB 拔出后继续流程）

---

## 安装/卸载流程要点（协议不变，避免业务误判）

### 1) 槽位已满（必须显式处理）

- 安装前通过 `APP_GET_SLOTS` + `parseSlotsData(data, 'app')` 查询空闲槽位
- 若全部 `status !== 0`：提示“设备存储已满，请先卸载一些应用”

### 2) 超时/断连/资源缺失的边界

- `fetch()` 本地 bin 失败（本地服务器未启动/文件不存在）：try-catch 并提示
- `waitResponse()` 超时：必须恢复 UI 状态（按钮、进度条、busy 标志位）
- 传输中断连：读写循环会抛错，页面侧要能中止当前操作并提示

---

## 数据语义坑位（高频误判点）

### 1) `appLibrary.name` 可能含尾随空格

- 来自“MCU 端固定长度字段”的遗留表现
- 展示层优先用 `fullName`（而不是 `name`）

### 2) `APP_GET_SLOTS` 的 `appId` 字段与 0xFF 约定

- 在 `parseSlotsData(..., 'app')` 结果中，每个槽位可带 `appId`
- **手动上传的 APP** 可能使用 `appId = 0xFF`
- 建议：刷新“已安装状态”时 **排除 `appId === 0xFF`**，避免把“手动上传”误当成“商店安装”

---

## 验收标准（最小集合）

- 新页面加载后控制台无异常（尤其是 `null.addEventListener` 与 `ReferenceError`）
- 断连/超时/重复点击不会把 UI 卡死（busy 能恢复）
- 槽位满时有可理解提示，不会继续发送下载
- 已安装状态判断不受 `0xFF` 槽位污染

