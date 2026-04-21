#include "page_weather.h"
#include "weather_manager.h"
#include "weather_service.h"

#include "esp_log.h"
#include "lvgl.h"
#include "app_fonts.h"
#include <stdio.h>
#include <time.h>

/* ============================================================================
 * 配色（沿用项目风格：深紫 + 青绿，天气状态独立着色）
 * ========================================================================= */

#define COLOR_BG         0x1E1B2E
#define COLOR_CARD       0x2D2640
#define COLOR_CARD_ALT   0x3A3354
#define COLOR_ACCENT     0x06B6D4
#define COLOR_TEXT       0xF1ECFF
#define COLOR_MUTED      0x9B94B5

static const char *TAG = "page_weather";

/* ============================================================================
 * UI 元素
 * ========================================================================= */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *city_lbl;
    lv_obj_t *weather_lbl;
    lv_obj_t *temp_lbl;
    lv_obj_t *min_lbl;
    lv_obj_t *max_lbl;
    lv_obj_t *humidity_lbl;
    lv_obj_t *updated_lbl;

    uint32_t last_updated_at;   /* 去重：同一条数据不重复刷 UI */

    lv_style_t style_card;
    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;
    lv_style_t style_row;
} page_weather_ui_t;

static page_weather_ui_t s_ui = {0};

/* ============================================================================
 * weather_code → 颜色映射
 * ========================================================================= */

static uint32_t code_color(uint8_t c)
{
    switch (c) {
    case WEATHER_CODE_CLEAR:    return 0xFBBF24; /* 金 */
    case WEATHER_CODE_CLOUDY:   return 0x94A3B8; /* 浅灰 */
    case WEATHER_CODE_OVERCAST: return 0x6B7280; /* 灰 */
    case WEATHER_CODE_RAIN:     return 0x3B82F6; /* 蓝 */
    case WEATHER_CODE_SNOW:     return 0xBAE6FD; /* 浅青 */
    case WEATHER_CODE_FOG:      return 0xA78BFA; /* 淡紫 */
    case WEATHER_CODE_THUNDER:  return 0xF97316; /* 橙 */
    default:                    return COLOR_MUTED;
    }
}

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
    lv_style_set_pad_all(&s_ui.style_card, 0);
    lv_style_set_shadow_width(&s_ui.style_card, 0);

    lv_style_init(&s_ui.style_topbtn);
    lv_style_set_bg_opa(&s_ui.style_topbtn, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_topbtn, 0);
    lv_style_set_shadow_width(&s_ui.style_topbtn, 0);
    lv_style_set_text_color(&s_ui.style_topbtn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_pad_all(&s_ui.style_topbtn, 4);

    lv_style_init(&s_ui.style_topbtn_pressed);
    lv_style_set_bg_color(&s_ui.style_topbtn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_topbtn_pressed, LV_OPA_20);

    lv_style_init(&s_ui.style_row);
    lv_style_set_bg_opa(&s_ui.style_row, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_row, 1);
    lv_style_set_border_side(&s_ui.style_row, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_ui.style_row, lv_color_hex(COLOR_CARD_ALT));
    lv_style_set_radius(&s_ui.style_row, 0);
    lv_style_set_shadow_width(&s_ui.style_row, 0);
    lv_style_set_pad_hor(&s_ui.style_row, 14);
    lv_style_set_pad_ver(&s_ui.style_row, 0);
}

/* ============================================================================
 * HTML - 布局
 * ========================================================================= */

static void create_top_bar(void)
{
    s_ui.back_btn = lv_btn_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.back_btn);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn, 0);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.back_btn, 6, 0);
    lv_obj_set_size(s_ui.back_btn, 80, 30);
    lv_obj_align(s_ui.back_btn, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *lbl = lv_label_create(s_ui.back_btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl, APP_FONT_TEXT, 0);
    lv_obj_center(lbl);

    s_ui.city_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.city_lbl, "--");
    lv_obj_set_style_text_color(s_ui.city_lbl, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(s_ui.city_lbl, APP_FONT_TEXT, 0);
    lv_obj_align(s_ui.city_lbl, LV_ALIGN_TOP_RIGHT, -14, 18);
}

static void create_main_display(void)
{
    s_ui.weather_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.weather_lbl, "Waiting...");
    lv_obj_set_style_text_color(s_ui.weather_lbl, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(s_ui.weather_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(s_ui.weather_lbl, LV_ALIGN_TOP_MID, 0, 60);

    s_ui.temp_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.temp_lbl, "--.-");
    lv_obj_set_style_text_color(s_ui.temp_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.temp_lbl, APP_FONT_LARGE, 0);
    lv_obj_align(s_ui.temp_lbl, LV_ALIGN_TOP_MID, 0, 110);
}

