/* ============================================================================
 * dynapp_storage.c —— 动态 App 脚本仓
 *
 * 文件布局：
 *   /littlefs/apps/<name>.js        正式脚本
 *   /littlefs/apps/<name>.js.tmp    write 时的临时文件（rename 前）
 *
 * 名称规则：name 必须满足
 *   - 非空、长度 ≤ DYNAPP_STORAGE_MAX_NAME_LEN
 *   - 仅由 [a-zA-Z0-9_-] 组成（避免路径穿越和 shell 元字符）
 * 这条规则对所有公开接口生效。
 * ========================================================================= */

#include "dynapp_storage.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "fs_littlefs.h"

static const char *TAG = "dynapp_storage";

#define APPS_DIR  FS_LITTLEFS_ROOT "/apps"
#define EXT       ".js"
#define EXT_LEN   3

/* ------------------------------------------------------------------ helpers */

static bool name_is_valid(const char *name)
{
    if (!name || !*name) return false;
    size_t n = strlen(name);
    if (n > DYNAPP_STORAGE_MAX_NAME_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/* 拼出 /littlefs/apps/<name>.js 或 .tmp，写入 out。
 * out 容量需 >= sizeof(APPS_DIR) + 1 + DYNAPP_STORAGE_MAX_NAME_LEN + 5. */
static void build_path(char *out, size_t cap, const char *name, bool tmp)
{
    if (tmp) snprintf(out, cap, APPS_DIR "/%s" EXT ".tmp", name);
    else     snprintf(out, cap, APPS_DIR "/%s" EXT,        name);
}

/* ------------------------------------------------------------------ init */

esp_err_t dynapp_storage_init(void)
{
    /* 确保 /littlefs/apps/ 存在；已存在不算错。 */
    int rc = mkdir(APPS_DIR, 0775);
    if (rc != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s failed: errno=%d", APPS_DIR, errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "scripts dir ready at %s", APPS_DIR);
    return ESP_OK;
}

/* ------------------------------------------------------------------ read */

esp_err_t dynapp_storage_read(const char *name,
                              uint8_t **out_buf,
                              size_t *out_len)
{
    if (!out_buf || !out_len) return ESP_ERR_INVALID_ARG;
    *out_buf = NULL; *out_len = 0;
    if (!name_is_valid(name)) return ESP_ERR_INVALID_ARG;

    char path[64];
    build_path(path, sizeof(path), name, false);

    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    /* 取文件大小：fseek+ftell 在 LittleFS 上工作正常。 */
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ESP_FAIL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return ESP_FAIL; }
    if ((size_t)sz > DYNAPP_STORAGE_MAX_SCRIPT_BYTES) {
        ESP_LOGW(TAG, "%s.js too large (%ld), refusing", name, sz);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);

    /* 多分配 1 字节，方便上层把 buf 当 C 字符串调试打印。 */
    uint8_t *buf = (uint8_t *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return ESP_FAIL;
    }
    buf[sz] = '\0';

    *out_buf = buf;
    *out_len = (size_t)sz;
    return ESP_OK;
}

void dynapp_storage_release(uint8_t *buf)
{
    free(buf);
}

/* ------------------------------------------------------------------ write */

esp_err_t dynapp_storage_write(const char *name,
                               const uint8_t *buf,
                               size_t len)
{
    if (!name_is_valid(name) || !buf) return ESP_ERR_INVALID_ARG;
    if (len == 0 || len > DYNAPP_STORAGE_MAX_SCRIPT_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    char tmp_path[64], final_path[64];
    build_path(tmp_path,   sizeof(tmp_path),   name, true);
    build_path(final_path, sizeof(final_path), name, false);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open %s for write failed: errno=%d", tmp_path, errno);
        return ESP_FAIL;
    }

    size_t put = fwrite(buf, 1, len, f);
    int    rc1 = fflush(f);
    int    rc2 = fclose(f);
    if (put != len || rc1 != 0 || rc2 != 0) {
        ESP_LOGE(TAG, "write %s short (%u/%u) flush=%d close=%d",
                 tmp_path, (unsigned)put, (unsigned)len, rc1, rc2);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    /* 原子替换：rename 在 LittleFS 上是原子的，断电不会出现半新半旧。
     * 已存在的 final_path 必须先删（POSIX rename 允许覆盖，但 LittleFS 老版本
     * 可能拒绝；显式 unlink 一下更稳）。 */
    unlink(final_path);
    if (rename(tmp_path, final_path) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed: errno=%d",
                 tmp_path, final_path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved %s.js (%u bytes)", name, (unsigned)len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ delete */

esp_err_t dynapp_storage_delete(const char *name)
{
    if (!name_is_valid(name)) return ESP_ERR_INVALID_ARG;
    char path[64];
    build_path(path, sizeof(path), name, false);
    if (unlink(path) != 0) {
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    ESP_LOGI(TAG, "deleted %s.js", name);
    return ESP_OK;
}

bool dynapp_storage_exists(const char *name)
{
    if (!name_is_valid(name)) return false;
    char path[64];
    build_path(path, sizeof(path), name, false);
    struct stat st;
    return stat(path, &st) == 0;
}

/* ------------------------------------------------------------------ list */

int dynapp_storage_list(char out[][DYNAPP_STORAGE_MAX_NAME_LEN + 1], int max)
{
    if (!out || max <= 0) return 0;

    DIR *d = opendir(APPS_DIR);
    if (!d) {
        ESP_LOGW(TAG, "opendir %s failed", APPS_DIR);
        return 0;
    }

    int n = 0;
    struct dirent *ent;
    while (n < max && (ent = readdir(d)) != NULL) {
        const char *fname = ent->d_name;
        size_t flen = strlen(fname);

        /* 必须以 .js 结尾，且长度合法。.tmp / 隐藏文件直接跳过。 */
        if (flen <= EXT_LEN) continue;
        if (strcmp(fname + flen - EXT_LEN, EXT) != 0) continue;
        size_t base_len = flen - EXT_LEN;
        if (base_len == 0 || base_len > DYNAPP_STORAGE_MAX_NAME_LEN) continue;

        memcpy(out[n], fname, base_len);
        out[n][base_len] = '\0';

        /* 复用 name_is_valid 防止路径穿越类的脏文件（例如手动塞进来的 ../x.js）。 */
        if (!name_is_valid(out[n])) continue;

        n++;
    }
    closedir(d);
    return n;
}
