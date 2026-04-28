#include "page_dynamic_app.h"

#include <string.h>

#include "app_fonts.h"
#include "dynamic_app.h"
#include "dynamic_app_ui.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "page_dynamic_app";

/*
 * 这个页面的定位：动态 app 的"宿主"。
 *
 * 两种切入方式：
 *
 *   A) 旧路径（保留兼容，不推荐用于新代码）：
 *        page_dynamic_app_set_pending("xxx");
 *        page_router_switch(PAGE_DYNAMIC_APP);
 *      → router 同步调 create()，create 里建 screen + 启脚本，先切屏后 build。
 *      → 用户能看到组件一个个冒出来，体验差。
 *
 *   B) 新路径（推荐）：
 *        page_dynamic_app_prepare_and_switch("xxx");
 *      → 立即返回，不切屏。后台建 off-screen 对象树 + 启脚本 build UI。
 *      → 脚本调 sys.ui.attachRootListener 时触发 ready 回调，
 *        回调里 page_router_commit_prepared() 瞬间切到完整 calc/alarm。
 *      → 用户视角：menu 一帧后突然变完整 app，零中间态。
 *
 * 销毁路径只有一条：page_router 销毁本页时调 destroy() —— 关 root + 停脚本 + 删 screen。
 */

#define COLOR_BG     0x1E1B2E
#define COLOR_ACCENT 0x06B6D4
#define COLOR_TEXT   0xF1ECFF
#define COLOR_MUTED  0x9B94B5

/* 超时兜底：脚本若 800ms 内没有调 attachRootListener，强切。
 * 800ms 留给最大的 app（alarm 6 张卡 + 时钟 + 编辑页占位 ~70 cmd），
 * 实测大多 100-300ms 完成；超时只是保底，不应在正常路径触发。 */
#define PREPARE_TIMEOUT_MS 800

typedef enum {
    PREP_IDLE = 0,
    PREP_PREPARING,    /* off-screen 建好、脚本跑中、等 ready_cb 或 timeout */
    PREP_COMMITTED,    /* router 已接管 prepared_screen，本页是 active page */
} prep_state_t;

typedef struct {
    /* —— 路由可见的 LVGL 子树 —— */
    lv_obj_t *screen;        /* 当前持有的 screen（committed 后是 active screen） */
    lv_obj_t *back_btn;
    lv_obj_t *title_lbl;
    lv_obj_t *list_root;     /* 脚本通过 set_root 注入的 root container */

    /* —— 样式 —— */
    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;

    /* —— prepare 状态机 —— */
    prep_state_t  state;
    lv_timer_t   *timeout_timer;  /* PREPARING 时存在，COMMITTED/IDLE 时为 NULL */
} page_dyn_ui_t;

static page_dyn_ui_t s_ui = {0};

/* 下一次 page create 时要启动的 app 名。空串表示"没设置"——
 * 若旧路径 page_router_switch(PAGE_DYNAMIC_APP) 没先调 set_pending，
 * registry_get 会拿空串失败、屏幕空白；新路径
 * page_dynamic_app_prepare_and_switch(name) 必定先写入这里。
 * 单源化后内嵌 alarm 已不存在，不再保留 "alarm" 这种向后兼容默认。 */
static char s_pending_app[16] = "";

/* 前向声明 */
static void build_screen_subtree(void);
static void teardown_subtree(void);
static void on_ready_cb(void *ud);
static void on_timeout_cb(lv_timer_t *t);
static void commit_now(const char *reason);

/* ============================================================================
 * §1. 公共 API
 * ========================================================================= */

void page_dynamic_app_set_pending(const char *app_name)
{
    if (!app_name || !app_name[0]) return;
    strncpy(s_pending_app, app_name, sizeof(s_pending_app) - 1);
    s_pending_app[sizeof(s_pending_app) - 1] = '\0';
}

