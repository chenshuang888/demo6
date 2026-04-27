/* ============================================================================
 * dynapp_upload_manager.c —— 上传/运维 消息中介 + 独占消费 task
 *
 * 文件结构：
 *   §1. 队列 item 类型
 *   §2. 状态机（IDLE / RECEIVING）
 *   §3. submit_* 提交函数（任意线程调，xQueueSend timeout=0）
 *   §4. dispatch_* 处理函数（consumer task 内部调，跑 FS 操作）
 *   §5. CRC32 计算
 *   §6. consumer task 主循环 + init
 *
 * 关键约束（自我提醒）：
 *   - 本文件不 include 任何 BLE/NimBLE 头文件（解耦验证：grep 一遍）
 *   - submit_* 永远 xQueueSend(0)，不阻塞
 *   - dispatch_* 跑在 consumer task 线程，可以慢慢写 FS
 * ========================================================================= */

#include "dynapp_upload_manager.h"
#include "dynapp_script_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "dynapp_upload";

/* ============================================================================
 * §1. 队列 item
 *
 * 把所有 op 塞进同一个 union 里，队列条目固定大小 ≈ 32B。
 * CHUNK 的数据怎么传？两种选择：
 *   a) item 里塞固定 200B 缓冲 → 队列总占 8 * 230B ≈ 2KB，简单
 *   b) item 里只放 heap 指针 + len，CHUNK 数据走 malloc
 * 选 a：8 * 230B 没多少，省掉每帧 malloc/free 的开销和失败处理。
 * ========================================================================= */

#define UPLOAD_QUEUE_LEN        8
#define UPLOAD_CHUNK_MAX_BYTES  196   /* 200B 帧 - 4B 头 (op+seq+len) */

typedef struct {
    upload_op_t op;
    union {
        struct {
            char     name[DYNAPP_UPLOAD_NAME_MAX + 1];
            uint32_t total_len;
            uint32_t crc32;
        } start;
        struct {
            uint32_t offset;
            uint16_t len;
            uint8_t  data[UPLOAD_CHUNK_MAX_BYTES];
        } chunk;
        struct {
            char name[DYNAPP_UPLOAD_NAME_MAX + 1];
        } del;
        /* END / LIST 无 payload */
    };
} upload_item_t;

static QueueHandle_t       s_queue   = NULL;
static upload_status_cb_t  s_cb      = NULL;
static TaskHandle_t        s_task    = NULL;

/* ============================================================================
 * §2. 当前会话状态机
 *
 * IDLE：没有进行中的上传
 * RECEIVING：开了 .tmp，等 chunk / end
 *
 * 重复 START：abort 旧的，开新的（容错策略，PC 网络抖动时友好）
 * ========================================================================= */

typedef enum { SESS_IDLE, SESS_RECEIVING } session_state_t;

static struct {
    session_state_t state;
    char            name[DYNAPP_UPLOAD_NAME_MAX + 1];
    uint32_t        total_len;
    uint32_t        crc_expected;
    uint32_t        crc_running;     /* 边收边算 */
    uint32_t        next_offset;     /* 下一个期望的 offset（防丢帧） */
    dynapp_script_writer_t *writer;
} s_sess = { .state = SESS_IDLE };

/* ============================================================================
 * §3. submit_* 提交函数
 *
 * 全部非阻塞：xQueueSend timeout=0。队列满直接返 false，service 层决定
 * 是 notify BUSY 给 PC 还是直接丢。
 * ========================================================================= */

bool dynapp_upload_submit_start(const char *name, uint32_t total_len, uint32_t crc32)
{
    if (!s_queue || !name || !*name) return false;

    upload_item_t it = { .op = UPL_OP_START };
    strncpy(it.start.name, name, sizeof(it.start.name) - 1);
    it.start.total_len = total_len;
    it.start.crc32     = crc32;

    return xQueueSend(s_queue, &it, 0) == pdTRUE;
}

bool dynapp_upload_submit_chunk(uint32_t offset, const uint8_t *data, uint16_t len)
{
    if (!s_queue || !data) return false;
    if (len == 0 || len > UPLOAD_CHUNK_MAX_BYTES) return false;

    upload_item_t it = { .op = UPL_OP_CHUNK };
    it.chunk.offset = offset;
    it.chunk.len    = len;
    memcpy(it.chunk.data, data, len);

    return xQueueSend(s_queue, &it, 0) == pdTRUE;
}

bool dynapp_upload_submit_end(void)
{
    if (!s_queue) return false;
    upload_item_t it = { .op = UPL_OP_END };
    return xQueueSend(s_queue, &it, 0) == pdTRUE;
}

