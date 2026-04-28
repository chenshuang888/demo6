#include "page_menu.h"
#include "page_menu_modal.h"
#include "page_dynamic_app.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app_fonts.h"
#include "ble_driver.h"
#include "lcd_panel.h"
#include "backlight_storage.h"
#include "dynamic_app_registry.h"
#include "dynapp_upload_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

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
 * 数据驱动菜单
 *
 * 列表项分两类：
 *   1) "静态项"  —— 内建页面或本地行为（蓝牙状态、背光、Time/About 等）
 *      由 s_static_entries[] 描述，回调由 entry->action 决定
 *   2) "动态项"  —— 由 dynamic_app_registry_list() 在 create 时枚举出来
 *      回调统一走 page_dynamic_app_prepare_and_switch(name)
 *
 * "回调没法写死"通过给每个 lv_obj 关联 user_data 解决：
 *   静态项 user_data = const menu_static_entry_t*
 *   动态项 user_data = char* (堆上的 app 名拷贝，destroy 时释放)
 * ========================================================================= */

typedef enum {
    MENU_ACT_PAGE,        /* 切到 entry->page_id */
    MENU_ACT_BACKLIGHT,   /* 切档背光，不切页 */
    MENU_ACT_BACK,        /* 返回 PAGE_TIME */
} menu_action_t;

typedef struct {
    const char     *icon;       /* LV_SYMBOL_* 字面量 */
    const char     *label;      /* 列表文字（英文） */
    menu_action_t   action;
    page_id_t       page_id;    /* action == MENU_ACT_PAGE 时有效 */
    bool            has_value;  /* 右侧是否动态值（蓝牙/背光） */
} menu_static_entry_t;

/* 内建页静态表。顺序即显示顺序。 */
static const menu_static_entry_t s_static_entries[] = {
    { LV_SYMBOL_BLUETOOTH, "Bluetooth",     MENU_ACT_PAGE,      PAGE_TIME /* 占位 */, true  },
    { LV_SYMBOL_EYE_OPEN,  "Backlight",     MENU_ACT_BACKLIGHT, PAGE_MAX, true  },
    { LV_SYMBOL_SETTINGS,  "Time & Date",   MENU_ACT_PAGE,      PAGE_TIME_ADJUST,    false },
    { LV_SYMBOL_IMAGE,     "Weather",       MENU_ACT_PAGE,      PAGE_WEATHER,        false },
    { LV_SYMBOL_BELL,      "Notifications", MENU_ACT_PAGE,      PAGE_NOTIFICATIONS,  false },
    { LV_SYMBOL_AUDIO,     "Music",         MENU_ACT_PAGE,      PAGE_MUSIC,          false },
    { LV_SYMBOL_BARS,      "System",        MENU_ACT_PAGE,      PAGE_SYSTEM,         false },
};
#define STATIC_ENTRY_COUNT  (int)(sizeof(s_static_entries) / sizeof(s_static_entries[0]))

/* 蓝牙状态项是个特例：它有右侧动态文字但点击行为是"不动"（暂时）。
 * 上面表里 page_id 写了 PAGE_TIME 占位但不会真的切——后面 on_static_clicked
 * 会显式把它当 no-op 处理。这样能把"有动态值"的两项保持在表里，
 * 不用为它单独切代码路径。 */
#define STATIC_INDEX_BT       0
#define STATIC_INDEX_BL       1

/* 单个 app 名最大长度（含 \0），与 dynamic_app_registry 对齐。 */
#define APP_NAME_BUF_LEN  (DYNAPP_REGISTRY_NAME_MAX + 1)

/* About 项始终在最后一行；和 s_static_entries 共用 create_list_item，
 * 但回调路径不同（要 cancel_prepare 然后切 PAGE_ABOUT）。
 * 单独处理而非进表，避免 menu_action_t 再加一个枚举值。 */

/* ============================================================================
 * UI 元素
 * ========================================================================= */

