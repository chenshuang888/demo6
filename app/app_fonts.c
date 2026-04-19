#include "app_fonts.h"

lv_font_t g_app_font_text;
lv_font_t g_app_font_title;

void app_fonts_init(void)
{
    /* LVGL 自带的 CJK 字体声明为 const，这里值拷贝到非 const 副本，
     * 以便挂 fallback 链。Montserrat 作为 fallback 覆盖极少数 CJK
     * 字体缺失的 FontAwesome 符号（如 LV_SYMBOL_BLUETOOTH）。 */
    g_app_font_text          = lv_font_source_han_sans_sc_14_cjk;
    g_app_font_text.fallback = &lv_font_montserrat_14;

    g_app_font_title          = lv_font_source_han_sans_sc_16_cjk;
    g_app_font_title.fallback = &lv_font_montserrat_20;
}
