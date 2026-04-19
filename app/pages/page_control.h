#pragma once

#include "page_router.h"

/**
 * 获取控制面板页面的回调函数
 * 2×2 触摸按钮 → BLE control_service → PC
 */
const page_callbacks_t *page_control_get_callbacks(void);
