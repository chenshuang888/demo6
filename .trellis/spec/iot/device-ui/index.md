# 设备 UI（LVGL / 屏幕 / 触摸）

> 本目录聚焦：屏幕与触摸接入、LVGL port 分层、坐标系/旋转、字体策略、内存与性能。

## 复用安全（必读）

- 复用任何条目前先做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（禁止不做适配就照搬实现细节）

## 文档

- `./lvgl-port-layering-playbook.md`：把显示/触摸驱动拆成"底层硬件层 + LVGL 对接层"，避免过度设计。
- `./lvgl-port-layering-decision-record.md`：为什么只拆两层（或三层），边界在哪里，如何回退。
- `./lvgl-page-router-minimal-contract.md`：LVGL 多页面路由最小契约（create/destroy/update + `lv_scr_load` 切屏，避免回调爆炸）。
- `./lvgl-screen-leak-no-auto-del-pitfall.md`：`lv_screen_load()` 不带 `auto_del` 且忘记 delete 旧 screen 的慢性泄漏（短期不崩、长期必崩）。
- `./lvgl-layout-constants-and-resolution-independence-playbook.md`：消灭魔法数字（320/240 等），用 `lv_pct(100)`/`lv_display_get_*_res` 做分辨率无关布局与触摸映射。
- `./lvgl-240x320-time-menu-page-design-playbook.md`：240×320 竖屏时间页/菜单页的耐看布局范式（卡片层级 + 强调色 + 背光/蓝牙状态条目）。
- `./lvgl-fullscreen-double-buffer-psram-render-mode-full-playbook.md`：LVGL 全屏双缓冲（PSRAM）+ full render 的落地账本（先算 buffer，再决定是否值得；**本项目选 40 行部分刷新**，参考 `./lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`）。
- `./st7789-ft6336u-lvgl9-bringup-playbook.md`：ESP32-S3 + ST7789（SPI）+ FT6336U（I2C）+ LVGL 9 的最小点屏/触摸 bring-up 清单（含 pinmap 与方向校准）。**注意**：本项目触摸 IC 为 **FT5x06**，I2C 地址/初始化序列差异见条目首部"本项目适配"。
- `./lvgl-screen-top-layer-statusbar-and-anim-safety-playbook.md`：LVGL 9 的 screen 切换/动画事件顺序 + `top_layer` 状态栏常驻 + 避免 `auto_del`/动画中途切屏的稳定性边界。
- `./display-touch-rotation-debug-playbook.md`：显示/触摸方向排查（"都反了"优先排全局旋转，不要乱改触摸回调）。
- `./display-tx-done-callback-contract.md`：显示底层用 `tx_done_cb + user_ctx` 解耦 LVGL（flush 完成通知）。
- `./lvgl-flush-ready-rtos-bridge-playbook.md`：flush 完成通知的 RTOS 桥接写法（完成回调直达 vs 通知 UI 任务）。
- `./lcd-panel-callback-registration-refactor-playbook.md`：把 `lcd_panel_init(cb, user_ctx)` 改为"init + register"的最小改动。
- `./assets-management-playbook.md`：资源管理（fonts/images）与解耦（目录结构 + Resource ID 映射）。
- `./font-manager-contract.md`：`font_manager` 职责边界与稳定接口（UI 只依赖 role，不依赖路径/格式）。**本项目目前**：字体在 `app/app_fonts.c` 直接初始化，未独立拆 font_manager；作将来拆分时参考。
- `./font-charset-and-generation-pipeline-playbook.md`：把"3500 常用字"做成仓库资产，并固化字体生成/验证流程（本项目走 Tiny TTF + 源码扫描，见 `tools/gen_font_subset.py`）。
- `./font-strategy-playbook.md`：小屏字体策略（是否中文、子集化、Tiny TTF vs 位图字体、落地步骤）。
- `./font-architecture-cn-en-icons-and-mixed-label-playbook.md`：中文/英文数字/图标字体分离，以及"图标+文字"标题混排（双 label vs fallback）的落地方案。
- `./bitmap-font-vs-tiny-ttf-decision-record.md`：位图字体 vs Tiny TTF 的路线选择：资源账本、切换开关、glyph cache 直觉校准。
- `./tiny-ttf-subset-playbook.md`：Tiny TTF 子集字体的工程化落地细节（EMBED_FILES、cache、分区调整、验收与回退）。
- `./lvgl-style-lifecycle-pitfall.md`：样式对象生命周期坑：第 3 次进页面卡死/崩溃的根因与修复方式。
- `./page-framework-contract.md`：页面框架契约（PageDescriptor_t 生命周期、职责、命名约定）。
- `./page-framework-new-page-playbook.md`：新增页面的落地步骤（按模板写，不乱写）。
- `./lvgl9-indev-create-timer-read-cb-playbook.md`：LVGL 9 输入设备接入要点：`lv_indev_create` 自带 timer、`read_cb` 调用链、`lv_indev_set_display` 必要性与排障路径。

### 本项目专属（demo6）

- `./tiny-ttf-plus-fontawesome-fallback-playbook.md`：Tiny TTF 中文字体 + Montserrat FontAwesome fallback 的完整落地（EMBED_FILES + 三字号 + glyph cache 走 PSRAM + fallback 链兜底 `LV_SYMBOL_*`）。
- `./lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`：为什么选 40 行部分刷新而非全屏双缓冲 PSRAM（38KB 内部 RAM vs 300KB PSRAM 的权衡 + 切换边界）。
