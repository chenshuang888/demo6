#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 时间服务
 *
 * 注册 Current Time Service (UUID: 0x1805)，支持通过蓝牙读取和设置系统时间
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_FAIL: 失败
 */
esp_err_t time_service_init(void);

/**
 * @brief 通知已连接的客户端时间已更新
 *
 * 当系统时间发生变化时，可调用此函数主动推送通知给已订阅的客户端
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_FAIL: 失败或无客户端连接
 */
esp_err_t time_service_notify_time_changed(void);

#ifdef __cplusplus
}
#endif
