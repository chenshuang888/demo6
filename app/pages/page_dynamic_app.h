#pragma once

#include "page_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamic App 页面（MicroQuickJS MVP）：
 * - 页面自身只负责“展示一个可被脚本更新的 label”
 * - 具体的脚本运行、sys.* API、跨线程 UI 更新队列都在 dynamic_app/ 组件中实现
 */

/**
 * 返回该页面的路由回调（create/destroy/...）。
 *
 * @note page_router 需要为每个页面提供一组 callbacks，至少包含 create。
 */
const page_callbacks_t *page_dynamic_app_get_callbacks(void);

#ifdef __cplusplus
}
#endif
