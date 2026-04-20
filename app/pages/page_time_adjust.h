#pragma once

#include "page_router.h"

/**
 * 获取时间/日期调节页面的回调函数
 *
 * 从菜单进入，负责手动调节系统时间。
 * 原 page_time.c 的调节卡片逻辑搬到这里，page_time 改为纯锁屏首页。
 */
const page_callbacks_t *page_time_adjust_get_callbacks(void);
