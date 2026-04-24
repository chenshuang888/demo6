/* ============================================================================
 * dynamic_app_natives.c —— JS 侧"API 层"
 *
 * 职责：
 *   把 C 系统能力翻译成 JS 函数。所有 JS 脚本能调到的 sys.xxx 都在这里实现。
 *
 * 文件目录：
 *   §1. 通用 helper（参数解析、id/parent 提取、回调表 reset）
 *   §2. JS Native：sys.log
 *   §3. JS Native：sys.ui.*  （setText/createLabel/createPanel/createButton/setStyle/onClick）
 *   §4. JS Native：sys.time.* （uptimeMs/uptimeStr）
 *   §5. JS Native：setInterval / clearInterval
 *   §6. tick 循环服务（run_intervals_once / drain_ui_events_once / next_deadline）
 *   §7. cfunc 表注册：register（runtime.c 调）
 *   §8. JS 全局对象绑定：bind（runtime.c 调）
 *
 * 单个 native fn 的标准三段式（参考 §3 第一个例子）：
 *   1. argc 校验，必要时 ThrowTypeError
 *   2. JS_ToCStringLen / JS_ToInt32 把 JS 参数取出来
 *   3. 调下层 enqueue_* / 系统 API
 * ========================================================================= */

#include "dynamic_app_internal.h"
#include "dynamic_app_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mqjs.h"
#include "lvgl.h"   /* 只为拿 LV_SYMBOL_* 字面量挂到 sys.symbols */

static const char *TAG = "dynamic_app_natives";

/* ============================================================================
 * §1. 通用 helper
 * ========================================================================= */

/* 从 argv[i] 取一个 string|null|undefined 当作 parent_id：
 *   null/undefined → 表示"无 parent"（pid=NULL, plen=0），让下层回落到 root
 *   string         → 把字符串指针写出
 *   其它类型       → ThrowTypeError 并返回 false
 *
 * holder 持有 JSCStringBuf，调用方栈上分配；valid==true 时 pid 才有效。
 */
typedef struct {
    JSCStringBuf buf;
    bool valid;
} parent_str_t;

static bool extract_parent_id(JSContext *ctx, JSValue v,
                              const char **out_pid, size_t *out_len,
                              parent_str_t *holder)
{
    holder->valid = false;
    *out_pid = NULL;
    *out_len = 0;

    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        return true;   /* 落到 root */
    }
    if (!JS_IsString(ctx, v)) {
        JS_ThrowTypeError(ctx, "parent must be string|null|undefined");
        return false;
    }
    const char *s = JS_ToCStringLen(ctx, out_len, v, &holder->buf);
    if (!s) return false;
    holder->valid = true;
    *out_pid = s;
    return true;
}

/* setInterval / onClick 注册的 GCRef 在 teardown 时统一释放。 */
void dynamic_app_intervals_reset(JSContext *ctx)
{
    for (int i = 0; i < MAX_INTERVALS; i++) {
        if (s_rt.intervals[i].allocated) {
            JS_DeleteGCRef(ctx, &s_rt.intervals[i].func);
            s_rt.intervals[i].allocated = false;
        }
    }
}

void dynamic_app_click_handlers_reset(JSContext *ctx)
{
    for (int i = 0; i < MAX_CLICK_HANDLERS; i++) {
        if (s_rt.handlers[i].allocated) {
            JS_DeleteGCRef(ctx, &s_rt.handlers[i].func);
            s_rt.handlers[i].allocated = false;
        }
    }
}

/* ============================================================================
 * §2. JS Native：sys.log
 * ========================================================================= */

static JSValue js_sys_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;

    JSCStringBuf buf;
    size_t len = 0;
    const char *s = JS_ToCStringLen(ctx, &len, argv[0], &buf);
    if (!s) return JS_EXCEPTION;

    ESP_LOGI("dynapp", "%.*s", (int)len, s);
    return JS_UNDEFINED;
}

/* ============================================================================
 * §3. JS Native：sys.ui.*
 *
 *   全部走"取参数 → 入队 → 返回 bool"模式。
 *   绝不直接调 LVGL，所有动作都丢给 UI 线程的 dynamic_app_ui_drain。
 * ========================================================================= */

