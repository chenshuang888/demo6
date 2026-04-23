#include "dynamic_app_ui.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "dynamic_app_ui";

#define UI_QUEUE_LEN 128

/* 字体指针由 page 注入，避免本组件反向依赖 app 层 */
static const lv_font_t *s_font_text  = NULL;
static const lv_font_t *s_font_title = NULL;
static const lv_font_t *s_font_huge  = NULL;

/* 只允许在 PAGE_DYNAMIC_APP 创建 UI：该 root 仅由该页面的 UI Task 设置/清空 */
static volatile lv_obj_t *s_root = NULL;

typedef enum {
    UI_OBJ_LABEL = 1,
    UI_OBJ_PANEL,
    UI_OBJ_BUTTON,
} ui_obj_type_t;

typedef struct {
    bool used;
    ui_obj_type_t type;
    char id[DYNAMIC_APP_UI_ID_MAX_LEN];
    lv_obj_t *obj;
    uint32_t click_handler_id;   /* 0 = 未绑定 */
} ui_registry_entry_t;

static QueueHandle_t s_ui_queue = NULL;
static QueueHandle_t s_event_queue = NULL;
static ui_registry_entry_t s_registry[DYNAMIC_APP_UI_REGISTRY_MAX];

/* ============================================================================
 * UTF-8 安全截断
 * ========================================================================= */

static size_t utf8_truncate_len(const char *s, size_t len)
{
    if (!s) return 0;
    if (len == 0) return 0;

    size_t n = len;
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if ((c & 0x80) == 0) {
            return n;
        }

        size_t start = n - 1;
        while (start > 0 && (((unsigned char)s[start] & 0xC0) == 0x80)) {
            start--;
        }

        unsigned char lead = (unsigned char)s[start];
        size_t need = 1;
        if ((lead & 0xE0) == 0xC0) need = 2;
        else if ((lead & 0xF0) == 0xE0) need = 3;
        else if ((lead & 0xF8) == 0xF0) need = 4;
        else {
            n = start;
            continue;
        }

        if (start + need <= n) {
            return n;
        }
        n = start;
    }
    return 0;
}

static void utf8_copy_trunc(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || src_len == 0) return;

    size_t max_copy = dst_size - 1;
    if (src_len < max_copy) max_copy = src_len;
    size_t safe_len = utf8_truncate_len(src, max_copy);
    if (safe_len > 0) {
        memcpy(dst, src, safe_len);
    }
    dst[safe_len] = '\0';
}

/* ============================================================================
 * Registry helpers
 * ========================================================================= */

static int registry_find(const char *id)
{
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < DYNAMIC_APP_UI_REGISTRY_MAX; i++) {
        if (!s_registry[i].used) continue;
        if (strncmp(s_registry[i].id, id, sizeof(s_registry[i].id)) == 0) {
            return i;
        }
    }
    return -1;
}

static int registry_alloc(const char *id, ui_obj_type_t type, lv_obj_t *obj)
{
    for (int i = 0; i < DYNAMIC_APP_UI_REGISTRY_MAX; i++) {
        if (!s_registry[i].used) {
            s_registry[i].used = true;
            s_registry[i].type = type;
            s_registry[i].obj = obj;
            s_registry[i].click_handler_id = 0;
            strncpy(s_registry[i].id, id, sizeof(s_registry[i].id) - 1);
            s_registry[i].id[sizeof(s_registry[i].id) - 1] = '\0';
            return i;
        }
    }
    return -1;
}

/* parent_id 为空字符串 / NULL / 找不到 → 回落到 s_root */
static lv_obj_t *resolve_parent(const char *parent_id)
{
    lv_obj_t *root = (lv_obj_t *)s_root;
    if (!parent_id || parent_id[0] == '\0') return root;

    int slot = registry_find(parent_id);
    if (slot < 0) {
        ESP_LOGW(TAG, "parent id '%s' not found, fallback to root", parent_id);
        return root;
    }
    lv_obj_t *p = s_registry[slot].obj;
    if (!p || !lv_obj_is_valid(p)) {
        ESP_LOGW(TAG, "parent id '%s' obj invalid, fallback to root", parent_id);
        return root;
    }
    return p;
}

/* ============================================================================
 * LVGL 反向事件回调（在 UI 线程上下文）
 * ========================================================================= */

static void on_lv_click(lv_event_t *e)
{
    uint32_t handler_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (handler_id == 0 || !s_event_queue) return;

    dynamic_app_ui_event_t ev = { .handler_id = handler_id };
    /* 队列满则丢弃，绝不阻塞 LVGL 调度。 */
    (void)xQueueSend(s_event_queue, &ev, 0);
}

