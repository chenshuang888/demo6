# Decision Record：BLE 自定义服务的 UUID 分配方案（奇偶配对）

## 背景

项目需要若干自定义 BLE GATT 服务：weather / notification / media / system（外加 ESP 侧一个标准 CTS）。每个 service 要有 128-bit UUID，每个 characteristic 也要有独立 UUID，且：

- 要能从 UUID 一眼看出"哪个 service"、"哪个 characteristic"
- 未来可能加服务或 characteristic，UUID 分配要可扩展
- 扫描器里能直观排查，不要"一堆随机 UUID 猜不到对应关系"
- 跨端（ESP C / Python bleak）必须语义一致

同时考虑 BLE base UUID 的惯例：把 16-bit 短码嵌在 128-bit base UUID 里（类似 Nordic / Dialog 的 vendor 做法）。

> 注：原先还有一个 `control_service`（短码 `0x8a5c0005/0006`）用于"屏上按钮 → PC 系统动作"。该 service 于 2026-04-21 退役，短码永久保留 RETIRED，不再复用（见下方表格和"退役 UUID"一节）。

## 选项对比

### 方案 A：每个 service 独立 base UUID

- 每个 service 一个 base，例如：
  - weather: `aaaaXXXX-...-...-...`
  - control: `bbbbXXXX-...-...-...`
- 每个 characteristic 在各自 base 下用 `0x0001, 0x0002...`

优点：
- 每个 service 内部独立编号，不冲突
- 扩容 characteristic 只需在本 service 内 +1

缺点：
- 不同 service 的 UUID 长得不像，扫描器里分辨困难
- 跨端代码要维护多套 base UUID（C 宏 + Python 常量）
- 未来加 service 还要再"随机"一个 base

### 方案 B：全局单 base + 短码奇偶配对（本项目选）

- 统一 base：`8a5c0000-0000-4aef-b87e-4fa1e0c7e0f6`
- 每个 service 占一对短码（奇数 = service UUID, 偶数 = characteristic UUID）：

  | 短码 | 角色 | 方向 | payload |
  | ---- | ---- | ---- | ------- |
  | `0x8a5c0001` | weather **service** | — | — |
  | `0x8a5c0002` | weather write **char** | PC → ESP (WRITE) | 68B packed |
  | `0x8a5c0003` | notification service | — | — |
  | `0x8a5c0004` | notification char | PC → ESP (WRITE) | 136B packed |
  | `0x8a5c0005` | ~~control service~~ **RETIRED** (2026-04-21) | — | — |
  | `0x8a5c0006` | ~~control char~~ **RETIRED** (2026-04-21) | — | — |
  | `0x8a5c0007` | media service | — | — |
  | `0x8a5c0008` | media write char | PC → ESP (WRITE) | 92B packed |
  | `0x8a5c0009` | system **service** | — | — |
  | `0x8a5c000a` | system write char | PC → ESP (WRITE) | 16B packed |
  | `0x8a5c000b` | weather **req** char（末尾追加） | ESP → PC (NOTIFY) | 1B seq |
  | `0x8a5c000c` | system **req** char（末尾追加） | ESP → PC (NOTIFY) | 1B seq |
  | `0x8a5c000d` | media **button** char（末尾追加） | ESP → PC (NOTIFY) | 4B packed (id/action/seq) |

  （标准 Current Time Service 0x1805 / 0x2A2B 沿用蓝牙 SIG 定义，不纳入本方案；time 的反向请求复用 0x2A2B 的 NOTIFY flag，不新增 UUID）

优点：
- 一份 base UUID，跨端代码只维护一套
- 扫描器里所有项目自定义服务都以 `8a5c00xx` 开头，视觉上一目了然
- 奇偶配对规则：service 短码 +1 = 对应 characteristic 短码，记忆成本低
- 扩容：下一个 service 用 `0x0009/0x000a`，继续累加
- Python / C 代码里只改一对 hex 数值，base 宏复用

缺点：
- 单 service 内多 characteristic 时，"奇偶配对"规则就变成"成对连号"——需要轻度约定
- `8a5c` 这段 magic 是项目选的，和第三方设备撞号几乎不可能但没 IANA 保障

