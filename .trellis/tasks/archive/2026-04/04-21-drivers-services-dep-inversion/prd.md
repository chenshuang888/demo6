# PRD：依赖反转 —— 消除 `drivers → services` 反常依赖 + 退役 `ble_conn` 中转层

## 背景 / 目标

当前架构里 `drivers` 组件 `REQUIRES services`（反常方向），原因是 `drivers/ble_driver.c` 需要 include 5 个 `*_service.h` 并在 `ble_driver_init()` 内部集中触发 `*_service_init()`。

这个反常依赖又反过来催生了一块"绕路"代码：

| 症状 | 根因 |
|---|---|
| `services/ble_conn.{c,h}`（34 行）| 因为 `services` 不能再反过来 `REQUIRES drivers`（否则循环），所以当 service 要主动 notify 必须借助 "drivers 写 / services 读" 的共享状态中转层 |
| `drivers/ble_driver.c` 里 `BLE_GAP_EVENT_SUBSCRIBE` 硬编码分发到 3 个 `*_service_on_subscribe()` | 同理，drivers 已经 include 了 service 头，索性直接分发 |
| 新增 service 必须同时改 `ble_driver.c`（include + service_init 调用 + 可能加 subscribe 分发） | 违背"开闭原则"，service 自管应该意味着 drivers 无感 |

**最终态**：`drivers ◀──REQUIRES── services`（依赖方向归正）。drivers 成为纯粹的 NimBLE 生命周期管理器，对业务 service 完全无感；每个 service 自管 GATT 表注册 + 订阅 SUBSCRIBE 事件；`ble_conn` 删除；`app_main` 显式编排启动时序。

## 关键决策

| 决策点 | 选择 | 理由 |
|---|---|---|
| `ble_driver_init()` 拆分 | **拆成 `ble_driver_nimble_init()` + `ble_driver_nimble_start()`** | 中间夹 service_init 窗口，GATT 表必须在 host task 启动前注册完毕 |
| 连接句柄获取路径 | **新增 `ble_driver_get_conn_handle(uint16_t *out)`**（drivers 侧暴露）| 替代 `ble_conn_get_handle`，方向归正 |
| SUBSCRIBE 事件分发 | **回调数组订阅：`ble_driver_register_subscribe_cb(cb)`** | 替代硬编码 `time/weather/system_service_on_subscribe()` 调用；未来加 service 不改 drivers |
| 回调数组容量 | **`BLE_DRIVER_MAX_SUB_CBS = 8`** | 当前只需 3（time/weather/system），预留 5 个余量；溢出返回 `ESP_ERR_NO_MEM` |
| `*_service_on_subscribe` 可见性 | **改 `static`，`.h` 删公开声明** | 清理：由 service_init 内部注册，不再对外暴露 |
| `ble_conn.{c,h}` | **彻底删除**（归档非必要，文件本就是 34 行中转层）| 依赖反转后失去存在理由 |
| 历史 spec 文档处理 | **归档 `ble-conn-shared-state-to-avoid-component-cycle-playbook.md`** 到 `_archived_unrelated/2026-04-21/firmware/`；更新 firmware/index.md 删引用 | 这条 playbook 描述的问题被根治，不再是"本项目硬规则"，沿用 retire-control-service 的归档惯例 |
| 是否写新 playbook 描述新架构 | **暂不写，留给 session record 汇总时判断**（可选产出）| 先聚焦代码正确；spec 经验若必要再写，避免过度沉淀 |

## 新旧架构对比

### 依赖方向

```
现在（反常）:                    重构后（正常）:

drivers ──REQUIRES──▶ services   drivers ◀──REQUIRES── services
   │                                 │
   │ #include "*_service.h" × 5      │ (无业务 service 引用)
   │ 调 xxx_service_init() × 5       │
   │ 硬编码 SUBSCRIBE 分发 × 3       │
   │                                 │
   └──▶ services/ble_conn (中转)     ▼
                                    drivers 暴露:
                                      get_conn_handle / register_subscribe_cb
                                      nimble_init / nimble_start
```

### 启动时序

