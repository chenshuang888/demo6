/* ============================================================================
 * dynamic_app_ui.c —— UI 侧"调度层"
 *
 * 职责：
 *   1. 持有共享全局状态（s_ui_queue / s_event_queue / s_root / s_registry / fonts）
 *   2. 接收脚本入队的命令（enqueue_*）
 *   3. UI Task 主循环里 drain 命令并分发：CREATE / SET_TEXT / SET_STYLE / ATTACH_ROOT_LISTENER
 *   4. 反向事件入队（on_lv_root_event）+ 出队（pop_event）
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

#include <stdlib.h>
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

/* 当前手势会话状态（同时只有一根手指，单 indev 假设）。
 * 在 root listener 的 PRESSED 中记录，PRESSING 累计位移，
 * RELEASED 清空。unregister_all 也会清，避免残留指针。 */
static struct {
    lv_obj_t *active_target;
    char      active_id[DYNAMIC_APP_UI_ID_MAX_LEN];
    int16_t   acc_dx, acc_dy;
} s_gesture = { .active_target = NULL };

/* 一次性 build-ready 回调：drain 在处理完 ATTACH_ROOT_LISTENER 后触发并自动清空。
 * 用于宿主页判断"脚本已经把对象树搭完，可以提交给 router 切屏"。 */
static dynamic_app_ui_ready_cb_t s_ready_cb       = NULL;
static void                     *s_ready_cb_ud   = NULL;

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
    /* 手势状态也清，避免下次进 app 时残留指针 */
    s_gesture.active_target = NULL;
    s_gesture.active_id[0] = '\0';
    s_gesture.acc_dx = s_gesture.acc_dy = 0;
    dynamic_app_ui_clear_event_queue();
    /* prepare 中途取消时，没机会触发 ready_cb，这里强清避免下次误触发 */
    s_ready_cb    = NULL;
    s_ready_cb_ud = NULL;
}

/* ============================================================================
 * §3. 字体注入 / root 门禁
 * ========================================================================= */

void dynamic_app_ui_set_root(lv_obj_t *root)
{
    s_root = root;
}

