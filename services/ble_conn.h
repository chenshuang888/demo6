#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 连接状态中转层。
 *
 * ble_driver（drivers 组件）在 GAP CONNECT / DISCONNECT 事件里调用
 * ble_conn_set() 上报状态；其他 service（services 组件内）通过
 * ble_conn_get_handle() 取当前 central 的 conn_handle 来发 notify。
 *
 * 存在理由：drivers 已 REQUIRES services，services 不能再 REQUIRES drivers
 * （会循环依赖），因此把"当前连接"这一小块共享状态放在 services 侧，
 * 由 drivers 主动上报，保持依赖方向单一。
 */

/** BLE host 线程调用（在 GAP event handler 里） */
void ble_conn_set(bool connected, uint16_t conn_handle);

/**
 * 任意线程可调（存储是 volatile + 单写）。
 * @return 当前有连接返回 true 并写 *out；未连接返回 false
 */
bool ble_conn_get_handle(uint16_t *out_handle);

/** 等价于 ble_conn_get_handle(NULL) 的语义糖 */
bool ble_conn_is_connected(void);

#ifdef __cplusplus
}
#endif
