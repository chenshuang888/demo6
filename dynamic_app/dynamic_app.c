#include "dynamic_app.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "dynamic_app_ui.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mqjs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mquickjs.h"

/*
 * 动态 App（MicroQuickJS）MVP：脚本运行时 + Bridge（JS ↔ 系统/LVGL）
 *
 * 你在屏幕上看到“时间每秒变化”，完整链路是：
 *   app.js(setInterval) → sys.time.uptimeStr() / sys.ui.setText()
 *     → dynamic_app_ui_enqueue_set_text() 入队
 *     → UI Task 每帧 dynamic_app_ui_drain() 出队
 *     → lv_label_set_text() 真正更新屏幕
 *
 * 最重要的线程安全规则：
 * - Script Task 禁止直接调用任何 LVGL API（LVGL 非线程安全）。
 * - JS 侧所有 UI 操作必须通过队列交给 UI Task 执行。
 */
static const char *TAG = "dynamic_app";

/*
 * JS 堆大小（MVP 固定值）。
 *
 * 说明：
 * - 这里使用 PSRAM（MALLOC_CAP_SPIRAM），给 JS VM 一块连续的大内存。
 * - 好处：实现简单、可预期；坏处：占用固定内存且没有做动态伸缩。
 * - 后续如果要跑更复杂脚本/更多 UI，建议把它做成可配置项并结合实际内存评估。
 */
#define JS_HEAP_SIZE_BYTES (1024 * 1024) /* 1MB PSRAM 固定堆（MVP） */
#define MAX_INTERVALS 8

#define NOTIFY_START (1u << 0)
#define NOTIFY_STOP  (1u << 1)

typedef struct {
    bool allocated;
    JSGCRef func;
    int64_t next_ms;
    int interval_ms;
} js_interval_t;

typedef struct {
    TaskHandle_t task;
    bool app_running;

    JSContext *ctx;
    uint8_t *js_mem;
    size_t js_mem_size;

    JSCFunctionDef *cfunc_table;
    size_t cfunc_table_count;

    JSSTDLibraryDef stdlib_def;

    js_interval_t intervals[MAX_INTERVALS];

    int func_idx_sys_log;
    int func_idx_sys_ui_set_text;
    int func_idx_sys_ui_create_label;
    int func_idx_sys_time_uptime_ms;
    int func_idx_sys_time_uptime_str;
    int func_idx_set_interval;
    int func_idx_clear_interval;
} dynamic_app_runtime_t;

static dynamic_app_runtime_t s_rt = {0};

/*
 * 注意：ESP-IDF 的 EMBED_TXTFILES 生成的符号名基于“文件名”而非“相对路径”。
 * 当前 `EMBED_TXTFILES "scripts/app.js"` 会生成：
 *   - _binary_app_js_start / _binary_app_js_end
 */
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[]   asm("_binary_app_js_end");

static int64_t now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000);
}

static void js_write_func(void *opaque, const void *buf, size_t buf_len)
{
    (void)opaque;
    fwrite(buf, 1, buf_len, stdout);
}

static int js_interrupt_handler(JSContext *ctx, void *opaque)
{
    (void)ctx;
    static int64_t last_yield = 0;
    int64_t cur = now_ms();
    if (cur - last_yield > 100) {
        /*
         * 给 FreeRTOS 一点喘息时间：
         * - JS 可能执行较长逻辑（例如 while 循环/大量计算）
         * - 这里每隔一段时间主动 yield，避免脚本任务“霸占”CPU 导致系统卡顿。
         */
        vTaskDelay(1);
        last_yield = cur;
    }
    (void)opaque;
    return 0;
}

static void dump_exception(JSContext *ctx)
{
    /* 打印 JS 异常的详细信息（便于你在串口上定位脚本错误）。 */
    JSValue ex = JS_GetException(ctx);
    JS_PrintValueF(ctx, ex, JS_DUMP_LONG);
    fwrite("\n", 1, 1, stdout);
}

static JSValue js_sys_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }

    JSCStringBuf buf;
    size_t len = 0;
    const char *s = JS_ToCStringLen(ctx, &len, argv[0], &buf);
    if (!s) {
        return JS_EXCEPTION;
    }

    ESP_LOGI("dynapp", "%.*s", (int)len, s);
    return JS_UNDEFINED;
}

