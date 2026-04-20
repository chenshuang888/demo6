#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tiny TTF 运行时从嵌入的子集 TTF 创建字体对象；指针在 app_fonts_init 赋值 */
extern lv_font_t *g_app_font_text;
extern lv_font_t *g_app_font_title;
extern lv_font_t *g_app_font_huge;

#define APP_FONT_TEXT   (g_app_font_text)               /* 正文 14px（CJK TTF 渲染） */
#define APP_FONT_TITLE  (g_app_font_title)              /* 标题 16px（CJK TTF 渲染） */
#define APP_FONT_LARGE  (&lv_font_montserrat_24)        /* 大号数字 / ASCII-only */
#define APP_FONT_HUGE   (g_app_font_huge)               /* 48px 巨号（锁屏时间专用，TTF 渲染） */

/**
 * 初始化字体：从固件嵌入的 TTF 创建 14/16/48 三个 lv_font_t，挂 Montserrat fallback。
 * 必须在 lvgl_port_init() 之后、任何页面 create 之前调用。
 */
void app_fonts_init(void);

#ifdef __cplusplus
}
#endif