```
现在:                                重构后:
persist_init()                       persist_init()
storage / manager init...            storage / manager init...
ble_driver_init()                    ble_driver_nimble_init()      // nimble_port_init + gap/gatt
  ├─ nimble_port_init                time_service_init()           // 注册 GATT + subscribe_cb
  ├─ ble_svc_gap/gatt_init           weather_service_init()
  ├─ 5 × service_init（内嵌）        notify_service_init()
  ├─ ble_hs_cfg + store_config       media_service_init()
  └─ nimble_port_freertos_init       system_service_init()
app_main_init()                      ble_driver_nimble_start()     // hs_cfg + store + host task
                                     app_main_init()
```

**关键时序约束**：`*_service_init()` 内部调用 `ble_gatts_add_svcs()`，必须发生在 `nimble_port_freertos_init()` 启动 host task **之前**（host task 跑起来后就会触发 sync 回调 → start_advertising，这时 GATT 表必须已经齐备）。

## 新 API 设计

```c
// drivers/ble_driver.h

/* --- NimBLE 生命周期（原 ble_driver_init 拆为两段） --- */
esp_err_t ble_driver_nimble_init(void);     // nimble_port_init + svc_gap/gatt_init + name set
esp_err_t ble_driver_nimble_start(void);    // ble_hs_cfg + store_config + nimble_port_freertos_init

/* --- 连接状态（吸收 ble_conn 的职责） --- */
bool ble_driver_is_connected(void);                   // 已有，保留
bool ble_driver_get_conn_handle(uint16_t *out);       // 新增，替代 ble_conn_get_handle

/* --- SUBSCRIBE 事件订阅（替代硬编码分发） --- */
typedef void (*ble_driver_subscribe_cb_t)(uint16_t attr_handle,
                                          uint8_t prev_notify,
                                          uint8_t cur_notify);
esp_err_t ble_driver_register_subscribe_cb(ble_driver_subscribe_cb_t cb);

/* --- 广播控制（已有，保留） --- */
esp_err_t ble_driver_start_advertising(void);
esp_err_t ble_driver_stop_advertising(void);
```

## 文件改动清单

### 新增

无新建文件。所有改动在现有文件内。

### 删除

- `services/ble_conn.c`（34 行）
- `services/ble_conn.h`（37 行）

### 修改

| 文件 | 改动 |
|---|---|
| `drivers/ble_driver.h` | 删 `ble_driver_init`；新增 `nimble_init / nimble_start / get_conn_handle / register_subscribe_cb / ble_driver_subscribe_cb_t` |
| `drivers/ble_driver.c` | 删 5 × `#include "*_service.h"` + 1 × `#include "ble_conn.h"`；init 拆两段；删除 init 里 5 × service_init 调用；删 `ble_conn_set` 调用（复用现有 `s_is_connected` / `s_conn_handle`）；SUBSCRIBE 分支改遍历 `s_sub_cbs[8]`；实现 `get_conn_handle` / `register_subscribe_cb` |
| `drivers/CMakeLists.txt` | `REQUIRES` 去掉 `services` |
| `services/CMakeLists.txt` | SRCS 去掉 `ble_conn.c`；`REQUIRES` 加 `drivers` |
| `services/time_service.h` | 删 `time_service_on_subscribe()` 声明 |
| `services/weather_service.h` | 删 `weather_service_on_subscribe()` 声明 |
| `services/system_service.h` | 删 `system_service_on_subscribe()` 声明 |
| `services/time_service.c` | include 换 `ble_conn.h` → `ble_driver.h`；`ble_conn_get_handle` → `ble_driver_get_conn_handle`；`on_subscribe` 改 `static`；`time_service_init()` 末尾加 `ble_driver_register_subscribe_cb(time_service_on_subscribe)` |
| `services/weather_service.c` | 同上（weather 对应） |
| `services/system_service.c` | 同上（system 对应） |
| `services/media_service.c` | include 换；`ble_conn_get_handle` → `ble_driver_get_conn_handle`；**不注册 subscribe_cb**（media 没有反向请求 char 的 NOTIFY 订阅语义，只有 `send_button`） |
| `services/notify_service.c` | **不改**（不用 ble_conn，不订阅 SUBSCRIBE） |
| `app/pages/page_music.c` | include 换 `ble_conn.h` → `ble_driver.h`；2 × `ble_conn_is_connected` → `ble_driver_is_connected` |
| `main/main.c` | 删 `ble_driver_init()`；新增启动序列：`nimble_init` → 5 × `service_init` → `nimble_start` |