/* sys.ui.setText(id, text) */
static JSValue js_sys_ui_set_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "sys.ui.setText(id, text) args missing");
    }

    JSCStringBuf id_buf, text_buf;
    size_t id_len = 0, text_len = 0;

    const char *id   = JS_ToCStringLen(ctx, &id_len,   argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;
    const char *text = JS_ToCStringLen(ctx, &text_len, argv[1], &text_buf);
    if (!text) return JS_EXCEPTION;

    (void)dynamic_app_ui_enqueue_set_text(id, id_len, text, text_len);
    return JS_UNDEFINED;
}

/* createLabel/Panel/Button 的公共骨架。typed enqueue 函数指针由调用方传入。 */
typedef bool (*enqueue_create_fn_t)(const char *, size_t, const char *, size_t);

static JSValue js_create_widget_common(JSContext *ctx, int argc, JSValue *argv,
                                       const char *fn_name,
                                       enqueue_create_fn_t enq)
{
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, fn_name);
    }

    JSCStringBuf id_buf;
    size_t id_len = 0;
    const char *id = JS_ToCStringLen(ctx, &id_len, argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;

    parent_str_t ph;
    const char *pid = NULL;
    size_t plen = 0;
    if (argc >= 2 && !extract_parent_id(ctx, argv[1], &pid, &plen, &ph)) {
        return JS_EXCEPTION;
    }

    bool ok = enq(id, id_len, pid, plen);
    return JS_NewBool(ok ? 1 : 0);
}

/* sys.ui.createLabel(id, parent?) */
static JSValue js_sys_ui_create_label(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return js_create_widget_common(ctx, argc, argv,
        "sys.ui.createLabel(id, parent?) args missing",
        dynamic_app_ui_enqueue_create_label);
}

/* sys.ui.createPanel(id, parent?) */
static JSValue js_sys_ui_create_panel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return js_create_widget_common(ctx, argc, argv,
        "sys.ui.createPanel(id, parent?) args missing",
        dynamic_app_ui_enqueue_create_panel);
}

/* sys.ui.createButton(id, parent?) */
static JSValue js_sys_ui_create_button(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return js_create_widget_common(ctx, argc, argv,
        "sys.ui.createButton(id, parent?) args missing",
        dynamic_app_ui_enqueue_create_button);
}

/* sys.ui.setStyle(id, key, a, b?, c?, d?)
 *   注：JS_ToInt32 在 esp-mquickjs 是 (ctx, int*, val)，
 *       int32_t* 不兼容（xtensa 上 int32_t = long int），必须用 int 接。
 */
static JSValue js_sys_ui_set_style(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "sys.ui.setStyle(id, key, a, b?, c?, d?) args missing");
    }

    JSCStringBuf id_buf;
    size_t id_len = 0;
    const char *id = JS_ToCStringLen(ctx, &id_len, argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;

    int key = 0;
    if (JS_ToInt32(ctx, &key, argv[1])) return JS_EXCEPTION;

    int a = 0, b = 0, c = 0, d = 0;
    if (JS_ToInt32(ctx, &a, argv[2])) return JS_EXCEPTION;
    if (argc >= 4 && JS_ToInt32(ctx, &b, argv[3])) return JS_EXCEPTION;
    if (argc >= 5 && JS_ToInt32(ctx, &c, argv[4])) return JS_EXCEPTION;
    if (argc >= 6 && JS_ToInt32(ctx, &d, argv[5])) return JS_EXCEPTION;

    bool ok = dynamic_app_ui_enqueue_set_style(id, id_len,
        (dynamic_app_style_key_t)key,
        (int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d);
    return JS_NewBool(ok ? 1 : 0);
}

/* sys.ui.onClick(id, fn)
 *
 *   1. 在 s_rt.handlers[] 找空 slot
 *   2. JS_AddGCRef 钉住 JS 函数（防 GC）
 *   3. handler_id = slot+1（0 保留为"无"，便于 lv_event user_data 区分）
 *   4. enqueue ATTACH_CLICK，让 UI 线程调 lv_obj_add_event_cb
 *   5. 入队失败时回滚 GCRef
 */
static JSValue js_sys_ui_on_click(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "sys.ui.onClick(id, fn) args missing");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "sys.ui.onClick: not a function");
    }

    JSCStringBuf id_buf;
    size_t id_len = 0;
    const char *id = JS_ToCStringLen(ctx, &id_len, argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;

    int slot = -1;
    for (int i = 0; i < MAX_CLICK_HANDLERS; i++) {
        if (!s_rt.handlers[i].allocated) { slot = i; break; }
    }
    if (slot < 0) {
        return JS_ThrowInternalError(ctx, "too many click handlers");
    }

    JSValue *pfunc = JS_AddGCRef(ctx, &s_rt.handlers[slot].func);
    *pfunc = argv[1];
    s_rt.handlers[slot].allocated = true;

    uint32_t hid = (uint32_t)(slot + 1);
    if (!dynamic_app_ui_enqueue_attach_click(id, id_len, hid)) {
        JS_DeleteGCRef(ctx, &s_rt.handlers[slot].func);
        s_rt.handlers[slot].allocated = false;
        return JS_NewBool(0);
    }
    return JS_NewBool(1);
}