static JSValue js_sys_ui_set_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "sys.ui.setText(id, text) args missing");
    }

    JSCStringBuf id_buf;
    JSCStringBuf text_buf;
    size_t id_len = 0;
    size_t text_len = 0;

    const char *id = JS_ToCStringLen(ctx, &id_len, argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;
    const char *text = JS_ToCStringLen(ctx, &text_len, argv[1], &text_buf);
    if (!text) return JS_EXCEPTION;

    /*
     * 关键点：这里不会直接调用 LVGL！
     * - 只把“更新 UI 的请求”入队；
     * - UI 线程会在 `dynamic_app_ui_drain()` 中真正执行 `lv_label_set_text()`。
     */
    bool ok = dynamic_app_ui_enqueue_set_text(id, id_len, text, text_len);
    if (!ok) {
        /* 队列满/未初始化等，MVP 阶段直接忽略即可 */
    }
    return JS_UNDEFINED;
}

static JSValue js_sys_ui_create_label(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "sys.ui.createLabel(id) args missing");
    }

    JSCStringBuf id_buf;
    size_t id_len = 0;

    const char *id = JS_ToCStringLen(ctx, &id_len, argv[0], &id_buf);
    if (!id) return JS_EXCEPTION;

    bool ok = dynamic_app_ui_enqueue_create_label(id, id_len);
    return JS_NewBool(ok ? 1 : 0);
}

static JSValue js_sys_time_uptime_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, now_ms());
}

static JSValue js_sys_time_uptime_str(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;

    int64_t ms = now_ms();
    int64_t sec_total = ms / 1000;

    int hours = (int)(sec_total / 3600);
    int minutes = (int)((sec_total / 60) % 60);
    int seconds = (int)(sec_total % 60);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours % 100, minutes, seconds);
    return JS_NewString(ctx, buf);
}

static void intervals_reset(JSContext *ctx)
{
    for (int i = 0; i < MAX_INTERVALS; i++) {
        if (s_rt.intervals[i].allocated) {
            /* setInterval 存的是函数引用：停止/重建上下文时需要释放，避免泄漏。 */
            JS_DeleteGCRef(ctx, &s_rt.intervals[i].func);
            s_rt.intervals[i].allocated = false;
        }
    }
}

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
    if (JS_ToInt32(ctx, &delay_ms, argv[1])) {
        return JS_EXCEPTION;
    }
    if (delay_ms < 1) delay_ms = 1;

    for (int i = 0; i < MAX_INTERVALS; i++) {
        js_interval_t *t = &s_rt.intervals[i];
        if (!t->allocated) {
            /*
             * GCRef 的意义：
             * - JSValue 本身可能被 GC 回收；
             * - 把回调函数保存为 GCRef，保证定时器持有它时不会被回收。
             */
            JSValue *pfunc = JS_AddGCRef(ctx, &t->func);
            *pfunc = argv[0];
            t->interval_ms = delay_ms;
            t->next_ms = now_ms() + delay_ms;
            t->allocated = true;
            return JS_NewInt32(ctx, i);
        }
    }

    return JS_ThrowInternalError(ctx, "too many intervals");
}

static JSValue js_clear_interval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_UNDEFINED;
    }

    int id = -1;
    if (JS_ToInt32(ctx, &id, argv[0])) {
        return JS_EXCEPTION;
    }
    if (id < 0 || id >= MAX_INTERVALS) {
        return JS_UNDEFINED;
    }

    js_interval_t *t = &s_rt.intervals[id];
    if (t->allocated) {
        JS_DeleteGCRef(ctx, &t->func);
        t->allocated = false;
    }
    return JS_UNDEFINED;
}

