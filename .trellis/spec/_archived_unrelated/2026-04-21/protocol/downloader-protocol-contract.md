# Contract：Downloader/上传/OTA 协议契约（命令表 + 槽位 + 状态机）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中对 `downloader.*`、`protocol.js`、`serial.js`、槽位解析与 OTA 流程的整理。
>
> 目标：把“设备↔主机工具”的约定写成单一事实来源，避免后续新增页面/新增命令时改漏。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 适用范围

- 参与方：
  - MCU：负责接收/写入 flash、返回 ACK/状态、触发 UI 回调
  - 主机工具（PC/Web）：负责组织上传、分包、重试/等待 ACK、展示进度
- 场景：
  - APP 模式（8KB 槽位）
  - OTA 模式（64KB 槽位）
  - 应用商店模式（MCU 主动请求列表/详情/下载）

---

## 一句话原则

**协议必须“可恢复、可观测、可验收”：每个分包都有 ACK（含序号），状态机可描述，错误必须显式返回。**

---

## 命令表（CMD）

对话中整理出的命令定义（主机端 JS 侧）：

```js
// 基础命令
PING: 0x01

// APP模式命令（槽位大小8KB）
APP_START: 0x02
APP_DATA: 0x03
APP_END: 0x04
APP_GET_SLOTS: 0x05
APP_DELETE_SLOT: 0x06

// OTA模式命令（槽位大小64KB）
OTA_START: 0x07
OTA_DATA: 0x08
OTA_END: 0x09
OTA_GET_SLOTS: 0x0A
OTA_DELETE_SLOT: 0x0B
OTA_SET_ACTIVE: 0x0C

// 应用商店命令（单片机请求）
STORE_GET_LIST: 0x10
STORE_GET_DETAIL: 0x11
STORE_DOWNLOAD: 0x12
APP_GET_INSTALLED_IDS: 0x13
```

> 注：`STORE_*` 属于 MCU 主动请求类命令（对话中标记 `CMD_REQUEST = [0x10, 0x11, 0x12]`）。

---

## 状态机（DownloaderState）

对话中给出的 downloader 侧状态机：

```c
typedef enum {
    STATE_IDLE = 0,         // 空闲
    STATE_DOWNLOADING,      // 下载中
    STATE_WRITING,          // 写Flash中（瞬间状态）
} DownloaderState_t;
```

落地要求：

- 所有写入流程都能落到这三态中描述（避免“隐式状态”散落在多个模块）
- 写 flash 属于短暂临界区（`STATE_WRITING`），外部行为需有明确反馈（ACK/状态）

---

## 响应与错误处理约定

对话中整理的错误处理原则：

- 正常回复：`STATUS_OK (0x00)`
- 异常回复：`STATUS_ERROR (0x01)`
- **DATA 包例外**：ACK 需要附带 2 字节序列号（用于分包确认）

示例（语义示意）：

```c
SendResponse(STATUS_OK, ack, 2);   // DATA包：ACK包含序列号
SendResponse(STATUS_OK, NULL, 0);  // 其它命令：无数据
```

---

## 槽位结构（APP 模式）

> 说明：`APP_GET_SLOTS` / `OTA_GET_SLOTS` 的响应数据整体结构一致，差异在于“每槽位记录长度与字段”。

### 响应整体格式（通用）

```
[slotCount] [slot1Record] [slot2Record] ...
```

### APP 模式槽位记录（每槽位 12 字节）

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | ---- | ---- |
| 0 | 1 | `status` | `0=空, 1=有效, 2=损坏, 3=写入中` |
| 1 | 8 | `appName` | APP 短名称（8 字节，`\0` 结尾/填充） |
| 9 | 2 | `dataSize` | 小端序，APP 数据大小（字节） |
| 11 | 1 | `appId` | 应用 ID（用于商店/安装状态判断） |

主机端解析建议统一为：

```js
{
  slotId: 0,
  status: 1,
  name: "myapp1",
  dataSize: 1234,
  appId: 0
}
```

### `appId = 0xFF` 的语义（重要约定）

- 手动上传的 APP 可能会使用 `appId = 0xFF`（用于区分“非商店安装”）
- 建议：商店页刷新“已安装状态”时 **排除 `appId === 0xFF`**，避免误判

---

## 槽位结构（OTA 模式）

对话中给出的 **OTA 模式槽位数据（每个 21 字节）**：

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | ---- | ---- |
| 0 | 1 | `status` | `0=空, 1=有效, 2=损坏, 3=写入中` |
| 1 | 16 | `fwName` | 固件名称（16 字节，`\0` 结尾） |
| 17 | 4 | `fwSize` | 小端序，固件大小 |

主机端解析后建议统一为：

```js
{
  slotId: 0,
  status: 1,
  name: "v1.0.0",
  dataSize: 4096
}
```

---

## 协议交互流程（摘要）

### 版本检查流程

对话中给出的流程摘要（保留语义，不强绑定实现细节）：

- MCU → PC：请求版本检查（携带 current_version）
- PC → MCU：回复 new_version（`0` 表示无更新）
- MCU：触发页面回调（OnVersionCheckResult）

### OTA 更新流程（分包 + ACK）

对话中给出的流程摘要：

1. MCU → PC：请求开始更新
2. PC → MCU：`OTA_START`（含 size/name/ver，固定 slot）
3. MCU → PC：ACK
4. PC → MCU：循环发送 `OTA_DATA(seq,len,data)`
5. MCU → PC：ACK + `seq`，并触发进度回调
6. PC → MCU：`OTA_END`
7. MCU：完成回调（并可自动 `SetActive`）

---

## 验收标准

- 命令表在 MCU 与主机工具侧一致（新增命令必须同时更新两端）
- 分包上传/OTA：每个 `*_DATA` 都能稳定得到 ACK（包含序列号）
- 任意异常必须返回 `STATUS_ERROR`（禁止 silent failure）
- 槽位解析一致：主机 UI 展示与 MCU 存储语义一致（`status/name/size` 不混用）
