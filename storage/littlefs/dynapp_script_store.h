#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dynapp_script_store —— "动态 App 脚本仓"业务层
 *
 * 与 fs_littlefs 的分工：
 *   fs_littlefs        只知道"挂了一个 LittleFS"
 *   dynapp_script_store 知道"app 脚本放 /littlefs/apps/<name>.js"，且管文件名约束、
 *                       大小上限、原子写、列举去重等业务规则
 *
 * 不在这里做的事：
 *   - 校验 JS 语法（runtime 层用 esp_mqjs eval 时自然会失败）
 *   - 决定哪些 app 优先内嵌哪些走 FS（registry 双源覆盖策略）
 *   - 网络下载（未来 WiFi 模块负责拉文件，落盘走本接口）
 */

/* 单脚本上限。超过的写入直接拒。
 * 当前最大 alarm.js / game2048.js 大约 15KB，留 4× 余量。 */
#define DYNAPP_SCRIPT_STORE_MAX_BYTES   (64 * 1024)

/* App 名长度上限（不含 .js 后缀和 NUL）。
 * registry 内嵌表的 s_current_name 缓冲是 16，对齐保守一点用 15。 */
#define DYNAPP_SCRIPT_STORE_MAX_NAME    15

/**
 * 在 fs_littlefs_init() 之后调用一次。负责确保 /littlefs/apps/ 目录存在，
 * 并清理上次崩溃可能留下的孤儿 .tmp 文件。
 */
esp_err_t dynapp_script_store_init(void);

/**
 * 读取一个 app 脚本。成功时 *out_buf 指向 heap，调用方用完后必须
 * dynapp_script_store_release(buf) 释放。
 *
 * 返回：
 *   ESP_OK              ：找到并读出
 *   ESP_ERR_NOT_FOUND   ：脚本不存在
 *   ESP_ERR_INVALID_ARG ：name 非法
 *   ESP_ERR_NO_MEM      ：分配失败
 *   其它 esp_err_t      ：底层 fopen/fread 失败
 */
esp_err_t dynapp_script_store_read(const char *name,
                                   uint8_t **out_buf,
                                   size_t *out_len);

/**
 * 释放 read 返回的 buffer。NULL 安全。
 */
void dynapp_script_store_release(uint8_t *buf);

/**
 * 写入（覆盖）一个 app 脚本。原子语义：先写 .tmp 再 rename，
 * 中途断电不会留下半截文件。
 *
 * 返回：
 *   ESP_OK / ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_SIZE / ESP_FAIL
 */
esp_err_t dynapp_script_store_write(const char *name,
                                    const uint8_t *buf,
                                    size_t len);

/**
 * 删除一个 app 脚本。脚本不存在返回 ESP_ERR_NOT_FOUND（非错误，调用方自行决定）。
 */
esp_err_t dynapp_script_store_delete(const char *name);

/**
 * 检查脚本是否存在。
 */
bool dynapp_script_store_exists(const char *name);

/**
 * 列举所有 FS 上的 app 名（不含 .js 后缀）。
 *
 * @param out      调用方提供的字符串数组缓冲，每条 ≤ DYNAPP_SCRIPT_STORE_MAX_NAME+1
 * @param max      out 的容量（条数）
 * @return         实际写入的条数；可能小于实际文件数（被截断）
 */
int dynapp_script_store_list(char out[][DYNAPP_SCRIPT_STORE_MAX_NAME + 1], int max);

/* ------------------------------------------------------------------
 * 流式写入（给 BLE 上传 worker 用）
 *
 * 典型时序：
 *   open_writer(name) → append(...) × N → commit() / abort()
 *
 * 语义：
 *   - 同一时刻只能存在一个活跃 writer（实现层面用单例，open 时若有未关闭的旧
 *     writer 会先自动 abort）
 *   - 数据先写 .tmp，commit 时 rename 成正式文件（断电安全）
 *   - abort 删掉 .tmp，正式文件不动
 *
 * 上限：append 累计字节数超过 DYNAPP_SCRIPT_STORE_MAX_BYTES 时返回 INVALID_SIZE
 *      并自动失效（writer 进入失败状态，后续 append/commit 都返回错误）
 * ------------------------------------------------------------------ */

typedef struct dynapp_script_writer dynapp_script_writer_t;

/**
 * 开始一次流式写入。
 *
 * 失败原因：name 非法 / fopen 失败 / 已存在另一个 writer 且无法 abort。
 */
dynapp_script_writer_t *dynapp_script_store_open_writer(const char *name);

esp_err_t dynapp_script_writer_append(dynapp_script_writer_t *w,
                                      const uint8_t *data, size_t len);

/**
 * 提交：fclose + rename(.tmp → .js)。成功后 writer 自动失效，调用方不再持有指针。
 * 失败时 writer 也已被释放（.tmp 被删），上层不需要再调 abort。
 */
esp_err_t dynapp_script_writer_commit(dynapp_script_writer_t *w);

/**
 * 取消：fclose + 删 .tmp。writer 失效。NULL 安全。
 */
void dynapp_script_writer_abort(dynapp_script_writer_t *w);

/**
 * 当前是否有未完成的 writer（worker 状态机调试用）。
 */
bool dynapp_script_store_has_active_writer(void);

#ifdef __cplusplus
}
#endif
