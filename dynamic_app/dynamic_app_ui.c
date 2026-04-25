/* ============================================================================
 * dynamic_app_ui.c —— UI 侧"调度层"
 *
 * 职责：
 *   1. 持有共享全局状态（s_ui_queue / s_event_queue / s_root / s_registry / fonts）
 *   2. 接收脚本入队的命令（enqueue_*）
 *   3. UI Task 主循环里 drain 命令并分发：CREATE / SET_TEXT / SET_STYLE / ATTACH_CLICK
 *   4. 反向事件入队（on_lv_click）+ 出队（pop_event）
 *   5. 生命周期：init / set_root / set_fonts / unregister_all / clear_event_queue
 *
 * 不做的事：
 *   - 字符串拷贝细节 → registry 文件（utf8_copy_trunc）
 *   - id↔obj 映射查找 → registry 文件（registry_find / resolve_parent）
 *   - 样式 9 个 case  → styles 文件（apply_style）
 *
 * 文件目录：
 *   §1. 全局变量与配置
 *   §2. 生命周期
 *   §3. 字体注入 / root 门禁
 *   §4. Script→UI 入队 API
 *   §5. UI→Script 反向事件 API
 *   §6. drain 主分发
 * ========================================================================= */

#include "dynamic_app_ui.h"
#include "dynamic_app_ui_internal.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "dynamic_app_ui";

/* ============================================================================
 * §1. 全局变量与配置
 * ========================================================================= */

#define UI_QUEUE_LEN 128   /* 命令队列容量。脚本一次 build 可能瞬间灌 60+ 条 */

QueueHandle_t        s_ui_queue    = NULL;
QueueHandle_t        s_event_queue = NULL;
volatile lv_obj_t   *s_root        = NULL;
ui_registry_entry_t  s_registry[DYNAMIC_APP_UI_REGISTRY_MAX];

/* 字体由 page 通过 set_fonts 注入，避免本组件反向依赖 app 层 */
const lv_font_t *s_font_text  = NULL;
const lv_font_t *s_font_title = NULL;
const lv_font_t *s_font_huge  = NULL;

/* 旧的"上层注册一个 label"路径（label 自注册之前的兼容 API） */
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
 * §2. 生命周期
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

void dynamic_app_ui_unregister_all(void)
{
    /* LVGL 对象本身由页面 lv_obj_del(screen) 级联释放，这里只清 registry。 */
    memset(s_registry, 0, sizeof(s_registry));
    dynamic_app_ui_clear_event_queue();
}

/* ============================================================================
 * §3. 字体注入 / root 门禁
 * ========================================================================= */

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

