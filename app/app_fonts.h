#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 非 const 字体副本：CJK + fallback 到 Montserrat，以兼容 LV_SYMBOL_* */
extern lv_font_t g_app_font_text;
extern lv_font_t g_app_font_title;

#define APP_FONT_TEXT   (&g_app_font_text)                  /* 正文（14px CJK） */
#define APP_FONT_TITLE  (&g_app_font_title)                 /* 标题（16px CJK） */
#define APP_FONT_LARGE  (&lv_font_montserrat_24)            /* 大号数字（仅 ASCII） */

/**
 * 初始化字体副本并挂上 fallback。
 * 必须在 lvgl_port_init() 之后、任何页面 create 之前调用。
 */
void app_fonts_init(void);

#ifdef __cplusplus
}
#endif