/* ============================================================================
 * 生命周期
 * ========================================================================= */

esp_err_t dynamic_app_ui_init(void)
{
    if (s_ui_queue && s_event_queue) {
        return ESP_OK;
    }

    if (!s_ui_queue) {
        s_ui_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(dynamic_app_ui_command_t));
        if (!s_ui_queue) return ESP_ERR_NO_MEM;
    }
    if (!s_event_queue) {
        s_event_queue = xQueueCreate(DYNAMIC_APP_UI_EVENT_QUEUE_LEN,
                                     sizeof(dynamic_app_ui_event_t));
        if (!s_event_queue) return ESP_ERR_NO_MEM;
    }

    memset(s_registry, 0, sizeof(s_registry));
    return ESP_OK;
}

void dynamic_app_ui_set_root(lv_obj_t *root)
{
    s_root = root;
}

void dynamic_app_ui_set_fonts(const lv_font_t *text,
                              const lv_font_t *title,
                              const lv_font_t *huge)
{
    s_font_text  = text;
    s_font_title = title;
    s_font_huge  = huge;
}

void dynamic_app_ui_unregister_all(void)
{
    /* LVGL 对象本身由页面 lv_obj_del(screen) 级联释放，这里只清 registry。 */
    memset(s_registry, 0, sizeof(s_registry));
    dynamic_app_ui_clear_event_queue();
}

