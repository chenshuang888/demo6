# Playbook：ESP → PC 主动请求模式（每业务 service 自管 NOTIFY）

## 上下文签名

- 目标平台：ESP32 + NimBLE（Peripheral 角色）
- 对端：PC / 手机（Central 角色）
- 场景：上电 / 重连 / 页面切换时，ESP 需要 PC 补推"天气 / 时间 / 系统监控"等数据
- 现有基础设施：每个业务 service（time / weather / system / ...）已有自己的 GATT characteristic

## 目标

让 ESP 能主动向 PC 发出"请给我 X 类数据"的请求，而**不需要**：
- 让 ESP 切到 Central 角色去主动读 PC
- 轮询等待 PC 定时推送
- 把触发端与响应端拆到两个不相关的 service（触发/响应跨 service 会让"这个协议到底属于谁"变得模糊）

成功标准：
- PC 端订阅业务 service 的 NOTIFY char 时，ESP 自动发一次请求，PC 在 ≤500ms 内用同 service 的 WRITE char 回写
- 页面切换时 ESP 调 `<service>_service_send_request()`，PC 行为同上
- 每个 NOTIFY 只承载 1 字节递增 seq 作为哨兵，不跨 service 复用通道

## 不变式（可直接复用）

1. **触发端与响应端同在一个 service 内**：`time_service` 自己管"请求时间同步"和"接收 CTS 写入"；`weather_service` 同理。别把请求信号塞到无关 service 的 NOTIFY 上。
2. **NOTIFY payload 固定极短**：1 字节递增 seq 足够。语义是"信号"而非"数据"；真正的数据由 PC 走同 service 的 WRITE char 回写。
3. **订阅上升沿自动触发一次**：`<svc>_on_subscribe(attr, prev, cur)` 检测 `prev==0 && cur==1`，调一次 `<svc>_send_request()`，覆盖"连上立即同步"的场景。
4. **Peripheral 用 NOTIFY，Central 用 WRITE**：方向不变，ESP 无须成为 Central。

## 参数清单（当前项目的落地）

- **NOTIFY payload**：1 字节 `uint8_t seq`（每个 service 独立 seq，wrap-around 可接受，因为 PC 端不强依赖去重）
- **UUID 分配**：业务 service 的"请求 char"按 `ble-custom-uuid-allocation-decision-record.md` 的"单 service char 数 >2 时末尾追加"规则分配：
  - `weather_service`: write `0x8a5c0002` / notify-req `0x8a5c000B`
  - `system_service`: write `0x8a5c000A` / notify-req `0x8a5c000C`
  - `time_service`: CTS 标准 char `0x2A2B` 同时 WRITE+NOTIFY（不新增 UUID，直接复用 NOTIFY flag）
- **订阅回调钩子**：每个业务 service 导出 `*_service_on_subscribe(handle, prev, cur)`，由 `drivers/ble_driver.c` 的 SUBSCRIBE 分支逐个调用

## 前置条件

- 对应业务 service 已注册（`<svc>_service_init` 返回 ESP_OK）
- `ble_conn` 能拿到当前 `conn_handle`
- PC 端分别 `start_notify` 了各业务的 notify char（不是只订阅 control）
- PC 端对每个 notify 注册一个独立 handler，在 handler 里调用对应的 push 函数

## 设计边界

- **不做**：再建一个"集中式请求 service"代理所有请求（会回到触发/响应分属两地的老坑）
- **不做**：在已退役的 control_service 里塞 REQUEST type（该 service 已下线，见"迁移说明"）
- **不做**：在 NOTIFY body 里塞大段参数（需要参数时，请求方先改 WRITE 再触发请求；NOTIFY 只做信号）
- **先做最小闭环**：单一 service 的请求跑通，再扩到 2 ~ 3 个

## 分层与职责

- **`<business>_service.c`（GATT 层，NimBLE host 线程）**：
  - GATT 表内定义 write char 和 notify char 两个 characteristic（time 例外：同 char 同时 WRITE+NOTIFY）
  - `<svc>_service_send_request(void)`：组 1B seq payload + `ble_gatts_notify_custom`
  - `<svc>_service_on_subscribe(attr, prev, cur)`：匹配本服务的 notify char val_handle + 上升沿检测 + 触发 `send_request`
- **业务层（UI 线程）**：
  - 页面 enter 时调 `<svc>_service_send_request()`（幂等；未连接时静默丢弃）
- **drivers/ble_driver.c（GAP 层）**：
  - `BLE_GAP_EVENT_SUBSCRIBE` 分支遍历所有业务 service 的 `on_subscribe` 钩子，各自判断是否是自己的 char
- **PC 端**：
  - `start_notify(<svc>_req_uuid, handler)`，每个 handler 只负责调对应 push 函数（`push_cts` / `push_weather` / `sys_pub.push_now`）
  - 不做跨 service 路由，不共享 handler

