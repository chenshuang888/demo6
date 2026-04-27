/* ============================================================================
 * dynamic_app_registry.c —— "app 名 → 脚本 buffer" 双源查找
 *
 * 双源：
 *   1) 内嵌（g_apps[]，编译期 EMBED_TXTFILES）—— 出厂 7 个核心 app
 *   2) FS（services/dynapp_storage，路径 /littlefs/apps/<name>.js）
 *      —— 通过 BLE 推送 / 未来 WiFi 下载得到的脚本
 *
 * 查找顺序：内嵌优先。同名时 FS 版本被忽略，保证脚本写错也能恢复。
 *
 * 内存归属：
 *   - 内嵌返回的 buf 指向 rodata，release 时不能 free
 *   - FS 返回的 buf 是 dynapp_storage 分配的 heap，release 时必须 free
 *   实现上用一张小表 s_heap_refs[] 记录"曾被 get() 返回的 heap 指针"，
 *   release 时查表区分。表大小 4 足够：runtime 当前只跑一个 app，
 *   多发出几个仅是为了 prepare/commit 切屏期间瞬时存在两份的兼容。
 * ========================================================================= */

#include "dynamic_app_registry.h"
#include "dynapp_storage.h"

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

/* ---- heap 指针追踪（区分内嵌 vs FS）---- */

#define HEAP_REFS_MAX 4
static uint8_t *s_heap_refs[HEAP_REFS_MAX];

static void heap_refs_add(uint8_t *p)
{
    for (int i = 0; i < HEAP_REFS_MAX; i++) {
        if (s_heap_refs[i] == NULL) { s_heap_refs[i] = p; return; }
    }
    /* 满了说明上层泄漏没释放——这里直接覆盖最早一个，至少不让新分配漏掉 release。
     * 同时它确保 release 不会 free 内嵌符号（因为内嵌符号永远不会出现在表里）。 */
    s_heap_refs[0] = p;
}

static bool heap_refs_take(const uint8_t *p)
{
    for (int i = 0; i < HEAP_REFS_MAX; i++) {
        if (s_heap_refs[i] == p) { s_heap_refs[i] = NULL; return true; }
    }
    return false;
}

/* ---- public ---- */

bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len)
{
    if (!name || !*name || !out_buf || !out_len) return false;

    /* 1) 内嵌优先 */
    for (int i = 0; i < G_APPS_COUNT; i++) {
        if (strcmp(g_apps[i].name, name) == 0) {
            *out_buf = g_apps[i].buf_start;
            *out_len = (size_t)(g_apps[i].buf_end - g_apps[i].buf_start);
            return true;
        }
    }

    /* 2) FS 兜底 */
    uint8_t *buf = NULL;
    size_t   len = 0;
    if (dynapp_storage_read(name, &buf, &len) != ESP_OK) return false;

    heap_refs_add(buf);
    *out_buf = buf;
    *out_len = len;
    return true;
}

void dynamic_app_registry_release(const uint8_t *buf)
{
    if (!buf) return;
    if (heap_refs_take(buf)) {
        dynapp_storage_release((uint8_t *)buf);
    }
    /* 不在表里 → 内嵌符号，no-op */
}

void dynamic_app_registry_get_prelude(const uint8_t **out_buf, size_t *out_len)
{
    if (out_buf) *out_buf = prelude_js_start;
    if (out_len) *out_len = (size_t)(prelude_js_end - prelude_js_start);
}

int dynamic_app_registry_list(dynamic_app_entry_t *out, int max)
{
    if (!out || max <= 0) return 0;

    int n = 0;

    /* 1) 内嵌全部入表 */
    for (int i = 0; i < G_APPS_COUNT && n < max; i++) {
        strncpy(out[n].name, g_apps[i].name, DYNAPP_REGISTRY_NAME_MAX);
        out[n].name[DYNAPP_REGISTRY_NAME_MAX] = '\0';
        out[n].builtin = true;
        n++;
    }

    /* 2) FS：跳过同名（内嵌优先） */
    char fs_names[8][DYNAPP_STORAGE_MAX_NAME_LEN + 1];
    int  fs_count = dynapp_storage_list(fs_names,
                       (int)(sizeof(fs_names) / sizeof(fs_names[0])));
    for (int i = 0; i < fs_count && n < max; i++) {
        bool dup = false;
        for (int j = 0; j < n; j++) {
            if (strcmp(out[j].name, fs_names[i]) == 0) { dup = true; break; }
        }
        if (dup) continue;
        strncpy(out[n].name, fs_names[i], DYNAPP_REGISTRY_NAME_MAX);
        out[n].name[DYNAPP_REGISTRY_NAME_MAX] = '\0';
        out[n].builtin = false;
        n++;
    }

    return n;
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
