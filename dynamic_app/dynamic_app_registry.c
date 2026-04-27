/* ============================================================================
 * dynamic_app_registry.c —— "app 名 → 脚本 buffer" 的查找层
 *
 * 当前实现：固件里嵌入两个脚本（alarm.js / calc.js），按名匹配。
 * 将来扩展：可改成查 NVS / SD / OTA cache，调用方完全不变。
 * 所有"哪些 app 存在"的知识都集中在 g_apps[] 一张表里。
 * ========================================================================= */

#include "dynamic_app_registry.h"

#include <string.h>

/* 嵌入资源符号（来自 EMBED_TXTFILES） */
extern const uint8_t prelude_js_start[]  asm("_binary_prelude_js_start");
extern const uint8_t prelude_js_end[]    asm("_binary_prelude_js_end");
extern const uint8_t alarm_js_start[]    asm("_binary_alarm_js_start");
extern const uint8_t alarm_js_end[]      asm("_binary_alarm_js_end");
extern const uint8_t calc_js_start[]     asm("_binary_calc_js_start");
extern const uint8_t calc_js_end[]       asm("_binary_calc_js_end");
extern const uint8_t timer_js_start[]    asm("_binary_timer_js_start");
extern const uint8_t timer_js_end[]      asm("_binary_timer_js_end");
extern const uint8_t game2048_js_start[] asm("_binary_game2048_js_start");
extern const uint8_t game2048_js_end[]   asm("_binary_game2048_js_end");
extern const uint8_t echo_js_start[]     asm("_binary_echo_js_start");
extern const uint8_t echo_js_end[]       asm("_binary_echo_js_end");
extern const uint8_t weather_js_start[]  asm("_binary_weather_js_start");
extern const uint8_t weather_js_end[]    asm("_binary_weather_js_end");
extern const uint8_t music_js_start[]    asm("_binary_music_js_start");
extern const uint8_t music_js_end[]      asm("_binary_music_js_end");

typedef struct {
    const char    *name;
    const uint8_t *buf_start;
    const uint8_t *buf_end;
} app_entry_t;

static const app_entry_t g_apps[] = {
    { "alarm",   alarm_js_start,    alarm_js_end    },
    { "calc",    calc_js_start,     calc_js_end     },
    { "timer",   timer_js_start,    timer_js_end    },
    { "2048",    game2048_js_start, game2048_js_end },
    { "echo",    echo_js_start,     echo_js_end     },
    { "weather", weather_js_start,  weather_js_end  },
    { "music",   music_js_start,    music_js_end    },
};
#define G_APPS_COUNT (int)(sizeof(g_apps) / sizeof(g_apps[0]))

bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len)
{
    if (!name || !out_buf || !out_len) return false;
    for (int i = 0; i < G_APPS_COUNT; i++) {
        if (strcmp(g_apps[i].name, name) == 0) {
            *out_buf = g_apps[i].buf_start;
            *out_len = (size_t)(g_apps[i].buf_end - g_apps[i].buf_start);
            return true;
        }
    }
    return false;
}

void dynamic_app_registry_get_prelude(const uint8_t **out_buf, size_t *out_len)
{
    if (out_buf) *out_buf = prelude_js_start;
    if (out_len) *out_len = (size_t)(prelude_js_end - prelude_js_start);
}

/* 当前 app 名：start 时由控制层写入，sys.app 系列 native 读取做 NVS key。
 * 默认空串：sys.app.saveState 在没有 current 时拒绝写。 */
static char s_current_name[16] = "";

void dynamic_app_registry_set_current(const char *name)
{
    if (!name) { s_current_name[0] = '\0'; return; }
    strncpy(s_current_name, name, sizeof(s_current_name) - 1);
    s_current_name[sizeof(s_current_name) - 1] = '\0';
}

const char *dynamic_app_registry_current(void)
{
    return s_current_name;
}
