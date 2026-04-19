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
#define CONTROL_EVENT_TYPE_BUTTON 0
#define CONTROL_EVENT_TYPE_SLIDER 1   /* 预留 */

#define CONTROL_EVENT_ACTION_PRESS 0
/* 预留: RELEASE=1, LONG_PRESS=2 */

/* 按钮 id 约定（MVP 四个）：
 *   0 = 锁屏    1 = 静音
 *   2 = 上一首  3 = 下一首
 */

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

#ifdef __cplusplus
}
#endif