## 实施步骤（添加一个新业务 service 的反向请求）

1. **GATT 表加 notify char**：在 `<svc>_service.c` 的 `ble_gatt_svc_def` 里新增一个 `BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY` 的 characteristic，UUID 按 DR 规则分配，val_handle 存独立 static。

2. **送 request 函数**：
   ```c
   esp_err_t <svc>_service_send_request(void) {
       uint16_t conn;
       if (!ble_conn_get_handle(&conn)) return ESP_ERR_INVALID_STATE;
       uint8_t payload = ++s_req_seq;
       struct os_mbuf *om = ble_hs_mbuf_from_flat(&payload, 1);
       return ble_gatts_notify_custom(conn, s_req_val_handle, om) ? ESP_FAIL : ESP_OK;
   }
   ```

3. **订阅钩子**：
   ```c
   void <svc>_service_on_subscribe(uint16_t attr, uint8_t prev, uint8_t cur) {
       if (attr != s_req_val_handle) return;
       if (prev || !cur) return;
       <svc>_service_send_request();
   }
   ```

4. **ble_driver 挂接**：`drivers/ble_driver.c` SUBSCRIBE 分支补一行调 `<svc>_service_on_subscribe(attr, prev, cur)`。

5. **PC 端订阅**：`tools/desktop_companion.py` 的 `run_session` 里：
   ```python
   await client.start_notify(
       <SVC>_REQ_CHAR_UUID,
       make_request_handler("<name>", lambda: push_<svc>(client)),
   )
   ```

6. **页面触发点**（可选，只在"进入页面立即要数据"的语义场景用）：`page_<svc>.c` 的 enter 调 `<svc>_service_send_request()`。

## 停手规则

- 把新业务的请求塞到已有其他 service 的 notify：**停**。除非有强绑定语义（如"订阅 time 时也要推天气"），否则各管各的。
- 在 NOTIFY body 里塞业务数据：**停**。NOTIFY 只做信号；业务数据走 WRITE。
- 不加 `on_subscribe` 只在 UI 里发 request：**停**。连上但没 enter 页面就永远不会触发"初次同步"，会导致"上电后时间/天气空白"这类 bug。

## 验证顺序

1. 最小冒烟：PC 端 `start_notify(<svc>_req_uuid)` 上升沿 → ESP 日志 `svc request sent: seq=1` → PC 日志 `[req] <svc> seq=1` → WRITE char 回调数据
2. 幂等：页面反复 enter/exit，每次 send_request 日志 seq 递增、PC 端每次都能触发 push（time 无副作用，weather 进入缓存，system 覆写最后一帧）
3. 断连重连：断开时 `ble_conn_get_handle` 返回 false，日志 `not connected, drop <svc> request`；重连后首次 subscribe 上升沿自动触发
4. UUID 冲突：扫描 GATT 表确认 notify char 在 service 下的位置，确认 PC 端 `start_notify` UUID 拼写一致

## 常见问题

- REQUEST 发出但 PC 没反应 → PC 端是否 `start_notify` 了对应 char？（不是订阅了 control 就万事大吉）
- 订阅上升沿触发的请求丢失 → PC 先 `start_notify` 再 handler 绑定；或 handler 先建好再 `start_notify`
- NOTIFY 发不出去（`notify failed rc=-1`）→ MTU / `flags` 配置 / characteristic 没开 `BLE_GATT_CHR_F_NOTIFY`
- 两个 service 的 req char UUID 混淆 → 在文件顶部用注释写清楚 short code；UUID DR 表格同步维护

## 迁移说明（历史演进）

**v1（已废弃）**：集中在 `control_service` 的 NOTIFY 上用 `type=REQUEST + id=<req>`。弃用原因：
- 触发端（control）与响应端（time/weather/system write char）跨 service，维护时难定位
- 新增 REQUEST 必须同步改 control 的 enum 和 PC 端路由表，复杂度随业务线性上升
- `control_service` 的名字与实际承载的语义漂移（不再是纯"控制"）

**v2（本文档所述）**：REQUEST 拆回各业务 service 自管，触发端与响应端同属一个 service。

**v3（2026-04-21 起，当前）**：`control_service` **整体退役**（`0x8a5c0005/0006` RETIRED）。由于 lock/mute 功能已放弃、媒体键（prev/pp/next）也按同一"触发端同 service"原则迁到 `media_service` 的 NOTIFY char `0x8a5c000d`（payload = 4B `media_button_event_t`），本项目不再有任何"ESP → PC 按钮事件"走独立 control 通道。

> **推论**：本项目范围内，所有 ESP → PC 的 NOTIFY（不论是"请 PC 推数据"还是"屏上按钮触发 PC 动作"）都归属**各自业务 service**。UUID 分配规则和本 playbook 描述完全一致，唯一区别是 media-button 的 payload 为 4B 事件结构而非 1B seq。
