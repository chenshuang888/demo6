# PRD：退役 `control_service` 与 `page_control`

## 背景 / 目标

`control_service` 最初承担"ESP → PC 瞬时事件"单一通道：锁屏/静音/媒体键按钮事件，8B `control_event_t` 由 `type/id` 共 2 字段路由。

先后经历两轮演进后现状已经"不值得留"：

| 演进 | 结果 |
|---|---|
| 曾加 `type=REQUEST + id=<req>` 复用为反向请求信道（TIME/WEATHER/SYSTEM） | 上轮重构（`docs/反向请求模式重构日志.md`）已拆回各业务 service |
| 本轮决定：锁屏/静音功能**不再有实际价值**（用户判断） | `control_service` 失去存在理由 |
| 媒体键（prev/pp/next）仍需保留 | 迁移到 `media_service` 新 NOTIFY char（语义归位） |

**最终态**：BLE 5 个自定义 service 表中不再有 control_service，`8a5c0005/0006` UUID 退役；page_menu 砍"控制面板"入口；PC 端 `ControlHandler` 整体下线；媒体键走 `media_service` 自管的 NOTIFY。

## 关键决策

| 决策点 | 选择 | 理由 |
|---|---|---|
| lock/mute 功能 | **彻底放弃** | 用户明确"目前这部分没什么用" |
| 媒体键（prev/pp/next） | **保留**，迁到 `media_service` NOTIFY | 和 media 语义同圈；延续"触发端与响应端同 service"原则 |
| `media_service` NOTIFY payload | **新定义 `media_button_event_t`（4B）** 不复用即将删除的 `control_event_t` | 精简；消除对 retired service 的残留依赖 |
| 退役 UUID 处理 | **标记 RETIRED 保留在 DR 表**，不复用 | 将来不踩"UUID 意外重用"坑 |
| `tools/control_panel_client.py` | **整个文件删除** | 它是 control_service 的独立客户端，前提消失 |
| 旧 spec 归档方式 | 归档到 `.trellis/spec/_archived_unrelated/2026-04-21/protocol/`（沿用既有归档路径） | 可审计；不污染有效 spec 索引 |

## BLE 协议变更

### 新增

| UUID | Char flags | Payload | 用途 |
|---|---|---|---|
| `0x8a5c000d` | READ + NOTIFY | `media_button_event_t` (4B) | ESP → PC 媒体键事件 |

```c
typedef struct {
    uint8_t  id;       // 0=prev / 1=play_pause / 2=next
    uint8_t  action;   // 0=press (预留 release=1 / long_press=2)
    uint16_t seq;      // 单调递增，PC 端去重/丢包检测
} __attribute__((packed)) media_button_event_t;  // 4 bytes
```

### 退役

| UUID | 状态 |
|---|---|
| `0x8a5c0005` | RETIRED（control service）|
| `0x8a5c0006` | RETIRED（control char）|

## 文件改动清单

### ESP 固件：**删除**

- `app/pages/page_control.c`（289 行）
- `app/pages/page_control.h`
- `services/control_service.c`（120 行）
- `services/control_service.h`

### ESP 固件：**修改**

| 文件 | 改动 |
|---|---|
| `services/media_service.h` | 追加 `media_button_event_t` 定义 + `esp_err_t media_service_send_button(uint8_t id);` |
| `services/media_service.c` | GATT 表加第二个 char（UUID `0x8a5c000d`，READ+NOTIFY）；实现 `media_service_send_button` |
| `services/CMakeLists.txt:11` | 删 `control_service.c` |
| `app/CMakeLists.txt:11` | 删 `pages/page_control.c` |
| `app/app_main.c:8,65` | 删 `#include "pages/page_control.h"` + `page_router_register(PAGE_CONTROL, ...)` |
| `framework/page_router.h:38` | 删 `PAGE_CONTROL` 枚举成员（`PAGE_MAX` 自动收缩）|
| `app/pages/page_menu.c` | 删 `control_item` 字段 + `on_control_clicked` 回调 + bind + UI 创建处菜单项 |
| `app/pages/page_music.c:6,305` | include 换 `media_service.h`；调用换 `media_service_send_button(id)`；id 常量改 0/1/2 |
| `drivers/ble_driver.c:14,203` | 删 `#include "control_service.h"` 与 `control_service_init()` 调用 |

