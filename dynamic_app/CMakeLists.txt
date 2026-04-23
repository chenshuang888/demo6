idf_component_register(
    SRCS
        "dynamic_app.c"
        "dynamic_app_ui.c"
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        # 把脚本打包进固件，生成 _binary_app_js_start/_binary_app_js_end 符号供 C 侧直接 eval
        "scripts/app.js"
    REQUIRES
        freertos
        esp_timer
        lvgl
        esp-mquickjs
)
