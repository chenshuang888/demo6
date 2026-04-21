#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 系统监控服务
 *
 * 注册自定义 GATT 服务：
 *   Service UUID:        8a5c0009-0000-4aef-b87e-4fa1e0c7e0f6
 *   Characteristic UUID: 8a5c000a-0000-4aef-b87e-4fa1e0c7e0f6  (WRITE)
 *
 * PC 端写入 system_payload_t（16 字节），收到后转发到 UI 线程。
 * 必须在 nimble_port_freertos_init() 之前调用。
 */
esp_err_t system_service_init(void);

#ifdef __cplusplus
}
#endif
