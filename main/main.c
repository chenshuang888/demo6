#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "app_main.h"
#include "ble_driver.h"
#include "time_manager.h"
#include "weather_manager.h"
#include "notify_manager.h"
#include "media_manager.h"
#include "system_manager.h"
#include "persist.h"
#include "settings_store.h"

static const char *TAG = "main";

static void init_default_time(void)
{
    struct tm timeinfo = {
        .tm_year = 2026 - 1900,
        .tm_mon = 4 - 1,
        .tm_mday = 17,
        .tm_hour = 12,
        .tm_min = 0,
        .tm_sec = 0
    };
    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application");

    // 最先初始化持久化，后续模块都可能用 NVS
    ESP_ERROR_CHECK(persist_init());
    ESP_ERROR_CHECK(settings_store_init());

    // 恢复上次关机前的时间；失败走硬编码默认
    struct timeval tv;
    if (settings_store_load_last_time(&tv) == ESP_OK) {
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system time restored from NVS");
    } else {
        init_default_time();
        ESP_LOGI(TAG, "system time initialized to default");
    }

    // 初始化时间管理器（BLE 需要在初始化前就绪）
    ESP_ERROR_CHECK(time_manager_init());

    // 初始化天气管理器（同上）
    ESP_ERROR_CHECK(weather_manager_init());

    // 初始化通知管理器（同上；内部会从 NVS 加载上次快照）
    ESP_ERROR_CHECK(notify_manager_init());

    // 初始化媒体管理器（同上；BLE 回调会 push 到它）
    ESP_ERROR_CHECK(media_manager_init());

    // 初始化系统监控管理器（PC 推 CPU/MEM/DISK/BAT/NET/Temp）
    ESP_ERROR_CHECK(system_manager_init());

    // 初始化 BLE
    ESP_ERROR_CHECK(ble_driver_init());

    // 再初始化应用
    ESP_ERROR_CHECK(app_main_init());

    ESP_LOGI(TAG, "Application started");
}