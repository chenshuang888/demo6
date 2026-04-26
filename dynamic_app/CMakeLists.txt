idf_component_register(
    SRCS
        # === JS 侧三层：控制 / 引擎 / API ===
        "dynamic_app.c"               # 控制层：生命周期 + script_task 主循环
        "dynamic_app_runtime.c"       # 引擎层：JSContext 创建/销毁/eval
        "dynamic_app_natives.c"       # API 层：sys.* 所有 native fn
        "dynamic_app_registry.c"      # 注册表：app 名 → 脚本 buffer

        # === UI 侧三层：调度 / 注册表 / 样式 ===
        "dynamic_app_ui.c"            # 调度层：drain 分发 + 入队 API
        "dynamic_app_ui_registry.c"   # 注册表层：UTF-8 + id↔obj 映射
        "dynamic_app_ui_styles.c"     # 样式层：apply_style 11-key 分发
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        # 把脚本打包进固件，每个 app 一个独立 buffer
        "scripts/alarm.js"
        "scripts/calc.js"
        "scripts/timer.js"
        "scripts/game2048.js"
    REQUIRES
        freertos
        esp_timer
        lvgl
        esp-mquickjs
        storage
)