### PC 端：**删除**

- `tools/control_panel_client.py`（178 行，独立 lock/mute 客户端脚本）

### PC 端：**修改**

`tools/desktop_companion.py`（1050 行，改动点集中）：

- 删：`CONTROL_CHAR_UUID`、`EVENT_TYPE_BUTTON`、`BUTTON_ACTIONS` 字典、`_lock_screen()`、`ControlHandler` 类、`ctrl.on_notify` 订阅、`main()` 里构造 `ControlHandler` 的分支、`run_session(ctrl)` 参数
- 加：`MEDIA_BUTTON_CHAR_UUID = "8a5c000d-..."`、`MEDIA_KEY_ACTIONS`（三个媒体键分别调用 `winsdk.Windows.Media.Control` 或 keybd 发 VK_MEDIA_*）、`media_button_on_notify` handler、`run_session` 里 `start_notify(MEDIA_BUTTON_CHAR_UUID, media_button_on_notify)`

### Spec 文档：**归档**

搬到 `.trellis/spec/_archived_unrelated/2026-04-21/protocol/`：
- `.trellis/spec/iot/protocol/ble-control-service-event-contract.md`
- `.trellis/spec/iot/guides/ble-service-boundary-control-vs-media-decision-record.md`

### Spec 文档：**修改**

| 文件 | 改动 |
|---|---|
| `.trellis/spec/iot/protocol/ble-custom-uuid-allocation-decision-record.md` | `0x8a5c0005/0006` 条目标记 **RETIRED**（保留行便于审计）；追加 `0x8a5c000d` 条目 |
| `.trellis/spec/iot/protocol/esp-to-pc-notify-request-pattern-playbook.md` | 把"control_service 回归按钮通道"改为"control_service 已退役，所有 ESP→PC 事件归属各业务 service"；更新通道总览表 |
| `.trellis/spec/iot/protocol/index.md` | 删 `ble-control-service-event-contract.md` 引用 |
| `.trellis/spec/iot/guides/index.md` | 删 `ble-service-boundary-control-vs-media-decision-record.md` 引用 |
| `.trellis/spec/iot/firmware/ble-conn-shared-state-to-avoid-component-cycle-playbook.md` | 例子里 `control_service` 的引用换成 `weather_service` 或 `media_service`（逻辑不变，只是避免引用不存在的 service） |
| `.trellis/spec/iot/host-tools/desktop-companion-bleak-multiplex-playbook.md` | 订阅表删除 control 行；`control_panel_client.py` 存在性说明改为"已退役" |

### README

- BLE 表删 `8a5c0005/0006` 行，追加 `8a5c000d` 行
- 功能表删"控制面板"条目；媒体页说明补"支持上/下/播暂键同步 PC"

## 执行分阶段（P1 → P6）

每阶段结束都能独立 `idf.py build` 通过，可独立烧录验证。中间任何一步失败可小范围回滚。

### Phase 1 — ESP 侧加 media notify（新增，不破坏）

- `media_service.h` 加 `media_button_event_t` + `media_service_send_button` 声明
- `media_service.c` GATT 表追加 char；实现 send_button
- **验证**：`idf.py build` + 烧录后 `nrfconnect` 能扫到 `0x8a5c000d` char

### Phase 2 — PC 端对齐新通道（并行准备）

- `desktop_companion.py` 加 `MEDIA_BUTTON_CHAR_UUID` + handler + `start_notify`
- **验证**：PC 脚本启动日志显示新订阅成功

### Phase 3 — page_music 切换后端

- `page_music.c` 换 include + 换调用 + id 常量 2/3/4 → 0/1/2
- **验证**：ESP 点媒体键，PC 端依然能控媒体（从老通道迁到新通道，行为不变）

