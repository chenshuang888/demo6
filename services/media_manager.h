#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_TITLE_MAX  48
#define MEDIA_ARTIST_MAX 32

/* 与 PC 端 struct.pack("<BBhhHI48s32s", ...) 严格对齐（92 字节）
 * PC 侧打包顺序与字段布局必须完全一致，否则 access_cb 会因长度校验拒收。 */
typedef struct {
    uint8_t  playing;                       // 0=paused, 1=playing
    uint8_t  _reserved;
    int16_t  position_sec;                  // 当前进度秒；-1 表示未知
    int16_t  duration_sec;                  // 总时长秒；-1 表示未知 / 直播
    uint16_t _pad;                          // 4 字节边界对齐
    uint32_t sample_ts;                     // PC 采样时刻 unix sec（调试/未来对时）
    char     title[MEDIA_TITLE_MAX];        // UTF-8，末尾必须 \0
    char     artist[MEDIA_ARTIST_MAX];      // UTF-8，末尾必须 \0
} __attribute__((packed)) media_payload_t;

/**
 * @brief 初始化媒体管理器（创建队列）
 *        必须在 BLE 回调可能触发之前调用。
 */
esp_err_t media_manager_init(void);

/**
 * @brief BLE host 线程调用，投递一条新的媒体状态到 UI 线程。
 *
 * 队列满时丢弃最旧的一条（媒体数据越新越好，不保留历史）。
 */
esp_err_t media_manager_push(const media_payload_t *payload);

/**
 * @brief UI 线程每帧调用：消费队列、刷新内部 latest 快照、更新 version 和收到时刻。
 */
void media_manager_process_pending(void);

/**
 * @brief 读取最新快照（仅 UI 线程，无锁）
 * @return NULL 表示尚未收到任何数据
 */
const media_payload_t *media_manager_get_latest(void);

/**
 * @brief 是否已至少收到一条数据
 */
bool media_manager_has_data(void);

/**
 * @brief 基于 esp_timer 插值得到"此刻"的播放位置（秒）。
 *
 * - 未收到数据 或 position_sec < 0 → 返回 -1
 * - playing=0 → 直接返回 latest.position_sec
 * - playing=1 → latest.position_sec + (now - received_at) / 1e6，
 *               并 clamp 到 [0, duration_sec]
 *
 * UI 进度条每帧读这个函数即可丝滑前进，无需 PC 高频推送。
 */
int16_t media_manager_get_position_now(void);

/**
 * @brief 版本号：process_pending 消费到新数据时 +1。
 *
 * UI 用它决定是否要重建 title/artist/总时长等"静态"视觉元素，
 * 避免每帧都重新 set_text（既省 CPU 又防闪烁）。
 */
uint32_t media_manager_version(void);

#ifdef __cplusplus
}
#endif