### Spec 文档

| 动作 | 路径 |
|---|---|
| 归档 | `.trellis/spec/iot/firmware/ble-conn-shared-state-to-avoid-component-cycle-playbook.md` → `.trellis/spec/_archived_unrelated/2026-04-21/firmware/` |
| 修改 | `.trellis/spec/iot/firmware/index.md`：删上一行的引用 |

### README / docs

根目录 `README.md` 若有依赖图 / 目录说明引用 `ble_conn`，需同步更新（最后 grep 核查）。

## 执行分阶段（P1 → P5，一次性全跑完）

> 遵循 memory `feedback_batch_phases_not_per_step`：概念上分阶段，代码一次性改完。每阶段不单独编译验收，最后统一交给用户 `idf.py build` 验证。

### Phase 1 — 扩展 ble_driver API（叠加不破坏）

- 改 `drivers/ble_driver.h`：加新类型和新函数声明
- 改 `drivers/ble_driver.c`：实现 `nimble_init` / `nimble_start` / `get_conn_handle` / `register_subscribe_cb`；保留旧 `ble_driver_init` 和 SUBSCRIBE 硬编码分发（老逻辑暂留）
- **中间态**：新老 API 并存，`ble_conn` 仍在

### Phase 2 — 改调用方（切换到新 API）

- 4 × `*_service.c`：include 换 + `ble_conn_get_handle` → `ble_driver_get_conn_handle`
- 3 × `*_service.c`（time/weather/system）：`on_subscribe` 改 static + 头文件删声明 + init 末尾注册 subscribe_cb
- `app/pages/page_music.c`：include 换 + `ble_conn_is_connected` → `ble_driver_is_connected`
- **中间态**：`ble_conn` 理论上无人调用（但函数体还在）

### Phase 3 — 清理 ble_driver.c

- 删 5 × service include + 1 × `ble_conn.h` include
- 删 `ble_driver_init()` 函数（已被拆）
- 删 `ble_conn_set()` 调用（GAP 事件直接写 `s_is_connected` / `s_conn_handle`，逻辑已有）
- 删 SUBSCRIBE 硬编码分发（改遍历 `s_sub_cbs[]`）
- **中间态**：drivers 不再 include 任何 service；ble_conn.c/h 文件还在但无人调

### Phase 4 — 改 main.c 编排启动时序

- `ble_driver_init()` 单行调用 → 7 行新序列：`nimble_init` → 5 × `service_init` → `nimble_start`
- **中间态**：启动顺序正确

### Phase 5 — 删除文件 + 改 CMakeLists + 归档 spec

- `rm services/ble_conn.c services/ble_conn.h`
- `drivers/CMakeLists.txt` REQUIRES 去掉 `services`
- `services/CMakeLists.txt` SRCS 去掉 `ble_conn.c`；REQUIRES 加 `drivers`
- 归档 `ble-conn-shared-state-to-avoid-component-cycle-playbook.md` 到 `_archived_unrelated/2026-04-21/firmware/`
- 更新 `firmware/index.md` 删引用
- **最终态**：依赖方向翻转完成

## 验收清单

### 编译验收（用户 `idf.py build` 跑）

- [ ] `idf.py build` 无 error / warning
- [ ] 首次启动成功，BLE 广播名为 `ESP32-S3-DEMO`

### 功能回归（用户烧录后验证）

- [ ] PC 端 `desktop_companion.py` 能连上 ESP
- [ ] CTS（时间同步）：PC 连上后 ESP 能 push time-request，PC 回写时间生效
- [ ] Weather：page_weather 能展示 PC 推的天气数据
- [ ] System：page_system_monitor 能展示 PC 推的 CPU/MEM 等
- [ ] Notify：PC push 通知 ESP 弹出 banner
- [ ] Media：page_music 点 prev/pp/next → PC 媒体键生效

### Grep 兜底（改完后验证）