static esp_err_t setup_stdlib_and_context(void)
{
    const JSSTDLibraryDef *base = esp_mqjs_get_stdlib_def();
    if (!base) return ESP_FAIL;

    size_t base_count = esp_mqjs_get_stdlib_c_function_count();
    if (base_count == 0) return ESP_FAIL;

    /*
     * 关键技巧：复制并扩展 esp-mquickjs 的 stdlib C function table。
     *
     * 为什么要复制？
     * - 上游 stdlib 的 c_function_table 通常是“固定表”；
     * - 我们要在尾部追加自己的 native API（sys.log/sys.ui.setText/...），因此需要一份可写的拷贝。
     *
     * 这里 extra=6：对应下面要注入到 JS 的 6 个函数。
     */
    const int extra = 7; /* sys.log / sys.ui.setText / sys.ui.createLabel / sys.time.uptimeMs / sys.time.uptimeStr / setInterval / clearInterval */
    size_t total = base_count + (size_t)extra;

    s_rt.cfunc_table = heap_caps_malloc(total * sizeof(JSCFunctionDef),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rt.cfunc_table) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_rt.cfunc_table, base->c_function_table, base_count * sizeof(JSCFunctionDef));
    s_rt.cfunc_table_count = total;

    s_rt.func_idx_sys_log            = (int)base_count + 0;
    s_rt.func_idx_sys_ui_set_text    = (int)base_count + 1;
    s_rt.func_idx_sys_ui_create_label = (int)base_count + 2;
    s_rt.func_idx_sys_time_uptime_ms = (int)base_count + 3;
    s_rt.func_idx_sys_time_uptime_str = (int)base_count + 4;
    s_rt.func_idx_set_interval       = (int)base_count + 5;
    s_rt.func_idx_clear_interval     = (int)base_count + 6;

    s_rt.cfunc_table[s_rt.func_idx_sys_log] = (JSCFunctionDef){
        .func.generic = js_sys_log,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 1,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_sys_ui_set_text] = (JSCFunctionDef){
        .func.generic = js_sys_ui_set_text,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 2,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_sys_ui_create_label] = (JSCFunctionDef){
        .func.generic = js_sys_ui_create_label,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 1,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_set_interval] = (JSCFunctionDef){
        .func.generic = js_set_interval,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 2,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_clear_interval] = (JSCFunctionDef){
        .func.generic = js_clear_interval,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 1,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_sys_time_uptime_ms] = (JSCFunctionDef){
        .func.generic = js_sys_time_uptime_ms,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 0,
        .magic = 0,
    };
    s_rt.cfunc_table[s_rt.func_idx_sys_time_uptime_str] = (JSCFunctionDef){
        .func.generic = js_sys_time_uptime_str,
        .name = JS_UNDEFINED,
        .def_type = JS_CFUNC_generic,
        .arg_count = 0,
        .magic = 0,
    };

    s_rt.stdlib_def = *base;
    s_rt.stdlib_def.c_function_table = s_rt.cfunc_table;

    /* 为 JS VM 分配固定堆，并创建 JSContext。 */
    s_rt.js_mem_size = JS_HEAP_SIZE_BYTES;
    s_rt.js_mem = heap_caps_malloc(s_rt.js_mem_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rt.js_mem) {
        return ESP_ERR_NO_MEM;
    }

    s_rt.ctx = JS_NewContext(s_rt.js_mem, s_rt.js_mem_size, &s_rt.stdlib_def);
    if (!s_rt.ctx) {
        return ESP_FAIL;
    }

    JS_SetLogFunc(s_rt.ctx, js_write_func);
    JS_SetInterruptHandler(s_rt.ctx, js_interrupt_handler);

    return ESP_OK;
}

static void teardown_context(void)
{
    if (s_rt.ctx) {
        intervals_reset(s_rt.ctx);
        JS_FreeContext(s_rt.ctx);
        s_rt.ctx = NULL;
    }

    if (s_rt.js_mem) {
        heap_caps_free(s_rt.js_mem);
        s_rt.js_mem = NULL;
        s_rt.js_mem_size = 0;
    }

    if (s_rt.cfunc_table) {
        heap_caps_free(s_rt.cfunc_table);
        s_rt.cfunc_table = NULL;
        s_rt.cfunc_table_count = 0;
    }
}

