#include "dynamic_app_ui.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "dynamic_app_ui";

#define UI_QUEUE_LEN 16
#define UI_REGISTRY_MAX 16

/* 只允许在 PAGE_DYNAMIC_APP 创建 UI：该 root 仅由该页面的 UI Task 设置/清空 */
static volatile lv_obj_t *s_root = NULL;

/*
 * UI 指令队列：
 * - Script Task 往这里投递 command（例如 SET_TEXT）
 * - UI Task 在 `dynamic_app_ui_drain()` 中取出并执行
 *
 * MVP 阶段的取舍：
 * - 队列满时选择丢弃（enqueue 返回 false），避免脚本线程阻塞导致“更卡”；
 * - max_len/registry 大小都较小：足够 demo，但不是最终形态（后续可按需扩展）。
 */

typedef struct {
    bool used;
    char id[DYNAMIC_APP_UI_ID_MAX_LEN];
    lv_obj_t *obj;
} ui_registry_entry_t;

static QueueHandle_t s_ui_queue = NULL;
static ui_registry_entry_t s_registry[UI_REGISTRY_MAX];

/*
 * UTF-8 安全截断：确保截断后依然是“合法的 UTF-8 字节序列”。
 *
 * 背景：
 * - 我们把 id/text 拷贝进定长数组，需要做长度裁剪；
 * - 如果直接按字节截断，可能截到一个多字节字符的中间，导致 LVGL/日志显示乱码甚至异常。
 *
 * 这里的策略：
 * - 给定最多 len 字节，向前回退，找到一个“字符边界”。
 */
static size_t utf8_truncate_len(const char *s, size_t len)
{
    if (!s) return 0;
    if (len == 0) return 0;

    size_t n = len;
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if ((c & 0x80) == 0) {
            return n;
        }

        size_t start = n - 1;
        while (start > 0 && (((unsigned char)s[start] & 0xC0) == 0x80)) {
            start--;
        }

        unsigned char lead = (unsigned char)s[start];
        size_t need = 1;
        if ((lead & 0xE0) == 0xC0) need = 2;
        else if ((lead & 0xF0) == 0xE0) need = 3;
        else if ((lead & 0xF8) == 0xF0) need = 4;
        else {
            n = start;
            continue;
        }

        if (start + need <= n) {
            return n;
        }
        n = start;
    }
    return 0;
}

static void utf8_copy_trunc(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || src_len == 0) return;

    size_t max_copy = dst_size - 1;
    if (src_len < max_copy) max_copy = src_len;
    size_t safe_len = utf8_truncate_len(src, max_copy);
    if (safe_len > 0) {
        memcpy(dst, src, safe_len);
    }
    dst[safe_len] = '\0';
}

esp_err_t dynamic_app_ui_init(void)
{
    if (s_ui_queue) {
        return ESP_OK;
    }

    /* 只在第一次调用时创建队列；队列元素是完整的 command 结构体。 */
    s_ui_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(dynamic_app_ui_command_t));
    if (!s_ui_queue) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_registry, 0, sizeof(s_registry));
    return ESP_OK;
}

bool dynamic_app_ui_enqueue_set_text(const char *id, size_t id_len,
                                     const char *text, size_t text_len)
{
    if (!s_ui_queue || !id || !text) return false;

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_SET_TEXT;

    /* 把输入参数拷贝到定长 buffer，并做 UTF-8 安全截断。 */
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    utf8_copy_trunc(cmd.text, sizeof(cmd.text), text, text_len);

    if (cmd.id[0] == '\0') return false;

    /*
     * MVP 取舍：不阻塞。
     * - 这里 timeout=0：队列满则直接失败并丢弃；
     * - 如果后续需要“必达”，可以改为等待/覆盖/合并等策略。
     */
    BaseType_t ok = xQueueSend(s_ui_queue, &cmd, 0);
    return ok == pdTRUE;
}

bool dynamic_app_ui_enqueue_create_label(const char *id, size_t id_len)
{
    if (!s_ui_queue || !id) return false;

    /* root 未设置时，表示当前不在 Dynamic App 页面：直接拒绝 */
    if (s_root == NULL) {
        return false;
    }

    dynamic_app_ui_command_t cmd = {0};
    cmd.type = DYNAMIC_APP_UI_CMD_CREATE_LABEL;
    utf8_copy_trunc(cmd.id, sizeof(cmd.id), id, id_len);
    cmd.text[0] = '\0';

    if (cmd.id[0] == '\0') return false;

    BaseType_t ok = xQueueSend(s_ui_queue, &cmd, 0);
    return ok == pdTRUE;
}

