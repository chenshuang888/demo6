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

/**
 * "后台 prepare + 准备好后瞬切"模式（推荐 menu 入口使用）：
 *
 * 流程：
 *   1) 调用方（典型是 page_menu）传入要跑的 app 名
 *   2) 本函数立即返回，*不切屏*。menu 仍在显示
 *   3) 内部在后台创建一棵 off-screen 对象树，启动脚本 build UI
 *   4) 脚本最末调 sys.ui.attachRootListener 时触发 ready 回调
 *   5) ready 回调内部调 page_router_commit_prepared()，瞬间切到完整 calc/alarm
 *   6) 万一脚本卡住，内部 ~800ms 超时强切，避免卡 menu
 *
 * 与 set_pending + page_router_switch 的差异：
 *   - 用户视觉上看不到 dynamic app 的"组件一个个冒出来"过程
 *   - 短暂多占 1 棵 LVGL 对象树的内存（PSRAM 上无压力）
 *
 * 调用时如有上一次 prepare 还没 commit/cancel，会先 cancel 它再开新的。
 *
 * @note 必须在 UI 线程调用。
 */
void page_dynamic_app_prepare_and_switch(const char *app_name);

/**
 * 取消"正在进行中的 prepare"（如有）。
 *
 * menu 上其它原生页跳转（System / Music / About 等）需要先调一次本函数兜底，
 * 防止 prepare 中途用户切走，留下野脚本继续往不存在的 root 灌命令。
 *
 * 状态非 PREPARING 时本函数为 no-op，可放心无脑调用。
 *
 * @note 必须在 UI 线程调用。
 */
void page_dynamic_app_cancel_prepare_if_any(void);

#ifdef __cplusplus
}
#endif
