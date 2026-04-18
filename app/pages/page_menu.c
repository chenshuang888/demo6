#include "page_menu.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ble_driver.h"
#include "lcd_panel.h"

/* ============================================================================
 * 配色（与时间页一致）
 * ========================================================================= */

#define COLOR_BG         0x1E1B2E
#define COLOR_CARD       0x2D2640
#define COLOR_CARD_ALT   0x3A3354
#define COLOR_ACCENT     0x06B6D4
#define COLOR_TEXT       0xF1ECFF
#define COLOR_MUTED      0x9B94B5
#define COLOR_SUCCESS    0x10B981
#define COLOR_OFFLINE    0x6B7280

static const char *TAG = "page_menu";

/* ============================================================================
 * UI 元素
 * ========================================================================= */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *bt_item;
    lv_obj_t *bl_item;
    lv_obj_t *about_item;
    lv_obj_t *exit_item;

    lv_obj_t *bt_status_lbl;   /* 蓝牙状态文字: "已连接"/"未连接" */
    lv_obj_t *bl_value_lbl;    /* 背光亮度文字: "50%" */

    lv_style_t style_card;
    lv_style_t style_item;         /* 列表项默认 */
    lv_style_t style_item_pressed; /* 列表项按下 */
    lv_style_t style_topbtn;       /* 返回按钮默认 */
    lv_style_t style_topbtn_pressed;
} page_menu_ui_t;

static page_menu_ui_t s_ui = {0};

/* 背光四档: 25% / 50% / 75% / 100% */
static const uint8_t BACKLIGHT_STEPS[] = {64, 128, 192, 255};

/* ============================================================================
 * CSS - 样式
 * ========================================================================= */

static void init_styles(void)
{
    /* 列表外层卡片 */
    lv_style_init(&s_ui.style_card);
    lv_style_set_bg_color(&s_ui.style_card, lv_color_hex(COLOR_CARD));
    lv_style_set_bg_opa(&s_ui.style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_card, 12);
    lv_style_set_border_width(&s_ui.style_card, 0);
    lv_style_set_pad_all(&s_ui.style_card, 0);
    lv_style_set_shadow_width(&s_ui.style_card, 0);

    /* 列表项 */
    lv_style_init(&s_ui.style_item);
    lv_style_set_bg_opa(&s_ui.style_item, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_item, 1);
    lv_style_set_border_side(&s_ui.style_item, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_ui.style_item, lv_color_hex(COLOR_CARD_ALT));
    lv_style_set_radius(&s_ui.style_item, 0);
    lv_style_set_shadow_width(&s_ui.style_item, 0);
    lv_style_set_pad_all(&s_ui.style_item, 0);
    lv_style_set_text_color(&s_ui.style_item, lv_color_hex(COLOR_TEXT));
    lv_style_set_text_font(&s_ui.style_item, &lv_font_montserrat_14);

    lv_style_init(&s_ui.style_item_pressed);
    lv_style_set_bg_color(&s_ui.style_item_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_item_pressed, LV_OPA_20);

    /* 顶部返回按钮 */
    lv_style_init(&s_ui.style_topbtn);
    lv_style_set_bg_opa(&s_ui.style_topbtn, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_topbtn, 0);
    lv_style_set_shadow_width(&s_ui.style_topbtn, 0);
    lv_style_set_text_color(&s_ui.style_topbtn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_pad_all(&s_ui.style_topbtn, 4);

    lv_style_init(&s_ui.style_topbtn_pressed);
    lv_style_set_bg_color(&s_ui.style_topbtn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_topbtn_pressed, LV_OPA_20);
}

/* ============================================================================
 * HTML - 布局
 * ========================================================================= */

/**
 * 创建一个列表项. 样式:
 *   [图标]  [主文字]                 [右侧值/箭头]
 */
static lv_obj_t *create_list_item(lv_obj_t *parent,
                                  const char *icon,
                                  const char *text,
                                  lv_obj_t **out_value_label,
                                  const char *value_init,
                                  uint32_t value_color,
                                  bool last)
{
    lv_obj_t *item = lv_btn_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_add_style(item, &s_ui.style_item, 0);
    lv_obj_add_style(item, &s_ui.style_item_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(item, lv_pct(100), 50);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    if (last) {
        /* 最后一项不画底部分割线 */
        lv_obj_set_style_border_width(item, 0, 0);
    }

    /* 左图标 */
    lv_obj_t *icon_lbl = lv_label_create(item);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    /* 主文字 */
    lv_obj_t *text_lbl = lv_label_create(item);
    lv_label_set_text(text_lbl, text);
    lv_obj_set_style_text_color(text_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(text_lbl, LV_ALIGN_LEFT_MID, 48, 0);

    /* 右侧值 */
    if (value_init) {
        lv_obj_t *val = lv_label_create(item);
        lv_label_set_text(val, value_init);
        lv_obj_set_style_text_color(val, lv_color_hex(value_color), 0);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -14, 0);
        if (out_value_label) *out_value_label = val;
    } else if (out_value_label == NULL) {
        /* 无动态值时显示 > 箭头占位 */
        lv_obj_t *arrow = lv_label_create(item);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_14, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
    }

    return item;
}

static void create_top_bar(void)
{
    /* 返回按钮: 箭头 + "菜单"标题 */
    s_ui.back_btn = lv_btn_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.back_btn);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn, 0);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.back_btn, 6, 0);
    lv_obj_set_size(s_ui.back_btn, 80, 30);
    lv_obj_align(s_ui.back_btn, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *arrow = lv_label_create(s_ui.back_btn);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT " Menu");
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_14, 0);
    lv_obj_center(arrow);
}

