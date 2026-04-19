#include "page_control.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app_fonts.h"
#include "control_service.h"
#include "ble_conn.h"

/* ============================================================================
 * 配色（沿用项目风格: 深紫 + 青绿）
 * ========================================================================= */

#define COLOR_BG         0x1E1B2E
#define COLOR_CARD       0x2D2640
#define COLOR_CARD_ALT   0x3A3354
#define COLOR_ACCENT     0x06B6D4
#define COLOR_TEXT       0xF1ECFF
#define COLOR_MUTED      0x9B94B5
#define COLOR_SUCCESS    0x10B981
#define COLOR_OFFLINE    0x6B7280

static const char *TAG = "page_control";

#define BTN_COUNT 4

typedef struct {
    uint8_t     id;
    const char *icon;
    const char *label;
} btn_def_t;

static const btn_def_t BUTTONS[BTN_COUNT] = {
    { 0, LV_SYMBOL_POWER, "Lock" },
    { 1, LV_SYMBOL_MUTE,  "Mute" },
    { 2, LV_SYMBOL_PREV,  "Prev" },
    { 3, LV_SYMBOL_NEXT,  "Next" },
};

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *status_lbl;          /* 顶栏右侧连接状态 */
    lv_obj_t *buttons[BTN_COUNT];

    bool last_connected;           /* 用于增量更新样式，避免每帧重绘 */

    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;
    lv_style_t style_btn;
    lv_style_t style_btn_pressed;
    lv_style_t style_btn_disabled;
} page_control_ui_t;

static page_control_ui_t s_ui = {0};

/* ============================================================================
 * CSS - 样式
 * ========================================================================= */

static void init_styles(void)
{
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

    /* 功能按钮（已连接） */
    lv_style_init(&s_ui.style_btn);
    lv_style_set_bg_color(&s_ui.style_btn, lv_color_hex(COLOR_CARD));
    lv_style_set_bg_opa(&s_ui.style_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_ui.style_btn, 14);
    lv_style_set_border_width(&s_ui.style_btn, 2);
    lv_style_set_border_color(&s_ui.style_btn, lv_color_hex(COLOR_CARD_ALT));
    lv_style_set_shadow_width(&s_ui.style_btn, 0);
    lv_style_set_pad_all(&s_ui.style_btn, 0);

    lv_style_init(&s_ui.style_btn_pressed);
    lv_style_set_bg_color(&s_ui.style_btn_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_btn_pressed, LV_OPA_40);
    lv_style_set_border_color(&s_ui.style_btn_pressed, lv_color_hex(COLOR_ACCENT));

    /* 功能按钮（未连接） */
    lv_style_init(&s_ui.style_btn_disabled);
    lv_style_set_border_color(&s_ui.style_btn_disabled, lv_color_hex(COLOR_OFFLINE));
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

    /* 连接状态 */
    s_ui.status_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.status_lbl, "Off");
    lv_obj_set_style_text_color(s_ui.status_lbl, lv_color_hex(COLOR_OFFLINE), 0);
    lv_obj_set_style_text_font(s_ui.status_lbl, APP_FONT_TEXT, 0);
    lv_obj_align(s_ui.status_lbl, LV_ALIGN_TOP_RIGHT, -14, 18);
}

static lv_obj_t *create_single_button(lv_obj_t *parent, const btn_def_t *def)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &s_ui.style_btn, 0);
    lv_obj_add_style(btn, &s_ui.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 100, 100);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* 图标（上方） */
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, def->icon);
    lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, APP_FONT_LARGE, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -12);

    /* 文字（下方） */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, def->label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(lbl, APP_FONT_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 22);

    /* 用 id 作为 user_data，回调里取出 */
    lv_obj_set_user_data(btn, (void *)(uintptr_t)def->id);

    return btn;
}

static void create_button_grid(void)
{
    /* 容器：220×220，flex row wrap, gap=10 */
    lv_obj_t *grid = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 220, 220);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                LV_FLEX_ALIGN_CENTER,
                                LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    for (int i = 0; i < BTN_COUNT; i++) {
        s_ui.buttons[i] = create_single_button(grid, &BUTTONS[i]);
    }
}

/* ============================================================================
 * 状态刷新
 * ========================================================================= */

static void apply_connection_style(bool connected)
{
    /* 顶栏状态文字 */
    lv_label_set_text(s_ui.status_lbl, connected ? "Connected" : "Off");
    lv_obj_set_style_text_color(s_ui.status_lbl,
        lv_color_hex(connected ? COLOR_SUCCESS : COLOR_OFFLINE), 0);

    /* 按钮边框颜色区分"可用/不可用"（不屏蔽点击，点击时会在日志里说明未连接） */
    for (int i = 0; i < BTN_COUNT; i++) {
        if (!s_ui.buttons[i]) continue;
        if (connected) {
            lv_obj_remove_style(s_ui.buttons[i], &s_ui.style_btn_disabled, 0);
        } else {
            lv_obj_add_style(s_ui.buttons[i], &s_ui.style_btn_disabled, 0);
        }
    }
}

/* ============================================================================
 * 事件回调
 * ========================================================================= */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    page_router_switch(PAGE_MENU);
}

static void on_btn_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);

    esp_err_t r = control_service_send_button(id);
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "button %u sent", id);
    } else {
        ESP_LOGW(TAG, "button %u send failed: 0x%x", id, r);
    }
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
    for (int i = 0; i < BTN_COUNT; i++) {
        lv_obj_add_event_cb(s_ui.buttons[i], on_btn_clicked, LV_EVENT_CLICKED, NULL);
    }
}

/* ============================================================================
 * 页面生命周期
 * ========================================================================= */

static void page_init(void)
{
    init_styles();
    create_top_bar();
    create_button_grid();
    bind_events();

    s_ui.last_connected = ble_conn_is_connected();
    apply_connection_style(s_ui.last_connected);
}

static lv_obj_t *page_control_create(void)
{
    ESP_LOGI(TAG, "Creating control page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    page_init();
    return s_ui.screen;
}

static void page_control_destroy(void)
{
    ESP_LOGI(TAG, "Destroying control page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);
    lv_style_reset(&s_ui.style_btn);
    lv_style_reset(&s_ui.style_btn_pressed);
    lv_style_reset(&s_ui.style_btn_disabled);

    s_ui.back_btn = NULL;
    s_ui.status_lbl = NULL;
    for (int i = 0; i < BTN_COUNT; i++) {
        s_ui.buttons[i] = NULL;
    }
}

static void page_control_update(void)
{
    bool now = ble_conn_is_connected();
    if (now != s_ui.last_connected) {
        s_ui.last_connected = now;
        apply_connection_style(now);
    }
}

static const page_callbacks_t s_callbacks = {
    .create  = page_control_create,
    .destroy = page_control_destroy,
    .update  = page_control_update,
};

const page_callbacks_t *page_control_get_callbacks(void)
{
    return &s_callbacks;
}