### Phase 4 — 下线 page_control

- 删 `page_control.{c,h}`
- 修 `app/CMakeLists.txt` / `app/app_main.c` / `framework/page_router.h`
- 修 `page_menu.c`（菜单砍一项）
- **验证**：`idf.py build` 通过；菜单无"控制面板"入口

### Phase 5 — 下线 control_service

- 删 `control_service.{c,h}`
- 修 `services/CMakeLists.txt` / `drivers/ble_driver.c`
- 修 `desktop_companion.py` 删 `ControlHandler`、CONTROL_CHAR_UUID、BUTTON_ACTIONS 等
- 删 `tools/control_panel_client.py`
- **验证**：`idf.py build` 通过；`git grep -n "control_service\|page_control"` 应只剩 spec 归档目录命中；PC 脚本启动订阅列表无 control

### Phase 6 — 文档与 spec 收尾

- 归档 2 个 spec 文档到 `_archived_unrelated/`
- 改 4 个 spec 文档
- 改 README
- 改 2 个 index.md
- **验证**：`.trellis/spec/` 下无引用已归档文件的断链

## 验收清单（合并）

- [ ] 主菜单无"控制面板"入口
- [ ] page_music 点 prev/pp/next → PC 媒体键生效（网易云/Spotify 切歌）
- [ ] `idf.py build` 无 warning / error
- [ ] `git grep -n "control_service"` 只在 `_archived_unrelated/` 下命中
- [ ] `git grep -n "page_control\|PAGE_CONTROL"` 零命中（或只有本 PRD 命中）
- [ ] PC `desktop_companion.py` 启动后日志显示订阅：CTS / weather-req / system-req / media-button（无 control）
- [ ] BLE 断连重连，媒体键依旧工作
- [ ] 未连接时 page_music 点按钮 WARN 不崩

## 风险 / 停手规则

| 风险 | 规避 |
|---|---|
| `page_menu.c` UI 布局是否依赖"控制面板"这一行的存在（比如硬编码 item 数量） | Phase 4 结束先跑 UI；若布局错位，回滚到"隐藏 item 但不删"先顶住 |
| `framework/page_router.h` 的 `PAGE_CONTROL` 删除后 `PAGE_MAX` 值变化，影响注册数组分配 | 只要 `page_router.c` 用 `PAGE_MAX` 作数组长度而非硬编码数字就无影响；Phase 4 编译检查 |
| PC 端 `winsdk` 媒体键 API 在部分 Windows 版本失效 | 保留 keybd `VK_MEDIA_*` fallback（当前代码已有）|
| `desktop_companion.py` 里其他地方依赖 `ControlHandler` 引用（如 `main` 参数、日志） | Phase 5 用 grep 全扫一遍 `ControlHandler` 再动手 |

**整体停手条件**：Phase 3 验证通过前不进入 Phase 4；Phase 5 删除前先把 Phase 3/4 验收确认过。

## 影响面快览（数字）

| 项 | 数 |
|---|---|
| 删除的文件 | 5（page_control.c/h + control_service.c/h + control_panel_client.py）|
| 删除代码行（净） | ~700 行 |
| 新增代码行（估） | ~80 行（media notify char + send_button + PC media_handler） |
| 修改的 ESP 源文件 | 9 |
| 修改的 PC 源文件 | 1 |
| 归档的 spec | 2 |
| 修改的 spec | 6 + 2 个 index.md + README |
| 退役的 BLE UUID | 2（`0x8a5c0005/0006`）|
| 新增的 BLE UUID | 1（`0x8a5c000d`）|

## 与上下游任务的关系

- **上游取代**：本任务取代已作废的 `04-21-media-button-channel-split`（其范围仅媒体键迁移 + page_control 瘦身；未覆盖 control_service 整体退役）
- **延续原则**：`docs/反向请求模式重构日志.md` 的"触发端与响应端同 service"原则继续贯彻
- **后续潜在**：若将来需要重新做"屏幕→PC 系统控制"（如音量条），应直接新建独立 service，不再复用 control_service 这个名字