void page_dynamic_app_cancel_prepare_if_any(void)
{
    if (s_ui.state != PREP_PREPARING) return;

    ESP_LOGI(TAG, "cancel prepare for app: %s", s_pending_app);

    /* 关 root 门禁 → 停脚本 → 清 registry → 删 off-screen */
    dynamic_app_ui_set_root(NULL);
    dynamic_app_stop();
    dynamic_app_ui_unregister_all();   /* 内部也会清 ready_cb */

    if (s_ui.timeout_timer) {
        lv_timer_del(s_ui.timeout_timer);
        s_ui.timeout_timer = NULL;
    }

    /* 我们持有的 off-screen：未提交给 router，必须自己删 */
    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
    }
    s_ui.screen     = NULL;
    s_ui.back_btn   = NULL;
    s_ui.title_lbl  = NULL;
    s_ui.list_root  = NULL;
    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);

    s_ui.state = PREP_IDLE;
}

void page_dynamic_app_prepare_and_switch(const char *app_name)
{
    if (!app_name || !app_name[0]) return;

    /* 如果有上一次 prepare 还没收尾（用户连点），先撤销 */
    if (s_ui.state == PREP_PREPARING) {
        page_dynamic_app_cancel_prepare_if_any();
    }

    /* 已在本页时不再 prepare（理论上不会发生 —— menu click 才进入这里） */
    if (s_ui.state == PREP_COMMITTED) {
        ESP_LOGW(TAG, "already on dynamic app, ignore prepare for %s", app_name);
        return;
    }

    page_dynamic_app_set_pending(app_name);
    ESP_LOGI(TAG, "prepare app in background: %s", s_pending_app);

    /* 建 off-screen 对象树（不 lv_scr_load，所以不参与 active 渲染） */
    build_screen_subtree();

    /* 注入字体 + root，准备接受脚本 cmd */
    dynamic_app_ui_set_fonts(APP_FONT_TEXT, APP_FONT_TITLE, APP_FONT_HUGE);
    dynamic_app_ui_set_root(s_ui.list_root);

    /* 注册 build-ready 回调 + 起超时 timer，二者择一触发 commit_now */
    dynamic_app_ui_set_ready_cb(on_ready_cb, NULL);
    s_ui.timeout_timer = lv_timer_create(on_timeout_cb, PREPARE_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(s_ui.timeout_timer, 1);   /* 一次性 */

    s_ui.state = PREP_PREPARING;

    /* 启动脚本：在脚本任务里 eval js → 入 cmd → drain 在 off-screen 上建对象树 */
    dynamic_app_start(s_pending_app);
}

/* ============================================================================
 * §2. 内部：subtree build/teardown 和样式
 * ========================================================================= */

static void init_styles(void)
{
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

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    page_router_switch(PAGE_MENU);
}

static void build_screen_subtree(void)
{
    init_styles();

    /* 关键：lv_obj_create(NULL) → 创建 *off-screen* 的 screen，
     * 不 lv_scr_load 它，LVGL 不会去 render，对 active screen 性能零影响。 */
    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部返回按钮 */
    s_ui.back_btn = lv_btn_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.back_btn);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn, 0);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.back_btn, 6, 0);
    lv_obj_set_size(s_ui.back_btn, 90, 30);
    lv_obj_align(s_ui.back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(s_ui.back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *arrow = lv_label_create(s_ui.back_btn);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_font(arrow, APP_FONT_TEXT, 0);
    lv_obj_center(arrow);

    /* 标题（纯展示用） */
    s_ui.title_lbl = lv_label_create(s_ui.screen);
    lv_label_set_text(s_ui.title_lbl, "Dynamic App");
    lv_obj_set_style_text_color(s_ui.title_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.title_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(s_ui.title_lbl, LV_ALIGN_TOP_MID, 0, 14);

    /* 脚本根 container */
    s_ui.list_root = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.list_root);
    lv_obj_set_size(s_ui.list_root, 220, 250);
    lv_obj_align(s_ui.list_root, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_pad_all(s_ui.list_root, 0, 0);
    lv_obj_set_style_text_color(s_ui.list_root, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_ui.list_root, APP_FONT_TEXT, 0);
    lv_obj_set_style_bg_opa(s_ui.list_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.list_root, 0, 0);
    lv_obj_set_scrollbar_mode(s_ui.list_root, LV_SCROLLBAR_MODE_AUTO);
}

static void teardown_subtree(void)
{
    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }
    s_ui.back_btn  = NULL;
    s_ui.title_lbl = NULL;
    s_ui.list_root = NULL;

    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);
}

