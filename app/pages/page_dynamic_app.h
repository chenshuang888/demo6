#pragma once

#include "page_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamic App 页面（MicroQuickJS）：
 * - 这是动态 app 的"宿主页面"：本身不写死任何 app 业务，
 *   只做"清空 → 启动指定 app → 等退出"的容器
 * - 切到本页前调用 page_dynamic_app_set_pending("alarm" / "calc")
 *   告诉宿主该跑哪个 app
 * - 没设置时默认跑 "alarm"
 */

/**
 * 返回该页面的路由回调（create/destroy/...）。
 */
const page_callbacks_t *page_dynamic_app_get_callbacks(void);

/**
 * 切到本页之前调用，指定要启动哪个 app。
 *
 * @param app_name 注册表中的 app 名（"alarm" / "calc" / ...）
 *
 * 调用顺序：
 *   page_dynamic_app_set_pending("calc");
 *   page_router_switch(PAGE_DYNAMIC_APP);
 *
 * 名称会被拷贝到内部，调用方不必保留字符串生命周期。
 */
void page_dynamic_app_set_pending(const char *app_name);

#ifdef __cplusplus
}
#endif