/* ============================================================================
 * §4. JS Native：sys.time.*
 * ========================================================================= */

static JSValue js_sys_time_uptime_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt64(ctx, dynamic_app_now_ms());
}

static JSValue js_sys_time_uptime_str(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;

    int64_t ms = dynamic_app_now_ms();
    int64_t sec_total = ms / 1000;
    int hours   = (int)(sec_total / 3600);
    int minutes = (int)((sec_total / 60) % 60);
    int seconds = (int)(sec_total % 60);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours % 100, minutes, seconds);
    return JS_NewString(ctx, buf);
}

/* ============================================================================
 * §5. JS Native：setInterval / clearInterval
 * ========================================================================= */

static JSValue js_set_interval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setInterval(fn, ms) args missing");
    }
    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "setInterval: not a function");
    }

    int delay_ms = 0;
    if (JS_ToInt32(ctx, &delay_ms, argv[1])) return JS_EXCEPTION;
    if (delay_ms < 1) delay_ms = 1;

    for (int i = 0; i < MAX_INTERVALS; i++) {
        js_interval_t *t = &s_rt.intervals[i];
        if (!t->allocated) {
            JSValue *pfunc = JS_AddGCRef(ctx, &t->func);
            *pfunc = argv[0];
            t->interval_ms = delay_ms;
            t->next_ms = dynamic_app_now_ms() + delay_ms;
            t->allocated = true;
            return JS_NewInt32(ctx, i);
        }
    }
    return JS_ThrowInternalError(ctx, "too many intervals");
}

static JSValue js_clear_interval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;

    int id = -1;
    if (JS_ToInt32(ctx, &id, argv[0])) return JS_EXCEPTION;
    if (id < 0 || id >= MAX_INTERVALS) return JS_UNDEFINED;

    js_interval_t *t = &s_rt.intervals[id];
    if (t->allocated) {
        JS_DeleteGCRef(ctx, &t->func);
        t->allocated = false;
    }
    return JS_UNDEFINED;
}

/* ============================================================================
 * §6. tick 循环服务
 *
 *   被 dynamic_app.c 的 script_task 主循环周期性调用。
 *
 *   - run_intervals_once：跑所有到期的 setInterval
 *   - drain_ui_events_once：消化 UI→Script 事件队列里的点击事件
 *   - next_interval_deadline_ms：算下一个 deadline 决定 sleep 多久
 * ========================================================================= */

bool dynamic_app_run_intervals_once(JSContext *ctx, int64_t cur_ms)
{
    for (int i = 0; i < MAX_INTERVALS; i++) {
        js_interval_t *t = &s_rt.intervals[i];
        if (!t->allocated) continue;
        if (t->next_ms > cur_ms) continue;

        if (JS_StackCheck(ctx, 2)) {
            dynamic_app_dump_exception(ctx);
            return false;
        }

        JS_PushArg(ctx, t->func.val);
        JS_PushArg(ctx, JS_NULL);
        JSValue ret = JS_Call(ctx, 0);
        if (JS_IsException(ret)) {
            dynamic_app_dump_exception(ctx);
            return false;
        }

        int64_t next = t->next_ms + t->interval_ms;
        if (next <= cur_ms) {
            next = cur_ms + t->interval_ms;
        }
        t->next_ms = next;
    }
    return true;
}

