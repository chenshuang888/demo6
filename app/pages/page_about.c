#include "page_about.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app_fonts.h"

/* ============================================================================
 * 配色（沿用项目风格: 深紫 + 青绿）
 * ========================================================================= */

#define COLOR_BG         0x1E1B2E
#define COLOR_CARD       0x2D2640
#define COLOR_CARD_ALT   0x3A3354
#define COLOR_ACCENT     0x06B6D4
#define COLOR_TEXT       0xF1ECFF
#define COLOR_MUTED      0x9B94B5

static const char *TAG = "page_about";

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;

    lv_style_t style_card;
    lv_style_t style_icon;
    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;
    lv_style_t style_row;
} page_about_ui_t;

static page_about_ui_t s_ui = {0};

/* ============================================================================
 * CSS - 样式
 * ========================================================================= */

static void init_styles(void)
{
    /* 信息卡片 */
    lv_style_init(&s_ui.style_card);
    lv_style_set_bg_color(&s_ui.style_card, lv_color_hex(COLOR_CARD));
    lv_style_set_bg_opa(&s_ui.style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_card, 12);
    lv_style_set_border_width(&s_ui.style_card, 0);
    lv_style_set_pad_all(&s_ui.style_card, 0);
    lv_style_set_shadow_width(&s_ui.style_card, 0);

    /* 圆形图标容器 */
    lv_style_init(&s_ui.style_icon);
    lv_style_set_bg_color(&s_ui.style_icon, lv_color_hex(COLOR_CARD));
    lv_style_set_bg_opa(&s_ui.style_icon, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_icon, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_ui.style_icon, 2);
    lv_style_set_border_color(&s_ui.style_icon, lv_color_hex(COLOR_ACCENT));
    lv_style_set_shadow_width(&s_ui.style_icon, 0);
    lv_style_set_pad_all(&s_ui.style_icon, 0);

    /* 返回按钮 */
    lv_style_init(&s_ui.style_topbtn);
    lv_style_set_bg_opa(&s_ui.style_topbtn, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_topbtn, 0);
    lv_style_set_shadow_width(&s_ui.style_topbtn, 0);
    lv_style_set_text_color(&s_ui.style_topbtn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_pad_all(&s_ui.style_topbtn, 4);

    lv_style_init(&s_ui.style_topbtn_pressed);
    lv_style_set_bg_color(&s_ui.style_topbtn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_topbtn_pressed, LV_OPA_20);

    /* 信息行 (Key-Value) */
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
}

static void create_icon_badge(void)
{
    /* 外圆: 青绿边框 */
    lv_obj_t *badge = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(badge);
    lv_obj_add_style(badge, &s_ui.style_icon, 0);
    lv_obj_set_size(badge, 72, 72);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    /* 中心大图标 */
    lv_obj_t *icon = lv_label_create(badge);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, APP_FONT_LARGE, 0);
    lv_obj_center(icon);
}

static void create_title_block(void)
{
    /* 主标题 */
    lv_obj_t *name = lv_label_create(s_ui.screen);
    lv_label_set_text(name, "ESP32-S3 Demo");
    lv_obj_set_style_text_color(name, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(name, APP_FONT_TITLE, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 132);

    /* 副标题 */
    lv_obj_t *sub = lv_label_create(s_ui.screen);
    lv_label_set_text(sub, "BLE Time Sync");
    lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(sub, APP_FONT_TEXT, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 160);
}

static void create_info_row(lv_obj_t *parent, const char *key,
                            const char *value, bool last)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &s_ui.style_row, 0);
    lv_obj_set_size(row, lv_pct(100), 30);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (last) {
        lv_obj_set_style_border_width(row, 0, 0);
    }

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(k, APP_FONT_TEXT, 0);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_color(v, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(v, APP_FONT_TEXT, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_info_card(void)
{
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    lv_obj_set_size(card, 220, 124);   /* 4 行 × 30 + 上下 padding */
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_style_pad_ver(card, 2, 0);

    create_info_row(card, "Version",   "v0.4",            false);
    create_info_row(card, "Device",    "ESP32-S3",        false);
    create_info_row(card, "Framework", "ESP-IDF 5.4",     false);
    create_info_row(card, "GUI",       "LVGL 9.5",        true);
}

/* ============================================================================
 * 事件回调
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
 * 页面生命周期
 * ========================================================================= */

static void page_init(void)
{
    init_styles();
    create_top_bar();
    create_icon_badge();
    create_title_block();
    create_info_card();
    bind_events();
}

static lv_obj_t *page_about_create(void)
{
    ESP_LOGI(TAG, "Creating about page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    page_init();
    return s_ui.screen;
}

static void page_about_destroy(void)
{
    ESP_LOGI(TAG, "Destroying about page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_icon);
    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);
    lv_style_reset(&s_ui.style_row);

    s_ui.back_btn = NULL;
}

static const page_callbacks_t s_callbacks = {
    .create  = page_about_create,
    .destroy = page_about_destroy,
    .update  = NULL,
};

const page_callbacks_t *page_about_get_callbacks(void)
{
    return &s_callbacks;
}