static void create_minmax_card(void)
{
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    lv_obj_set_size(card, 220, 50);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *low_hint = lv_label_create(card);
    lv_label_set_text(low_hint, "Low");
    lv_obj_set_style_text_color(low_hint, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(low_hint, APP_FONT_TEXT, 0);
    lv_obj_align(low_hint, LV_ALIGN_LEFT_MID, 20, 0);

    s_ui.min_lbl = lv_label_create(card);
    lv_label_set_text(s_ui.min_lbl, "--");
    lv_obj_set_style_text_color(s_ui.min_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.min_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(s_ui.min_lbl, LV_ALIGN_LEFT_MID, 55, 0);

    lv_obj_t *sep = lv_obj_create(card);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, 30);
    lv_obj_set_style_bg_color(sep, lv_color_hex(COLOR_CARD_ALT), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_center(sep);

    lv_obj_t *high_hint = lv_label_create(card);
    lv_label_set_text(high_hint, "High");
    lv_obj_set_style_text_color(high_hint, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(high_hint, APP_FONT_TEXT, 0);
    lv_obj_align(high_hint, LV_ALIGN_LEFT_MID, 120, 0);

    s_ui.max_lbl = lv_label_create(card);
    lv_label_set_text(s_ui.max_lbl, "--");
    lv_obj_set_style_text_color(s_ui.max_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.max_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(s_ui.max_lbl, LV_ALIGN_LEFT_MID, 160, 0);
}

static void create_info_card(void)
{
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    lv_obj_set_size(card, 220, 70);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 235);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_style_pad_ver(card, 2, 0);

    /* 湿度行 */
    lv_obj_t *row1 = lv_obj_create(card);
    lv_obj_remove_style_all(row1);
    lv_obj_add_style(row1, &s_ui.style_row, 0);
    lv_obj_set_size(row1, lv_pct(100), 30);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k1 = lv_label_create(row1);
    lv_label_set_text(k1, "Humidity");
    lv_obj_set_style_text_color(k1, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(k1, APP_FONT_TEXT, 0);
    lv_obj_align(k1, LV_ALIGN_LEFT_MID, 0, 0);

    s_ui.humidity_lbl = lv_label_create(row1);
    lv_label_set_text(s_ui.humidity_lbl, "--%");
    lv_obj_set_style_text_color(s_ui.humidity_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.humidity_lbl, APP_FONT_TEXT, 0);
    lv_obj_align(s_ui.humidity_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 更新时间行 */
    lv_obj_t *row2 = lv_obj_create(card);
    lv_obj_remove_style_all(row2);
    lv_obj_add_style(row2, &s_ui.style_row, 0);
    lv_obj_set_size(row2, lv_pct(100), 30);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k2 = lv_label_create(row2);
    lv_label_set_text(k2, "Updated");
    lv_obj_set_style_text_color(k2, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(k2, APP_FONT_TEXT, 0);
    lv_obj_align(k2, LV_ALIGN_LEFT_MID, 0, 0);

    s_ui.updated_lbl = lv_label_create(row2);
    lv_label_set_text(s_ui.updated_lbl, "--:--");
    lv_obj_set_style_text_color(s_ui.updated_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.updated_lbl, APP_FONT_TEXT, 0);
    lv_obj_align(s_ui.updated_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ============================================================================
 * 事件
 * ========================================================================= */

static void on_back_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_MENU);
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
}

/* ============================================================================
 * 显示刷新
 * ========================================================================= */

static void update_display(void)
{
    const weather_payload_t *w = weather_manager_get_latest();
    if (!w) {
        return;
    }

    /* 同一条数据不重复刷 UI，避免闪烁 */
    if (w->updated_at == s_ui.last_updated_at) {
        return;
    }
    s_ui.last_updated_at = w->updated_at;

    lv_label_set_text(s_ui.city_lbl, w->city);

    lv_label_set_text(s_ui.weather_lbl, w->description);
    lv_obj_set_style_text_color(s_ui.weather_lbl,
        lv_color_hex(code_color(w->weather_code)), 0);

    char buf[16];
    int t = w->temp_c_x10;
    int ti = t / 10;
    int tf = (t < 0 ? -t : t) % 10;
    snprintf(buf, sizeof(buf), "%d.%d°", ti, tf);
    lv_label_set_text(s_ui.temp_lbl, buf);

    snprintf(buf, sizeof(buf), "%d°", w->temp_min_x10 / 10);
    lv_label_set_text(s_ui.min_lbl, buf);

    snprintf(buf, sizeof(buf), "%d°", w->temp_max_x10 / 10);
    lv_label_set_text(s_ui.max_lbl, buf);

    snprintf(buf, sizeof(buf), "%d%%", w->humidity);
    lv_label_set_text(s_ui.humidity_lbl, buf);

    /* PC 推送的 updated_at 是 UTC unix timestamp，按本地时区显示 */
    time_t ts = (time_t)w->updated_at;
    struct tm tm;
    localtime_r(&ts, &tm);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(s_ui.updated_lbl, buf);
}

/* ============================================================================
 * 页面生命周期
 * ========================================================================= */

static void page_init(void)
{
    init_styles();
    create_top_bar();
    create_main_display();
    create_minmax_card();
    create_info_card();
    bind_events();

    s_ui.last_updated_at = 0;  /* 强制首次刷新 */
    update_display();
}

static lv_obj_t *page_weather_create(void)
{
    ESP_LOGI(TAG, "Creating weather page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    page_init();

    /* 进入页面立刻向 PC 请求最新天气；未连接/未订阅时静默失败，
     * UI 继续展示 weather_manager 里的旧快照。PC 端 10 分钟缓存防 API 爆量。 */
    weather_service_send_request();

    return s_ui.screen;
}

static void page_weather_destroy(void)
{
    ESP_LOGI(TAG, "Destroying weather page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);
    lv_style_reset(&s_ui.style_row);

    s_ui.back_btn = NULL;
    s_ui.city_lbl = NULL;
    s_ui.weather_lbl = NULL;
    s_ui.temp_lbl = NULL;
    s_ui.min_lbl = NULL;
    s_ui.max_lbl = NULL;
    s_ui.humidity_lbl = NULL;
    s_ui.updated_lbl = NULL;
}

static void page_weather_update(void)
{
    update_display();
}

static const page_callbacks_t s_callbacks = {
    .create  = page_weather_create,
    .destroy = page_weather_destroy,
    .update  = page_weather_update,
};

const page_callbacks_t *page_weather_get_callbacks(void)
{
    return &s_callbacks;
}
