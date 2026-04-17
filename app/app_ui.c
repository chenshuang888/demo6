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

static lv_style_t style_card;
static lv_style_t style_time_label;
static lv_style_t style_date_label;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;

static void init_styles(void)
{
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x2C3E50));
    lv_style_set_radius(&style_card, 15);
    lv_style_set_border_width(&style_card, 0);
    lv_style_set_pad_all(&style_card, 15);
    lv_style_set_shadow_width(&style_card, 10);
    lv_style_set_shadow_color(&style_card, lv_color_hex(0x000000));
    lv_style_set_shadow_ofs_y(&style_card, 3);

    lv_style_init(&style_time_label);
    lv_style_set_text_color(&style_time_label, lv_color_hex(0xECF0F1));
    lv_style_set_text_font(&style_time_label, &lv_font_montserrat_24);

    lv_style_init(&style_date_label);
    lv_style_set_text_color(&style_date_label, lv_color_hex(0xBDC3C7));
    lv_style_set_text_font(&style_date_label, &lv_font_montserrat_20);

    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x3498DB));
    lv_style_set_radius(&style_btn, 10);
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_shadow_width(&style_btn, 5);
    lv_style_set_shadow_color(&style_btn, lv_color_hex(0x000000));
    lv_style_set_shadow_ofs_y(&style_btn, 2);
    lv_style_set_text_color(&style_btn, lv_color_hex(0xFFFFFF));

    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x2980B9));
    lv_style_set_shadow_width(&style_btn_pressed, 2);
    lv_style_set_shadow_ofs_y(&style_btn_pressed, 1);
}

static lv_obj_t *create_card(int y_offset, int height)
{
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, 220, height);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_add_style(card, &style_card, 0);
    return card;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 50, 40);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

static void create_ui_layout(void)
{
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1A1A2E), 0);

    init_styles();

    lv_obj_t *time_card = create_card(10, 140);

    lv_obj_t *time_title = lv_label_create(time_card);
    lv_label_set_text(time_title, "TIME");
    lv_obj_set_style_text_color(time_title, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(time_title, &lv_font_montserrat_14, 0);
    lv_obj_align(time_title, LV_ALIGN_TOP_LEFT, 0, 0);

    ui.time_label = lv_label_create(time_card);
    lv_label_set_text(ui.time_label, "00:00:00");
    lv_obj_add_style(ui.time_label, &style_time_label, 0);
    lv_obj_align(ui.time_label, LV_ALIGN_TOP_MID, 0, 25);

    ui.hour_up_btn = create_button(time_card, "+", 30, 75);
    ui.hour_down_btn = create_button(time_card, "-", 30, 75 + 45);
    ui.min_up_btn = create_button(time_card, "+", 140, 75);
    ui.min_down_btn = create_button(time_card, "-", 140, 75 + 45);

    lv_obj_t *hour_hint = lv_label_create(time_card);
    lv_label_set_text(hour_hint, "Hour");
    lv_obj_set_style_text_color(hour_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(hour_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(hour_hint, ui.hour_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *min_hint = lv_label_create(time_card);
    lv_label_set_text(min_hint, "Min");
    lv_obj_set_style_text_color(min_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(min_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(min_hint, ui.min_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *date_card = create_card(160, 150);

    lv_obj_t *date_title = lv_label_create(date_card);
    lv_label_set_text(date_title, "DATE");
    lv_obj_set_style_text_color(date_title, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(date_title, &lv_font_montserrat_14, 0);
    lv_obj_align(date_title, LV_ALIGN_TOP_LEFT, 0, 0);

    ui.date_label = lv_label_create(date_card);
    lv_label_set_text(ui.date_label, "2026-04-16");
    lv_obj_add_style(ui.date_label, &style_date_label, 0);
    lv_obj_align(ui.date_label, LV_ALIGN_TOP_MID, 0, 25);

    ui.year_up_btn = create_button(date_card, "+", 10, 65);
    ui.year_down_btn = create_button(date_card, "-", 10, 65 + 45);
    ui.mon_up_btn = create_button(date_card, "+", 85, 65);
    ui.mon_down_btn = create_button(date_card, "-", 85, 65 + 45);
    ui.day_up_btn = create_button(date_card, "+", 160, 65);
    ui.day_down_btn = create_button(date_card, "-", 160, 65 + 45);

    lv_obj_t *year_hint = lv_label_create(date_card);
    lv_label_set_text(year_hint, "Year");
    lv_obj_set_style_text_color(year_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(year_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(year_hint, ui.year_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *mon_hint = lv_label_create(date_card);
    lv_label_set_text(mon_hint, "Mon");
    lv_obj_set_style_text_color(mon_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(mon_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(mon_hint, ui.mon_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *day_hint = lv_label_create(date_card);
    lv_label_set_text(day_hint, "Day");
    lv_obj_set_style_text_color(day_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(day_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(day_hint, ui.day_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);
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
        vTaskDelay(pdMS_TO_TICKS(10));
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