int64_t dynamic_app_next_interval_deadline_ms(int64_t cur_ms)
{
    int64_t next = cur_ms + 1000;
    bool found = false;
    for (int i = 0; i < MAX_INTERVALS; i++) {
        js_interval_t *t = &s_rt.intervals[i];
        if (!t->allocated) continue;
        if (!found || t->next_ms < next) {
            next = t->next_ms;
            found = true;
        }
    }
    if (!found) return cur_ms + 200;
    return next;
}

void dynamic_app_drain_ui_events_once(JSContext *ctx)
{
    /* 每 tick 最多消 8 个点击事件，避免大量回调阻塞主循环 */
    int budget = 8;
    dynamic_app_ui_event_t ev;
    while (budget-- > 0 && dynamic_app_ui_pop_event(&ev)) {
        if (ev.handler_id == 0 || ev.handler_id > MAX_CLICK_HANDLERS) continue;
        js_click_handler_t *h = &s_rt.handlers[ev.handler_id - 1];
        if (!h->allocated) continue;

        if (JS_StackCheck(ctx, 2)) {
            dynamic_app_dump_exception(ctx);
            return;
        }

        JS_PushArg(ctx, h->func.val);
        JS_PushArg(ctx, JS_NULL);
        JSValue ret = JS_Call(ctx, 0);
        if (JS_IsException(ret)) {
            dynamic_app_dump_exception(ctx);
            /* 单个点击回调异常不影响其它，继续 */
        }
    }
}

/* ============================================================================
 * §7. cfunc 表注册
 *
 *   被 runtime.c 的 setup 调用：
 *     1. 给每个自定义 native 分配一个表索引
 *     2. 填 JSCFunctionDef（指向 C 函数 + 参数个数）
 *
 *   如果以后要加新 native，只在这个函数里改：
 *     - 分配 idx
 *     - 填 JSCFunctionDef
 *     - 在 §8 的 bind 里把 idx 转成 JSValue 并挂到 sys.* 上
 * ========================================================================= */

#define DEF_CFN(idx_field, fn, argn) \
    s_rt.cfunc_table[s_rt.idx_field] = (JSCFunctionDef){ \
        .func.generic = (fn), \
        .name = JS_UNDEFINED, \
        .def_type = JS_CFUNC_generic, \
        .arg_count = (argn), \
        .magic = 0, \
    }

void dynamic_app_natives_register(dynamic_app_runtime_t *rt, size_t base_count)
{
    /* 索引分配 */
    rt->func_idx_sys_log              = (int)base_count + 0;
    rt->func_idx_sys_ui_set_text      = (int)base_count + 1;
    rt->func_idx_sys_ui_create_label  = (int)base_count + 2;
    rt->func_idx_sys_ui_create_panel  = (int)base_count + 3;
    rt->func_idx_sys_ui_create_button = (int)base_count + 4;
    rt->func_idx_sys_ui_set_style     = (int)base_count + 5;
    rt->func_idx_sys_ui_on_click      = (int)base_count + 6;
    rt->func_idx_sys_time_uptime_ms   = (int)base_count + 7;
    rt->func_idx_sys_time_uptime_str  = (int)base_count + 8;
    rt->func_idx_set_interval         = (int)base_count + 9;
    rt->func_idx_clear_interval       = (int)base_count + 10;

    /* 函数定义填充 */
    DEF_CFN(func_idx_sys_log,              js_sys_log,              1);
    DEF_CFN(func_idx_sys_ui_set_text,      js_sys_ui_set_text,      2);
    DEF_CFN(func_idx_sys_ui_create_label,  js_sys_ui_create_label,  2);
    DEF_CFN(func_idx_sys_ui_create_panel,  js_sys_ui_create_panel,  2);
    DEF_CFN(func_idx_sys_ui_create_button, js_sys_ui_create_button, 2);
    DEF_CFN(func_idx_sys_ui_set_style,     js_sys_ui_set_style,     6);
    DEF_CFN(func_idx_sys_ui_on_click,      js_sys_ui_on_click,      2);
    DEF_CFN(func_idx_sys_time_uptime_ms,   js_sys_time_uptime_ms,   0);
    DEF_CFN(func_idx_sys_time_uptime_str,  js_sys_time_uptime_str,  0);
    DEF_CFN(func_idx_set_interval,         js_set_interval,         2);
    DEF_CFN(func_idx_clear_interval,       js_clear_interval,       1);
}

