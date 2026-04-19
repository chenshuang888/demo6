#include "page_time.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app_fonts.h"
#include <time.h>
#include <sys/time.h>

/* ============================================================================
 * 配色（深紫 + 青绿）
 * ========================================================================= */

#define COLOR_BG         0x1E1B2E
#define COLOR_CARD       0x2D2640
#define COLOR_CARD_ALT   0x3A3354
#define COLOR_ACCENT     0x06B6D4
#define COLOR_ACCENT_H   0x0891B2
#define COLOR_TEXT       0xF1ECFF
#define COLOR_MUTED      0x9B94B5

static const char *TAG = "page_time";

/* ============================================================================
 * UI 元素
 * ========================================================================= */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *menu_btn;

    lv_obj_t *hour_up, *hour_dn;
    lv_obj_t *min_up, *min_dn;
    lv_obj_t *year_up, *year_dn;
    lv_obj_t *mon_up, *mon_dn;
    lv_obj_t *day_up, *day_dn;

    lv_style_t style_card;
    lv_style_t style_btn;
    lv_style_t style_btn_pressed;
    lv_style_t style_menu_btn;
    lv_style_t style_menu_btn_pressed;
} page_time_ui_t;

static page_time_ui_t s_ui = {0};

static const char *WEEKDAY_EN[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* ============================================================================
 * CSS - 样式
 * ========================================================================= */

static void init_styles(void)
{
    lv_style_init(&s_ui.style_card);
    lv_style_set_bg_color(&s_ui.style_card, lv_color_hex(COLOR_CARD));
    lv_style_set_bg_opa(&s_ui.style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_card, 12);
    lv_style_set_border_width(&s_ui.style_card, 0);
    lv_style_set_pad_all(&s_ui.style_card, 12);
    lv_style_set_shadow_width(&s_ui.style_card, 0);

    lv_style_init(&s_ui.style_btn);
    lv_style_set_bg_color(&s_ui.style_btn, lv_color_hex(COLOR_CARD_ALT));
    lv_style_set_bg_opa(&s_ui.style_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_btn, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_ui.style_btn, 0);
    lv_style_set_shadow_width(&s_ui.style_btn, 0);
    lv_style_set_text_color(&s_ui.style_btn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_text_font(&s_ui.style_btn, APP_FONT_TITLE);
    lv_style_set_pad_all(&s_ui.style_btn, 0);

    lv_style_init(&s_ui.style_btn_pressed);
    lv_style_set_bg_color(&s_ui.style_btn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_text_color(&s_ui.style_btn_pressed, lv_color_hex(COLOR_TEXT));

    lv_style_init(&s_ui.style_menu_btn);
    lv_style_set_bg_opa(&s_ui.style_menu_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_menu_btn, 0);
    lv_style_set_shadow_width(&s_ui.style_menu_btn, 0);
    lv_style_set_text_color(&s_ui.style_menu_btn, lv_color_hex(COLOR_MUTED));

    lv_style_init(&s_ui.style_menu_btn_pressed);
    lv_style_set_bg_color(&s_ui.style_menu_btn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_menu_btn_pressed, LV_OPA_20);
    lv_style_set_text_color(&s_ui.style_menu_btn_pressed, lv_color_hex(COLOR_ACCENT));
}

/* ============================================================================
 * HTML - 布局
 * ========================================================================= */

static lv_obj_t *create_card(lv_obj_t *parent, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *create_round_btn(lv_obj_t *parent, const char *symbol, int size)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &s_ui.style_btn, 0);
    lv_obj_add_style(btn, &s_ui.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, size, size);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_small_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, APP_FONT_TEXT, 0);
    return lbl;
}

static void create_display_card(void)
{
    lv_obj_t *card = create_card(s_ui.screen, 10, 220, 100);

    s_ui.date_label = create_small_label(card, "--", COLOR_MUTED);
    lv_obj_align(s_ui.date_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ui.menu_btn = lv_btn_create(card);
    lv_obj_remove_style_all(s_ui.menu_btn);
    lv_obj_add_style(s_ui.menu_btn, &s_ui.style_menu_btn, 0);
    lv_obj_add_style(s_ui.menu_btn, &s_ui.style_menu_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_ui.menu_btn, 34, 28);
    lv_obj_set_style_radius(s_ui.menu_btn, 6, 0);
    lv_obj_align(s_ui.menu_btn, LV_ALIGN_TOP_RIGHT, 0, -4);

    lv_obj_t *menu_icon = lv_label_create(s_ui.menu_btn);
    lv_label_set_text(menu_icon, LV_SYMBOL_LIST);
    lv_obj_center(menu_icon);

    s_ui.time_label = lv_label_create(card);
    lv_label_set_text(s_ui.time_label, "--:--:--");
    lv_obj_set_style_text_color(s_ui.time_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(s_ui.time_label, APP_FONT_LARGE, 0);
    lv_obj_align(s_ui.time_label, LV_ALIGN_CENTER, 0, 12);
}

static void create_time_adjust_card(void)
{
    lv_obj_t *card = create_card(s_ui.screen, 120, 220, 80);

    lv_obj_t *title = create_small_label(card, "TIME", COLOR_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 两组按钮: 组宽 66, 间距 64, 布满 196 内宽 */
    const int btn = 32;
    const int group_w = btn * 2 + 2;   /* 66 */
    const int gap = 196 - group_w * 2; /* 64 */

    /* Hour group */
    lv_obj_t *h_lbl = create_small_label(card, "Hour", COLOR_TEXT);
    lv_obj_align(h_lbl, LV_ALIGN_TOP_LEFT, group_w / 2 - 14, 22);

    s_ui.hour_dn = create_round_btn(card, "-", btn);
    lv_obj_align(s_ui.hour_dn, LV_ALIGN_TOP_LEFT, 0, 42);

    s_ui.hour_up = create_round_btn(card, "+", btn);
    lv_obj_align(s_ui.hour_up, LV_ALIGN_TOP_LEFT, btn + 2, 42);

    /* Min group */
    int min_x = group_w + gap;
    lv_obj_t *m_lbl = create_small_label(card, "Min", COLOR_TEXT);
    lv_obj_align(m_lbl, LV_ALIGN_TOP_LEFT, min_x + group_w / 2 - 11, 22);

    s_ui.min_dn = create_round_btn(card, "-", btn);
    lv_obj_align(s_ui.min_dn, LV_ALIGN_TOP_LEFT, min_x, 42);

    s_ui.min_up = create_round_btn(card, "+", btn);
    lv_obj_align(s_ui.min_up, LV_ALIGN_TOP_LEFT, min_x + btn + 2, 42);
}

static void create_date_adjust_card(void)
{
    lv_obj_t *card = create_card(s_ui.screen, 210, 220, 90);

    lv_obj_t *title = create_small_label(card, "DATE", COLOR_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 三组: 按钮 22, 组宽 46, 间距 29 */
    const int btn = 22;
    const int group_w = btn * 2 + 2;              /* 46 */
    const int gap = (196 - group_w * 3) / 2;      /* 29 */

    struct {
        const char *label;
        int label_half_w;   /* 粗略文字半宽, 用于居中偏移 */
        lv_obj_t **dn;
        lv_obj_t **up;
    } groups[] = {
        {"Year",  14, &s_ui.year_dn, &s_ui.year_up},
        {"Month", 17, &s_ui.mon_dn,  &s_ui.mon_up},
        {"Day",   11, &s_ui.day_dn,  &s_ui.day_up},
    };

    for (int i = 0; i < 3; i++) {
        int x = i * (group_w + gap);

        lv_obj_t *lbl = create_small_label(card, groups[i].label, COLOR_TEXT);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x + group_w / 2 - groups[i].label_half_w, 22);

        *groups[i].dn = create_round_btn(card, "-", btn);
        lv_obj_align(*groups[i].dn, LV_ALIGN_TOP_LEFT, x, 48);

        *groups[i].up = create_round_btn(card, "+", btn);
        lv_obj_align(*groups[i].up, LV_ALIGN_TOP_LEFT, x + btn + 2, 48);
    }
}

/* ============================================================================
 * 业务逻辑 - 时间调整
 * ========================================================================= */

static void adjust_time(int hour_d, int min_d, int sec_d)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_hour += hour_d;
    timeinfo.tm_min  += min_d;
    timeinfo.tm_sec  += sec_d;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

static void adjust_date(int year_d, int mon_d, int day_d)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_year += year_d;
    timeinfo.tm_mon  += mon_d;
    timeinfo.tm_mday += day_d;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

/* ============================================================================
 * 事件回调
 * ========================================================================= */

static void on_hour_up(lv_event_t *e)  { adjust_time(1, 0, 0); }
static void on_hour_dn(lv_event_t *e)  { adjust_time(-1, 0, 0); }
static void on_min_up(lv_event_t *e)   { adjust_time(0, 1, 0); }
static void on_min_dn(lv_event_t *e)   { adjust_time(0, -1, 0); }
static void on_year_up(lv_event_t *e)  { adjust_date(1, 0, 0); }
static void on_year_dn(lv_event_t *e)  { adjust_date(-1, 0, 0); }
static void on_mon_up(lv_event_t *e)   { adjust_date(0, 1, 0); }
static void on_mon_dn(lv_event_t *e)   { adjust_date(0, -1, 0); }
static void on_day_up(lv_event_t *e)   { adjust_date(0, 0, 1); }
static void on_day_dn(lv_event_t *e)   { adjust_date(0, 0, -1); }

static void on_menu_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_MENU);
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.hour_up, on_hour_up, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.hour_dn, on_hour_dn, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.min_up,  on_min_up,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.min_dn,  on_min_dn,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.year_up, on_year_up, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.year_dn, on_year_dn, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.mon_up,  on_mon_up,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.mon_dn,  on_mon_dn,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.day_up,  on_day_up,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.day_dn,  on_day_dn,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.menu_btn, on_menu_clicked, LV_EVENT_CLICKED, NULL);
}

/* ============================================================================
 * 显示刷新
 * ========================================================================= */

static void update_display(void)
{
    if (!s_ui.time_label || !s_ui.date_label) return;

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             t.tm_hour, t.tm_min, t.tm_sec);
    lv_label_set_text(s_ui.time_label, time_buf);

    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d  %s",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             WEEKDAY_EN[t.tm_wday % 7]);
    lv_label_set_text(s_ui.date_label, date_buf);
}