bool dynapp_upload_submit_delete(const char *name)
{
    if (!s_queue || !name || !*name) return false;
    upload_item_t it = { .op = UPL_OP_DELETE };
    strncpy(it.del.name, name, sizeof(it.del.name) - 1);
    return xQueueSend(s_queue, &it, 0) == pdTRUE;
}

bool dynapp_upload_submit_list(void)
{
    if (!s_queue) return false;
    upload_item_t it = { .op = UPL_OP_LIST };
    return xQueueSend(s_queue, &it, 0) == pdTRUE;
}

/* ============================================================================
 * §4. dispatch_*（consumer task 内部）
 *
 * 这里的函数都跑在 manager 自己的 consumer task 线程：可以慢慢调 FS、
 * 可以阻塞，但只有一个 task 跑这些 → 不需要互斥。
 * ========================================================================= */

static void notify(upload_op_t op, upload_result_t r, const char *name, uint32_t extra,
                   const uint8_t *list_buf, size_t list_len)
{
    if (s_cb) s_cb(op, r, name, extra, list_buf, list_len);
}

static void session_reset(void)
{
    if (s_sess.writer) dynapp_script_writer_abort(s_sess.writer);
    memset(&s_sess, 0, sizeof(s_sess));
    s_sess.state = SESS_IDLE;
}

/* ---- CRC32 (IEEE 802.3, table-less, slow but仅 ≤64KB) ---- */
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(int32_t)(crc & 1));
        }
    }
    return ~crc;
}

static void dispatch_start(const upload_item_t *it)
{
    if (s_sess.state != SESS_IDLE) {
        ESP_LOGW(TAG, "START while session active, aborting previous %s", s_sess.name);
        session_reset();
    }

    dynapp_script_writer_t *w = dynapp_script_store_open_writer(it->start.name);
    if (!w) {
        notify(UPL_OP_START, UPL_RESULT_FS_ERROR, it->start.name, 0, NULL, 0);
        return;
    }

    s_sess.state         = SESS_RECEIVING;
    strncpy(s_sess.name, it->start.name, sizeof(s_sess.name) - 1);
    s_sess.total_len     = it->start.total_len;
    s_sess.crc_expected  = it->start.crc32;
    s_sess.crc_running   = 0;
    s_sess.next_offset   = 0;
    s_sess.writer        = w;

    ESP_LOGI(TAG, "START %s (%u B, crc=0x%08x)",
             s_sess.name, (unsigned)s_sess.total_len, (unsigned)s_sess.crc_expected);
    notify(UPL_OP_START, UPL_RESULT_OK, s_sess.name, 0, NULL, 0);
}

static void dispatch_chunk(const upload_item_t *it)
{
    if (s_sess.state != SESS_RECEIVING) {
        notify(UPL_OP_CHUNK, UPL_RESULT_NO_SESSION, NULL, 0, NULL, 0);
        return;
    }
    if (it->chunk.offset != s_sess.next_offset) {
        ESP_LOGW(TAG, "CHUNK offset mismatch: got %u expected %u (drop session)",
                 (unsigned)it->chunk.offset, (unsigned)s_sess.next_offset);
        notify(UPL_OP_CHUNK, UPL_RESULT_BAD_FRAME, s_sess.name, s_sess.next_offset, NULL, 0);
        session_reset();
        return;
    }

    esp_err_t err = dynapp_script_writer_append(s_sess.writer,
                                                it->chunk.data, it->chunk.len);
    if (err != ESP_OK) {
        upload_result_t r = (err == ESP_ERR_INVALID_SIZE) ? UPL_RESULT_TOO_LARGE
                                                          : UPL_RESULT_FS_ERROR;
        notify(UPL_OP_CHUNK, r, s_sess.name, 0, NULL, 0);
        session_reset();
        return;
    }

    s_sess.crc_running = crc32_update(s_sess.crc_running, it->chunk.data, it->chunk.len);
    s_sess.next_offset += it->chunk.len;

    notify(UPL_OP_CHUNK, UPL_RESULT_OK, s_sess.name, s_sess.next_offset, NULL, 0);
}

