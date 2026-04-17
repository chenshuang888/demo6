#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 驱动
 *
 * 初始化 NimBLE 协议栈，配置 GAP 和 GATT 服务
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_FAIL: 失败
 */
esp_err_t ble_driver_init(void);

/**
 * @brief 启动 BLE 广播
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_FAIL: 失败
 */
esp_err_t ble_driver_start_advertising(void);

/**
 * @brief 停止 BLE 广播
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_FAIL: 失败
 */
esp_err_t ble_driver_stop_advertising(void);

/**
 * @brief 获取 BLE 连接状态
 *
 * @return
 *      - true: 已连接
 *      - false: 未连接
 */
bool ble_driver_is_connected(void);

#ifdef __cplusplus
}
#endif
