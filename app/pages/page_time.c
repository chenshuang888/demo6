#include "page_time.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

static const char *TAG = "page_time";

/* ============================================================================
 * UI Elements
 * ============================================================================ */

typedef struct {
    lv_obj_t *screen;
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
    lv_obj_t *menu_btn;

    // 样式对象
    lv_style_t style_card;
    lv_style_t style_time_label;
    lv_style_t style_date_label;
    lv_style_t style_btn;
    lv_style_t style_btn_pressed;
} page_time_ui_t;

static page_time_ui_t s_ui = {0};

/* ============================================================================
 * CSS - 样式定义
 * ============================================================================ */

static void init_styles(void)
{
    lv_style_init(&s_ui.style_card);
    lv_style_set_bg_color(&s_ui.style_card, lv_color_hex(0x2C3E50));
    lv_style_set_radius(&s_ui.style_card, 15);
    lv_style_set_border_width(&s_ui.style_card, 0);
    lv_style_set_pad_all(&s_ui.style_card, 15);
    lv_style_set_shadow_width(&s_ui.style_card, 10);
    lv_style_set_shadow_color(&s_ui.style_card, lv_color_hex(0x000000));
    lv_style_set_shadow_ofs_y(&s_ui.style_card, 3);

    lv_style_init(&s_ui.style_time_label);
    lv_style_set_text_color(&s_ui.style_time_label, lv_color_hex(0xECF0F1));
    lv_style_set_text_font(&s_ui.style_time_label, &lv_font_montserrat_24);

    lv_style_init(&s_ui.style_date_label);
    lv_style_set_text_color(&s_ui.style_date_label, lv_color_hex(0xBDC3C7));
    lv_style_set_text_font(&s_ui.style_date_label, &lv_font_montserrat_20);

    lv_style_init(&s_ui.style_btn);
    lv_style_set_bg_color(&s_ui.style_btn, lv_color_hex(0x3498DB));
    lv_style_set_radius(&s_ui.style_btn, 10);
    lv_style_set_border_width(&s_ui.style_btn, 0);
    lv_style_set_shadow_width(&s_ui.style_btn, 5);
    lv_style_set_shadow_color(&s_ui.style_btn, lv_color_hex(0x000000));
    lv_style_set_shadow_ofs_y(&s_ui.style_btn, 2);
    lv_style_set_text_color(&s_ui.style_btn, lv_color_hex(0xFFFFFF));

    lv_style_init(&s_ui.style_btn_pressed);
    lv_style_set_bg_color(&s_ui.style_btn_pressed, lv_color_hex(0x2980B9));
    lv_style_set_shadow_width(&s_ui.style_btn_pressed, 2);
    lv_style_set_shadow_ofs_y(&s_ui.style_btn_pressed, 1);
}

/* ============================================================================
 * HTML - 页面结构
 * ============================================================================ */

