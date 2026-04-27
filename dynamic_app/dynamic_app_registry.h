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
 * 当前实现：脚本嵌入固件。将来可改为从 NVS / SD / BLE 推送加载，
 * 调用方接口不变。
 */
bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len);

/**
 * 取标准库 prelude 脚本（含 VDOM / makeBle / setDispatcher）。
 * runtime 在每次 eval 业务脚本之前先 eval 一次 prelude，
 * 业务脚本因此能直接使用 VDOM / h / makeBle 等全局符号。
 */
void dynamic_app_registry_get_prelude(const uint8_t **out_buf,
                                      size_t *out_len);

/**
 * 当前正在跑的 app 名（runtime 在 eval 前调用 set，
 * sys.app.saveState/loadState 用它当 NVS key）。
 */
void        dynamic_app_registry_set_current(const char *name);
const char *dynamic_app_registry_current(void);

#ifdef __cplusplus
}
#endif