### 方案 C：用蓝牙 SIG 的 16-bit 短 UUID（`0xFF00` 起）

优点：短

缺点：
- 16-bit 短 UUID 本质属于"vendor 预留"，严格说应走蓝牙 SIG 注册
- 对调试工具不友好（分不清"自定义"和"标准"）
- 扩展性差，和未来其他设备容易冲突

## 结论

**选择方案 B：全局 base `8a5c0000-0000-4aef-b87e-4fa1e0c7e0f6`，短码奇偶配对。**

- **选择理由**：跨端一致性高 + 视觉可读性强 + 扩展简单
- **证据**：
  - `services/weather_service.c`：weather service + characteristic UUID 宏
  - `services/notify_service.c`：notification service + characteristic UUID 宏
  - `services/media_service.c`：media service + characteristic UUID 宏（含 WRITE `0x0008` 与末尾追加的 media-button NOTIFY `0x000d`）
  - `services/system_service.c`：system service + 末尾追加的 system-req NOTIFY `0x000c`
  - README.md "BLE 协议约定" 节表格
- **边界（什么情况换方案）**：
  1. 项目要对外发布 BLE 设备并通过蓝牙 SIG 认证：可能需要申请正式 vendor ID
  2. 某 service 内 characteristic 超过 2 个（如加 config/status 读属性），规则升级为"每 service 占 4 个短码"（service + 主 char + config + status）
- **回退**：切方案 A 要改 5 套 base UUID + 跨端常量 + 文档，代价高但不破坏协议本身
- **代价**：需要在每个 `*_service.c` 用统一宏风格写 UUID，避免出现"零散 UUID 版本"

## 扩展规则（约定）

当新增 service 时：
1. 从当前最大偶数短码 + 1 作为新 service 的奇数短码
2. 紧接的偶数短码作为主 characteristic
3. 需要多个 characteristic 的 service，提前预留连续 4 / 8 个短码，避免"同一 service 的 characteristic 散落在不同号段"
4. 同步更新本 DR 表格 + README 的 BLE 协议表 + 各 `./ble-*-contract.md`

### 例外：已落地 service 事后需要追加 char（v2 反向请求重构）

某 service 初始只分配了一个 char（奇偶对），事后发现需要新增 char（如 ESP→PC 反向请求用的 notify char）。此时有两种权衡：

- **推倒重排**：把该 service 的短码整体迁到一个预留了 4 短码的段，所有已发布的 UUID 都要改。代价极高。
- **末尾追加**：在整张表的末尾拿下一个可用短码作为附加 char。优点是不动既有 UUID；缺点是"service 内 char 的短码不连号"。

当前项目采用**末尾追加**：`0x000b` 是 weather service 的第二个 char、`0x000c` 是 system service 的第二个 char、`0x000d` 是 media service 的第二个 char（按钮事件 NOTIFY）。文件顶部注释 + 本 DR 表格双重说明归属。

触发该例外的典型场景：业务从"PC 单向推数据"演进到"ESP 也能请 PC 推"或"ESP 向 PC 发按钮事件"，多出 1 个方向的 char，但原 service 的奇偶对已占满。

### 退役 UUID（RETIRED，永不复用）

| 短码 | 原角色 | 退役日期 | 原因 |
|---|---|---|---|
| `0x8a5c0005` | control service | 2026-04-21 | 锁屏/静音功能放弃，媒体键迁至 media service |
| `0x8a5c0006` | control char | 2026-04-21 | 同上 |

**规则**：RETIRED 短码在表格中保留不删除（便于审计 + 防"下一个开发者看见空位随手用上撞老固件"）。新 service 分配一律跳过这些短码，从当前未使用的最大号 + 1 继续。

## 参考

- `./ble-weather-service-payload-contract.md`、`./ble-notification-service-payload-contract.md`、`./ble-media-service-payload-contract.md`、`./ble-cts-current-time-binary-contract.md`：每个 service 的 payload 契约
- `README.md` "BLE 协议约定" 节
