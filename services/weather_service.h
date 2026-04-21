#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 天气服务
 *
 * 注册自定义 GATT 服务：
 *   Service UUID:          8a5c0001-0000-4aef-b87e-4fa1e0c7e0f6
 *   Write characteristic:  8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6  (WRITE)
 *     PC → ESP 写入 weather_payload_t 结构体，ESP 转发到 UI 线程
 *   Notify characteristic: 8a5c000b-0000-4aef-b87e-4fa1e0c7e0f6  (NOTIFY)
 *     ESP → PC 发 1B 递增 seq，语义为"请 PC 推一次天气"
 *
 * 必须在 nimble_port_freertos_init() 之前调用。
 */
esp_err_t weather_service_init(void);

/**
 * @brief 发一次反向请求（ESP→PC），要求 PC 写入最新天气
 *
 * 未连接 / 未订阅 / mbuf 分配失败时静默返回错误，只打 WARN，不崩溃。
 * body 为 1 字节递增 seq，PC 端可用于去重。
 */
esp_err_t weather_service_send_request(void);

/**
 * @brief BLE 驱动层在 GAP_EVENT_SUBSCRIBE 里调用
 *
 * 内部检查 attr_handle 是否是本服务的 notify char、cur_notify 是否 0→1 上升沿；
 * 匹配时自动发一次 weather_service_send_request()。
 */
void weather_service_on_subscribe(uint16_t attr_handle,
                                  uint8_t prev_notify,
                                  uint8_t cur_notify);

#ifdef __cplusplus
}
#endif