esp_err_t dynamic_app_ui_register_label(const char *id, lv_obj_t *obj)
{
    if (!id || id[0] == '\0' || !obj) return ESP_ERR_INVALID_ARG;
    if (!lv_obj_is_valid(obj)) return ESP_ERR_INVALID_STATE;
    if (!lv_obj_has_class(obj, &lv_label_class)) {
        ESP_LOGE(TAG, "register failed: obj is not label (id=%s)", id);
        return ESP_ERR_INVALID_ARG;
    }

    int slot = registry_find(id);
    if (slot >= 0) {
        s_registry[slot].obj = obj;
        s_registry[slot].type = UI_OBJ_LABEL;
        return ESP_OK;
    }

    if (registry_alloc(id, UI_OBJ_LABEL, obj) < 0) {
        ESP_LOGE(TAG, "UI registry full");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ============================================================================
 * Script -> UI 入队
 * ========================================================================= */

bool dynamic_app_ui_enqueue_set_text(const char *id, size_t id_len,
                                     const char *text, size_t text_len)
{
    if (!s_ui_queue || !id || !text) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_SET_TEXT;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    utf8_copy_trunc(cmd.u.text, sizeof(cmd.u.text), text, text_len);

    if (cmd.id[0] == '\0') return false;

    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

static bool enqueue_create(dynamic_app_ui_cmd_type_t type,
                           const char *id, size_t id_len,
                           const char *parent_id, size_t parent_len)
{
    if (!s_ui_queue || !id) return false;
    if (s_root == NULL) return false;   /* root 门禁 */

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = type;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    utf8_copy_trunc(cmd.u.parent_id, sizeof(cmd.u.parent_id), parent_id, parent_len);

    if (cmd.id[0] == '\0') return false;

    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool dynamic_app_ui_enqueue_create_label(const char *id, size_t id_len,
                                         const char *parent_id, size_t parent_len)
{
    return enqueue_create(DYNAMIC_APP_UI_CMD_CREATE_LABEL, id, id_len, parent_id, parent_len);
}

bool dynamic_app_ui_enqueue_create_panel(const char *id, size_t id_len,
                                         const char *parent_id, size_t parent_len)
{
    return enqueue_create(DYNAMIC_APP_UI_CMD_CREATE_PANEL, id, id_len, parent_id, parent_len);
}

bool dynamic_app_ui_enqueue_create_button(const char *id, size_t id_len,
                                          const char *parent_id, size_t parent_len)
{
    return enqueue_create(DYNAMIC_APP_UI_CMD_CREATE_BUTTON, id, id_len, parent_id, parent_len);
}

bool dynamic_app_ui_enqueue_set_style(const char *id, size_t id_len,
                                      dynamic_app_style_key_t key,
                                      int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (!s_ui_queue || !id) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_SET_STYLE;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    cmd.u.style.key = (int32_t)key;
    cmd.u.style.a = a;
    cmd.u.style.b = b;
    cmd.u.style.c = c;
    cmd.u.style.d = d;

    if (cmd.id[0] == '\0') return false;
    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool dynamic_app_ui_enqueue_attach_click(const char *id, size_t id_len,
                                         uint32_t handler_id)
{
    if (!s_ui_queue || !id || handler_id == 0) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_ATTACH_CLICK;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    cmd.u.handler_id = handler_id;

    if (cmd.id[0] == '\0') return false;
    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

/* ============================================================================
 * UI -> Script 反向事件
 * ========================================================================= */

bool dynamic_app_ui_pop_event(dynamic_app_ui_event_t *out)
{
    if (!s_event_queue || !out) return false;
    return xQueueReceive(s_event_queue, out, 0) == pdTRUE;
}

void dynamic_app_ui_clear_event_queue(void)
{
    if (!s_event_queue) return;
    xQueueReset(s_event_queue);
}

/* ============================================================================
 * Drain 各命令分支
 * ========================================================================= */

/* JS 侧约定的 align 枚举顺序，与 sys.align.* 对齐 */
static const lv_align_t k_align_map[] = {
    LV_ALIGN_TOP_LEFT,    /* 0 */
    LV_ALIGN_TOP_MID,     /* 1 */
    LV_ALIGN_TOP_RIGHT,   /* 2 */
    LV_ALIGN_LEFT_MID,    /* 3 */
    LV_ALIGN_CENTER,      /* 4 */
    LV_ALIGN_RIGHT_MID,   /* 5 */
    LV_ALIGN_BOTTOM_LEFT, /* 6 */
    LV_ALIGN_BOTTOM_MID,  /* 7 */
    LV_ALIGN_BOTTOM_RIGHT,/* 8 */
};
#define K_ALIGN_MAP_LEN ((int)(sizeof(k_align_map) / sizeof(k_align_map[0])))

static lv_coord_t resolve_size(int32_t v)
{
    /* 约定：v >= 0 → 像素；v < 0 → lv_pct(-v) */
    if (v < 0) return lv_pct(-v);
    return (lv_coord_t)v;
}

static const lv_font_t *resolve_font(int32_t a)
{
    switch (a) {
        case 0: return s_font_text;
        case 1: return s_font_title;
        case 2: return s_font_huge;
        default: return s_font_text;
    }
}

static void apply_style(lv_obj_t *obj, const dynamic_app_ui_command_t *cmd)
{
    int32_t a = cmd->u.style.a;
    int32_t b = cmd->u.style.b;
    int32_t c = cmd->u.style.c;
    int32_t d = cmd->u.style.d;

    switch ((dynamic_app_style_key_t)cmd->u.style.key) {
        case DYNAMIC_APP_STYLE_BG_COLOR:
            lv_obj_set_style_bg_color(obj, lv_color_hex((uint32_t)a), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            break;
        case DYNAMIC_APP_STYLE_TEXT_COLOR:
            lv_obj_set_style_text_color(obj, lv_color_hex((uint32_t)a), 0);
            break;
        case DYNAMIC_APP_STYLE_RADIUS:
            lv_obj_set_style_radius(obj, (lv_coord_t)a, 0);
            break;
        case DYNAMIC_APP_STYLE_SIZE:
            lv_obj_set_size(obj, resolve_size(a), resolve_size(b));
            break;
        case DYNAMIC_APP_STYLE_ALIGN:
            if (a >= 0 && a < K_ALIGN_MAP_LEN) {
                lv_obj_align(obj, k_align_map[a], (lv_coord_t)b, (lv_coord_t)c);
            }
            break;
        case DYNAMIC_APP_STYLE_PAD:
            lv_obj_set_style_pad_left(obj,   (lv_coord_t)a, 0);
            lv_obj_set_style_pad_top(obj,    (lv_coord_t)b, 0);
            lv_obj_set_style_pad_right(obj,  (lv_coord_t)c, 0);
            lv_obj_set_style_pad_bottom(obj, (lv_coord_t)d, 0);
            break;
        case DYNAMIC_APP_STYLE_BORDER_BOTTOM:
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(obj, lv_color_hex((uint32_t)a), 0);
            break;
        case DYNAMIC_APP_STYLE_FLEX:
            lv_obj_set_flex_flow(obj,
                a == 1 ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(obj,
                LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(obj, 0, 0);
            break;
        case DYNAMIC_APP_STYLE_FONT: {
            const lv_font_t *f = resolve_font(a);
            if (f) lv_obj_set_style_text_font(obj, f, 0);
            break;
        }
        default:
            ESP_LOGW(TAG, "unknown style key %d", (int)cmd->u.style.key);
            break;
    }
}

/* 创建 LVGL 对象并入 registry。返回 0 表示成功，非 0 表示失败/丢弃。 */
static int do_create(const dynamic_app_ui_command_t *cmd, ui_obj_type_t type)
{
    lv_obj_t *root = (lv_obj_t *)s_root;
    if (!root || !lv_obj_is_valid(root)) {
        s_root = NULL;
        return -1;
    }

    /* 同 id 复用 */
    int slot = registry_find(cmd->id);
    if (slot >= 0) {
        if (s_registry[slot].obj && lv_obj_is_valid(s_registry[slot].obj)) {
            return 0;
        }
        /* obj 已失效，回收槽位 */
        s_registry[slot].used = false;
    }

    lv_obj_t *parent = resolve_parent(cmd->u.parent_id);
    if (!parent || !lv_obj_is_valid(parent)) {
        ESP_LOGW(TAG, "parent invalid, drop create id=%s", cmd->id);
        return -2;
    }

    lv_obj_t *obj = NULL;
    switch (type) {
        case UI_OBJ_LABEL: {
            obj = lv_label_create(parent);
            lv_label_set_text(obj, "");
            lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
            break;
        }
        case UI_OBJ_PANEL: {
            obj = lv_obj_create(parent);
            lv_obj_remove_style_all(obj);
            /* 默认透明、无边框、无 pad；保留 SCROLLABLE，脚本若不需要可后续改样式覆盖 */
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_pad_all(obj, 0, 0);
            break;
        }
        case UI_OBJ_BUTTON: {
            obj = lv_btn_create(parent);
            lv_obj_remove_style_all(obj);
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_shadow_width(obj, 0, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            /* 按下态：青色蒙层，给点击一个明显的视觉反馈 */
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x06B6D4), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(obj, LV_OPA_30, LV_STATE_PRESSED);
            break;
        }
    }

    if (!obj) return -3;

    if (registry_alloc(cmd->id, type, obj) < 0) {
        ESP_LOGW(TAG, "UI registry full, drop create id=%s", cmd->id);
        lv_obj_del(obj);
        return -4;
    }
    return 0;
}

void dynamic_app_ui_drain(int max_count)
{
    if (!s_ui_queue || max_count <= 0) return;

    dynamic_app_ui_command_t cmd;
    int handled = 0;

    while (handled < max_count && xQueueReceive(s_ui_queue, &cmd, 0) == pdTRUE) {
        handled++;

        switch (cmd.type) {
            case DYNAMIC_APP_UI_CMD_CREATE_LABEL:
                (void)do_create(&cmd, UI_OBJ_LABEL);
                break;

            case DYNAMIC_APP_UI_CMD_CREATE_PANEL:
                (void)do_create(&cmd, UI_OBJ_PANEL);
                break;

            case DYNAMIC_APP_UI_CMD_CREATE_BUTTON:
                (void)do_create(&cmd, UI_OBJ_BUTTON);
                break;

            case DYNAMIC_APP_UI_CMD_SET_TEXT: {
                int slot = registry_find(cmd.id);
                if (slot < 0) break;
                lv_obj_t *obj = s_registry[slot].obj;
                if (!obj || !lv_obj_is_valid(obj)) {
                    s_registry[slot].obj = NULL;
                    break;
                }
                if (lv_obj_has_class(obj, &lv_label_class)) {
                    lv_label_set_text(obj, cmd.u.text);
                }
                break;
            }

            case DYNAMIC_APP_UI_CMD_SET_STYLE: {
                int slot = registry_find(cmd.id);
                if (slot < 0) break;
                lv_obj_t *obj = s_registry[slot].obj;
                if (!obj || !lv_obj_is_valid(obj)) {
                    s_registry[slot].obj = NULL;
                    break;
                }
                apply_style(obj, &cmd);
                break;
            }

            case DYNAMIC_APP_UI_CMD_ATTACH_CLICK: {
                int slot = registry_find(cmd.id);
                if (slot < 0) break;
                lv_obj_t *obj = s_registry[slot].obj;
                if (!obj || !lv_obj_is_valid(obj)) {
                    s_registry[slot].obj = NULL;
                    break;
                }
                if (s_registry[slot].click_handler_id != 0) {
                    /* 首版：禁止重复绑定，避免 GCRef 泄漏 */
                    ESP_LOGW(TAG, "onClick already bound on id=%s, ignore", cmd.id);
                    break;
                }
                lv_obj_add_event_cb(obj, on_lv_click, LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)cmd.u.handler_id);
                s_registry[slot].click_handler_id = cmd.u.handler_id;
                break;
            }

            default:
                break;
        }
    }
}
