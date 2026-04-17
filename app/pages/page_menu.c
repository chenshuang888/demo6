#include "page_menu.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "page_menu";

/* ============================================================================
 * UI Elements
 * ============================================================================ */

typedef struct {
    lv_obj_t *screen;

    // 样式对象
    lv_style_t style_card;
    lv_style_t style_btn;
    lv_style_t style_btn_pressed;
} page_menu_ui_t;

static page_menu_ui_t s_ui = {0};

/* ============================================================================
 * CSS - 样式定义
 * ============================================================================ */

static void init_styles(void)
{
    lv_style_init(&s_ui.style_card);
    lv_style_set_bg_color(&s_ui.style_card, lv_color_hex(0x2C3E50));
    lv_style_set_radius(&s_ui.style_card, 15);
    lv_style_set_border_width(&s_ui.style_card, 0);
    lv_style_set_pad_all(&s_ui.style_card, 20);
    lv_style_set_shadow_width(&s_ui.style_card, 10);
    lv_style_set_shadow_color(&s_ui.style_card, lv_color_hex(0x000000));
    lv_style_set_shadow_ofs_y(&s_ui.style_card, 3);

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
 * JavaScript - 事件响应
 * ============================================================================ */

static void on_back_clicked(lv_event_t *e)
{
    page_router_switch(PAGE_TIME);
}

static void bind_events(void)
{
    // 事件已在 create_menu_card 中绑定
}

/* ============================================================================
 * HTML - 页面结构
 * ============================================================================ */

static void create_menu_card(void)
{
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_set_size(card, 220, 200);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(card, &s_ui.style_card, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "MENU");
    lv_obj_set_style_text_color(title, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, "This is a demo\nmenu page");
    lv_obj_set_style_text_color(desc, lv_color_hex(0xBDC3C7), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *back_btn = lv_btn_create(card);
    lv_obj_set_size(back_btn, 120, 50);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(back_btn, &s_ui.style_btn, 0);
    lv_obj_add_style(back_btn, &s_ui.style_btn_pressed, LV_STATE_PRESSED);

    lv_obj_t *btn_label = lv_label_create(back_btn);
    lv_label_set_text(btn_label, "Back");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
}

/* ============================================================================
 * 页面初始化入口
 * ============================================================================ */

static void page_init(void)
{
    init_styles();
    create_menu_card();
    bind_events();
}

/* ============================================================================
 * Page Lifecycle
 * ============================================================================ */

static lv_obj_t *page_menu_create(void)
{
    ESP_LOGI(TAG, "Creating menu page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(0x1A1A2E), 0);

    page_init();

    return s_ui.screen;
}

static void page_menu_destroy(void)
{
    ESP_LOGI(TAG, "Destroying menu page");

    // 删除 screen 会自动删除所有子对象，并解除 style 引用
    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    // 重置所有 style 对象，清理 LVGL 内部状态
    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_btn);
    lv_style_reset(&s_ui.style_btn_pressed);
}

static const page_callbacks_t s_callbacks = {
    .create = page_menu_create,
    .destroy = page_menu_destroy,
    .update = NULL,
};

const page_callbacks_t *page_menu_get_callbacks(void)
{
    return &s_callbacks;
}
