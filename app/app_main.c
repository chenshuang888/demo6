#include "app_main.h"
#include "pages/page_time.h"
#include "pages/page_menu.h"
#include "pages/page_about.h"
#include "pages/page_weather.h"
#include "pages/page_notifications.h"
#include "page_router.h"
#include "lvgl_port.h"
#include "lcd_panel.h"
#include "time_manager.h"
#include "weather_manager.h"
#include "notify_manager.h"
#include "settings_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app_main";

static void ui_task(void *arg)
{
    while (1) {
        time_manager_process_pending();
        weather_manager_process_pending();
        notify_manager_process_pending();
        page_router_update();
        lvgl_port_task_handler();

        /* 周期性持久化（内部自判是否到时间/dirty，绝大多数 tick 即返回） */
        notify_manager_tick_flush();
        settings_store_tick_save_time();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t app_main_init(void)
{
    ESP_LOGI(TAG, "Initializing application");

    ESP_ERROR_CHECK(lvgl_port_init());

    /* 恢复上次背光亮度（lvgl_port_init 内部已初始化 lcd_panel） */
    lcd_panel_set_backlight(settings_store_get_backlight());

    ESP_ERROR_CHECK(page_router_init());

    ESP_ERROR_CHECK(page_router_register(PAGE_TIME, page_time_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_MENU, page_menu_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_ABOUT, page_about_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_WEATHER, page_weather_get_callbacks()));
    ESP_ERROR_CHECK(page_router_register(PAGE_NOTIFICATIONS, page_notifications_get_callbacks()));

    ESP_ERROR_CHECK(page_router_switch(PAGE_TIME));

    BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Application initialized");
    return ESP_OK;
}
