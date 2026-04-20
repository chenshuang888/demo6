#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 事件类型 / 动作
 * 与 PC 端严格对齐，不要随意改数值
 * ------------------------------------------------------------------ */
#define CONTROL_EVENT_TYPE_BUTTON  0
#define CONTROL_EVENT_TYPE_SLIDER  1   /* 预留 */
#define CONTROL_EVENT_TYPE_REQUEST 2   /* ESP 反向请求 PC 主动推送数据 */

#define CONTROL_EVENT_ACTION_PRESS 0
/* 预留: RELEASE=1, LONG_PRESS=2 */

/* 按钮 id 约定（MVP 四个）：
 *   0 = 锁屏    1 = 静音
 *   2 = 上一首  3 = 下一首
 */

/* REQUEST 事件的 id 语义（PC 端同步维护）：
 *   0 = 请求 PC 写 CTS 推送当前时间
 *   预留：1 = 天气重推，2 = 媒体重推……
 */
#define CONTROL_REQUEST_TIME_SYNC 0

/* 与 PC 端 struct.pack("<BBBBhH", ...) 严格对齐（8 字节） */
typedef struct {
    uint8_t  type;
    uint8_t  id;
    uint8_t  action;
    uint8_t  _reserved;
    int16_t  value;      /* button 事件恒为 0；给 slider 预留 */
    uint16_t seq;        /* 单调递增，PC 端去重/丢包检测 */
} __attribute__((packed)) control_event_t;

/**
 * @brief 初始化 BLE 控制服务
 *
 * 注册自定义 GATT 服务：
 *   Service UUID:        8a5c0005-0000-4aef-b87e-4fa1e0c7e0f6
 *   Characteristic UUID: 8a5c0006-0000-4aef-b87e-4fa1e0c7e0f6  (READ | NOTIFY)
 *
 * 方向与 weather / notify 相反：ESP 主动 Notify 推送事件给 PC。
 * 必须在 nimble_port_freertos_init() 之前调用。
 */
esp_err_t control_service_init(void);

/**
 * @brief 推送一个按钮事件到已连接的 central（UI 线程调用）
 *
 * 未连接 / 未订阅 / mbuf 分配失败时静默丢弃，只打印 WARN，不崩溃。
 * 因为按钮点击是瞬时动作，丢一次不影响用户体验。
 */
esp_err_t control_service_send_button(uint8_t id);

/**
 * @brief 推送一个 REQUEST 事件，要求 PC 主动推送某类数据
 *
 * 典型用法：ESP 连上 PC 后发 CONTROL_REQUEST_TIME_SYNC 让 PC 写 CTS。
 * 与 send_button 共用同一条 characteristic，仅 type 字段不同。
 */
esp_err_t control_service_send_request(uint8_t req_id);

/**
 * @brief BLE 驱动层在 GAP_EVENT_SUBSCRIBE 里调用
 *
 * 内部判断 attr_handle 是否是 control_char、cur_notify 是否 0→1 上升沿；
 * 匹配时自动发一次 CONTROL_REQUEST_TIME_SYNC 给 PC。
 */
void control_service_on_subscribe(uint16_t attr_handle,
                                  uint8_t prev_notify,
                                  uint8_t cur_notify);

#ifdef __cplusplus
}
#endif
