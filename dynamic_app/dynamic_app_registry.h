#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 动态 App 注册表 —— 把"app 名"映射到"脚本 buffer"。
 *
 * 单源：业务 app 一律来自 LittleFS（storage/littlefs/dynapp_script_store，
 * 路径 /littlefs/apps/<name>.js），通过 BLE 推送 / WiFi 下载得到。
 *
 * 例外：prelude.js 是 runtime 的标准库（提供 VDOM / makeBle / setDispatcher），
 * 编译期内嵌进固件，runtime 启动不依赖 FS。它不是"app"，业务脚本不可见。
 *
 * 模型对齐：等价于智能手机的"应用商店 app"——系统功能（电话/短信级别）
 * 由原生 C 页面提供，本注册表只服务于用户下载的脚本 app。
 */

/**
 * 取脚本 buffer。
 *
 * 调用方约定：用完 *out_buf 后必须调 dynamic_app_registry_release(*out_buf)。
 * release 内部走 dynapp_script_store_release（heap free）。
 *
 * 返回 false 表示找不到（包括 name == NULL / 空字符串 / FS 上不存在）。
 */
bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len);

/**
 * 释放 get 返回的 buffer。NULL 安全。
 */
void dynamic_app_registry_release(const uint8_t *buf);

/**
 * 取标准库 prelude 脚本（含 VDOM / makeBle / setDispatcher）。
 * 永远从内嵌 rodata 读，不走 FS。
 */
void dynamic_app_registry_get_prelude(const uint8_t **out_buf,
                                      size_t *out_len);

/* ---- 列举接口（菜单页用，决定要画多少个动态 app 入口） ---- */

#define DYNAPP_REGISTRY_NAME_MAX  15

typedef struct {
    char name[DYNAPP_REGISTRY_NAME_MAX + 1];
} dynamic_app_entry_t;

/**
 * 列举所有 FS 上的可用 app。
 *
 * @param out  调用方提供的数组
 * @param max  数组容量
 * @return     实际写入的条数（FS 为空时返回 0，是合法情况）
 */
int dynamic_app_registry_list(dynamic_app_entry_t *out, int max);

/**
 * 当前正在跑的 app 名（runtime 在 eval 前调用 set，
 * sys.app.saveState/loadState 用它当 NVS key）。
 */
void        dynamic_app_registry_set_current(const char *name);
const char *dynamic_app_registry_current(void);

#ifdef __cplusplus
}
#endif

