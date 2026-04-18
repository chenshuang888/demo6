#include "notify_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "notify_mgr";

#define NOTIFY_QUEUE_DEPTH 4

static QueueHandle_t s_queue = NULL;

/* 环形缓冲：s_head 为下一个写入位，s_count 为已存条数 */
static notification_payload_t s_ring[NOTIFY_STORE_MAX];
static size_t   s_head = 0;
static size_t   s_count = 0;
static uint32_t s_version = 0;

esp_err_t notify_manager_init(void)
{
    if (s_queue) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    s_queue = xQueueCreate(NOTIFY_QUEUE_DEPTH, sizeof(notification_payload_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }

    s_head = 0;
    s_count = 0;
    s_version = 0;

    ESP_LOGI(TAG, "Notify manager initialized (queue=%d, store=%d)",
             NOTIFY_QUEUE_DEPTH, NOTIFY_STORE_MAX);
    return ESP_OK;
}

esp_err_t notify_manager_push(const notification_payload_t *n)
{
    if (!s_queue) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!n) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 队列满时丢最旧 — 与 weather_manager 一致，避免 BLE 回调阻塞 */
    if (xQueueSend(s_queue, n, 0) != pdTRUE) {
        notification_payload_t dropped;
        xQueueReceive(s_queue, &dropped, 0);
        if (xQueueSend(s_queue, n, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue push failed after drop");
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Queue was full, oldest entry dropped");
    }

    return ESP_OK;
}

void notify_manager_process_pending(void)
{
    if (!s_queue) {
        return;
    }

    notification_payload_t tmp;
    bool changed = false;
    while (xQueueReceive(s_queue, &tmp, 0) == pdTRUE) {
        /* 防御：确保字符串有 \0 结尾 */
        tmp.title[NOTIFY_TITLE_MAX - 1] = '\0';
        tmp.body[NOTIFY_BODY_MAX - 1]   = '\0';

        memcpy(&s_ring[s_head], &tmp, sizeof(tmp));
        s_head = (s_head + 1) % NOTIFY_STORE_MAX;
        if (s_count < NOTIFY_STORE_MAX) {
            s_count++;
        }
        changed = true;

        ESP_LOGI(TAG, "Notification stored: [%u] %s", tmp.category, tmp.title);
    }

    if (changed) {
        s_version++;
    }
}

size_t notify_manager_count(void)
{
    return s_count;
}

const notification_payload_t *notify_manager_get_at(size_t index)
{
    if (index >= s_count) {
        return NULL;
    }
    /* index 0 = 最新 = head-1-index（环形） */
    size_t slot = (s_head + NOTIFY_STORE_MAX - 1 - index) % NOTIFY_STORE_MAX;
    return &s_ring[slot];
}

uint32_t notify_manager_version(void)
{
    return s_version;
}

void notify_manager_clear(void)
{
    s_head = 0;
    s_count = 0;
    s_version++;
    ESP_LOGI(TAG, "All notifications cleared");
}