/* ============================================================================
 * §3. 内部：ready / timeout / commit
 * ========================================================================= */

static void on_ready_cb(void *ud)
{
    (void)ud;
    if (s_ui.state != PREP_PREPARING) return;
    commit_now("ready");
}

static void on_timeout_cb(lv_timer_t *t)
{
    (void)t;
    if (s_ui.state != PREP_PREPARING) return;
    ESP_LOGW(TAG, "prepare timeout (%dms) for app: %s, force commit",
             PREPARE_TIMEOUT_MS, s_pending_app);
    commit_now("timeout");
}

static void commit_now(const char *reason)
{
    /* timer 是一次性的，但显式删除避免引用被销毁后的对象 */
    if (s_ui.timeout_timer) {
        lv_timer_del(s_ui.timeout_timer);
        s_ui.timeout_timer = NULL;
    }
    /* ready_cb 已经 一次性消费；这里再清一次保险（timeout 路径需要） */
    dynamic_app_ui_set_ready_cb(NULL, NULL);

    lv_obj_t *prepared = s_ui.screen;   /* 提交前先抓住，下面 router 会接管 */

    /* router 会销毁 menu 旧页 + lv_scr_load(prepared)。
     * 如果 commit_prepared 失败（理论上不会），不能让 prepared 泄漏。 */
    esp_err_t err = page_router_commit_prepared(PAGE_DYNAMIC_APP, prepared);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit_prepared failed err=0x%x, rolling back", err);
        /* 回滚成 IDLE：脚本停了，对象树删了，root 关了 */
        dynamic_app_ui_set_root(NULL);
        dynamic_app_stop();
        dynamic_app_ui_unregister_all();
        teardown_subtree();
        s_ui.state = PREP_IDLE;
        return;
    }

    /* commit 成功：state 进入 COMMITTED；router 持有 prepared 直到下次切屏触发 destroy。 */
    s_ui.state = PREP_COMMITTED;
    ESP_LOGI(TAG, "committed dynamic app=%s reason=%s", s_pending_app, reason);
}

/* ============================================================================
 * §4. router callbacks
 * ========================================================================= */

static lv_obj_t *page_dynamic_app_create(void)
{
    /* 新路径（prepare_and_switch）：本函数其实不会被 router 调到 ——
     * commit_prepared 走的不是 create 通路。但为了向后兼容旧路径
     * （set_pending + page_router_switch）和兜底，我们这里仍提供同步 build。 */
    if (s_ui.state == PREP_PREPARING || s_ui.state == PREP_COMMITTED) {
        /* 不应该走到这：如果走到，说明外部直接调了 page_router_switch
         * 而本页处于 prepare 中。安全起见返回当前 screen，
         * router 接管后状态转为 COMMITTED。 */
        ESP_LOGW(TAG, "create() called while state=%d, returning current screen", s_ui.state);
        s_ui.state = PREP_COMMITTED;
        return s_ui.screen;
    }

    ESP_LOGI(TAG, "Creating dynamic app page (legacy sync path) for: %s", s_pending_app);

    build_screen_subtree();

    dynamic_app_ui_set_fonts(APP_FONT_TEXT, APP_FONT_TITLE, APP_FONT_HUGE);
    dynamic_app_ui_set_root(s_ui.list_root);

    s_ui.state = PREP_COMMITTED;
    dynamic_app_start(s_pending_app);

    return s_ui.screen;
}

static void page_dynamic_app_destroy(void)
{
    ESP_LOGI(TAG, "Destroying dynamic app page");

    /* 先关 root 门禁，再停脚本并清 registry：避免销毁过程中仍创建/更新对象 */
    dynamic_app_ui_set_root(NULL);
    dynamic_app_stop();
    dynamic_app_ui_unregister_all();   /* 内部也清 ready_cb */

    /* 兜底：若有遗留 timer（理论上 commit_now/cancel 都会清，但保险） */
    if (s_ui.timeout_timer) {
        lv_timer_del(s_ui.timeout_timer);
        s_ui.timeout_timer = NULL;
    }

    teardown_subtree();
    s_ui.state = PREP_IDLE;
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
