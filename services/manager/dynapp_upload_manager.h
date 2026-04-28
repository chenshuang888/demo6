#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dynapp_upload_manager —— 动态 App 上传/运维 的"消息中介 + 执行线程"
 *
 * 角色：
 *   - 持有上传协议状态机（IDLE / RECEIVING）+ 入队 queue
 *   - 自带一个 consumer task，从队列取消息、做 CRC 校验、调 dynapp_script_store
 *     落盘；做完通过 status_cb 通知调用方
 *   - 上层（services/dynapp_upload_service）只看到"提交意图"接口，
 *     不接触队列细节、不接触 task
 *
 * 跟其它 manager 的差异（设计取舍）：
 *   notify/time/media manager 跑在 UI 线程（工作量小，蹭 UI 即可）；
 *   upload manager 工作量大（fwrite 单次 100-500ms），不能阻塞 UI 也不能阻塞
 *   BLE host task。所以 init 时建一个独占 task 跑消费循环。
 *
 * 不做的事：
 *   - 完全不依赖 BLE / NimBLE 头文件（service 层负责拆帧后调 submit_*）
 *   - 不解析帧字节
 *
 * 线程模型：
 *   submit_*  —— 任意线程可调（实际是 NimBLE host task），仅 xQueueSend(timeout=0)
 *   status_cb —— 永远在 manager 自己的 consumer task 线程触发；调用方实现里禁阻塞
 */

/* ---- 类型 ---- */

#define DYNAPP_UPLOAD_NAME_MAX  15  /* 与 dynapp_script_store 对齐 */

typedef enum {
    UPL_RESULT_OK              = 0,
    UPL_RESULT_BAD_FRAME       = 1,  /* 协议本身错（offset 跳号、name 非法等） */
    UPL_RESULT_NO_SESSION      = 2,  /* 没 START 就 CHUNK/END */
    UPL_RESULT_TOO_LARGE       = 3,  /* 累计超 64KB */
    UPL_RESULT_CRC_MISMATCH    = 4,
    UPL_RESULT_FS_ERROR        = 5,
    UPL_RESULT_BUSY            = 6,  /* 队列满或并发冲突 */
} upload_result_t;

typedef enum {
    UPL_OP_START   = 0x01,
    UPL_OP_CHUNK   = 0x02,
    UPL_OP_END     = 0x03,
    UPL_OP_DELETE  = 0x10,
    UPL_OP_LIST    = 0x11,
} upload_op_t;

/**
 * manager → 上层的状态回调。
 * - op:        触发本次状态的 op
 * - result:    UPL_RESULT_*
 * - name:      START/END/DELETE 时为 app 名，CHUNK/LIST 时可能 NULL
 * - extra:     CHUNK 成功时为 next_expected_offset，LIST 时为剩余条数提示，否则 0
 * - list_buf:  仅 LIST OK 时非 NULL，指向 NUL 分隔的 name 列表（若干条）
 * - list_len:  list_buf 字节数（含末尾 NUL）
 *
 * 实现要求：不阻塞、不写 NVS / FS、可调 NimBLE notify（线程安全）。
 */
typedef void (*upload_status_cb_t)(upload_op_t op,
                                    upload_result_t result,
                                    const char *name,
                                    uint32_t extra,
                                    const uint8_t *list_buf,
                                    size_t list_len);

/* ---- 生命周期 ---- */

esp_err_t dynapp_upload_manager_init(upload_status_cb_t status_cb);

/**
 * 追加注册一个 status 回调（多消费者）。
 *
 * - 容量上限 DYNAPP_UPLOAD_MAX_STATUS_CBS（含 init 时注册的那一个）
 * - 同一个函数指针重复注册：no-op，返回 ESP_OK（idempotent）
 * - 满了 / cb==NULL：返回 ESP_ERR_NO_MEM / ESP_ERR_INVALID_ARG
 * - 注册的 cb 与 init 时的 cb 共享线程契约（manager consumer task，禁阻塞 / 禁碰 LVGL）
 *
 * 典型用例：菜单页在 create 时注册一个观察者（仅 set 一个 dirty flag），
 * 在 UI 线程的 update tick 里再做真正的列表重建。
 */
#define DYNAPP_UPLOAD_MAX_STATUS_CBS  4
esp_err_t dynapp_upload_manager_register_status_cb(upload_status_cb_t cb);

/**
 * 注册"运行中检测"hook（可选）。
 *
 * 用途：dispatch_delete 在删脚本前调一次，若返回 true 则拒删并 notify(BUSY)。
 * 解耦：manager 不知道 dynamic_app / page_router 这些上层概念，由上层注册。
 *
 * 没注册（默认）→ 不做拦截，保持原有行为（删什么走什么）。
 * 同一函数指针重复注册 → 覆盖。
 *
 * cb 实现要求：必须线程安全（manager consumer task 上下文调用），
 * 通常只读取一个原子状态字段。
 */
typedef bool (*upload_app_running_cb_t)(const char *app_name);
void dynapp_upload_manager_set_running_check(upload_app_running_cb_t cb);

/* ---- 提交接口（service 用，全部非阻塞）---- */

bool dynapp_upload_submit_start (const char *name, uint32_t total_len, uint32_t crc32);
bool dynapp_upload_submit_chunk (uint32_t offset, const uint8_t *data, uint16_t len);
bool dynapp_upload_submit_end   (void);
bool dynapp_upload_submit_delete(const char *name);
bool dynapp_upload_submit_list  (void);

#ifdef __cplusplus
}
#endif