#undef DEF_CFN

/* ============================================================================
 * §8. JS 全局对象绑定
 *
 *   把 native fn 挂到 sys.ui / sys.time 等子对象，
 *   再挂上枚举常量 sys.symbols / sys.style / sys.align / sys.font。
 *
 *   JS 侧 enum 与 C 侧 enum 必须对齐：
 *     - sys.style.* 与 dynamic_app_style_key_t 数值相同
 *     - sys.align.* 与 styles.c 的 k_align_map[] 索引相同
 *     - sys.font.*  与 styles.c 的 resolve_font() switch 相同
 * ========================================================================= */

#define BIND_FN(parent, name, idx) do { \
        JSValue _fn = JS_NewCFunctionParams(ctx, s_rt.idx, JS_UNDEFINED); \
        if (JS_IsException(_fn)) return ESP_FAIL; \
        (void)JS_SetPropertyStr(ctx, parent, name, _fn); \
    } while (0)

esp_err_t dynamic_app_natives_bind(JSContext *ctx)
{
    JSValue global  = JS_GetGlobalObject(ctx);
    JSValue sys     = JS_NewObject(ctx);
    JSValue ui      = JS_NewObject(ctx);
    JSValue time    = JS_NewObject(ctx);
    JSValue symbols = JS_NewObject(ctx);
    JSValue style   = JS_NewObject(ctx);
    JSValue align   = JS_NewObject(ctx);
    JSValue font    = JS_NewObject(ctx);

    /* sys.ui.* */
    BIND_FN(ui, "setText",      func_idx_sys_ui_set_text);
    BIND_FN(ui, "createLabel",  func_idx_sys_ui_create_label);
    BIND_FN(ui, "createPanel",  func_idx_sys_ui_create_panel);
    BIND_FN(ui, "createButton", func_idx_sys_ui_create_button);
    BIND_FN(ui, "setStyle",     func_idx_sys_ui_set_style);
    BIND_FN(ui, "onClick",      func_idx_sys_ui_on_click);

    /* sys.time.* */
    BIND_FN(time, "uptimeMs",  func_idx_sys_time_uptime_ms);
    BIND_FN(time, "uptimeStr", func_idx_sys_time_uptime_str);

    /* sys.symbols.* —— LVGL 内置 UTF-8 图标字面量 */
    (void)JS_SetPropertyStr(ctx, symbols, "BLUETOOTH", JS_NewString(ctx, LV_SYMBOL_BLUETOOTH));
    (void)JS_SetPropertyStr(ctx, symbols, "EYE_OPEN",  JS_NewString(ctx, LV_SYMBOL_EYE_OPEN));
    (void)JS_SetPropertyStr(ctx, symbols, "SETTINGS",  JS_NewString(ctx, LV_SYMBOL_SETTINGS));
    (void)JS_SetPropertyStr(ctx, symbols, "IMAGE",     JS_NewString(ctx, LV_SYMBOL_IMAGE));
    (void)JS_SetPropertyStr(ctx, symbols, "BELL",      JS_NewString(ctx, LV_SYMBOL_BELL));
    (void)JS_SetPropertyStr(ctx, symbols, "AUDIO",     JS_NewString(ctx, LV_SYMBOL_AUDIO));
    (void)JS_SetPropertyStr(ctx, symbols, "BARS",      JS_NewString(ctx, LV_SYMBOL_BARS));
    (void)JS_SetPropertyStr(ctx, symbols, "PLAY",      JS_NewString(ctx, LV_SYMBOL_PLAY));
    (void)JS_SetPropertyStr(ctx, symbols, "LIST",      JS_NewString(ctx, LV_SYMBOL_LIST));
    (void)JS_SetPropertyStr(ctx, symbols, "LEFT",      JS_NewString(ctx, LV_SYMBOL_LEFT));
    (void)JS_SetPropertyStr(ctx, symbols, "RIGHT",     JS_NewString(ctx, LV_SYMBOL_RIGHT));

    /* sys.style.* —— 必须与 dynamic_app_style_key_t 数值一致 */
    (void)JS_SetPropertyStr(ctx, style, "BG_COLOR",      JS_NewInt32(ctx, DYNAMIC_APP_STYLE_BG_COLOR));
    (void)JS_SetPropertyStr(ctx, style, "TEXT_COLOR",    JS_NewInt32(ctx, DYNAMIC_APP_STYLE_TEXT_COLOR));
    (void)JS_SetPropertyStr(ctx, style, "RADIUS",        JS_NewInt32(ctx, DYNAMIC_APP_STYLE_RADIUS));
    (void)JS_SetPropertyStr(ctx, style, "SIZE",          JS_NewInt32(ctx, DYNAMIC_APP_STYLE_SIZE));
    (void)JS_SetPropertyStr(ctx, style, "ALIGN",         JS_NewInt32(ctx, DYNAMIC_APP_STYLE_ALIGN));
    (void)JS_SetPropertyStr(ctx, style, "PAD",           JS_NewInt32(ctx, DYNAMIC_APP_STYLE_PAD));
    (void)JS_SetPropertyStr(ctx, style, "BORDER_BOTTOM", JS_NewInt32(ctx, DYNAMIC_APP_STYLE_BORDER_BOTTOM));
    (void)JS_SetPropertyStr(ctx, style, "FLEX",          JS_NewInt32(ctx, DYNAMIC_APP_STYLE_FLEX));
    (void)JS_SetPropertyStr(ctx, style, "FONT",          JS_NewInt32(ctx, DYNAMIC_APP_STYLE_FONT));

    /* sys.align.* —— 必须与 styles.c 的 k_align_map[] 索引一致 */
    (void)JS_SetPropertyStr(ctx, align, "TOP_LEFT",     JS_NewInt32(ctx, 0));
    (void)JS_SetPropertyStr(ctx, align, "TOP_MID",      JS_NewInt32(ctx, 1));
    (void)JS_SetPropertyStr(ctx, align, "TOP_RIGHT",    JS_NewInt32(ctx, 2));
    (void)JS_SetPropertyStr(ctx, align, "LEFT_MID",     JS_NewInt32(ctx, 3));
    (void)JS_SetPropertyStr(ctx, align, "CENTER",       JS_NewInt32(ctx, 4));
    (void)JS_SetPropertyStr(ctx, align, "RIGHT_MID",    JS_NewInt32(ctx, 5));
    (void)JS_SetPropertyStr(ctx, align, "BOTTOM_LEFT",  JS_NewInt32(ctx, 6));
    (void)JS_SetPropertyStr(ctx, align, "BOTTOM_MID",   JS_NewInt32(ctx, 7));
    (void)JS_SetPropertyStr(ctx, align, "BOTTOM_RIGHT", JS_NewInt32(ctx, 8));

    /* sys.font.* */
    (void)JS_SetPropertyStr(ctx, font, "TEXT",  JS_NewInt32(ctx, 0));
    (void)JS_SetPropertyStr(ctx, font, "TITLE", JS_NewInt32(ctx, 1));
    (void)JS_SetPropertyStr(ctx, font, "HUGE",  JS_NewInt32(ctx, 2));

    /* 装配 sys */
    BIND_FN(sys, "log", func_idx_sys_log);
    (void)JS_SetPropertyStr(ctx, sys, "ui",      ui);
    (void)JS_SetPropertyStr(ctx, sys, "time",    time);
    (void)JS_SetPropertyStr(ctx, sys, "symbols", symbols);
    (void)JS_SetPropertyStr(ctx, sys, "style",   style);
    (void)JS_SetPropertyStr(ctx, sys, "align",   align);
    (void)JS_SetPropertyStr(ctx, sys, "font",    font);

    /* 挂到全局 */
    (void)JS_SetPropertyStr(ctx, global, "sys", sys);
    BIND_FN(global, "setInterval",   func_idx_set_interval);
    BIND_FN(global, "clearInterval", func_idx_clear_interval);

    return ESP_OK;
}

#undef BIND_FN
