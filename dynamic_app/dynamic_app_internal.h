#pragma once

/* ============================================================================
 * dynamic_app_internal.h —— JS 侧三个 .c 文件之间的内部共享头
 *
 * 为什么需要这个文件：
 *   把 dynamic_app.c 拆成 dynamic_app.c / runtime.c / natives.c 后，
 *   它们需要共享同一个全局 runtime 状态 s_rt。这个头给三方提供统一的
 *   类型定义 + extern 声明 + 跨文件函数原型。
 *
 * 不对外暴露：
 *   本头不在任何 dynamic_app.h 中被 include，外部模块（page/app）看不到。
 * ========================================================================= */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mquickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * §1. 配置常量
 * ========================================================================= */

#define JS_HEAP_SIZE_BYTES   (1024 * 1024)   /* JS 堆固定 1MB（PSRAM） */
#define MAX_INTERVALS        8                /* setInterval 槽位数 */

#define NOTIFY_START         (1u << 0)        /* TaskNotify bit：启动脚本 */
#define NOTIFY_STOP          (1u << 1)        /* TaskNotify bit：停止脚本 */

/* ============================================================================
 * §2. 内部数据结构
 * ========================================================================= */

/* setInterval 注册槽：func 用 GCRef 钉住防止 JS 引擎回收 */
typedef struct {
    bool allocated;
    JSGCRef func;
    int64_t next_ms;
    int interval_ms;
} js_interval_t;

/* JS 运行时全局状态。整个 dynamic_app 模块只有一个实例：s_rt。 */
typedef struct {
    /* 任务与生命周期 */
    TaskHandle_t task;
    bool app_running;

    /* QuickJS 上下文与堆 */
    JSContext *ctx;
    uint8_t *js_mem;
    size_t js_mem_size;

    /* 扩展后的 stdlib c-function 表（base + 自定义 native fn） */
    JSCFunctionDef *cfunc_table;
    size_t cfunc_table_count;
    JSSTDLibraryDef stdlib_def;

    /* setInterval 注册表 */
    js_interval_t intervals[MAX_INTERVALS];

    /* root delegation 路径的 JS 分发函数。由 sys.__setDispatcher 注册。
     * 用 GCRef 钉住防 GC，teardown 时释放。allocated=false 表示未注册。 */
    bool dispatcher_allocated;
    JSGCRef dispatcher;

    /* 每个自定义 native fn 在 cfunc_table 里的索引。
     * 由 runtime 在 setup_stdlib_and_context 阶段填好，
     * 由 natives.c 在 bind_sys_and_timers 阶段读取使用。 */
    int func_idx_sys_log;
    int func_idx_sys_ui_set_text;
    int func_idx_sys_ui_create_label;
    int func_idx_sys_ui_create_panel;
    int func_idx_sys_ui_create_button;
    int func_idx_sys_ui_set_style;
    int func_idx_sys_ui_attach_root_listener;
    int func_idx_sys_ui_destroy;
    int func_idx_sys_set_dispatcher;
    int func_idx_sys_time_uptime_ms;
    int func_idx_sys_time_uptime_str;
    int func_idx_set_interval;
    int func_idx_clear_interval;
    int func_idx_sys_app_save_state;
    int func_idx_sys_app_load_state;
    int func_idx_sys_app_erase_state;
} dynamic_app_runtime_t;

/* 全局唯一实例。定义在 dynamic_app.c。 */
extern dynamic_app_runtime_t s_rt;

/* ============================================================================
 * §3. 跨文件 helper（runtime ↔ natives ↔ main）
 * ========================================================================= */

/* 时间戳（ms），自启动起。dynamic_app.c 实现 */
int64_t dynamic_app_now_ms(void);

/* 打印 JS 异常详情。dynamic_app.c 实现 */
void dynamic_app_dump_exception(JSContext *ctx);

/* JS 引擎生命周期。dynamic_app_runtime.c 实现。 */
esp_err_t dynamic_app_runtime_setup(void);          /* 申请堆 + 注册 cfunc + 创建 ctx */
void      dynamic_app_runtime_teardown(void);       /* 释放 ctx + 释放堆 */
esp_err_t dynamic_app_runtime_bind_globals(JSContext *ctx);  /* 把 sys.* 挂到 JS 全局 */
esp_err_t dynamic_app_runtime_eval_app(JSContext *ctx);      /* eval 嵌入的 app.js */

/* JS native 函数表的两个集合方法。dynamic_app_natives.c 实现。 */

/* 在 cfunc_table 里登记所有自定义 native 的 JSCFunctionDef。
 * runtime.c 在分配 cfunc_table 之后调一次。 */
void dynamic_app_natives_register(dynamic_app_runtime_t *rt, size_t base_count);

/* 把 native fn 挂到 sys.ui / sys.time 等对象上，并挂常量（symbols/style/...）。
 * runtime.c 的 bind_globals 会调它。 */
esp_err_t dynamic_app_natives_bind(JSContext *ctx);

/* ============================================================================
 * §4. tick 循环里的两个 drain（natives 表里的回调消费）
 *     由 dynamic_app.c 的 script_task 主循环调用。
 * ========================================================================= */

/* 跑所有到期的 setInterval。返回 false 表示遇到 JS 异常，应停 app。 */
bool dynamic_app_run_intervals_once(JSContext *ctx, int64_t cur_ms);

/* 计算下一个 setInterval 的 deadline（用于决定 vTaskDelay 长度） */
int64_t dynamic_app_next_interval_deadline_ms(int64_t cur_ms);

/* 消化"UI → Script"反向事件队列里所有点击事件 */
void dynamic_app_drain_ui_events_once(JSContext *ctx);

/* 释放所有 setInterval 槽位的 GCRef。
 * runtime.c 的 teardown 调用，避免泄漏。 */
void dynamic_app_intervals_reset(JSContext *ctx);

#ifdef __cplusplus
}
#endif
