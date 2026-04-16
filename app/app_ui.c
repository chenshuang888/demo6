#include "app_ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "app_ui";

/* ============================================================================
 * UI Elements (类似 HTML - 界面元素定义)
 * ============================================================================ */

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *hour_up_btn;
    lv_obj_t *hour_down_btn;
    lv_obj_t *min_up_btn;
    lv_obj_t *min_down_btn;
    lv_obj_t *year_up_btn;
    lv_obj_t *year_down_btn;
    lv_obj_t *mon_up_btn;
    lv_obj_t *mon_down_btn;
    lv_obj_t *day_up_btn;
    lv_obj_t *day_down_btn;
} ui_elements_t;

static ui_elements_t ui;

/* ============================================================================
 * Styles (类似 CSS - 样式定义)
 * ============================================================================ */

#define BTN_WIDTH  50
#define BTN_HEIGHT 40

#define TIME_LABEL_Y  -80
#define DATE_LABEL_Y  -40

#define TIME_BTN_START_X  40
#define TIME_BTN_UP_Y     100
#define TIME_BTN_DOWN_Y   145
#define TIME_BTN_SPACING  55

#define DATE_BTN_START_X  10
#define DATE_BTN_UP_Y     220
#define DATE_BTN_DOWN_Y   265
#define DATE_BTN_SPACING  55

static lv_obj_t *create_button(const char *text, int x, int y)
{
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_set_pos(btn, x, y);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

static lv_obj_t *create_hint_label(const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void create_ui_layout(void)
{
    // 时间显示标签
    ui.time_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ui.time_label, "00:00:00");
    lv_obj_align(ui.time_label, LV_ALIGN_CENTER, 0, TIME_LABEL_Y);

    // 时间调节按钮
    ui.hour_up_btn = create_button("+", TIME_BTN_START_X, TIME_BTN_UP_Y);
    ui.hour_down_btn = create_button("-", TIME_BTN_START_X, TIME_BTN_DOWN_Y);
    ui.min_up_btn = create_button("+", TIME_BTN_START_X + TIME_BTN_SPACING, TIME_BTN_UP_Y);
    ui.min_down_btn = create_button("-", TIME_BTN_START_X + TIME_BTN_SPACING, TIME_BTN_DOWN_Y);

    create_hint_label("H", TIME_BTN_START_X + 15, 190);
    create_hint_label("M", TIME_BTN_START_X + TIME_BTN_SPACING + 15, 190);

    // 日期显示标签
    ui.date_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ui.date_label, "2026-04-16");
    lv_obj_align(ui.date_label, LV_ALIGN_CENTER, 0, DATE_LABEL_Y);

    // 日期调节按钮
    ui.year_up_btn = create_button("+", DATE_BTN_START_X, DATE_BTN_UP_Y);
    ui.year_down_btn = create_button("-", DATE_BTN_START_X, DATE_BTN_DOWN_Y);
    ui.mon_up_btn = create_button("+", DATE_BTN_START_X + DATE_BTN_SPACING, DATE_BTN_UP_Y);
    ui.mon_down_btn = create_button("-", DATE_BTN_START_X + DATE_BTN_SPACING, DATE_BTN_DOWN_Y);
    ui.day_up_btn = create_button("+", DATE_BTN_START_X + DATE_BTN_SPACING * 2, DATE_BTN_UP_Y);
    ui.day_down_btn = create_button("-", DATE_BTN_START_X + DATE_BTN_SPACING * 2, DATE_BTN_DOWN_Y);

    create_hint_label("Y", DATE_BTN_START_X + 15, 310);
    create_hint_label("M", DATE_BTN_START_X + DATE_BTN_SPACING + 15, 310);
    create_hint_label("D", DATE_BTN_START_X + DATE_BTN_SPACING * 2 + 15, 310);
}

/* ============================================================================
 * Logic (类似 JavaScript - 业务逻辑)
 * ============================================================================ */

static void adjust_time(int hour_delta, int min_delta, int sec_delta)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_hour += hour_delta;
    timeinfo.tm_min += min_delta;
    timeinfo.tm_sec += sec_delta;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void adjust_date(int year_delta, int mon_delta, int day_delta)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_year += year_delta;
    timeinfo.tm_mon += mon_delta;
    timeinfo.tm_mday += day_delta;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void update_display(void)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    lv_label_set_text_fmt(ui.time_label, "%02d:%02d:%02d",
                          timeinfo.tm_hour,
                          timeinfo.tm_min,
                          timeinfo.tm_sec);

    lv_label_set_text_fmt(ui.date_label, "%04d-%02d-%02d",
                          timeinfo.tm_year + 1900,
                          timeinfo.tm_mon + 1,
                          timeinfo.tm_mday);
}

// 事件回调函数
static void hour_up_cb(lv_event_t *e) { adjust_time(1, 0, 0); }
static void hour_down_cb(lv_event_t *e) { adjust_time(-1, 0, 0); }
static void min_up_cb(lv_event_t *e) { adjust_time(0, 1, 0); }
static void min_down_cb(lv_event_t *e) { adjust_time(0, -1, 0); }
static void year_up_cb(lv_event_t *e) { adjust_date(1, 0, 0); }
static void year_down_cb(lv_event_t *e) { adjust_date(-1, 0, 0); }
static void mon_up_cb(lv_event_t *e) { adjust_date(0, 1, 0); }
static void mon_down_cb(lv_event_t *e) { adjust_date(0, -1, 0); }
static void day_up_cb(lv_event_t *e) { adjust_date(0, 0, 1); }
static void day_down_cb(lv_event_t *e) { adjust_date(0, 0, -1); }

static void bind_events(void)
{
    lv_obj_add_event_cb(ui.hour_up_btn, hour_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.hour_down_btn, hour_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.min_up_btn, min_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.min_down_btn, min_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.year_up_btn, year_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.year_down_btn, year_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.mon_up_btn, mon_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.mon_down_btn, mon_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.day_up_btn, day_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.day_down_btn, day_down_cb, LV_EVENT_CLICKED, NULL);
}

static void init_default_time(void)
{
    struct tm timeinfo = {
        .tm_year = 2026 - 1900,
        .tm_mon = 4 - 1,
        .tm_mday = 16,
        .tm_hour = 21,
        .tm_min = 0,
        .tm_sec = 0
    };
    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

/* ============================================================================
 * Task & Initialization (任务和初始化)
 * ============================================================================ */

static void ui_task(void *arg)
{
    create_ui_layout();
    bind_events();

    while (1) {
        update_display();
        lvgl_port_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t app_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI");

    ESP_ERROR_CHECK(lvgl_port_init());

    init_default_time();

    BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
