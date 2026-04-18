#include "app_main.h"
#include "pages/page_time.h"
#include "pages/page_menu.h"
#include "pages/page_about.h"
#include "pages/page_weather.h"
#include "page_router.h"
#include "lvgl_port.h"
#include "time_manager.h"
#include "weather_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app_main";

static void ui_task(void *arg)
{
    while (1) {
        time_manager_process_pending();
        weather_manager_process_pending();
        page_router_update();
        lvgl_port_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t app_main_init(void)
{
    ESP_LOGI(TAG, "Initializing application");

    ESP_ERROR_CHECK(lvgl_port_init());

    ESP_ERROR_CHECK(page_router_init());

    ESP_ERROR_CHECK(page_router_register(PAGE_TIME, page_time_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_MENU, page_menu_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_ABOUT, page_about_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_WEATHER, page_weather_get_callbacks()));

    ESP_ERROR_CHECK(page_router_switch(PAGE_TIME));

    BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Application initialized");
    return ESP_OK;
}