static lv_obj_t *create_card(lv_obj_t *parent, int y_offset, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 220, height);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    return card;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 50, 40);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_style(btn, &s_ui.style_btn, 0);
    lv_obj_add_style(btn, &s_ui.style_btn_pressed, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

static void create_time_card(void)
{
    lv_obj_t *time_card = create_card(s_ui.screen, 10, 140);

    lv_obj_t *time_title = lv_label_create(time_card);
    lv_label_set_text(time_title, "TIME");
    lv_obj_set_style_text_color(time_title, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(time_title, &lv_font_montserrat_14, 0);
    lv_obj_align(time_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ui.time_label = lv_label_create(time_card);
    lv_label_set_text(s_ui.time_label, "00:00:00");
    lv_obj_add_style(s_ui.time_label, &s_ui.style_time_label, 0);
    lv_obj_align(s_ui.time_label, LV_ALIGN_TOP_MID, 0, 25);

    s_ui.hour_up_btn = create_button(time_card, "+", 30, 75);
    s_ui.hour_down_btn = create_button(time_card, "-", 30, 75 + 45);
    s_ui.min_up_btn = create_button(time_card, "+", 140, 75);
    s_ui.min_down_btn = create_button(time_card, "-", 140, 75 + 45);

    lv_obj_t *hour_hint = lv_label_create(time_card);
    lv_label_set_text(hour_hint, "Hour");
    lv_obj_set_style_text_color(hour_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(hour_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(hour_hint, s_ui.hour_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *min_hint = lv_label_create(time_card);
    lv_label_set_text(min_hint, "Min");
    lv_obj_set_style_text_color(min_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(min_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(min_hint, s_ui.min_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);
}

static void create_date_card(void)
{
    lv_obj_t *date_card = create_card(s_ui.screen, 160, 150);

    lv_obj_t *date_title = lv_label_create(date_card);
    lv_label_set_text(date_title, "DATE");
    lv_obj_set_style_text_color(date_title, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(date_title, &lv_font_montserrat_14, 0);
    lv_obj_align(date_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ui.date_label = lv_label_create(date_card);
    lv_label_set_text(s_ui.date_label, "2026-04-17");
    lv_obj_add_style(s_ui.date_label, &s_ui.style_date_label, 0);
    lv_obj_align(s_ui.date_label, LV_ALIGN_TOP_MID, 0, 25);

    s_ui.year_up_btn = create_button(date_card, "+", 10, 65);
    s_ui.year_down_btn = create_button(date_card, "-", 10, 65 + 45);
    s_ui.mon_up_btn = create_button(date_card, "+", 85, 65);
    s_ui.mon_down_btn = create_button(date_card, "-", 85, 65 + 45);
    s_ui.day_up_btn = create_button(date_card, "+", 160, 65);
    s_ui.day_down_btn = create_button(date_card, "-", 160, 65 + 45);

    lv_obj_t *year_hint = lv_label_create(date_card);
    lv_label_set_text(year_hint, "Year");
    lv_obj_set_style_text_color(year_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(year_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(year_hint, s_ui.year_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *mon_hint = lv_label_create(date_card);
    lv_label_set_text(mon_hint, "Mon");
    lv_obj_set_style_text_color(mon_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(mon_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(mon_hint, s_ui.mon_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);

    lv_obj_t *day_hint = lv_label_create(date_card);
    lv_label_set_text(day_hint, "Day");
    lv_obj_set_style_text_color(day_hint, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(day_hint, &lv_font_montserrat_14, 0);
    lv_obj_align_to(day_hint, s_ui.day_up_btn, LV_ALIGN_OUT_TOP_MID, 0, -5);
}

static void create_menu_button(void)
{
    s_ui.menu_btn = lv_btn_create(s_ui.screen);
    lv_obj_set_size(s_ui.menu_btn, 60, 30);
    lv_obj_align(s_ui.menu_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_style(s_ui.menu_btn, &s_ui.style_btn, 0);
    lv_obj_add_style(s_ui.menu_btn, &s_ui.style_btn_pressed, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(s_ui.menu_btn);
    lv_label_set_text(label, "Menu");
    lv_obj_center(label);
}

/* ============================================================================
 * Business Logic
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

/* ============================================================================
 * JavaScript - 事件响应
 * ============================================================================ */

static void on_hour_up_clicked(lv_event_t *e)
{
    adjust_time(1, 0, 0);
}

static void on_hour_down_clicked(lv_event_t *e)
{
    adjust_time(-1, 0, 0);
}

static void on_min_up_clicked(lv_event_t *e)
{
    adjust_time(0, 1, 0);
}

static void on_min_down_clicked(lv_event_t *e)
{
    adjust_time(0, -1, 0);
}

static void on_year_up_clicked(lv_event_t *e)
{
    adjust_date(1, 0, 0);
}

static void on_year_down_clicked(lv_event_t *e)
{
    adjust_date(-1, 0, 0);
}

static void on_mon_up_clicked(lv_event_t *e)
{
    adjust_date(0, 1, 0);
}

static void on_mon_down_clicked(lv_event_t *e)
{
    adjust_date(0, -1, 0);
}

static void on_day_up_clicked(lv_event_t *e)
{
    adjust_date(0, 0, 1);
}

static void on_day_down_clicked(lv_event_t *e)
{
    adjust_date(0, 0, -1);
}

static void on_menu_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_MENU);
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.hour_up_btn, on_hour_up_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.hour_down_btn, on_hour_down_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.min_up_btn, on_min_up_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.min_down_btn, on_min_down_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.year_up_btn, on_year_up_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.year_down_btn, on_year_down_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.mon_up_btn, on_mon_up_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.mon_down_btn, on_mon_down_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.day_up_btn, on_day_up_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.day_down_btn, on_day_down_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.menu_btn, on_menu_clicked, LV_EVENT_CLICKED, NULL);
}

/* ============================================================================
 * JavaScript - 定时更新
 * ============================================================================ */

static void update_display(void)
{
    if (!s_ui.time_label || !s_ui.date_label) {
        return;
    }

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_label_set_text(s_ui.time_label, time_buf);

    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    lv_label_set_text(s_ui.date_label, date_buf);
}

/* ============================================================================
 * 页面初始化入口
 * ============================================================================ */

static void page_init(void)
{
    init_styles();
    create_time_card();
    create_date_card();
    create_menu_button();
    bind_events();
}

/* ============================================================================
 * Page Lifecycle
 * ============================================================================ */

static lv_obj_t *page_time_create(void)
{
    ESP_LOGI(TAG, "Creating time page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(0x1A1A2E), 0);

    page_init();

    return s_ui.screen;
}

static void page_time_destroy(void)
{
    ESP_LOGI(TAG, "Destroying time page");

    // 删除 screen 会自动删除所有子对象，并解除 style 引用
    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    // 重置所有 style 对象，清理 LVGL 内部状态
    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_time_label);
    lv_style_reset(&s_ui.style_date_label);
    lv_style_reset(&s_ui.style_btn);
    lv_style_reset(&s_ui.style_btn_pressed);

    // 清零 UI 对象指针（不包括 style 对象）
    s_ui.time_label = NULL;
    s_ui.date_label = NULL;
    s_ui.hour_up_btn = NULL;
    s_ui.hour_down_btn = NULL;
    s_ui.min_up_btn = NULL;
    s_ui.min_down_btn = NULL;
    s_ui.year_up_btn = NULL;
    s_ui.year_down_btn = NULL;
    s_ui.mon_up_btn = NULL;
    s_ui.mon_down_btn = NULL;
    s_ui.day_up_btn = NULL;
    s_ui.day_down_btn = NULL;
    s_ui.menu_btn = NULL;
}

static void page_time_update(void)
{
    update_display();
}

static const page_callbacks_t s_callbacks = {
    .create = page_time_create,
    .destroy = page_time_destroy,
    .update = page_time_update,
};

const page_callbacks_t *page_time_get_callbacks(void)
{
    return &s_callbacks;
}