/* ============================================================================
 * §4. Script→UI 入队 API
 *
 *   全部用 100ms 阻塞 send：队列满则让 Script Task 等 UI 消化，绝不丢命令。
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

/* CREATE_LABEL/PANEL/BUTTON 共用入队逻辑：仅 cmd.type 不同 */
static bool enqueue_create(dynamic_app_ui_cmd_type_t type,
                           const char *id, size_t id_len,
                           const char *parent_id, size_t parent_len)
{
    if (!s_ui_queue || !id) return false;
    if (s_root == NULL) return false;   /* root 门禁：只允许在 PAGE_DYNAMIC_APP 期间创建 */

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = type;
    utf8_copy_trunc(cmd.id,           sizeof(cmd.id),           id,        id_len);
    utf8_copy_trunc(cmd.u.parent_id,  sizeof(cmd.u.parent_id),  parent_id, parent_len);

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

bool dynamic_app_ui_enqueue_attach_root_listener(const char *id, size_t id_len)
{
    if (!s_ui_queue || !id) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_ATTACH_ROOT_LISTENER;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);

    if (cmd.id[0] == '\0') return false;
    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

/* ============================================================================
 * §5. UI→Script 反向事件
 *
 *   on_lv_click 在 LVGL UI 线程上下文执行：绝不能 JS_Call，只能入队。
 *   pop_event 由 Script Task 主循环调用。
 *
 *   Phase 3：新增 on_lv_root_click —— 用于 ATTACH_ROOT_LISTENER 路径。
 *   点击沿 LVGL 父链冒泡到 root，cb 里用 lv_event_get_target 拿到被点
 *   的真正子对象，反查 registry 得到 node_id 字符串，一起入队。
 * ========================================================================= */

static void on_lv_click(lv_event_t *e)
{
    uint32_t handler_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (handler_id == 0 || !s_event_queue) return;

    dynamic_app_ui_event_t ev = {0};
    ev.handler_id = handler_id;
    /* 队列满则丢弃，绝不阻塞 LVGL 调度 */
    (void)xQueueSend(s_event_queue, &ev, 0);
}

static void on_lv_root_click(lv_event_t *e)
{
    if (!s_event_queue) return;

    /* target = 冒泡起点的叶子对象；current_target = 挂 cb 的 root 本身。
     * 这里需要叶子，才能把"被点中是谁"告诉 JS。
     * LVGL v9 用 lv_event_get_target_obj 显式拿 lv_obj_t*。 */
    lv_obj_t *target = lv_event_get_target_obj(e);
    if (!target) return;

    int slot = registry_find_by_obj(target);
    if (slot < 0) return;   /* 非 registry 管辖的对象，忽略 */

    dynamic_app_ui_event_t ev = {0};
    ev.handler_id = 0;   /* 走 delegation 路径，handler_id 不用 */
    strncpy(ev.node_id, s_registry[slot].id, sizeof(ev.node_id) - 1);
    ev.node_id[sizeof(ev.node_id) - 1] = '\0';

    (void)xQueueSend(s_event_queue, &ev, 0);
}

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
 * §6. drain 主分发
 *
 *   被 UI 线程主循环（app_main.c）周期性调用。
 *   每次最多消 max_count 条命令，避免长时间阻塞 LVGL 渲染。
 *
 *   分发 6 条路径：
 *     CREATE_LABEL / PANEL / BUTTON → do_create()
 *     SET_TEXT      → 查 registry → lv_label_set_text
 *     SET_STYLE     → 查 registry → apply_style()  [styles.c]
 *     ATTACH_CLICK  → 查 registry → lv_obj_add_event_cb(on_lv_click)
 * ========================================================================= */

/* 创建 LVGL 对象并入 registry。返回 0 = 成功。 */
static int do_create(const dynamic_app_ui_command_t *cmd, ui_obj_type_t type)
{
    lv_obj_t *root = (lv_obj_t *)s_root;
    if (!root || !lv_obj_is_valid(root)) {
        s_root = NULL;
        return -1;
    }

    /* 同 id 复用：已存在且对象有效则直接返回 */
    int slot = registry_find(cmd->id);
    if (slot >= 0) {
        if (s_registry[slot].obj && lv_obj_is_valid(s_registry[slot].obj)) {
            return 0;
        }
        /* 对象已失效，回收槽位重建 */
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
            /* 默认透明、无边框、无 pad；保留 SCROLLABLE，脚本可后续覆盖 */
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
            /* 按下态：青色蒙层，给点击一个明显的视觉反馈（与菜单页一致） */
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x06B6D4), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa  (obj, LV_OPA_30,              LV_STATE_PRESSED);
            break;
        }
    }

    if (!obj) return -3;

    /* Phase 3: 默认开启事件冒泡，让 click 能传到 root listener。
     * LVGL 默认 EVENT_BUBBLE 关闭 —— 子对象的事件不会触发父级 cb。
     * 我们这里统一开启，这样无论有没有 attachRootListener 都不影响功能；
     * 一对一 onClick 路径下，handler_id 直接绑在该 obj 上，冒泡不影响触发。 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);

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
                apply_style(obj, &cmd);   /* → styles.c */
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
                    /* 首版禁止重复绑定，避免 GCRef 泄漏 */
                    ESP_LOGW(TAG, "onClick already bound on id=%s, ignore", cmd.id);
                    break;
                }
                lv_obj_add_event_cb(obj, on_lv_click, LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)cmd.u.handler_id);
                s_registry[slot].click_handler_id = cmd.u.handler_id;
                break;
            }

            case DYNAMIC_APP_UI_CMD_ATTACH_ROOT_LISTENER: {
                int slot = registry_find(cmd.id);
                if (slot < 0) {
                    ESP_LOGW(TAG, "attach_root_listener: id %s not found", cmd.id);
                    break;
                }
                lv_obj_t *obj = s_registry[slot].obj;
                if (!obj || !lv_obj_is_valid(obj)) {
                    s_registry[slot].obj = NULL;
                    break;
                }
                /* user_data 不用，target 信息从 lv_event_get_target 取 */
                lv_obj_add_event_cb(obj, on_lv_root_click, LV_EVENT_CLICKED, NULL);
                break;
            }

            default:
                break;
        }
    }
}
