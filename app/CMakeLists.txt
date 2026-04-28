idf_component_register(
    SRCS
        "app_main.c"
        "app_fonts.c"
        "pages/page_time.c"
        "pages/page_time_adjust.c"
        "pages/page_menu.c"
        "pages/page_menu_modal.c"
        "pages/page_about.c"
        "pages/page_weather.c"
        "pages/page_notifications.c"
        "pages/page_music.c"
        "pages/page_system.c"
        # Dynamic App（MicroQuickJS MVP）演示页
        "pages/page_dynamic_app.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "fonts/srhs_sc_subset.ttf"
    REQUIRES
        framework
        drivers
        services
        storage
        # dynamic_app 组件：脚本运行时 + Script->UI 队列桥接
        dynamic_app
        lvgl
)
