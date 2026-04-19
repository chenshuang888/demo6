#pragma once

#include <sys/time.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化设置存储；从 NVS 加载背光等设置到内存缓存
 *
 * 必须在 persist_init() 之后调用。
 * 若 NVS 中无记录，使用默认值。
 */
esp_err_t settings_store_init(void);

/* ------------------------------------------------------------------
 * 背光
 *
 * settings_store 仅负责持久化 + 缓存；硬件生效由调用方
 * （lcd_panel_set_backlight）自行处理，避免 services -> drivers 的反向依赖。
 * ------------------------------------------------------------------ */

uint8_t settings_store_get_backlight(void);

/** 更新背光值到内存缓存并立即持久化到 NVS。
 *  不直接改变屏幕亮度 —— 调用方还需显式调 lcd_panel_set_backlight(duty)。 */
esp_err_t settings_store_set_backlight(uint8_t duty);

/* ------------------------------------------------------------------
 * 最近系统时间
 *
 * 启动时 load_last_time 读取 NVS 里保存的 time_t；上层把结果传给
 * settimeofday()。若 NVS 没有，返回 ESP_ERR_NVS_NOT_FOUND，上层
 * 回退到硬编码默认时间。
 *
 * 启动后 UI 任务周期调用 tick_save_time，内部按 5 分钟间隔写一次。
 * ------------------------------------------------------------------ */

esp_err_t settings_store_load_last_time(struct timeval *out);

void settings_store_tick_save_time(void);

#ifdef __cplusplus
}
#endif
