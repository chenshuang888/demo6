#pragma once

#include "lvgl.h"
#include "esp_err.h"

/**
 * 页面生命周期回调
 */
typedef struct {
    /**
     * 创建页面（必须实现）
     * @return 页面的根对象（必须是独立的 screen，不能是 lv_scr_act()）
     */
    lv_obj_t *(*create)(void);

    /**
     * 销毁页面（可选）
     * 如果不实现，框架会自动调用 lv_obj_del() 删除页面对象
     */
    void (*destroy)(void);

    /**
     * 更新页面（可选）
     * 在 UI 线程中周期性调用，用于更新动态内容（如时间显示）
     */
    void (*update)(void);
} page_callbacks_t;

/**
 * 页面ID枚举
 */
typedef enum {
    PAGE_TIME,      // 时间调节页面
    PAGE_MENU,      // 菜单页面
    PAGE_ABOUT,     // 关于页面
    PAGE_MAX        // 页面总数（自动计算）
} page_id_t;

/**
 * 初始化页面路由器
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t page_router_init(void);

/**
 * 注册页面
 * @param id 页面ID
 * @param callbacks 页面回调函数（create 不能为 NULL）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t page_router_register(page_id_t id, const page_callbacks_t *callbacks);

/**
 * 切换到指定页面
 * @param id 目标页面ID
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t page_router_switch(page_id_t id);

/**
 * 获取当前页面ID
 * @return 当前页面ID
 */
page_id_t page_router_get_current(void);

/**
 * 更新当前页面
 * 在 UI 线程的主循环中调用，会调用当前页面的 update 回调
 */
void page_router_update(void);