void dynamic_app_ui_set_ready_cb(dynamic_app_ui_ready_cb_t cb, void *user_data)
{
    s_ready_cb    = cb;
    s_ready_cb_ud = user_data;
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

bool dynamic_app_ui_enqueue_attach_root_listener(const char *id, size_t id_len)
{
    if (!s_ui_queue || !id) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_ATTACH_ROOT_LISTENER;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);

    if (cmd.id[0] == '\0') return false;
    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool dynamic_app_ui_enqueue_destroy(const char *id, size_t id_len)
{
    if (!s_ui_queue || !id) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_DESTROY;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);

    if (cmd.id[0] == '\0') return false;
    return xQueueSend(s_ui_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

/* ============================================================================
 * §5. UI→Script 反向事件
 *
 *   on_lv_root_event 在 LVGL UI 线程上下文执行：绝不能 JS_Call，只能入队。
 *   pop_event 由 Script Task 主循环调用。
 *
 *   一个 cb 处理 4 种 LVGL 事件：
 *     PRESSED   → EV_PRESS   记录按下对象，重置累计位移
 *     PRESSING  → EV_DRAG    每帧从 indev 拿增量，累计 ≥2px 才入队
 *     RELEASED  → EV_RELEASE 释放按下对象
 *     CLICKED   → EV_CLICK   LVGL 已保证只在没拖动时触发
 *
 *   PRESSING/RELEASED 的 target 用按下时记下的，避免手指拖出按钮范围
 *   后 LVGL 报错 target 导致业务接到不一致的 id。
 * ========================================================================= */

/* 当前手势会话状态（同时只有一根手指，单 indev 假设）
 * 定义在文件顶部 s_gesture，这里的 cb 直接读写。 */

#define DRAG_THRESHOLD_PX  2

static void enqueue_event(uint8_t type, const char *id, int16_t dx, int16_t dy)
{
    if (!s_event_queue || !id || id[0] == '\0') return;
    dynamic_app_ui_event_t ev = {0};
    ev.type = type;
    ev.dx = dx;
    ev.dy = dy;
    strncpy(ev.node_id, id, sizeof(ev.node_id) - 1);
    ev.node_id[sizeof(ev.node_id) - 1] = '\0';
    (void)xQueueSend(s_event_queue, &ev, 0);
}

static void on_lv_root_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_t *target = lv_event_get_target_obj(e);
        if (!target) return;
        int slot = registry_find_by_obj(target);
        if (slot < 0) return;
        s_gesture.active_target = target;
        s_gesture.acc_dx = s_gesture.acc_dy = 0;
        strncpy(s_gesture.active_id, s_registry[slot].id,
                sizeof(s_gesture.active_id) - 1);
        s_gesture.active_id[sizeof(s_gesture.active_id) - 1] = '\0';
        enqueue_event(DYNAMIC_APP_UI_EV_PRESS, s_gesture.active_id, 0, 0);
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!s_gesture.active_target) return;
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t v = {0};
        lv_indev_get_vect(indev, &v);
        s_gesture.acc_dx += (int16_t)v.x;
        s_gesture.acc_dy += (int16_t)v.y;
        if (abs(s_gesture.acc_dx) + abs(s_gesture.acc_dy) >= DRAG_THRESHOLD_PX) {
            enqueue_event(DYNAMIC_APP_UI_EV_DRAG, s_gesture.active_id,
                          s_gesture.acc_dx, s_gesture.acc_dy);
            s_gesture.acc_dx = s_gesture.acc_dy = 0;
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        if (!s_gesture.active_target) return;
        enqueue_event(DYNAMIC_APP_UI_EV_RELEASE, s_gesture.active_id, 0, 0);
        s_gesture.active_target = NULL;
        s_gesture.active_id[0] = '\0';
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        /* CLICKED 只在没拖动时触发；用 LVGL 报告的 target，
         * 因为 active_target 在 RELEASED 已清。 */
        lv_obj_t *target = lv_event_get_target_obj(e);
        if (!target) return;
        int slot = registry_find_by_obj(target);
        if (slot < 0) return;
        enqueue_event(DYNAMIC_APP_UI_EV_CLICK, s_registry[slot].id, 0, 0);
        return;
    }

    if (code == LV_EVENT_LONG_PRESSED) {
        /* LVGL 默认 ~400ms 触发；与 PRESSED 共享 active_target，
         * 没拖动时才发（拖过会被 LVGL 自己抑制） */
        if (!s_gesture.active_target) return;
        enqueue_event(DYNAMIC_APP_UI_EV_LONG_PRESS, s_gesture.active_id, 0, 0);
        return;
    }
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
 *     SET_TEXT             → 查 registry → lv_label_set_text
 *     SET_STYLE            → 查 registry → apply_style()  [styles.c]
 *     ATTACH_ROOT_LISTENER → 查 registry → lv_obj_add_event_cb(on_lv_root_event ×4)
 *     DESTROY              → 查 registry → lv_obj_del + slot.used=false
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
            /* 默认透明、无边框、无 pad；脚本可后续覆盖 */
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_pad_all(obj, 0, 0);
            /* 默认禁用滚动：嵌套页面里多个 panel 同时 scrollable
             * 会出现"页面内容意外被推走"的怪象。需要滚动时业务自己开。 */
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
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

    /* 默认开启事件冒泡，让 click 能传到 root listener。
     * LVGL 默认 EVENT_BUBBLE 关闭 —— 子对象的事件不会触发父级 cb。
     * 我们这里统一开启，root listener 才能收到所有子按钮的点击。 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 默认开启 PRESS_LOCK：手指按下后即使移出 obj 范围，PRESSING/RELEASED
     * 仍持续发给该 obj。LVGL 默认只 button 自带这个 flag；label/panel 没有，
     * 导致用户按在卡片内的子 label 上稍微一动 LVGL 就视为 release，
     * 我们的手势状态机会立即回弹。统一开启此 flag 后，按在哪都能拖。 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);

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
                /* 一个 cb 订四种事件，cb 内部按 code 分发 */
                lv_obj_add_event_cb(obj, on_lv_root_event, LV_EVENT_PRESSED,      NULL);
                lv_obj_add_event_cb(obj, on_lv_root_event, LV_EVENT_PRESSING,     NULL);
                lv_obj_add_event_cb(obj, on_lv_root_event, LV_EVENT_RELEASED,     NULL);
                lv_obj_add_event_cb(obj, on_lv_root_event, LV_EVENT_CLICKED,      NULL);
                lv_obj_add_event_cb(obj, on_lv_root_event, LV_EVENT_LONG_PRESSED, NULL);

                /* "build ready" 信号：脚本通常在 mount + 首次 rerender 完成后
                 * 才调 attachRootListener，因此这里也是宿主页可以放心切屏的时机。
                 * 一次性触发：取出 cb 后立即清空，避免重复触发。 */
                if (s_ready_cb) {
                    dynamic_app_ui_ready_cb_t cb = s_ready_cb;
                    void *ud = s_ready_cb_ud;
                    s_ready_cb    = NULL;
                    s_ready_cb_ud = NULL;
                    cb(ud);
                }
                break;
            }

            case DYNAMIC_APP_UI_CMD_DESTROY: {
                int slot = registry_find(cmd.id);
                if (slot < 0) break;   /* 已被前一次 destroy 释放，幂等 */
                lv_obj_t *obj = s_registry[slot].obj;
                if (obj && lv_obj_is_valid(obj)) {
                    /* lv_obj_del 会级联删子对象。约定 JS 侧自底向上调 destroy，
                     * 子 slot 已先被释放；万一没递归，残留 slot 留待后续操作时
                     * 由 lv_obj_is_valid 检查清掉，最坏 page 退出 unregister_all。 */
                    lv_obj_del(obj);
                }
                s_registry[slot].used = false;
                s_registry[slot].obj = NULL;
                s_registry[slot].id[0] = '\0';
                break;
            }

            default:
                break;
        }
    }
}