static void create_menu_list(void)
{
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_ui.style_card, 0);
    lv_obj_set_size(card, 220, 204);  /* 4 × 50 + 边距 */
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 用 flex 纵向排列列表项 */
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 0, 0);

    s_ui.bt_item = create_list_item(card,
        LV_SYMBOL_BLUETOOTH, "Bluetooth",
        &s_ui.bt_status_lbl, "Off", COLOR_OFFLINE, false);

    s_ui.bl_item = create_list_item(card,
        LV_SYMBOL_EYE_OPEN, "Backlight",
        &s_ui.bl_value_lbl, "50%", COLOR_ACCENT, false);

    s_ui.about_item = create_list_item(card,
        LV_SYMBOL_LIST, "About",
        NULL, NULL, 0, false);

    s_ui.exit_item = create_list_item(card,
        LV_SYMBOL_HOME, "Back to Clock",
        NULL, NULL, 0, true);
}

/* ============================================================================
 * 业务逻辑
 * ========================================================================= */

static void update_bt_status(void)
{
    if (!s_ui.bt_status_lbl) return;
    bool connected = ble_driver_is_connected();
    lv_label_set_text(s_ui.bt_status_lbl, connected ? "Connected" : "Off");
    lv_obj_set_style_text_color(s_ui.bt_status_lbl,
        lv_color_hex(connected ? COLOR_SUCCESS : COLOR_OFFLINE), 0);
}

static void update_backlight_label(void)
{
    if (!s_ui.bl_value_lbl) return;
    uint8_t duty = lcd_panel_get_backlight();
    int pct = (duty * 100 + 127) / 255;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_ui.bl_value_lbl, buf);
}

/* ============================================================================
 * 事件回调
 * ========================================================================= */

static void on_back_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_TIME);
}

static void on_backlight_clicked(lv_event_t *e)
{
    uint8_t cur = lcd_panel_get_backlight();
    /* 找当前档位, 切换到下一档 */
    int idx = 0;
    for (int i = 0; i < (int)(sizeof(BACKLIGHT_STEPS) / sizeof(BACKLIGHT_STEPS[0])); i++) {
        if (cur <= BACKLIGHT_STEPS[i]) { idx = i; break; }
        idx = i;
    }
    int next = (idx + 1) % (sizeof(BACKLIGHT_STEPS) / sizeof(BACKLIGHT_STEPS[0]));
    lcd_panel_set_backlight(BACKLIGHT_STEPS[next]);
    update_backlight_label();
    ESP_LOGI(TAG, "Backlight -> %d", BACKLIGHT_STEPS[next]);
}

static void on_about_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_ABOUT);
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.back_btn,   on_back_clicked,     LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.bl_item,    on_backlight_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.about_item, on_about_clicked,    LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.exit_item,  on_back_clicked,     LV_EVENT_CLICKED, NULL);
}

/* ============================================================================
 * 页面生命周期
 * ========================================================================= */

static void page_init(void)
{
    init_styles();
    create_top_bar();
    create_menu_list();
    bind_events();

    update_bt_status();
    update_backlight_label();
}

static lv_obj_t *page_menu_create(void)
{
    ESP_LOGI(TAG, "Creating menu page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    page_init();
    return s_ui.screen;
}

static void page_menu_destroy(void)
{
    ESP_LOGI(TAG, "Destroying menu page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_item);
    lv_style_reset(&s_ui.style_item_pressed);
    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);

    s_ui.back_btn = NULL;
    s_ui.bt_item = s_ui.bl_item = s_ui.about_item = s_ui.exit_item = NULL;
    s_ui.bt_status_lbl = NULL;
    s_ui.bl_value_lbl = NULL;
}

static void page_menu_update(void)
{
    /* 周期性刷新 BLE 状态, 让用户看到实时变化 */
    update_bt_status();
}

static const page_callbacks_t s_callbacks = {
    .create  = page_menu_create,
    .destroy = page_menu_destroy,
    .update  = page_menu_update,
};

const page_callbacks_t *page_menu_get_callbacks(void)
{
    return &s_callbacks;
}
