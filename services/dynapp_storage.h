#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dynapp_storage —— "动态 App 脚本仓"业务层
 *
 * 与 fs_littlefs 的分工：
 *   fs_littlefs   只知道"挂了一个 LittleFS"。
 *   dynapp_storage 知道"app 脚本放 /littlefs/apps/<name>.js"，且管文件名约束、
 *                  大小上限、原子写、列举去重等业务规则。
 *
 * 不在这里做的事：
 *   - 校验 JS 语法（runtime 层用 esp_mqjs eval 时自然会失败）
 *   - 决定哪些 app 优先内嵌哪些走 FS（registry 双源覆盖策略）
 *   - 网络下载（未来 WiFi 模块负责拉文件，落盘走本接口）
 */

/* 单脚本上限。超过的写入直接拒。
 * 当前最大 alarm.js / game2048.js 大约 15KB，留 4× 余量。 */
#define DYNAPP_STORAGE_MAX_SCRIPT_BYTES  (64 * 1024)

/* App 名长度上限（不含 .js 后缀和 NUL）。
 * registry 内嵌表的 s_current_name 缓冲是 16，对齐保守一点用 15。 */
#define DYNAPP_STORAGE_MAX_NAME_LEN      15

/**
 * 在 fs_littlefs_init() 之后调用一次。负责确保 /littlefs/apps/ 目录存在。
 */
esp_err_t dynapp_storage_init(void);

/**
 * 读取一个 app 脚本。成功时 *out_buf 指向 heap，调用方用完后必须
 * dynapp_storage_release(buf) 释放。
 *
 * 返回：
 *   ESP_OK              ：找到并读出
 *   ESP_ERR_NOT_FOUND   ：脚本不存在
 *   ESP_ERR_INVALID_ARG ：name 非法
 *   ESP_ERR_NO_MEM      ：分配失败
 *   其它 esp_err_t      ：底层 fopen/fread 失败
 */
esp_err_t dynapp_storage_read(const char *name,
                              uint8_t **out_buf,
                              size_t *out_len);

/**
 * 释放 read 返回的 buffer。NULL 安全。
 */
void dynapp_storage_release(uint8_t *buf);

/**
 * 写入（覆盖）一个 app 脚本。原子语义：先写 .tmp 再 rename，
 * 中途断电不会留下半截文件。
 *
 * 返回：
 *   ESP_OK / ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_SIZE / ESP_FAIL
 */
esp_err_t dynapp_storage_write(const char *name,
                               const uint8_t *buf,
                               size_t len);

/**
 * 删除一个 app 脚本。脚本不存在返回 ESP_ERR_NOT_FOUND（非错误，调用方自行决定）。
 */
esp_err_t dynapp_storage_delete(const char *name);

/**
 * 检查脚本是否存在。
 */
bool dynapp_storage_exists(const char *name);

/**
 * 列举所有 FS 上的 app 名（不含 .js 后缀）。
 *
 * @param out      调用方提供的字符串数组缓冲，每条 ≤ DYNAPP_STORAGE_MAX_NAME_LEN+1
 * @param max      out 的容量（条数）
 * @return         实际写入的条数；可能小于实际文件数（被截断）
 */
int dynapp_storage_list(char out[][DYNAPP_STORAGE_MAX_NAME_LEN + 1], int max);

#ifdef __cplusplus
}
#endif
