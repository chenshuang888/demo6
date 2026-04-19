#include "settings_store.h"
#include "persist.h"

#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "settings_store";

/* ------------------------------------------------------------------
 * NVS 布局
 * ------------------------------------------------------------------ */

#define NS_SETTINGS       "settings"
#define KEY_BACKLIGHT     "bl"
#define KEY_LAST_TIME_SEC "last_ts"

/* ------------------------------------------------------------------
 * 默认值
 * ------------------------------------------------------------------ */

#define DEFAULT_BACKLIGHT 128  /* 与 lcd_panel 的默认亮度一致（约 50%） */

/* ------------------------------------------------------------------
 * 时间保存节奏
 * ------------------------------------------------------------------ */

#define TIME_SAVE_INTERVAL_US (5LL * 60 * 1000000)  /* 5 分钟 */

/* ------------------------------------------------------------------
 * 内存缓存
 * ------------------------------------------------------------------ */

static uint8_t  s_backlight = DEFAULT_BACKLIGHT;
static int64_t  s_last_time_save_us = 0;  /* esp_timer_get_time() 时刻 */

/* ------------------------------------------------------------------
 * init
 * ------------------------------------------------------------------ */

esp_err_t settings_store_init(void)
{
    uint8_t bl = DEFAULT_BACKLIGHT;
    esp_err_t err = persist_get_u8(NS_SETTINGS, KEY_BACKLIGHT, &bl);
    if (err == ESP_OK) {
        s_backlight = bl;
        ESP_LOGI(TAG, "loaded backlight=%u", bl);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "backlight not stored, using default=%u", s_backlight);
    } else {
        ESP_LOGW(TAG, "failed to load backlight: 0x%x (using default)", err);
    }

    s_last_time_save_us = esp_timer_get_time();  /* 避免启动立刻就写 */
    return ESP_OK;
}

/* ------------------------------------------------------------------
 * 背光
 * ------------------------------------------------------------------ */

uint8_t settings_store_get_backlight(void)
{
    return s_backlight;
}

esp_err_t settings_store_set_backlight(uint8_t duty)
{
    if (duty == s_backlight) {
        return ESP_OK;  /* 无变化不写 NVS */
    }
    s_backlight = duty;
    esp_err_t err = persist_set_u8(NS_SETTINGS, KEY_BACKLIGHT, duty);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "persist backlight failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "backlight saved=%u", duty);
    }
    return err;
}

/* ------------------------------------------------------------------
 * 最近系统时间
 * ------------------------------------------------------------------ */

esp_err_t settings_store_load_last_time(struct timeval *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    int64_t ts = 0;
    esp_err_t err = persist_get_i64(NS_SETTINGS, KEY_LAST_TIME_SEC, &ts);
    if (err != ESP_OK) {
        return err;
    }
    if (ts <= 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    out->tv_sec = (time_t)ts;
    out->tv_usec = 0;
    ESP_LOGI(TAG, "loaded last_ts=%lld", (long long)ts);
    return ESP_OK;
}

void settings_store_tick_save_time(void)
{
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_time_save_us < TIME_SAVE_INTERVAL_US) {
        return;
    }
    s_last_time_save_us = now_us;

    time_t now = time(NULL);
    if (now <= 0) {
        return;
    }

    esp_err_t err = persist_set_i64(NS_SETTINGS, KEY_LAST_TIME_SEC, (int64_t)now);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "last_ts periodically saved=%lld", (long long)now);
    } else {
        ESP_LOGW(TAG, "save last_ts failed: 0x%x", err);
    }
}