| 检查 | 期望 |
|---|---|
| `git grep -n "ble_conn" -- app services drivers main` | 0 命中（spec 归档目录可能有历史引用，不计）|
| `git grep -n "ble_conn_get_handle\|ble_conn_set\|ble_conn_is_connected"` | 只在 spec 归档文件命中 |
| `git grep -n "#include \"time_service\\|weather_service\\|notify_service\\|media_service\\|system_service" drivers/` | 0 命中 |
| `git grep -n "ble_driver_init\b"` | 0 命中（被拆）|
| `ls services/ble_conn.*` | 不存在 |
| `grep REQUIRES drivers/CMakeLists.txt` | 不含 `services` |
| `grep REQUIRES services/CMakeLists.txt` | 含 `drivers` |

## 风险 / 停手规则

| 风险 | 对策 |
|---|---|
| **NimBLE 时序**：`ble_gatts_add_svcs` 必须在 `nimble_port_run` 之前；若编排错了，sync → adv 可能在 GATT 表不完整时启动 | main.c 严格按 `nimble_init → service_init × 5 → nimble_start` 顺序；`nimble_start` 内部才调 `nimble_port_freertos_init` |
| **subscribe_cb 数组写保护**：`register_subscribe_cb` 若被多线程调用（虽然实际只有 main 线程调）可能撕裂 | 文档约束：仅 `nimble_init` 到 `nimble_start` 之间的 init 阶段允许调用；运行时不再注册 |
| **`ble_hs_cfg.sync_cb` 时机**：`on_stack_sync` 内部调用 `ble_driver_start_advertising()`，若 hs_cfg 在 nimble_start 才设置，是否会错过 sync？ | `nimble_port_freertos_init` 是启动 host task，host task 跑起来后才触发 sync；hs_cfg 在启动 host task 之前设置即可——顺序对的 |
| **`s_is_connected` / `s_conn_handle` 跨线程读写**：原 `ble_conn` 用 `volatile` 保证 UI 读不被优化；复用 drivers 自己的 `s_is_connected`（已 `volatile`）+ `s_conn_handle`（未 volatile）| `s_conn_handle` 改成 `volatile uint16_t`；符合 `nimble-ui-thread-communication-contract.md` 的"单写多读标量" |
| **spec 归档路径下游断链**：`firmware/index.md` 和可能的其他 index 引用这条 playbook | 改完后 `grep -r "ble-conn-shared-state" .trellis/spec/iot/` 核查 |

**整体停手条件**：如果重构后 `idf.py build` 失败，逐 Phase 回退排查（Phase 5 → 4 → 3 → 2 → 1）；Phase 1/2 相对独立，Phase 3/4/5 是强绑定三阶段，无法部分回退。

## 影响面快览

| 项 | 数 |
|---|---|
| 删除的文件 | 2（`ble_conn.c/h`）|
| 删除代码行（净） | ~80 行（ble_conn 34 + 46 + SUBSCRIBE 硬编码分发 + ble_driver_init）|
| 新增代码行（估） | ~90 行（subscribe_cb 数组 + register 函数 + nimble_init/start 拆分 + get_conn_handle）|
| 修改的源文件 | 11（drivers × 2，services × 7，app × 1，main × 1）|
| 修改的 CMakeLists | 2（drivers + services）|
| 归档的 spec | 1 |
| 修改的 spec index | 1 |

## 与上下游任务的关系

- **上游前置（已完成）**：`04-21-retire-control-service` 退役了 control_service，简化了 drivers 里的 service init 列表（从 6 个变 5 个）
- **与本轮协同完成的外围重构（已完成，未 commit）**：
  - `services/manager/` 子目录（5 个 `*_manager` 搬迁）
  - `storage/` 独立 component（`persist` + `backlight_storage` + `time_storage` + `notify_storage`）
  - 本轮是重构三部曲的最后一轮
- **延续原则**：`nimble-ui-thread-communication-contract.md` 的"BLE 回调不阻塞、UI 单写"继续贯彻（本次不改线程模型）
- **后续潜在**：若未来加新 BLE service，只需 (1) 写 `*_service.c/h`；(2) 在 `main.c` 的启动序列里加一行 `xxx_service_init()`；drivers 代码零改动
