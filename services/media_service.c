#include "media_service.h"
#include "media_manager.h"
#include "ble_conn.h"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "media_svc";

/* Service UUID: 8a5c0007-0000-4aef-b87e-4fa1e0c7e0f6 */
static const ble_uuid128_t s_media_svc_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x07, 0x00, 0x5c, 0x8a
);

/* Write characteristic UUID: 8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6 */
static const ble_uuid128_t s_media_chr_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x08, 0x00, 0x5c, 0x8a
);

/* Button notify characteristic UUID: 8a5c000d-0000-4aef-b87e-4fa1e0c7e0f6
 * 在服务内 char 数 > 2 时按 UUID DR 的"末尾追加"例外分配 */
static const ble_uuid128_t s_media_btn_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x0d, 0x00, 0x5c, 0x8a
);

static uint16_t s_chr_val_handle;
static uint16_t s_btn_val_handle;
static uint16_t s_btn_seq = 0;

static int media_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGW(TAG, "Unsupported op: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != sizeof(media_payload_t)) {
        ESP_LOGE(TAG, "Invalid payload length: %d (expected %d)",
                 len, (int)sizeof(media_payload_t));
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    media_payload_t payload;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, &payload, sizeof(payload), NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to flatten mbuf, error: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 投递到队列，由 UI 线程消费（非阻塞） */
    esp_err_t err = media_manager_push(&payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "media_manager_push failed: %d", err);
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGD(TAG, "Media payload received (%d bytes)", len);
    return 0;
}

/* READ 仅用于让客户端"发现"button char，真正数据走 NOTIFY。
 * 返回最近一次 seq 作心跳占位。 */
static int media_btn_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGW(TAG, "Unsupported op: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }

    media_button_event_t snapshot = {
        .id     = 0,
        .action = 0,
        .seq    = s_btn_seq,
    };

    int rc = os_mbuf_append(ctxt->om, &snapshot, sizeof(snapshot));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_media_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_media_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_media_chr_uuid.u,
                .access_cb = media_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_chr_val_handle
            },
            {
                .uuid = &s_media_btn_uuid.u,
                .access_cb = media_btn_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_btn_val_handle
            },
            {0}
        }
    },
    {0}
};

esp_err_t media_service_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE Media Service");

    rc = ble_gatts_count_cfg(s_media_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_media_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE Media Service initialized");
    ESP_LOGI(TAG, "Service UUID: 8a5c0007-0000-4aef-b87e-4fa1e0c7e0f6");
    return ESP_OK;
}

esp_err_t media_service_send_button(uint8_t id)
{
    uint16_t conn_handle;
    if (!ble_conn_get_handle(&conn_handle)) {
        ESP_LOGD(TAG, "not connected, drop button id=%u", id);
        return ESP_ERR_INVALID_STATE;
    }

    media_button_event_t evt = {
        .id     = id,
        .action = MEDIA_BTN_ACTION_PRESS,
        .seq    = ++s_btn_seq,
    };

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&evt, sizeof(evt));
    if (!om) {
        ESP_LOGW(TAG, "mbuf alloc failed");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_btn_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d (client may not subscribe)", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "media button sent: id=%u seq=%u", id, evt.seq);
    return ESP_OK;
}
