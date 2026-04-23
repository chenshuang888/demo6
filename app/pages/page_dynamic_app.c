#include "page_dynamic_app.h"

#include "app_fonts.h"
#include "dynamic_app.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "page_dynamic_app";

/*
 * 这个页面的定位：一个“最小可用”的 Dynamic App 演示页。
 *
 * 页面本身只做三件事：
 * 1) 创建一个 label（s_ui.time_lbl）用于展示文本
 * 2) 把它注册到 dynamic_app_ui 的 registry（id="time"）
 * 3) 启动脚本（dynamic_app_start），让 app.js 通过 sys.ui.setText() 来更新这个 label
 *
 * 你需要重点记住的线程规则：
 * - 本文件里创建/注册 LVGL 对象的代码都在 UI 线程执行（安全）
 * - 真正的跨线程桥接发生在 dynamic_app_ui_enqueue_set_text() / dynamic_app_ui_drain()
 */

#define COLOR_BG     0x1E1B2E
#define COLOR_ACCENT 0x06B6D4
#define COLOR_TEXT   0xF1ECFF
#define COLOR_MUTED  0x9B94B5

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *title_lbl;
    lv_obj_t *time_lbl;
    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;
} page_dyn_ui_t;

static page_dyn_ui_t s_ui = {0};

static void init_styles(void)
{
    /* 顶部返回按钮的样式（正常/按下）。 */
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

static void create_top_bar(void)
{
    /* 返回按钮（点击回菜单）。 */
    s_ui.back_btn = lv_btn_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.back_btn);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn, 0);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.back_btn, 6, 0);
    lv_obj_set_size(s_ui.back_btn, 90, 30);
    lv_obj_align(s_ui.back_btn, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *arrow = lv_label_create(s_ui.back_btn);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_font(arrow, APP_FONT_TEXT, 0);
    lv_obj_center(arrow);

    /* 标题（纯展示用）。 */
    s_ui.title_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.title_lbl, "Dynamic App");
    lv_obj_set_style_text_color(s_ui.title_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.title_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(s_ui.title_lbl, LV_ALIGN_TOP_MID, 0, 14);
}

static void create_body(void)
{
    /* 提示文字：解释“脚本线程不直接操作 LVGL”的原则。 */
    lv_obj_t *hint = lv_label_create(s_ui.screen);
    lv_label_set_text(hint, "JS 通过队列异步更新 UI（脚本线程不直接操作 LVGL）");
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(hint, APP_FONT_TEXT, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 这个 label 会被脚本通过 id="time" 更新。 */
    s_ui.time_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.time_lbl, "--");
    lv_obj_set_style_text_color(s_ui.time_lbl, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(s_ui.time_lbl, APP_FONT_HUGE, 0);
    lv_obj_set_style_text_align(s_ui.time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ui.time_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.time_lbl, lv_pct(100));
    lv_obj_align(s_ui.time_lbl, LV_ALIGN_CENTER, 0, -10);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    page_router_switch(PAGE_MENU);
}

static void bind_events(void)
{
    lv_obj_add_event_cb(s_ui.back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
}

static void page_init(void)
{
    init_styles();
    create_top_bar();
    create_body();
    bind_events();

    /*
     * 本页面只把“可被脚本更新的控件”注册出去。
     *
     * 约定：
     * - JS 侧会调用：sys.ui.setText("time", "...")；
     * - C 侧在这里把 id="time" 映射到 s_ui.time_lbl；
     * - UI 线程会在主循环中调用 dynamic_app_ui_drain()，把队列里的 setText 命令真正落到 LVGL 上。
     */

    /* “母版”约定：本页面只暴露 label 注册入口 */
    esp_err_t err = dynamic_app_ui_register_label("time", s_ui.time_lbl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dynamic_app_ui_register_label failed: %s", esp_err_to_name(err));
    }

    /* 启动脚本：脚本线程开始跑 app.js，并通过队列异步驱动 UI。 */
    dynamic_app_start();
}

static lv_obj_t *page_dynamic_app_create(void)
{
    ESP_LOGI(TAG, "Creating dynamic app page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    page_init();
    return s_ui.screen;
}

static void page_dynamic_app_destroy(void)
{
    ESP_LOGI(TAG, "Destroying dynamic app page");

    /* 先停脚本，再清 registry：避免脚本继续投递 UI 命令命中无效对象。 */
    dynamic_app_stop();
    dynamic_app_ui_unregister_all();

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);

    s_ui.back_btn = NULL;
    s_ui.title_lbl = NULL;
    s_ui.time_lbl = NULL;
}

static const page_callbacks_t s_callbacks = {
    .create  = page_dynamic_app_create,
    .destroy = page_dynamic_app_destroy,
    .update  = NULL,
};

const page_callbacks_t *page_dynamic_app_get_callbacks(void)
{
    return &s_callbacks;
}
