# 设备 UI（LVGL 9.5 / ST7789 / FT5x06）

> 本目录聚焦：**本项目**的 LVGL 9.5 落地要点（240×320 + 40 行部分刷新 + Tiny TTF 字体链）。
> 通用 LVGL / 字体 / 显示/触摸 bring-up 经验已移到 `.trellis/spec/_general_library/device-ui/`。

## 复用安全（必读）

- 复用前做 Fit Check：`../guides/spec-reuse-safety-playbook.md`

## 显示与渲染

- `./lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`：**本项目核心 DR** —— 为什么选 40 行部分刷新而非全屏双缓冲 PSRAM
- `./lvgl-port-layering-decision-record.md`：**port 分层决策依据** —— 为什么只拆两层（硬件层 + LVGL 对接层），边界在哪
- `./lvgl-port-layering-playbook.md`：`drivers/lcd_panel` + `drivers/lvgl_port` 两层拆分落地方案

## 字体（Tiny TTF）

- `./tiny-ttf-plus-fontawesome-fallback-playbook.md`：**字体核心** —— Tiny TTF 中文 + Montserrat FontAwesome fallback 三字号链，EMBED_FILES + PSRAM glyph cache
- `./tiny-ttf-subset-playbook.md`：Tiny TTF 中文子集生成（`tools/gen_font_subset.py` + 源码扫描）
- `./font-strategy-playbook.md`：小屏字体策略总纲（是否中文/子集化/Tiny TTF vs 位图）

## 页面框架

- `./lvgl-page-router-minimal-contract.md`：本项目 `framework/page_router` 契约（create/destroy/update）—— 每次加新页必查
