#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "app_main.h"
#include "ble_driver.h"

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

    // 先初始化系统时间
    init_default_time();

    // 初始化 BLE
    ESP_ERROR_CHECK(ble_driver_init());

    // 再初始化应用
    ESP_ERROR_CHECK(app_main_init());

    ESP_LOGI(TAG, "Application started");
}