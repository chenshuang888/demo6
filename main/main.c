#include <stdio.h>
#include "esp_log.h"
#include "app_ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application");

    ESP_ERROR_CHECK(app_ui_init());

    ESP_LOGI(TAG, "Application started");
}