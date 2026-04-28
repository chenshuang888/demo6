/* ============================================================================
 * dynamic_app_registry.c —— "app 名 → 脚本 buffer" 单源（FS）查找
 *
 * 设计：
 *   - 业务 app 一律来自 LittleFS：路径 /littlefs/apps/<name>.js
 *     由 BLE 上传 (dynapp_upload_service / dynapp_upload_manager) 写入，
 *     通过 storage/littlefs/dynapp_script_store 读出
 *   - prelude.js 是唯一例外：作为 runtime 标准库内嵌进固件，
 *     runtime 启动不依赖 FS（FS 没挂或为空也能初始化）
 *
 * 内存归属：
 *   - get() 返回的 buf 永远是 dynapp_script_store 分配的 heap
 *   - release() 直接 free，不再需要区分 rodata vs heap
 *
 * 历史：曾经 g_apps[] 内嵌 7 个 .js（alarm/calc/timer/2048/echo/weather/music），
 * 与 FS 双源 + 内嵌优先去重。模型对齐智能手机后改为单源：核心功能未来用 C 写
 * 成系统页（page_router），脚本通道只负责"应用商店"语义的下载式 app。
 * ========================================================================= */

#include "dynamic_app_registry.h"
#include "dynapp_script_store.h"

#include <string.h>

/* prelude 内嵌符号（来自 EMBED_TXTFILES "scripts/prelude.js"） */
extern const uint8_t prelude_js_start[]  asm("_binary_prelude_js_start");
extern const uint8_t prelude_js_end[]    asm("_binary_prelude_js_end");

/* ---- public ---- */

bool dynamic_app_registry_get(const char *name,
                              const uint8_t **out_buf,
                              size_t *out_len)
{
    if (!name || !*name || !out_buf || !out_len) return false;

    uint8_t *buf = NULL;
    size_t   len = 0;
    if (dynapp_script_store_read(name, &buf, &len) != ESP_OK) return false;

    *out_buf = buf;
    *out_len = len;
    return true;
}

void dynamic_app_registry_release(const uint8_t *buf)
{
    if (!buf) return;
    dynapp_script_store_release((uint8_t *)buf);
}

void dynamic_app_registry_get_prelude(const uint8_t **out_buf, size_t *out_len)
{
    if (out_buf) *out_buf = prelude_js_start;
    if (out_len) *out_len = (size_t)(prelude_js_end - prelude_js_start);
}

int dynamic_app_registry_list(dynamic_app_entry_t *out, int max)
{
    if (!out || max <= 0) return 0;

    char fs_names[8][DYNAPP_SCRIPT_STORE_MAX_NAME + 1];
    int  fs_count = dynapp_script_store_list(fs_names,
                       (int)(sizeof(fs_names) / sizeof(fs_names[0])));

    int n = 0;
    for (int i = 0; i < fs_count && n < max; i++) {
        strncpy(out[n].name, fs_names[i], DYNAPP_REGISTRY_NAME_MAX);
        out[n].name[DYNAPP_REGISTRY_NAME_MAX] = '\0';
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
