# Playbook：ESP → PC 主动请求模式（复用 Control Service 的 NOTIFY + REQUEST id）

## 上下文签名

- 目标平台：ESP32 + NimBLE（Peripheral 角色）
- 对端：PC / 手机（Central 角色）
- 场景：上电 / 重连 / 页面切换时，ESP 需要 PC 补推"天气 / 时间 / 正在播放"等数据
- 现有基础设施：已有 `control_service`（ESP → PC NOTIFY 通道，承载按钮事件）

## 目标

让 ESP 能主动向 PC 发出"请给我 X 类数据"的请求，而**不需要**：
- 引入新的 GATT service / characteristic
- 让 ESP 切到 Central 角色去主动读 PC
- 轮询等待 PC 定时推送

成功标准：
- 订阅 `control_service` 后 ESP 自动发一次 `REQUEST_TIME_SYNC`，PC 在 ≤500ms 内回写时间
- 切到天气页时 ESP 发 `REQUEST_WEATHER`，PC 回写天气
- 所有 REQUEST 共享一个 NOTIFY characteristic，通过 `type = REQUEST` + `id = <请求类型>` 区分

## 不变式（可直接复用）

1. **Peripheral 用 NOTIFY，Central 用 WRITE**：一条既有的 NOTIFY characteristic 就足够表达"请求"
2. **type + id 多路复用**：同一 characteristic 的 payload 里用 `type` 字段区分不同语义（BUTTON / REQUEST / 未来更多）
3. **请求带 seq**：每次 REQUEST 递增 `seq`，PC 回写时可选择性带回 `req_seq` 做去重/对账
4. **响应走既有 service**：weather REQUEST → PC 用 weather characteristic WRITE；time REQUEST → PC 用 CTS WRITE；**不要**把响应塞回 control NOTIFY

## 参数清单（必须由当前项目提供）

- **NOTIFY payload 结构**：本项目 `control_event_t` 8 字节 `<BBBBhH>`（type / id / action / reserved / value / seq）
- **REQUEST id 枚举表**：`CONTROL_REQUEST_TIME_SYNC=1` / `CONTROL_REQUEST_WEATHER=2` / `CONTROL_REQUEST_MEDIA=3` / ...（按项目扩充）
- **响应超时**：业务侧决定（本项目不设硬超时，页面下次进入再重发）
- **订阅回调钩子**：`control_service_on_subscribe(handle, prev, cur)` 在订阅上升沿触发

## 前置条件

- `control_service` 已注册（`control_service_init` 返回 ESP_OK）
- `ble_conn` 能拿到当前 `conn_handle`（`ble_conn_get_handle` 返回 true）
- PC 端订阅了 `control_service` 的 NOTIFY characteristic
- PC 端能解析 `type = REQUEST` 分支并用对应的 characteristic 回写

## 设计边界

- **不做**：为每种 REQUEST 单独建 GATT characteristic（会炸 MTU / 增加 discovery 成本）
- **不做**：ESP 切 Central 角色去 read PC（BLE 4.2 Peripheral 做不到，也没必要）
- **不做**：在 NOTIFY 里塞长字符串（payload 固定 8 字节，需要更多数据时拆多次 REQUEST）
- **先做最小闭环**：TIME_SYNC 单个 REQUEST 跑通，再扩展 WEATHER / MEDIA

## 可替换点（示例映射）

- `control_event_t` 8 字节结构可替换为任何带 `type/id/seq` 的定长结构
- REQUEST id 枚举值由项目自定义（本项目用 1/2/3；其他项目可用 UUID 前缀 / magic number）
- 订阅回调的"自动触发首次 REQUEST"逻辑可按需开关

## 分层与职责

- **`control_service.c`（GATT 层，NimBLE host 线程）**：
  - 提供 `control_service_send_request(req_id)`：组 `control_event_t` + `ble_gatts_notify_custom`
  - `control_service_on_subscribe(...)` 在上升沿自动调 `send_request(TIME_SYNC)`
