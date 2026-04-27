/* ============================================================================
 * dynapp_upload_service.c —— 动态 App 上传 BLE service
 *
 * 与 dynapp_bridge_service 完全独立：UUID 段不同 (a3a4 vs a3a3)，
 * 协议格式不同（这里是二进制定长头，bridge 是透传）。
 *
 * access_cb 职责：
 *   1) 解协议头 (op/seq/len)
 *   2) 解出 payload，调 manager submit_*
 *   3) 立刻 return（FS 写在 manager 的 consumer task 里做）
 * ========================================================================= */

#include "dynapp_upload_service.h"
#include "dynapp_upload_manager.h"
#include "ble_driver.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "dynapp_upload";

/* Service:  a3a40001-0000-4aef-b87e-4fa1e0c7e0f6 */
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x01, 0x00, 0xa4, 0xa3
);
/* rx (WRITE):     a3a40002 */
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x02, 0x00, 0xa4, 0xa3
);
/* status (NOTIFY): a3a40003 */
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(
    0xf6, 0xe0, 0xc7, 0xe0, 0xa1, 0x4f, 0x7e, 0xb8,
    0xef, 0x4a, 0x00, 0x00, 0x03, 0x00, 0xa4, 0xa3
);

static uint16_t s_rx_handle;
static uint16_t s_status_handle;
static volatile bool s_status_subscribed = false;

/* 当前 op 的 seq，access_cb 暂存给 status_cb echo 用。
 * 一次只处理一帧，简单全局变量足够。 */
static volatile uint8_t s_last_seq = 0;

#define HEADER_LEN  4   /* op+seq+len_lo+len_hi */
#define NAME_LEN    15
#define MAX_PAYLOAD 200

/* ============================================================================
 * §1. 协议小工具
 * ========================================================================= */

static inline uint16_t rd_u16le(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr_u32le(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* 从 NUL-padded 15B 取 C 字符串到 out。out 容量 ≥ 16。 */
static void copy_name(char *out, const uint8_t *src)
{
    memcpy(out, src, NAME_LEN);
    out[NAME_LEN] = '\0';
}

/* ============================================================================
 * §2. 帧拆解 → submit
 * ========================================================================= */

static void handle_frame(const uint8_t *frame, uint16_t total)
{
    if (total < HEADER_LEN) return;
    uint8_t  op       = frame[0];
    uint8_t  seq      = frame[1];
    uint16_t pay_len  = rd_u16le(frame + 2);
    const uint8_t *pay = frame + HEADER_LEN;

    if ((uint32_t)HEADER_LEN + pay_len > total) {
        ESP_LOGW(TAG, "frame: declared payload %u > avail %u", pay_len, total - HEADER_LEN);
        return;
    }
    s_last_seq = seq;

    switch (op) {
    case UPL_OP_START: {
        if (pay_len < NAME_LEN + 8) { ESP_LOGW(TAG, "START short"); return; }
        char name[NAME_LEN + 1];
        copy_name(name, pay);
        uint32_t total_len = rd_u32le(pay + NAME_LEN);
        uint32_t crc       = rd_u32le(pay + NAME_LEN + 4);
        if (!dynapp_upload_submit_start(name, total_len, crc)) {
            ESP_LOGW(TAG, "submit_start refused (queue full?)");
        }
        break;
    }
    case UPL_OP_CHUNK: {
        if (pay_len < 4) { ESP_LOGW(TAG, "CHUNK short"); return; }
        uint32_t off = rd_u32le(pay);
        const uint8_t *data = pay + 4;
        uint16_t dlen = pay_len - 4;
        if (!dynapp_upload_submit_chunk(off, data, dlen)) {
            ESP_LOGW(TAG, "submit_chunk refused");
        }
        break;
    }
    case UPL_OP_END:
        if (!dynapp_upload_submit_end()) ESP_LOGW(TAG, "submit_end refused");
        break;
    case UPL_OP_DELETE: {
        if (pay_len < NAME_LEN) { ESP_LOGW(TAG, "DELETE short"); return; }
        char name[NAME_LEN + 1];
        copy_name(name, pay);
        if (!dynapp_upload_submit_delete(name)) ESP_LOGW(TAG, "submit_delete refused");
        break;
    }
    case UPL_OP_LIST:
        if (!dynapp_upload_submit_list()) ESP_LOGW(TAG, "submit_list refused");
        break;
    default:
        ESP_LOGW(TAG, "unknown op 0x%02x", op);
        break;
    }
}

/* ============================================================================
 * §3. GATT access_cb
 * ========================================================================= */

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > MAX_PAYLOAD) {
        ESP_LOGW(TAG, "rx bad len=%u", len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buf[MAX_PAYLOAD];
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), NULL);
    if (rc != 0) return BLE_ATT_ERR_UNLIKELY;

    handle_frame(buf, len);
    return 0;
}