static esp_err_t bind_sys_and_timers(JSContext *ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;

    /*
     * 在 JS 全局对象上挂载我们自己的 API：
     * - sys.log / sys.ui.setText / sys.time.uptimeMs / sys.time.uptimeStr
     * - setInterval / clearInterval
     *
     * 这样 app.js 就可以直接调用这些函数，而无需引入额外模块系统。
     */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys = JS_NewObject(ctx);
    JSValue ui = JS_NewObject(ctx);
    JSValue time = JS_NewObject(ctx);

    JSValue fn_log = JS_NewCFunctionParams(ctx, s_rt.func_idx_sys_log, JS_UNDEFINED);
    if (JS_IsException(fn_log)) return ESP_FAIL;
    JSValue fn_set_text = JS_NewCFunctionParams(ctx, s_rt.func_idx_sys_ui_set_text, JS_UNDEFINED);
    if (JS_IsException(fn_set_text)) return ESP_FAIL;
    JSValue fn_create_label = JS_NewCFunctionParams(ctx, s_rt.func_idx_sys_ui_create_label, JS_UNDEFINED);
    if (JS_IsException(fn_create_label)) return ESP_FAIL;
    JSValue fn_uptime_ms = JS_NewCFunctionParams(ctx, s_rt.func_idx_sys_time_uptime_ms, JS_UNDEFINED);
    if (JS_IsException(fn_uptime_ms)) return ESP_FAIL;
    JSValue fn_uptime_str = JS_NewCFunctionParams(ctx, s_rt.func_idx_sys_time_uptime_str, JS_UNDEFINED);
    if (JS_IsException(fn_uptime_str)) return ESP_FAIL;
    JSValue fn_set_interval = JS_NewCFunctionParams(ctx, s_rt.func_idx_set_interval, JS_UNDEFINED);
    if (JS_IsException(fn_set_interval)) return ESP_FAIL;
    JSValue fn_clear_interval = JS_NewCFunctionParams(ctx, s_rt.func_idx_clear_interval, JS_UNDEFINED);
    if (JS_IsException(fn_clear_interval)) return ESP_FAIL;

    (void)JS_SetPropertyStr(ctx, ui, "setText", fn_set_text);
    (void)JS_SetPropertyStr(ctx, ui, "createLabel", fn_create_label);
    (void)JS_SetPropertyStr(ctx, time, "uptimeMs", fn_uptime_ms);
    (void)JS_SetPropertyStr(ctx, time, "uptimeStr", fn_uptime_str);
    (void)JS_SetPropertyStr(ctx, sys, "log", fn_log);
    (void)JS_SetPropertyStr(ctx, sys, "ui", ui);
    (void)JS_SetPropertyStr(ctx, sys, "time", time);
    (void)JS_SetPropertyStr(ctx, global, "sys", sys);

    (void)JS_SetPropertyStr(ctx, global, "setInterval", fn_set_interval);
    (void)JS_SetPropertyStr(ctx, global, "clearInterval", fn_clear_interval);

    return ESP_OK;
}

