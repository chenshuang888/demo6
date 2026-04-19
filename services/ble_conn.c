#include "ble_conn.h"

/* 跨线程：BLE host 线程写、UI 线程读 —— volatile 够用，读是单次 16-bit 赋值/原子。 */
static volatile bool     s_connected = false;
static volatile uint16_t s_handle    = 0;

void ble_conn_set(bool connected, uint16_t conn_handle)
{
    /* 先写 handle 再置 connected，避免被 reader 看到"已连接但 handle 还没更新"的中间态 */
    if (connected) {
        s_handle    = conn_handle;
        s_connected = true;
    } else {
        s_connected = false;
        s_handle    = 0;
    }
}

bool ble_conn_get_handle(uint16_t *out_handle)
{
    if (!s_connected) {
        return false;
    }
    if (out_handle) {
        *out_handle = s_handle;
    }
    return true;
}

bool ble_conn_is_connected(void)
{
    return s_connected;
}
