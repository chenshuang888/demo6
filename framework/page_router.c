#include "page_router.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "page_router";

typedef struct {
    bool initialized;
    page_id_t current_page;
    lv_obj_t *current_screen;
    const page_callbacks_t *pages[PAGE_MAX];
} router_state_t;

static router_state_t s_router = {
    .initialized = false,
    .current_page = PAGE_MAX,
    .current_screen = NULL,
};

esp_err_t page_router_init(void)
{
    if (s_router.initialized) {
        ESP_LOGW(TAG, "Router already initialized");
        return ESP_OK;
    }

    memset(&s_router, 0, sizeof(s_router));
    s_router.initialized = true;
    s_router.current_page = PAGE_MAX;

    ESP_LOGI(TAG, "Router initialized");
    return ESP_OK;
}

esp_err_t page_router_register(page_id_t id, const page_callbacks_t *callbacks)
{
    if (!s_router.initialized) {
        ESP_LOGE(TAG, "Router not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (id >= PAGE_MAX) {
        ESP_LOGE(TAG, "Invalid page ID: %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    if (!callbacks || !callbacks->create) {
        ESP_LOGE(TAG, "Invalid callbacks (create is required)");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_router.pages[id]) {
        ESP_LOGW(TAG, "Page %d already registered, overwriting", id);
    }

    s_router.pages[id] = callbacks;
    ESP_LOGI(TAG, "Page %d registered", id);
    return ESP_OK;
}

esp_err_t page_router_switch(page_id_t id)
{
    if (!s_router.initialized) {
        ESP_LOGE(TAG, "Router not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (id >= PAGE_MAX) {
        ESP_LOGE(TAG, "Invalid page ID: %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_router.pages[id]) {
        ESP_LOGE(TAG, "Page %d not registered", id);
        return ESP_ERR_NOT_FOUND;
    }

    if (s_router.current_page == id) {
        ESP_LOGW(TAG, "Already on page %d", id);
        return ESP_OK;
    }

    // 销毁当前页面
    if (s_router.current_screen) {
        const page_callbacks_t *old_callbacks = s_router.pages[s_router.current_page];
        if (old_callbacks && old_callbacks->destroy) {
            ESP_LOGD(TAG, "Calling destroy for page %d", s_router.current_page);
            old_callbacks->destroy();
        } else {
            ESP_LOGD(TAG, "Auto deleting screen for page %d", s_router.current_page);
            lv_obj_del(s_router.current_screen);
        }
        s_router.current_screen = NULL;
    }

    // 创建新页面
    const page_callbacks_t *new_callbacks = s_router.pages[id];
    ESP_LOGI(TAG, "Switching to page %d", id);
    lv_obj_t *new_screen = new_callbacks->create();

    if (!new_screen) {
        ESP_LOGE(TAG, "Failed to create page %d", id);
        return ESP_FAIL;
    }

    // 加载新页面
    lv_scr_load(new_screen);
    s_router.current_screen = new_screen;
    s_router.current_page = id;

    ESP_LOGI(TAG, "Switched to page %d", id);
    return ESP_OK;
}

page_id_t page_router_get_current(void)
{
    return s_router.current_page;
}

void page_router_update(void)
{
    if (!s_router.initialized || s_router.current_page >= PAGE_MAX) {
        return;
    }

    const page_callbacks_t *callbacks = s_router.pages[s_router.current_page];
    if (callbacks && callbacks->update) {
        callbacks->update();
    }
}