static esp_err_t eval_embedded_app(JSContext *ctx)
{
    /*
     * app.js 是通过组件的 EMBED_TXTFILES 嵌入到固件里的，
     * 这里直接拿到编译器生成的 start/end 指针并 eval。
     */
    const char *script = (const char *)app_js_start;
    size_t script_len = (size_t)(app_js_end - app_js_start);

    if (!script || script_len == 0) {
        ESP_LOGE(TAG, "embedded app.js missing");
        return ESP_FAIL;
    }

    JSValue val = JS_Eval(ctx, script, script_len, "app.js", 0);
    if (JS_IsException(val)) {
        dump_exception(ctx);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool run_intervals_once(JSContext *ctx, int64_t cur_ms)
{
    for (int i = 0; i < MAX_INTERVALS; i++) {
        js_interval_t *t = &s_rt.intervals[i];
        if (!t->allocated) continue;
        if (t->next_ms > cur_ms) continue;

        /* StackCheck 可以尽早发现递归过深/栈不足等问题。 */
        if (JS_StackCheck(ctx, 2)) {
            dump_exception(ctx);
            return false;
        }

        /* 调用 JS 回调：这里用 mquickjs 的 PushArg/Call 形式。 */
        JS_PushArg(ctx, t->func.val);
        JS_PushArg(ctx, JS_NULL);
        JSValue ret = JS_Call(ctx, 0);
        if (JS_IsException(ret)) {
            dump_exception(ctx);
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

static int64_t get_next_interval_deadline_ms(int64_t cur_ms)
{
    /* 找到所有 interval 中最早的 next_ms，用于决定脚本任务 sleep 多久。 */
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

static void script_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "script task started");

    /*
     * 脚本任务主循环（简化状态机）：
     * - 一直等待通知（start/stop）
     * - 收到 start：初始化上下文 -> eval app.js -> 进入 tick 循环
     * - tick 循环里：跑到期的 interval 回调，并定期检查 stop 通知
     * - 收到 stop 或 JS 异常：释放上下文并回到等待 start
     */
    while (1) {
        uint32_t notify_val = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notify_val, portMAX_DELAY);

        /* 如果 start/stop 同时到来，视为“取消启动” */
        if ((notify_val & NOTIFY_START) && (notify_val & NOTIFY_STOP)) {
            continue;
        }

        if (notify_val & NOTIFY_START) {
            if (s_rt.app_running) {
                continue;
            }

            ESP_LOGI(TAG, "starting dynamic app");

            memset(s_rt.intervals, 0, sizeof(s_rt.intervals));
            s_rt.app_running = true;

            if (setup_stdlib_and_context() != ESP_OK) {
                ESP_LOGE(TAG, "setup context failed");
                teardown_context();
                s_rt.app_running = false;
                continue;
            }

            if (bind_sys_and_timers(s_rt.ctx) != ESP_OK) {
                ESP_LOGE(TAG, "bind sys failed");
                teardown_context();
                s_rt.app_running = false;
                continue;
            }

            if (eval_embedded_app(s_rt.ctx) != ESP_OK) {
                ESP_LOGE(TAG, "eval app.js failed");
                teardown_context();
                s_rt.app_running = false;
                continue;
            }

            while (s_rt.app_running) {
                uint32_t ev = 0;
                if (xTaskNotifyWait(0, UINT32_MAX, &ev, 0) == pdTRUE) {
                    if (ev & NOTIFY_STOP) {
                        break;
                    }
                }

                int64_t cur = now_ms();
                if (!run_intervals_once(s_rt.ctx, cur)) {
                    ESP_LOGE(TAG, "JS exception, stopping app");
                    break;
                }

                int64_t deadline = get_next_interval_deadline_ms(cur);
                int64_t sleep_ms = deadline - cur;
                if (sleep_ms < 1) sleep_ms = 1;
                /*
                 * 限制最大 sleep：
                 * - 即便没有 interval 很快到期，也不要睡太久；
                 * - 这样 stop 通知能够更及时地被处理（响应更快）。
                 */
                if (sleep_ms > 50) sleep_ms = 50; /* 保持 stop 响应及时 */
                vTaskDelay(pdMS_TO_TICKS((uint32_t)sleep_ms));
            }

            ESP_LOGI(TAG, "stopping dynamic app");
            teardown_context();
            s_rt.app_running = false;
        }
    }
}

esp_err_t dynamic_app_init(void)
{
    /* 先初始化 UI 桥接层：脚本侧的 sys.ui.setText 会依赖它入队。 */
    ESP_RETURN_ON_ERROR(dynamic_app_ui_init(), TAG, "ui init failed");

    if (s_rt.task) {
        return ESP_OK;
    }

    /* 脚本任务建议固定在 Core0（UI 任务通常在 Core1），减少互相抢占。 */
    BaseType_t ret = xTaskCreatePinnedToCore(script_task, "script_task",
                                            16384, NULL, 4, &s_rt.task, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create script task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void dynamic_app_start(void)
{
    if (!s_rt.task) return;
    /* 通过通知位触发脚本任务启动（异步）。 */
    xTaskNotify(s_rt.task, NOTIFY_START, eSetBits);
}

void dynamic_app_stop(void)
{
    if (!s_rt.task) return;
    /* 通过通知位触发脚本任务停止（异步）。 */
    xTaskNotify(s_rt.task, NOTIFY_STOP, eSetBits);
}
