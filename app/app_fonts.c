#include "app_fonts.h"
#include "esp_log.h"

static const char *TAG = "app_fonts";

/* EMBED_FILES 从 app/fonts/srhs_sc_subset.ttf 生成如下符号。
 * 文件名中的 '.' 被替换为 '_'：srhs_sc_subset.ttf → srhs_sc_subset_ttf */
extern const uint8_t srhs_ttf_start[] asm("_binary_srhs_sc_subset_ttf_start");
extern const uint8_t srhs_ttf_end[]   asm("_binary_srhs_sc_subset_ttf_end");

lv_font_t *g_app_font_text  = NULL;
lv_font_t *g_app_font_title = NULL;
lv_font_t *g_app_font_huge  = NULL;

void app_fonts_init(void)
{
    const size_t ttf_size = srhs_ttf_end - srhs_ttf_start;
    ESP_LOGI(TAG, "embedded TTF size: %u bytes", (unsigned)ttf_size);

    /* Tiny TTF 返回堆分配的 lv_font_t*；内部 glyph cache 由 LVGL malloc 申请。
     * 开了 LV_USE_CLIB_MALLOC 后大块 cache 自动走 PSRAM。
     * 对 CJK 文字 kerning 基本无意义，显式关闭节省 cache 空间。 */
    g_app_font_text = lv_tiny_ttf_create_data_ex(
        srhs_ttf_start, ttf_size,
        14, LV_FONT_KERNING_NONE, 256);

    g_app_font_title = lv_tiny_ttf_create_data_ex(
        srhs_ttf_start, ttf_size,
        16, LV_FONT_KERNING_NONE, 256);

    /* 锁屏大号时间 HH:MM 只有 0-9 和 ':' 共 11 个字符，cache 给 32 足够 */
    g_app_font_huge = lv_tiny_ttf_create_data_ex(
        srhs_ttf_start, ttf_size,
        48, LV_FONT_KERNING_NONE, 32);

    if (!g_app_font_text || !g_app_font_title || !g_app_font_huge) {
        ESP_LOGE(TAG, "lv_tiny_ttf_create_data_ex failed (text=%p title=%p huge=%p)",
                 g_app_font_text, g_app_font_title, g_app_font_huge);
        return;
    }

    /* CJK TTF 不含 FontAwesome 私有区（LV_SYMBOL_* 的 U+F001~U+F8FF），
     * fallback 到 Montserrat 画 LV_SYMBOL_LEFT / PLAY / BLUETOOTH 等图标。 */
    g_app_font_text->fallback  = &lv_font_montserrat_14;
    g_app_font_title->fallback = &lv_font_montserrat_20;
    g_app_font_huge->fallback  = &lv_font_montserrat_24;
}