- **业务层（UI 线程 / 各 manager）**：
  - 页面 enter 时调 `control_service_send_request(<对应 id>)`
  - 各 service（weather / time / notify / media）保持现有接收逻辑不变，不感知 REQUEST 机制
- **PC 端**：
  - 订阅 control NOTIFY，解析 payload
  - 若 `type == REQUEST`，按 `id` 用对应 service 的 write characteristic 回写数据
  - 可选：带上 `req_seq` 做去重

## 实施步骤

1. **定义 REQUEST 枚举**（`services/control_service.h`）：

   ```c
   typedef enum {
       CONTROL_EVENT_TYPE_BUTTON  = 0,
       CONTROL_EVENT_TYPE_REQUEST = 1,
   } control_event_type_t;

   typedef enum {
       CONTROL_REQUEST_TIME_SYNC = 1,
       CONTROL_REQUEST_WEATHER   = 2,
       CONTROL_REQUEST_MEDIA     = 3,
   } control_request_id_t;
   ```

2. **扩展 `control_service`**（参考本项目 `services/control_service.c:122-153`）：

   ```c
   esp_err_t control_service_send_request(uint8_t req_id);
   void      control_service_on_subscribe(uint16_t attr, uint8_t prev, uint8_t cur);
   ```

   实现里复用 `control_event_t` struct，只改 `type = CONTROL_EVENT_TYPE_REQUEST`，`id = req_id`。

3. **订阅钩子挂接 GAP event**：在 `drivers/ble_driver.c` 的 `gap_event_cb` 里，收到 `BLE_GAP_EVENT_SUBSCRIBE` 时调：

   ```c
   control_service_on_subscribe(event->subscribe.attr_handle,
                                event->subscribe.prev_notify,
                                event->subscribe.cur_notify);
   ```

4. **页面 enter 发 REQUEST**（可选）：

   ```c
   // page_weather.c enter
   control_service_send_request(CONTROL_REQUEST_WEATHER);
   ```

5. **PC 端解析**（Python bleak 参考）：

   ```python
   async def on_control_notify(sender, data):
       type_, id_, action, _, value, seq = struct.unpack('<BBBBhH', data)
       if type_ == 1:  # REQUEST
           if id_ == 1:   await write_cts_time()
           elif id_ == 2: await write_weather()
           elif id_ == 3: await write_media()
       elif type_ == 0:  # BUTTON
           handle_button(id_, action, value, seq)
   ```

## 停手规则

- 已经为 REQUEST 新建 GATT characteristic：**停**，先想"既有 NOTIFY characteristic 是否足够表达"
- REQUEST payload 超过现有定长 struct：**停**，不要塞长字符串，拆多次 REQUEST 或用 `value` 字段编码参数
- PC 端收到 REQUEST 后在 NOTIFY 上回写：**停**，响应走对应 service 的 write characteristic

## 验证顺序

1. 最小冒烟：TIME_SYNC REQUEST 跑通（订阅后 PC 收到 `type=1, id=1`，并 WRITE CTS）
2. 扩展验证：进天气页 → WEATHER REQUEST → PC 回写 weather payload → UI 更新
3. 压力：反复订阅 / 取消订阅 50 次，REQUEST 计数和日志匹配
4. 边界：未订阅状态下发 REQUEST，`ble_conn_get_handle` 返回 false，日志 `not connected, drop`

## 常见问题

- REQUEST 发出但 PC 没反应 → 检查 PC 端 NOTIFY 订阅是否成功（`start_notify(control_uuid)`）
- 订阅上升沿触发的 TIME_SYNC 丢失 → PC 端 `start_notify` 可能先于 handler 注册，确保 handler 先挂再 `start_notify`
- NOTIFY 发不出去（`notify failed rc=-1`）→ MTU / `flags` 配置 / characteristic 没开 `BLE_GATT_CHR_F_NOTIFY`
- PC 端把 REQUEST 当 BUTTON 处理 → 解析时没判 `type` 字段就直接看 `id`