static void dispatch_end(void)
{
    if (s_sess.state != SESS_RECEIVING) {
        notify(UPL_OP_END, UPL_RESULT_NO_SESSION, NULL, 0, NULL, 0);
        return;
    }

    /* 长度核对 */
    if (s_sess.next_offset != s_sess.total_len) {
        ESP_LOGW(TAG, "END length mismatch: got %u expected %u",
                 (unsigned)s_sess.next_offset, (unsigned)s_sess.total_len);
        notify(UPL_OP_END, UPL_RESULT_BAD_FRAME, s_sess.name, s_sess.next_offset, NULL, 0);
        session_reset();
        return;
    }

    /* CRC 核对 */
    if (s_sess.crc_running != s_sess.crc_expected) {
        ESP_LOGW(TAG, "END crc mismatch: got 0x%08x expected 0x%08x",
                 (unsigned)s_sess.crc_running, (unsigned)s_sess.crc_expected);
        notify(UPL_OP_END, UPL_RESULT_CRC_MISMATCH, s_sess.name, 0, NULL, 0);
        session_reset();
        return;
    }

    /* commit 后 writer 失效（无论成败都已被 free） */
    char name_copy[DYNAPP_UPLOAD_NAME_MAX + 1];
    strncpy(name_copy, s_sess.name, sizeof(name_copy));
    dynapp_script_writer_t *w = s_sess.writer;
    s_sess.writer = NULL;
    esp_err_t err = dynapp_script_writer_commit(w);
    s_sess.state = SESS_IDLE;

    if (err != ESP_OK) {
        notify(UPL_OP_END, UPL_RESULT_FS_ERROR, name_copy, 0, NULL, 0);
        return;
    }
    ESP_LOGI(TAG, "END %s OK", name_copy);
    notify(UPL_OP_END, UPL_RESULT_OK, name_copy, 0, NULL, 0);
}

static void dispatch_delete(const upload_item_t *it)
{
    /* 删除时不要破坏正在进行的上传 */
    if (s_sess.state == SESS_RECEIVING && strcmp(s_sess.name, it->del.name) == 0) {
        notify(UPL_OP_DELETE, UPL_RESULT_BUSY, it->del.name, 0, NULL, 0);
        return;
    }
    esp_err_t err = dynapp_script_store_delete(it->del.name);
    upload_result_t r = (err == ESP_OK)               ? UPL_RESULT_OK
                      : (err == ESP_ERR_NOT_FOUND)    ? UPL_RESULT_OK   /* 幂等 */
                      :                                 UPL_RESULT_FS_ERROR;
    notify(UPL_OP_DELETE, r, it->del.name, 0, NULL, 0);
}

static void dispatch_list(void)
{
    /* 把所有 FS 上的名字 NUL-分隔打包成一段。最大 8 条 × 16B ≤ 130B，
     * 单帧 notify 200B 装得下。 */
    char names[8][DYNAPP_SCRIPT_STORE_MAX_NAME + 1];
    int  n = dynapp_script_store_list(names, 8);

    uint8_t buf[8 * (DYNAPP_SCRIPT_STORE_MAX_NAME + 1)];
    size_t  off = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(names[i]) + 1;  /* 含 \0 */
        if (off + len > sizeof(buf)) break;
        memcpy(buf + off, names[i], len);
        off += len;
    }
    notify(UPL_OP_LIST, UPL_RESULT_OK, NULL, (uint32_t)n, buf, off);
}

/* ============================================================================
 * §6. consumer task 主循环 + init
 * ========================================================================= */

static void consumer_task(void *arg)
{
    (void)arg;
    upload_item_t it;

    ESP_LOGI(TAG, "consumer task running");

    for (;;) {
        if (xQueueReceive(s_queue, &it, portMAX_DELAY) != pdTRUE) continue;

        switch (it.op) {
        case UPL_OP_START:  dispatch_start(&it); break;
        case UPL_OP_CHUNK:  dispatch_chunk(&it); break;
        case UPL_OP_END:    dispatch_end();      break;
        case UPL_OP_DELETE: dispatch_delete(&it); break;
        case UPL_OP_LIST:   dispatch_list();     break;
        default:
            ESP_LOGW(TAG, "unknown op 0x%02x", it.op);
            break;
        }
    }
}

esp_err_t dynapp_upload_manager_init(upload_status_cb_t status_cb)
{
    if (s_task) return ESP_OK;  /* idempotent */

    s_cb = status_cb;
    s_queue = xQueueCreate(UPLOAD_QUEUE_LEN, sizeof(upload_item_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    /* prio=2: 比 LVGL UI（通常 4）低，比 idle(0) 高
     * stack=4KB: dispatch_chunk 走 fwrite 几百字节栈即可
     * core=tskNO_AFFINITY: 让调度器自由放置 */
    BaseType_t ok = xTaskCreatePinnedToCore(consumer_task, "dyn_upl",
                                            4096, NULL, 2, &s_task,
                                            tskNO_AFFINITY);
    if (ok != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}
