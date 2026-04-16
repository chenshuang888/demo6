#include "app_ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "app_ui";

static int counter = 0;
static lv_obj_t *label;

static void btn_event_cb(lv_event_t *e)
{
    counter++;
    lv_label_set_text_fmt(label, "%d", counter);
    ESP_LOGI(TAG, "Button clicked, counter: %d", counter);
}

static void create_ui(void)
{
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);

    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "0");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
}

static void ui_task(void *arg)
{
    create_ui();

    while (1) {
        lvgl_port_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t app_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI");

    ESP_ERROR_CHECK(lvgl_port_init());

    BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
