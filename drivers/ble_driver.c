#include "ble_driver.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* 外部库函数声明 */
void ble_store_config_init(void);

static const char *TAG = "ble_driver";

/* BLE 设备名称 */
#define BLE_DEVICE_NAME "ESP32-S3-DEMO"

/* 连接状态 */
static bool s_is_connected = false;
static uint16_t s_conn_handle = 0;

/* 前向声明 */
static void ble_host_task(void *param);
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* ============================================================================
 * BLE 协议栈回调函数
 * ============================================================================ */

static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "BLE stack reset, reason: %d", reason);
}

static void on_stack_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE stack synced");

    /* 确保使用随机地址 */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address, error: %d", rc);
        return;
    }

    /* 获取设备地址 */
    uint8_t addr[6] = {0};
    rc = ble_hs_id_infer_auto(0, &addr[0]);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address, error: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Device address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    /* 开始广播 */
    ble_driver_start_advertising();
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            s_is_connected = true;
            s_conn_handle = event->connect.conn_handle;
        } else {
            /* 连接失败，重新开始广播 */
            ble_driver_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_is_connected = false;
        s_conn_handle = 0;

        /* 断开连接后重新开始广播 */
        ble_driver_start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.value);
        break;

    default:
        break;
    }

    return 0;
}

/* ============================================================================
 * BLE 主机任务
 * ============================================================================ */

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");

    /* 运行 NimBLE 主机，此函数会阻塞直到 nimble_port_stop() 被调用 */
    nimble_port_run();

    /* 清理 */
    nimble_port_freertos_deinit();
}

/* ============================================================================
 * 公共接口函数
 * ============================================================================ */

esp_err_t ble_driver_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE driver");

    /* 初始化 NVS（NimBLE 需要 NVS 存储绑定信息） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS, error: %d", ret);
        return ESP_FAIL;
    }

    /* 初始化 NimBLE 协议栈 */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE stack, error: %d", ret);
        return ESP_FAIL;
    }

    /* 初始化 GAP 服务 */
    ble_svc_gap_init();

    /* 初始化 GATT 服务 */
    ble_svc_gatt_init();

    /* 设置设备名称 */
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name, error: %d", rc);
        return ESP_FAIL;
    }

    /* 配置 NimBLE 主机 */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* 初始化存储配置 */
    ble_store_config_init();

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE driver initialized successfully");
    return ESP_OK;
}

esp_err_t ble_driver_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    ESP_LOGI(TAG, "Starting BLE advertising");

    /* 设置广播数据 */
    memset(&fields, 0, sizeof(fields));

    /* 设置标志 */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 设置设备名称 */
    fields.name = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    /* 设置广播数据 */
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data, error: %d", rc);
        return ESP_FAIL;
    }

    /* 配置广播参数 */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  /* 可连接 */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* 通用可发现 */
    adv_params.itvl_min = 0x20;  /* 20ms */
    adv_params.itvl_max = 0x40;  /* 40ms */

    /* 开始广播 */
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, error: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE advertising started");
    return ESP_OK;
}

esp_err_t ble_driver_stop_advertising(void)
{
    int rc;

    ESP_LOGI(TAG, "Stopping BLE advertising");

    rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to stop advertising, error: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE advertising stopped");
    return ESP_OK;
}

bool ble_driver_is_connected(void)
{
    return s_is_connected;
}
