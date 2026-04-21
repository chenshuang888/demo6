# Playbook：LVGL 全屏双缓冲（PSRAM）+ `RENDER_MODE_FULL` 的落地与账本

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）中 demo1 的显示方案：ST7789（320×240）RGB565，LVGL 9.x 采用 **两张全屏 draw buffer** 放在 PSRAM，渲染模式为 full。
>
> 目标：把"全屏双缓冲为什么可行/何时该用/怎么估算内存"写成可复用的工程账本，避免拍脑袋选 buffer 大小导致 PSRAM/内部 RAM 被挤爆或 UI 卡顿。

> **[本项目适配说明]**（demo6）：
> - 本项目**未采用**全屏双缓冲 PSRAM 方案，改走 **40 行部分刷新 + 内部 RAM + DMA**（`drivers/lvgl_port.c:79-83`）
> - 权衡决策详见 `./lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`
> - 本 playbook 的价值：将来若加全屏过渡动画 / 大幅图像 / 实时图表页面，切到全屏 PSRAM 方案的账本和验收步骤仍可用
> - **复用时**：不变式（每帧 flush 完才能画下一帧、tx_done 通知机制、PSRAM cache invalidate）通用；buffer 大小/分配位置需要项目重算

---

## 上下文签名（Context Signature，必填）

- 目标平台：ESP32‑S3 等（ESP‑IDF 5.x）+ LVGL 9.x
- 显示链路：SPI LCD（例如 ST7789）+ RGB565 等（bytes_per_pixel 影响巨大）
- PSRAM：通常需要 8MB 级别，且大块分配能路由到 PSRAM
- 风险等级：中到高（PSRAM/内存策略/flush 通知链路不稳会导致卡顿、花屏、OOM）

---

## 不变式（可直接复用）

- 先算账再决定：`bytes_per_frame = width * height * bytes_per_pixel`
- 没有 PSRAM 或 buffer 过大时，不要硬上全屏双缓冲（应改 partial buffer）
- flush 完成通知链路必须正确（否则双缓冲会更容易暴露时序问题）

---

## 参数清单（必须由当前项目提供/确认）

- 分辨率、像素格式、bytes_per_pixel（RGB565=2，其他格式另算）
- buffer 张数（1/2）与 render mode（FULL/…）
- PSRAM 是否可用、是否稳定、分配策略（大块是否能落到 PSRAM）
- 允许的内存预算：给 UI buffer 留多少，给 BLE/字体/文件系统留多少

---

## 停手规则（Stop Rules）

- 你还没跑通“单缓冲最小点屏/触摸”闭环：禁止直接上全屏双缓冲
- 不知道 bytes_per_pixel/分辨率就选 buffer 方案：禁止拍脑袋开 FULL + 双缓冲
- PSRAM 未稳定（启动日志/分配测试无证据）：禁止把大块 buffer 强塞进去

---

## 一句话结论

**当你有 8MB PSRAM 且追求观感/吞吐时，全屏双缓冲是最省心的方案；核心是先算清每张 buffer 的字节数，再决定是否值得。**

---

## buffer 大小怎么算

公式：

- `bytes_per_frame = width * height * bytes_per_pixel`

RGB565（16-bit）：

- `bytes_per_pixel = 2`

对话中的 320×240 示例：

- 单张全屏：`320 * 240 * 2 = 153600 bytes`（约 150KB）
- 双缓冲：约 300KB

这在 8MB PSRAM 上通常是可接受的（前提是你把大块分配路由到 PSRAM）。

---

## 为什么建议放到 PSRAM

全屏双缓冲的核心矛盾是：

- 内部 RAM 也就几百 KB 级，放两张全屏 buffer 会非常吃紧
- 但 PSRAM 有 MB 级空间，更适合承载这种“大而规则”的数据结构

因此常见的搭配是：

- 启用 PSRAM
- LVGL 内存源走系统 malloc（让 heap_caps 把大块分配推到 PSRAM）

---

## 适用场景 / 不适用场景

适用：

- UI 复杂、动画多、希望整体观感平滑
- 屏幕分辨率不算太大（240×320 / 320×240 等）
- PSRAM 足够（例如 8MB）

不适用：

- 没有 PSRAM
- 屏幕很大，算出来 buffer 过于夸张（先算账）
- 你要极致省电/极小内存（考虑 partial buffer）

---

## 验收标准

- 启动日志显示 PSRAM 正常（至少能稳定分配两张全屏 buffer）
- UI 刷新无明显撕裂/花屏（flush ready 路径正确，见本目录的 flush/tx_done 文档）