static int status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* status char 主要靠 NOTIFY 推送；READ 仅占位。 */
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t z = 0;
    return os_mbuf_append(ctxt->om, &z, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ============================================================================
 * §4. GATT 表
 * ========================================================================= */

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid       = &s_rx_uuid.u,
                .access_cb  = rx_access_cb,
                .flags      = BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_rx_handle,
            },
            {
                .uuid       = &s_status_uuid.u,
                .access_cb  = status_access_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_handle,
            },
            { 0 }
        }
    },
    { 0 }
};

/* ============================================================================
 * §5. status notify 回调（manager → service → BLE）
 *
 * 注意：本函数跑在 manager 的 consumer task 线程。NimBLE notify API 自带锁，
 * 跨线程调安全；但不要在这里碰 LVGL 或写 FS。
 * ========================================================================= */

static void send_status(uint8_t op_echo, uint8_t result,
                        const uint8_t *payload, uint16_t payload_len)
{
    if (!s_status_subscribed) return;
    uint16_t conn_h;
    if (!ble_driver_get_conn_handle(&conn_h)) return;

    uint8_t buf[MAX_PAYLOAD];
    if (4u + payload_len > sizeof(buf)) return;
    buf[0] = op_echo;
    buf[1] = result;
    buf[2] = s_last_seq;
    buf[3] = 0;
    if (payload && payload_len) memcpy(buf + 4, payload, payload_len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, 4 + payload_len);
    if (!om) return;
    int rc = ble_gatts_notify_custom(conn_h, s_status_handle, om);
    if (rc != 0) ESP_LOGW(TAG, "notify rc=%d", rc);
}

static void on_manager_status(upload_op_t op, upload_result_t result,
                             const char *name, uint32_t extra,
                             const uint8_t *list_buf, size_t list_len)
{
    (void)name;  /* PC 已经从 last_seq 知道是哪一帧；name 只用作日志 */

    if (op == UPL_OP_LIST && result == UPL_RESULT_OK && list_buf && list_len > 0) {
        send_status((uint8_t)op, (uint8_t)result, list_buf, (uint16_t)list_len);
        return;
    }
    if (op == UPL_OP_CHUNK && result == UPL_RESULT_OK) {
        uint8_t pay[4];
        wr_u32le(pay, extra);
        send_status((uint8_t)op, (uint8_t)result, pay, sizeof(pay));
        return;
    }
    send_status((uint8_t)op, (uint8_t)result, NULL, 0);
}

/* ============================================================================
 * §6. 订阅事件
 * ========================================================================= */

static void on_subscribe(uint16_t attr_handle, uint8_t prev_notify, uint8_t cur_notify)
{
    if (attr_handle != s_status_handle) return;
    s_status_subscribed = (cur_notify != 0);
    ESP_LOGI(TAG, "status subscribe -> %d", (int)s_status_subscribed);
}

/* ============================================================================
 * §7. init
 * ========================================================================= */

esp_err_t dynapp_upload_service_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE Dynamic App Upload service");

    /* manager 必须先起来，service 注册的 access_cb 一旦被调就要能 submit。 */
    esp_err_t err = dynapp_upload_manager_init(on_manager_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "manager init failed: %d", err);
        return err;
    }

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "count_cfg rc=%d", rc); return ESP_FAIL; }
    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "add_svcs rc=%d", rc); return ESP_FAIL; }

    err = ble_driver_register_subscribe_cb(on_subscribe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register subscribe failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Service UUID: a3a40001-0000-4aef-b87e-4fa1e0c7e0f6 (rx + status)");
    return ESP_OK;
}
