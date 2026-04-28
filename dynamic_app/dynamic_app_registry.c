/* ============================================================================
 * dynamic_app_registry.c —— "app_id → main.js + manifest" 单源（FS）查找
 *
 * 设计：
 *   - 业务 app 全部来自 LittleFS：/littlefs/apps/<app_id>/main.js
 *     manifest 元信息可选；缺失时 display name 回退到 app_id
 *   - prelude.js 仍是唯一内嵌脚本（rodata）
 * ========================================================================= */

#include "dynamic_app_registry.h"
#include "dynapp_script_store.h"

#include <string.h>

extern const uint8_t prelude_js_start[]  asm("_binary_prelude_js_start");
extern const uint8_t prelude_js_end[]    asm("_binary_prelude_js_end");

bool dynamic_app_registry_get(const char *app_id,
                              const uint8_t **out_buf,
                              size_t *out_len)
{
    if (!app_id || !*app_id || !out_buf || !out_len) return false;

    uint8_t *buf = NULL;
    size_t   len = 0;
    if (dynapp_app_file_read(app_id, DYNAPP_FILE_MAIN, &buf, &len) != 0) return false;

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
        strncpy(out[n].id, fs_names[i], DYNAPP_REGISTRY_NAME_MAX);
        out[n].id[DYNAPP_REGISTRY_NAME_MAX] = '\0';

        dynapp_manifest_t mf;
        if (dynapp_manifest_read(out[n].id, &mf) == 0 && mf.name[0]) {
            strncpy(out[n].display, mf.name, DYNAPP_REGISTRY_DISP_MAX);
            out[n].display[DYNAPP_REGISTRY_DISP_MAX] = '\0';
            out[n].has_manifest = true;
        } else {
            strncpy(out[n].display, out[n].id, DYNAPP_REGISTRY_DISP_MAX);
            out[n].display[DYNAPP_REGISTRY_DISP_MAX] = '\0';
            out[n].has_manifest = false;
        }
        n++;
    }
    return n;
}

static char s_current_id[16] = "";

void dynamic_app_registry_set_current(const char *app_id)
{
    if (!app_id) { s_current_id[0] = '\0'; return; }
    strncpy(s_current_id, app_id, sizeof(s_current_id) - 1);
    s_current_id[sizeof(s_current_id) - 1] = '\0';
}

const char *dynamic_app_registry_current(void)
{
    return s_current_id;
}