#define MAX_DYN_APPS  16  /* 实际用 dynamic_app_registry 列出来的数量 */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *list;          /* 列表容器（带 flex column） */

    lv_obj_t *bt_status_lbl; /* 蓝牙状态文字: "Connected"/"Off" */
    lv_obj_t *bl_value_lbl;  /* 背光亮度文字: "50%" */

    /* 动态 app 入口的"app 名拷贝"。释放时回收。 */
    char     *dyn_names[MAX_DYN_APPS];
    int       dyn_count;

    lv_style_t style_card;
    lv_style_t style_item;
    lv_style_t style_item_pressed;
    lv_style_t style_topbtn;
    lv_style_t style_topbtn_pressed;
} page_menu_ui_t;

static page_menu_ui_t s_ui = {0};

/* 上传/删除完成 → 菜单刷新通知 flag。
 * - manager consumer task 在 commit/delete 成功时 set
 * - UI 线程的 page_menu_update() poll 并清，重建动态项列表
 * 不能直接在 cb 里动 LVGL（跨线程崩溃）。 */
static volatile bool s_dirty = false;

/* 背光四档: 25% / 50% / 75% / 100% */
static const uint8_t BACKLIGHT_STEPS[] = {64, 128, 192, 255};

/* 前向声明 */
static void on_static_clicked(lv_event_t *e);
static void on_dyn_clicked(lv_event_t *e);
static void on_dyn_long_pressed(lv_event_t *e);
static void on_about_clicked(lv_event_t *e);
static void on_back_clicked(lv_event_t *e);
static void update_bt_status(void);
static void update_backlight_label(void);
static void rebuild_dynamic_items(void);

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

    lv_style_init(&s_ui.style_item);
    lv_style_set_bg_opa(&s_ui.style_item, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_ui.style_item, 1);
    lv_style_set_border_side(&s_ui.style_item, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_ui.style_item, lv_color_hex(COLOR_CARD_ALT));
    lv_style_set_radius(&s_ui.style_item, 0);
    lv_style_set_shadow_width(&s_ui.style_item, 0);
    lv_style_set_pad_all(&s_ui.style_item, 0);
    lv_style_set_text_color(&s_ui.style_item, lv_color_hex(COLOR_TEXT));
    lv_style_set_text_font(&s_ui.style_item, APP_FONT_TEXT);

    lv_style_init(&s_ui.style_item_pressed);
    lv_style_set_bg_color(&s_ui.style_item_pressed, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&s_ui.style_item_pressed, LV_OPA_20);

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
 *
 * value_init / value_color：
 *   - 非 NULL：右侧画一个值 label，指针通过 *out_value_label 回传
 *   - NULL  且 out_value_label == NULL：画一个 ">" 箭头占位
 *   - NULL  且 out_value_label != NULL：什么都不画（用于动态 app 项）
 * ========================================================================= */

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
        lv_obj_set_style_border_width(item, 0, 0);
    }

    lv_obj_t *icon_lbl = lv_label_create(item);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(icon_lbl, APP_FONT_TITLE, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *text_lbl = lv_label_create(item);
    lv_label_set_text(text_lbl, text);
    lv_obj_set_style_text_color(text_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(text_lbl, APP_FONT_TEXT, 0);
    lv_obj_align(text_lbl, LV_ALIGN_LEFT_MID, 48, 0);

    if (value_init) {
        lv_obj_t *val = lv_label_create(item);
        lv_label_set_text(val, value_init);
        lv_obj_set_style_text_color(val, lv_color_hex(value_color), 0);
        lv_obj_set_style_text_font(val, APP_FONT_TEXT, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -14, 0);
        if (out_value_label) *out_value_label = val;
    } else if (out_value_label == NULL) {
        lv_obj_t *arrow = lv_label_create(item);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(arrow, APP_FONT_TEXT, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
    }

    return item;
}

static void create_top_bar(void)
{
    s_ui.back_btn = lv_btn_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.back_btn);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn, 0);
    lv_obj_add_style(s_ui.back_btn, &s_ui.style_topbtn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ui.back_btn, 6, 0);
    lv_obj_set_size(s_ui.back_btn, 80, 30);
    lv_obj_align(s_ui.back_btn, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *arrow = lv_label_create(s_ui.back_btn);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT " Menu");
    lv_obj_set_style_text_font(arrow, APP_FONT_TEXT, 0);
    lv_obj_center(arrow);

    lv_obj_add_event_cb(s_ui.back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
}

/* 显示名 hook：单源化后所有动态 app 都来自 FS，没有"内嵌友好名"映射，
 * 直接显示脚本文件名。函数空壳保留：未来若加 manifest 元数据
 * (eg. /littlefs/apps/<name>.meta) 是天然 hook。 */
static const char *display_name_for_app(const char *name)
{
    return name;
}

/* 图标 hook：同上。当前所有动态 app 用通用 list 图标。 */
static const char *icon_for_app(const char *name)
{
    (void)name;
    return LV_SYMBOL_LIST;
}

static void create_static_items(lv_obj_t *card)
{
    for (int i = 0; i < STATIC_ENTRY_COUNT; i++) {
        const menu_static_entry_t *e = &s_static_entries[i];

        lv_obj_t **out_label = NULL;
        const char *init_val = NULL;
        uint32_t    init_clr = 0;

        if (i == STATIC_INDEX_BT) { out_label = &s_ui.bt_status_lbl; init_val = "Off"; init_clr = COLOR_OFFLINE; }
        if (i == STATIC_INDEX_BL) { out_label = &s_ui.bl_value_lbl;  init_val = "50%"; init_clr = COLOR_ACCENT;  }

        lv_obj_t *item = create_list_item(card,
            e->icon, e->label,
            out_label, init_val, init_clr,
            /*last=*/false);

        /* 把 entry 指针挂到 user_data；on_static_clicked 用它分支 */
        lv_obj_add_event_cb(item, on_static_clicked, LV_EVENT_CLICKED, (void *)e);
    }
}

static void create_dynamic_items(lv_obj_t *card)
{
    dynamic_app_entry_t entries[MAX_DYN_APPS];
    int n = dynamic_app_registry_list(entries, MAX_DYN_APPS);
    ESP_LOGI(TAG, "dynamic apps discovered: %d", n);

    s_ui.dyn_count = 0;
    for (int i = 0; i < n; i++) {
        /* heap 拷贝 app 名：作为 lv_obj 的 user_data，destroy 时回收 */
        char *copy = strdup(entries[i].name);
        if (!copy) {
            ESP_LOGW(TAG, "OOM copying app name %s", entries[i].name);
            continue;
        }

        lv_obj_t *item = create_list_item(card,
            icon_for_app(entries[i].name),
            display_name_for_app(entries[i].name),
            /*out=*/NULL, /*val=*/NULL, /*clr=*/0,
            /*last=*/false);

        lv_obj_add_event_cb(item, on_dyn_clicked, LV_EVENT_CLICKED, copy);
        /* 同一份 user_data 用于两个事件：长按删除时也需要 app 名。
         * LVGL v9 长按触发后会抑制 CLICKED，不会冲突。 */
        lv_obj_add_event_cb(item, on_dyn_long_pressed, LV_EVENT_LONG_PRESSED, copy);
        s_ui.dyn_names[s_ui.dyn_count++] = copy;
    }
}

static void create_about_item(lv_obj_t *card)
{
    lv_obj_t *item = create_list_item(card,
        LV_SYMBOL_LIST, "About",
        NULL, NULL, 0, /*last=*/true);
    lv_obj_add_event_cb(item, on_about_clicked, LV_EVENT_CLICKED, NULL);
}

static void create_menu_list(void)
{
    s_ui.list = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(s_ui.list);
    lv_obj_add_style(s_ui.list, &s_ui.style_card, 0);
    lv_obj_set_size(s_ui.list, 220, 250);   /* 视窗 250，超过会滚动 */
    lv_obj_align(s_ui.list, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_set_flex_flow(s_ui.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ui.list,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_ui.list, 0, 0);

    create_static_items(s_ui.list);
    create_dynamic_items(s_ui.list);
    create_about_item(s_ui.list);
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

static void cycle_backlight(void)
{
    uint8_t cur = lcd_panel_get_backlight();
    int idx = 0;
    int n = (int)(sizeof(BACKLIGHT_STEPS) / sizeof(BACKLIGHT_STEPS[0]));
    for (int i = 0; i < n; i++) {
        if (cur <= BACKLIGHT_STEPS[i]) { idx = i; break; }
        idx = i;
    }
    int next = (idx + 1) % n;
    uint8_t duty = BACKLIGHT_STEPS[next];

    backlight_storage_set(duty);
    lcd_panel_set_backlight(duty);

    update_backlight_label();
    ESP_LOGI(TAG, "Backlight -> %d", duty);
}

/* ============================================================================
 * 事件回调
 *
 * 凡是离开 menu 的入口，都先调一次 page_dynamic_app_cancel_prepare_if_any()。
 * 因为用户可能刚点了某个动态 app（后台 prepare 中），又改主意点了别的入口 ——
 * 必须先把进行中的 prepare 撤掉，否则脚本会继续往不存在的 root 灌命令。
 * ========================================================================= */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    page_dynamic_app_cancel_prepare_if_any();
    page_router_switch(PAGE_TIME);
}

static void on_about_clicked(lv_event_t *e)
{
    (void)e;
    page_dynamic_app_cancel_prepare_if_any();
    page_router_switch(PAGE_ABOUT);
}

static void on_static_clicked(lv_event_t *e)
{
    const menu_static_entry_t *entry =
        (const menu_static_entry_t *)lv_event_get_user_data(e);
    if (!entry) return;

    switch (entry->action) {
    case MENU_ACT_BACKLIGHT:
        cycle_backlight();
        break;
    case MENU_ACT_PAGE:
        /* 蓝牙项当前没目的页（page_id 是占位），点击 no-op */
        if (entry == &s_static_entries[STATIC_INDEX_BT]) return;
        page_dynamic_app_cancel_prepare_if_any();
        page_router_switch(entry->page_id);
        break;
    case MENU_ACT_BACK:
        page_dynamic_app_cancel_prepare_if_any();
        page_router_switch(PAGE_TIME);
        break;
    }
}

static void on_dyn_clicked(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;
    page_dynamic_app_prepare_and_switch(name);
}

/* 长按动态项 → 弹删除确认。
 * Delete 走 dynapp_upload_submit_delete()，与 PC 主动删除完全同一路径，
 * 共享 manager 的 BUSY 拦截、status 广播、菜单刷新。 */
static void on_delete_confirmed(void *ud)
{
    const char *name = (const char *)ud;
    if (!name) return;
    if (!dynapp_upload_submit_delete(name)) {
        ESP_LOGW(TAG, "submit_delete refused (queue full?): %s", name);
    }
    /* 刷新走 status_cb → s_dirty → page_menu_update 路径，不在这里手工重建。 */
}

static void on_dyn_long_pressed(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;

    /* 抑制后续 CLICKED：LVGL v9 中 LONG_PRESSED 触发后，手指抬起仍会发 CLICKED，
     * 会导致"弹出删除框的同时进入 app"的双触。lv_indev_wait_release 让 indev
     * 直到下次按下前都忽略当前手势的 release，从根上断掉 CLICKED。 */
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev) lv_indev_wait_release(indev);

    /* name 直接当 user_data 传——它在 s_ui.dyn_names[] 里持有，
     * 直到 page_menu_destroy 或 rebuild_dynamic_items 才 free。
     * modal 是 UI 线程同步交互，用户点 Delete 时 item 仍存活，name 有效。 */
    menu_modal_show_delete_confirm(name, on_delete_confirmed, (void *)name);
}

/* ============================================================================
 * 热刷新：上传完成 / 删除完成时，菜单页若 active 自动重建动态项
 *
 * 链路：
 *   manager consumer task → on_upload_status (cb，禁碰 LVGL，仅 set s_dirty)
 *                        ↓
 *   UI 线程 page_menu_update → poll s_dirty → rebuild_dynamic_items
 * ========================================================================= */

static void on_upload_status(upload_op_t op, upload_result_t result,
                             const char *name, uint32_t extra,
                             const uint8_t *list_buf, size_t list_len)
{
    (void)name; (void)extra; (void)list_buf; (void)list_len;
    /* 只关心"FS 内容真改变了"的两个 ok 事件 */
    if (result != UPL_RESULT_OK) return;
    if (op == UPL_OP_END || op == UPL_OP_DELETE) {
        s_dirty = true;
    }
}

/* 完整重建列表：清空 list 所有 child（含静态 / 动态 / about）+ free 旧 dyn_names，
 * 然后调原本 create_menu_list 用的三个 helper 重新填充。
 *
 * 简单粗暴但绝对正确：所有内部指针（bt_status_lbl / bl_value_lbl / dyn_names）
 * 都会被重新建立，不会出现野指针。240×320 屏 + ≤16 项，重建耗时几十毫秒可忽略。 */
static void rebuild_dynamic_items(void)
{
    if (!s_ui.list) return;

    ESP_LOGI(TAG, "rebuilding menu list (FS changed)");

    lv_obj_clean(s_ui.list);

    /* 旧 dyn_names 此时已随 lv_obj_clean 失去 user_data 关联，安全 free */
    for (int i = 0; i < s_ui.dyn_count; i++) {
        free(s_ui.dyn_names[i]);
        s_ui.dyn_names[i] = NULL;
    }
    s_ui.dyn_count = 0;

    /* 静态项里的 value label 指针先清，再由 create_static_items 重新写入 */
    s_ui.bt_status_lbl = NULL;
    s_ui.bl_value_lbl  = NULL;

    create_static_items(s_ui.list);
    create_dynamic_items(s_ui.list);
    create_about_item(s_ui.list);

    /* value label 重建后立刻刷一次内容 */
    update_bt_status();
    update_backlight_label();
}

/* ============================================================================
 * 页面生命周期
 * ========================================================================= */

static lv_obj_t *page_menu_create(void)
{
    ESP_LOGI(TAG, "Creating menu page");

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(COLOR_BG), 0);

    init_styles();
    create_top_bar();
    create_menu_list();

    update_bt_status();
    update_backlight_label();

    /* 注册上传 status 观察者：idempotent，多次进出菜单不会累积 */
    (void)dynapp_upload_manager_register_status_cb(on_upload_status);
    s_dirty = false;
    return s_ui.screen;
}

static void page_menu_destroy(void)
{
    ESP_LOGI(TAG, "Destroying menu page");

    if (s_ui.screen) {
        lv_obj_del(s_ui.screen);
        s_ui.screen = NULL;
    }

    /* 释放挂在动态项 user_data 上的 app 名 */
    for (int i = 0; i < s_ui.dyn_count; i++) {
        free(s_ui.dyn_names[i]);
        s_ui.dyn_names[i] = NULL;
    }
    s_ui.dyn_count = 0;

    lv_style_reset(&s_ui.style_card);
    lv_style_reset(&s_ui.style_item);
    lv_style_reset(&s_ui.style_item_pressed);
    lv_style_reset(&s_ui.style_topbtn);
    lv_style_reset(&s_ui.style_topbtn_pressed);

    s_ui.back_btn = NULL;
    s_ui.list = NULL;
    s_ui.bt_status_lbl = NULL;
    s_ui.bl_value_lbl = NULL;
}

static void page_menu_update(void)
{
    update_bt_status();
    if (s_dirty) {
        s_dirty = false;
        rebuild_dynamic_items();
    }
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