/* ============================================================================
 * 页面生命周期
 * ========================================================================= */

static void page_init(void)
{
    init_styles();
    create_display_card();
    create_time_adjust_card();
    create_date_adjust_card();
    bind_events();
}

static lv_obj_t *page_time_create(void)
{
    ESP_LOGI(TAG, "Creating time page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    page_init();
    return s_ui.screen;
}

static void page_time_destroy(void)
{
    ESP_LOGI(TAG, "Destroying time page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_btn);
    lv_style_reset(&s_ui.style_btn_pressed);
    lv_style_reset(&s_ui.style_menu_btn);
    lv_style_reset(&s_ui.style_menu_btn_pressed);

    s_ui.time_label = NULL;
    s_ui.date_label = NULL;
    s_ui.menu_btn = NULL;
    s_ui.hour_up = s_ui.hour_dn = NULL;
    s_ui.min_up  = s_ui.min_dn  = NULL;
    s_ui.year_up = s_ui.year_dn = NULL;
    s_ui.mon_up  = s_ui.mon_dn  = NULL;
    s_ui.day_up  = s_ui.day_dn  = NULL;
}

static void page_time_update(void)
{
    update_display();
}

static const page_callbacks_t s_callbacks = {
    .create  = page_time_create,
    .destroy = page_time_destroy,
    .update  = page_time_update,
};

const page_callbacks_t *page_time_get_callbacks(void)
{
    return &s_callbacks;
}
