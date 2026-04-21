#include "control_service.h"
#include "ble_conn.h"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "control_svc";

/* Service UUID: 8a5c0005-0000-4aef-b87e-4fa1e0c7e0f6 */
static const ble_uuid128_t s_ctrl_svc_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x05, 0x00, 0x5c, 0x8a
);

/* Characteristic UUID: 8a5c0006-0000-4aef-b87e-4fa1e0c7e0f6 */
static const ble_uuid128_t s_ctrl_chr_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x06, 0x00, 0x5c, 0x8a
);

static uint16_t s_chr_val_handle;
static uint16_t s_seq = 0;

/* READ 仅用来让客户端能"发现"characteristic，真正数据走 NOTIFY。
 * 返回最近一次事件的 seq 作心跳占位。 */
static int ctrl_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGW(TAG, "Unsupported op: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }

    control_event_t snapshot = {
        .type       = CONTROL_EVENT_TYPE_BUTTON,
        .id         = 0,
        .action     = 0,
        ._reserved  = 0,
        .value      = 0,
        .seq        = s_seq,
    };

    int rc = os_mbuf_append(ctxt->om, &snapshot, sizeof(snapshot));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_ctrl_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ctrl_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_ctrl_chr_uuid.u,
                .access_cb = ctrl_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_chr_val_handle,
            },
            {0}
        },
    },
    {0}
};

esp_err_t control_service_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE Control Service");

    rc = ble_gatts_count_cfg(s_ctrl_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_ctrl_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE Control Service initialized");
    ESP_LOGI(TAG, "Service UUID: 8a5c0005-0000-4aef-b87e-4fa1e0c7e0f6");
    return ESP_OK;
}

esp_err_t control_service_send_button(uint8_t id)
{
    uint16_t conn_handle;
    if (!ble_conn_get_handle(&conn_handle)) {
        ESP_LOGD(TAG, "not connected, drop button id=%u", id);
        return ESP_ERR_INVALID_STATE;
    }

    control_event_t evt = {
        .type       = CONTROL_EVENT_TYPE_BUTTON,
        .id         = id,
        .action     = CONTROL_EVENT_ACTION_PRESS,
        ._reserved  = 0,
        .value      = 0,
        .seq        = ++s_seq,
    };

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&evt, sizeof(evt));
    if (!om) {
        ESP_LOGW(TAG, "mbuf alloc failed");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d (client may not subscribe)", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "button event sent: id=%u seq=%u", id, evt.seq);
    return ESP_OK;
}
