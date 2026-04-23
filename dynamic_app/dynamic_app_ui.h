#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dynamic_app_ui：Script Task -> UI Task 的“安全桥接层”。
 *
 * 目标：
 * - 让脚本线程可以发起 UI 更新（例如 setText），但又不直接触碰 LVGL。
 *
 * 手段：
 * - Script Task 只负责把“要做什么 UI 操作”封装成 command，塞进 FreeRTOS Queue；
 * - UI Task 在自己的循环里调用 `dynamic_app_ui_drain()`，逐条取出并执行（调用 LVGL API）。
 *
 * 线程规则（非常重要）：
 * - enqueue：可以在任意线程调用（典型是脚本线程）
 * - register/unregister/drain：必须在 UI 线程调用（因为涉及 LVGL 对象与 LVGL API）
 */

#define DYNAMIC_APP_UI_ID_MAX_LEN   32
#define DYNAMIC_APP_UI_TEXT_MAX_LEN 128

typedef enum {
    DYNAMIC_APP_UI_CMD_SET_TEXT = 1,
    DYNAMIC_APP_UI_CMD_CREATE_LABEL = 2,
} dynamic_app_ui_cmd_type_t;

typedef struct {
    dynamic_app_ui_cmd_type_t type;
    char id[DYNAMIC_APP_UI_ID_MAX_LEN];
    char text[DYNAMIC_APP_UI_TEXT_MAX_LEN];
} dynamic_app_ui_command_t;

/**
 * 初始化 UI 桥接层（创建队列、清空 registry）。
 *
 * @note 多次调用是安全的：已经初始化则直接返回 ESP_OK。
 */
esp_err_t dynamic_app_ui_init(void);

/**
 * Script Task：异步请求 UI 把某个 id 对应的 label 文本更新为 text。
 *
 * 说明：
 * - 这里不会调用 LVGL；只是入队一条命令；
 * - id/text 会被拷贝进 command（并做 UTF-8 安全截断），因此调用结束后原指针可立即失效；
 * - 如果队列满了，会返回 false（MVP 阶段选择“丢弃”而不是阻塞 UI）。
 */
bool dynamic_app_ui_enqueue_set_text(const char *id, size_t id_len,
                                     const char *text, size_t text_len);

/**
 * Script Task：请求在 Dynamic App 页面内创建一个 label，并注册到 registry。
 *
 * @note 该函数只负责入队，不直接触碰 LVGL。真正创建发生在 UI Task 的 dynamic_app_ui_drain()。
 * @return true=成功入队；false=队列满/参数错误/当前不在 Dynamic App 页面（root 未设置）。
 */
bool dynamic_app_ui_enqueue_create_label(const char *id, size_t id_len);

/**
 * UI Task：把一个 label 注册到 registry，让脚本可以通过 id 找到它。
 */
esp_err_t dynamic_app_ui_register_label(const char *id, lv_obj_t *obj);

/**
 * UI Task：设置/清空 Dynamic App 页面可挂载的 root 容器。
 *
 * @note 为了保证“只允许在 PAGE_DYNAMIC_APP 创建”，root 只在该页面 create 时设置，
 *       destroy 时清空。root==NULL 时会丢弃 CREATE_LABEL 命令。
 */
void dynamic_app_ui_set_root(lv_obj_t *root);

/**
 * UI Task：清空所有 registry 项。
 */
void dynamic_app_ui_unregister_all(void);

/**
 * UI Task：消费队列并执行命令。
 *
 * @param max_count 本次最多处理的命令条数（控制每一帧的耗时）
 */
void dynamic_app_ui_drain(int max_count);

#ifdef __cplusplus
}
#endif