esp_err_t dynamic_app_ui_register_label(const char *id, lv_obj_t *obj)
{
    if (!id || id[0] == '\0' || !obj) {
        return ESP_ERR_INVALID_ARG;
    }

    /* UI 线程校验：对象必须还有效。 */
    if (!lv_obj_is_valid(obj)) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 为了简化脚本侧能力，本 MVP 只支持更新 label 的 text。 */
    if (!lv_obj_has_class(obj, &lv_label_class)) {
        ESP_LOGE(TAG, "register failed: obj is not label (id=%s)", id);
        return ESP_ERR_INVALID_ARG;
    }

    /* 如果 id 已存在：直接覆盖 obj 指针（方便页面重建后复用同一个 id）。 */
    for (int i = 0; i < UI_REGISTRY_MAX; i++) {
        if (s_registry[i].used && strncmp(s_registry[i].id, id, sizeof(s_registry[i].id)) == 0) {
            s_registry[i].obj = obj;
            return ESP_OK;
        }
    }

    /* 否则找一个空位新增。 */
    for (int i = 0; i < UI_REGISTRY_MAX; i++) {
        if (!s_registry[i].used) {
            s_registry[i].used = true;
            strncpy(s_registry[i].id, id, sizeof(s_registry[i].id) - 1);
            s_registry[i].id[sizeof(s_registry[i].id) - 1] = '\0';
            s_registry[i].obj = obj;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "UI registry full");
    return ESP_ERR_NO_MEM;
}

void dynamic_app_ui_set_root(lv_obj_t *root)
{
    /* 只允许 UI Task 调用；这里不做强校验（root 可能为 NULL） */
    s_root = root;
}

void dynamic_app_ui_unregister_all(void)
{
    /* 只清 registry，不删除 LVGL 对象：对象由页面自己创建/销毁。 */
    memset(s_registry, 0, sizeof(s_registry));
}

void dynamic_app_ui_drain(int max_count)
{
    if (!s_ui_queue || max_count <= 0) return;

    dynamic_app_ui_command_t cmd;
    int handled = 0;

    while (handled < max_count && xQueueReceive(s_ui_queue, &cmd, 0) == pdTRUE) {
        handled++;
        if (cmd.type == DYNAMIC_APP_UI_CMD_CREATE_LABEL) {
            lv_obj_t *root = (lv_obj_t *)s_root;
            if (!root || !lv_obj_is_valid(root)) {
                /* 页面已销毁或未设置 root：丢弃 */
                s_root = NULL;
                continue;
            }

            /* 找到可用的 registry 槽位：同 id 复用，否则占用一个空槽 */
            int slot = -1;
            bool already_ok = false;
            for (int i = 0; i < UI_REGISTRY_MAX; i++) {
                if (!s_registry[i].used) continue;
                if (strncmp(s_registry[i].id, cmd.id, sizeof(s_registry[i].id)) != 0) continue;
                slot = i;
                already_ok = (s_registry[i].obj && lv_obj_is_valid(s_registry[i].obj));
                break;
            }
            if (already_ok) {
                continue;
            }

            if (slot < 0) {
                for (int i = 0; i < UI_REGISTRY_MAX; i++) {
                    if (!s_registry[i].used) {
                        slot = i;
                        break;
                    }
                }
            }
            if (slot < 0) {
                ESP_LOGW(TAG, "UI registry full, createLabel dropped (id=%s)", cmd.id);
                continue;
            }

            lv_obj_t *lbl = lv_label_create(root);
            lv_label_set_text(lbl, "");
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, lv_pct(100));

            s_registry[slot].used = true;
            strncpy(s_registry[slot].id, cmd.id, sizeof(s_registry[slot].id) - 1);
            s_registry[slot].id[sizeof(s_registry[slot].id) - 1] = '\0';
            s_registry[slot].obj = lbl;
            continue;
        }

        if (cmd.type != DYNAMIC_APP_UI_CMD_SET_TEXT) {
            continue;
        }

        /* 通过 id 在 registry 里找到目标 label，然后执行 LVGL 更新。 */
        for (int i = 0; i < UI_REGISTRY_MAX; i++) {
            if (!s_registry[i].used) continue;
            if (strncmp(s_registry[i].id, cmd.id, sizeof(s_registry[i].id)) != 0) continue;

            lv_obj_t *obj = s_registry[i].obj;
            if (!obj || !lv_obj_is_valid(obj)) {
                /* 目标对象已失效：清空指针，避免后续继续命中。 */
                s_registry[i].obj = NULL;
                break;
            }
            lv_label_set_text(obj, cmd.text);
            break;
        }
    }
}
