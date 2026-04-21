# Decision Record：LVGL 渲染缓冲选 40 行部分刷新（而非全屏双缓冲 PSRAM）

## 背景

项目是 ESP32-S3 N16R8 + ST7789 240×320 屏，跑 LVGL 9.5。渲染缓冲候选：

- 40 行部分刷新（partial render mode），双 buffer，内部 RAM
- 全屏单缓冲，PSRAM
- 全屏双缓冲，PSRAM
- 全屏双缓冲，内部 RAM（不可行，内部 RAM 不够）

同时项目还要跑：
- NimBLE（启用 `MEM_ALLOC_MODE_EXTERNAL` 把堆放 PSRAM）
- Tiny TTF glyph cache（自动用 `heap_caps` 走 PSRAM）
- 嵌入 3.4MB 中文字体子集到 factory 分区

目标：在不压榨资源的前提下满足"240×320 卡片式 UI + 偶尔动画 + 偶尔滚动通知列表"的刷新体验。

## 选项对比

### 方案 A：40 行部分刷新 + 双 buffer + 内部 RAM + DMA（本项目选）

- **buffer 大小**：`240 × 40 × 2 bytes = 19200 bytes × 2 = 38.4KB`
- **驻留位置**：内部 RAM（`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`）
- **渲染模式**：`LV_DISPLAY_RENDER_MODE_PARTIAL`

优点：
- DMA 零拷贝直接走内部 RAM + SPI，吞吐高
- 不占 PSRAM，把 PSRAM 让给 NimBLE + glyph cache + 未来图像资源
- 上电即跑，PSRAM 初始化失败也能降级
- buffer 大小恒定，不随页面复杂度变化

缺点：
- 复杂动画（整屏过渡、滑动切换）会多次 flush，每次 40 行
- `tearing` 风险在高速变化场景存在（但卡片式 UI 不触发）
- 每帧必须等 SPI flush 完才能画下一块

风险：
- 滚动长列表（`page_notifications`）一帧画超过 40 行会触发多段 flush，可能掉帧
- 某些全屏动画用 full mode 会更顺滑，但本项目不做全屏动画

### 方案 B：全屏双缓冲 + PSRAM + `LV_DISPLAY_RENDER_MODE_FULL`

- **buffer 大小**：`240 × 320 × 2 bytes = 153.6KB × 2 = 307.2KB`
- **驻留位置**：PSRAM（`MALLOC_CAP_SPIRAM`）
- **渲染模式**：`LV_DISPLAY_RENDER_MODE_FULL`

优点：
- 整屏一次 flush，动画顺滑
- UI 线程先画后台 buffer，切换时只交换指针，帧率上限高

缺点：
- 吃掉 300KB+ PSRAM，和 NimBLE + glyph cache + BLE host 堆竞争
- PSRAM 访问延迟 + ESP32-S3 cache miss，每像素写比内部 RAM 慢 2~5x
- SPI DMA 从 PSRAM 读也有额外 cache invalidate 开销
- 每帧都重绘 240×320 所有像素，CPU 占用高于部分刷新
- PSRAM 和 Flash 共享 MSPI，SPI flush 阶段轻微抢 bus（ESP32-S3 特有）

风险：
- 内存压力上升，GATT attr 多时 NimBLE 可能 OOM
- `LV_COLOR_DEPTH_16_SWAP` 如果配置不匹配，PSRAM 读写方向错乱很难定位

### 方案 C：全屏单缓冲 + PSRAM

- 介于 A 和 B 之间，资源占用 ≈ 150KB PSRAM
- 无 double-buffer 优势，tearing 风险更明显
- 不常用，跳过

## 结论

**选择方案 A：40 行部分刷新 + 双 buffer + 内部 RAM + DMA。**

- **选择理由**：本项目 UI 是卡片式菜单 + 静态时间页 + 偶尔动画，40 行部分刷新够用；把 PSRAM 让给 NimBLE + glyph cache 的综合收益更大
- **证据**：
  - `drivers/lvgl_port.c:79-83` 用 `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` 分配双 buffer
  - `drivers/lvgl_port.c:83` 渲染模式 `LV_DISPLAY_RENDER_MODE_PARTIAL`
  - `drivers/board_config.h:34` 定义 `BOARD_LVGL_BUFFER_LINES 40`
- **边界（什么情况换方案）**：
  1. 加全屏过渡动画（`lv_scr_load_anim` 带 fade/move），40 行明显掉帧
  2. 引入大块图像（JPEG 背景 / 视频帧），切 B 的收益超过 PSRAM 占用成本
  3. 引入"每帧都要重绘整屏"的页面（如实时图表 / 示波器）
- **回退**：切 B 只需改两处——`lvgl_port.c` 里 `buf_size = H*V*2` + `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`，渲染模式改 `LV_DISPLAY_RENDER_MODE_FULL`。配套：重新测内存占用、重新跑 NimBLE 压力
- **代价**：A → B 是一次性改动，改前先确认 PSRAM 剩余空间够 300KB+

## 参考

- `./lvgl-fullscreen-double-buffer-psram-render-mode-full-playbook.md`：B 方案完整落地账本
- `drivers/lvgl_port.c:79-83`：A 方案实际实现
- `drivers/board_config.h:34`：`BOARD_LVGL_BUFFER_LINES`
- `../firmware/nimble-mem-external-psram-playbook.md`：NimBLE 吃 PSRAM 的决定也影响这个 DR
