#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dynamic_app_ui：Script Task <-> UI Task 双向桥接层。
 *
 * 单向（Script -> UI）命令队列 s_ui_queue：
 *   脚本侧把 createLabel/createPanel/createButton/setText/setStyle/
 *   attachRootListener 等"要做什么"封装成 command，UI Task 在
 *   dynamic_app_ui_drain() 出队执行。
 *
 * 反向（UI -> Script）事件队列 s_event_queue：
 *   LVGL root listener 在用户点击时拿到被点对象的 id 字符串入队；
 *   Script Task 在主循环里 pop 并通过 JS 全局 dispatcher 派发。
 *
 * 线程规则：
 * - enqueue_*：可在任意线程调用（典型是脚本线程）
 * - unregister/drain/pop_event：必须在它们对应的所有者线程调用
 */

#define DYNAMIC_APP_UI_ID_MAX_LEN     32
#define DYNAMIC_APP_UI_TEXT_MAX_LEN   128
#define DYNAMIC_APP_UI_REGISTRY_MAX   64
#define DYNAMIC_APP_UI_EVENT_QUEUE_LEN 8

typedef enum {
    DYNAMIC_APP_UI_CMD_SET_TEXT = 1,
    DYNAMIC_APP_UI_CMD_CREATE_LABEL,
    DYNAMIC_APP_UI_CMD_CREATE_PANEL,
    DYNAMIC_APP_UI_CMD_CREATE_BUTTON,
    DYNAMIC_APP_UI_CMD_SET_STYLE,
    DYNAMIC_APP_UI_CMD_ATTACH_ROOT_LISTENER,   /* 在 root 上挂一个总 cb */
    DYNAMIC_APP_UI_CMD_DESTROY,                /* 删除单个 obj 并释放 registry slot */
} dynamic_app_ui_cmd_type_t;

/*
 * 样式 key：约定与 JS 侧 sys.style.* 数值常量对齐（dynamic_app.c 里绑定）。
 * a/b/c/d 字段含义随 key 变化（详见 dynamic_app_ui.c drain 实现）。
 */
typedef enum {
    DYNAMIC_APP_STYLE_BG_COLOR = 1,    /* a = 0xRRGGBB */
    DYNAMIC_APP_STYLE_TEXT_COLOR,      /* a = 0xRRGGBB */
    DYNAMIC_APP_STYLE_RADIUS,          /* a = px */
    DYNAMIC_APP_STYLE_SIZE,            /* a = w, b = h；负数取 abs 当百分比 */
    DYNAMIC_APP_STYLE_ALIGN,           /* a = align id, b = x, c = y */
    DYNAMIC_APP_STYLE_PAD,              /* a/b/c/d = left/top/right/bottom */
    DYNAMIC_APP_STYLE_BORDER_BOTTOM,   /* a = 0xRRGGBB */
    DYNAMIC_APP_STYLE_FLEX,            /* a = 0(column) / 1(row) */
    DYNAMIC_APP_STYLE_FONT,            /* a = 0(text) / 1(title) / 2(huge) */
} dynamic_app_style_key_t;

typedef struct {
    dynamic_app_ui_cmd_type_t type;
    char id[DYNAMIC_APP_UI_ID_MAX_LEN];
    union {
        char text[DYNAMIC_APP_UI_TEXT_MAX_LEN];          /* SET_TEXT */
        char parent_id[DYNAMIC_APP_UI_ID_MAX_LEN];        /* CREATE_* */
        struct {
            int32_t key;
            int32_t a, b, c, d;
        } style;                                          /* SET_STYLE */
    } u;
} dynamic_app_ui_command_t;

typedef struct {
    /* root delegation 路径：被点中对象的 id 字符串 */
    char node_id[DYNAMIC_APP_UI_ID_MAX_LEN];
} dynamic_app_ui_event_t;

/* ---------------- 生命周期 ---------------- */

esp_err_t dynamic_app_ui_init(void);

void dynamic_app_ui_set_root(lv_obj_t *root);
void dynamic_app_ui_unregister_all(void);
void dynamic_app_ui_drain(int max_count);

/* 由上层（page）在进入 Dynamic App 页面前注入字体指针。任意指针为 NULL 时
 * 该字体对应的 setStyle(FONT, x) 将回退到 LVGL 默认字体。 */
void dynamic_app_ui_set_fonts(const lv_font_t *text,
                              const lv_font_t *title,
                              const lv_font_t *huge);

/* ---------------- Script -> UI ---------------- */

bool dynamic_app_ui_enqueue_set_text(const char *id, size_t id_len,
                                     const char *text, size_t text_len);

/* parent_id 可为 NULL（落到 root）；len 同步 */
bool dynamic_app_ui_enqueue_create_label(const char *id, size_t id_len,
                                         const char *parent_id, size_t parent_len);
bool dynamic_app_ui_enqueue_create_panel(const char *id, size_t id_len,
                                         const char *parent_id, size_t parent_len);
bool dynamic_app_ui_enqueue_create_button(const char *id, size_t id_len,
                                          const char *parent_id, size_t parent_len);

bool dynamic_app_ui_enqueue_set_style(const char *id, size_t id_len,
                                      dynamic_app_style_key_t key,
                                      int32_t a, int32_t b, int32_t c, int32_t d);

/* 把指定 id 的对象升级为"根监听器"——在它上面挂一个 LVGL cb，
 * 子对象冒泡上来的 click 都从这里捕获，事件入队时携带"被点中对象"的 id。
 * 通常脚本会对最外层 root container 调一次。 */
bool dynamic_app_ui_enqueue_attach_root_listener(const char *id, size_t id_len);

/* 销毁单个对象：lv_obj_del(obj) + 释放 registry slot。
 * 调用方（JS VDOM.destroy）负责自底向上递归，C 侧只处理一个 id。
 * 防御：若 JS 没递归就直接 destroy 父对象，LVGL 会级联删子对象，
 *       此时遗留 slot 会在 drain 的 lv_obj_is_valid 检查里被清。 */
bool dynamic_app_ui_enqueue_destroy(const char *id, size_t id_len);

/* ---------------- UI -> Script ---------------- */

bool dynamic_app_ui_pop_event(dynamic_app_ui_event_t *out);
void dynamic_app_ui_clear_event_queue(void);

#ifdef __cplusplus
}
#endif
