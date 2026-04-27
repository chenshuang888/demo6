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
 * 双源实现：
 *   1) 编译期内嵌（dynamic_app/scripts 下的 .js，CMakeLists 的 EMBED_TXTFILES）
 *      —— 核心 app 永远在固件里，FS 损坏也能用，是兜底
 *   2) 运行时 LittleFS（services/dynapp_storage，路径 /littlefs/apps/<name>.js）
 *      —— 通过 BLE 推送 / WiFi 下载得到的 app
 *
 * 查找顺序：先内嵌再 FS。同名内嵌优先，保证脚本写错也能恢复。
 */

/**
 * 取脚本 buffer。
 *
 * 调用方约定：用完 *out_buf 后必须调 dynamic_app_registry_release(*out_buf)。
 *   - 内嵌脚本：release 是 no-op
 *   - FS 脚本：release 释放 heap
 *
 * 返回 false 表示找不到（包括 name == NULL / 空字符串）。
 */
bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len);

/**
 * 释放 get 返回的 buffer。NULL 安全。
 *
 * 注意：必须传 get() 出来的同一个指针；
 * 不要传内嵌符号 (alarm_js_start 等) 否则触发 free(rodata) 崩溃。
 */
void dynamic_app_registry_release(const uint8_t *buf);

/**
 * 取标准库 prelude 脚本（含 VDOM / makeBle / setDispatcher）。
 * 永远从内嵌读，不走 FS。
 */
void dynamic_app_registry_get_prelude(const uint8_t **out_buf,
                                      size_t *out_len);

/* ---- 列举接口（菜单页用，决定要画多少个动态 app 入口） ---- */

#define DYNAPP_REGISTRY_NAME_MAX  15

typedef struct {
    char name[DYNAPP_REGISTRY_NAME_MAX + 1];
    bool builtin;     /* true=来自内嵌，false=来自 FS */
} dynamic_app_entry_t;

/**
 * 列举所有可用 app（内嵌 + FS，按名去重，内嵌优先）。
 *
 * @param out  调用方提供的数组
 * @param max  数组容量
 * @return     实际写入的条数
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

