#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * BLE 媒体服务
 *
 * 承担两类通道：
 *   - WRITE  (8a5c0008): PC → ESP 推送 media_payload_t（曲目/进度/状态）
 *   - NOTIFY (8a5c000d): ESP → PC 推送 media_button_event_t（屏上媒体键）
 *
 * 两个 char 同属一个 service，符合"触发端与响应端同在一个 service"原则。
 * ------------------------------------------------------------------ */

/* 与 PC 端 struct.pack("<BBH", ...) 严格对齐（4 bytes） */
typedef struct {
    uint8_t  id;       // 0=prev, 1=play_pause, 2=next
    uint8_t  action;   // 0=press (预留 release=1 / long_press=2)
    uint16_t seq;      // 单调递增；PC 端用于去重 / 丢包检测
} __attribute__((packed)) media_button_event_t;

/* 按钮 id 常量（与 PC 端 desktop_companion.py 对齐） */
#define MEDIA_BTN_PREV        0
#define MEDIA_BTN_PLAY_PAUSE  1
#define MEDIA_BTN_NEXT        2

#define MEDIA_BTN_ACTION_PRESS 0

/**
 * @brief 初始化 BLE 媒体服务（注册 GATT 表）
 *
 * 必须在 nimble_port_freertos_init() 之前调用。
 */
esp_err_t media_service_init(void);

/**
 * @brief 从屏上按钮事件通道推送一次（UI 线程调用）
 *
 * 未连接 / 未订阅 / mbuf 分配失败时静默丢弃，只打印 WARN，不崩溃。
 * 按钮是瞬时动作，丢一次不影响体验。
 */
esp_err_t media_service_send_button(uint8_t id);

#ifdef __cplusplus
}
#endif
