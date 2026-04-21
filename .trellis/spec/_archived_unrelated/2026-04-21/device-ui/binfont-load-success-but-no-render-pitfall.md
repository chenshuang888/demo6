# Pitfall：binfont“能加载成功，但屏幕不显示”（metrics 正常但像素 payload/解码不兼容）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）里一次非常典型的故障：LittleFS 已挂载、文件可读、`lv_binfont_create()` 返回成功、`glyph probe` 能读到 dsc/metrics，但 UI 仍然空白；最终通过重新生成“无压缩/无预过滤”的 binfont 立即恢复。
>
> 目标：给出一个可执行的定位路径，让你下次遇到“加载成功但不显示”不再乱猜。

---
## 上下文签名（Context Signature）

> 这是“坑点复盘（Pitfall）”。症状相似不代表根因相同。
> 如果无法对齐本节上下文与证据：**禁止**直接给“最终修复实现”，只能给排查路径与最小验证闭环。

- 目标平台：ESP32/ESP32-S3/STM32/…
- SDK/版本：ESP-IDF x.y / LVGL x.y / HAL 版本 / …
- 关键外设：LCD/Touch/I2C/SPI/UART/Wi‑Fi/BLE/…
- 资源约束：Flash/RAM/是否有 PSRAM / heap 策略
- 并发模型：谁是 single-writer？哪些回调/中断上下文？

---

## 证据最小集（必须补齐，否则只给排查清单）

- 复现步骤：最短 3~5 步
- 关键日志：至少 10 行（含时间戳/线程/错误码）
- 关键配置：`sdkconfig`/分区表/LVGL 配置/驱动配置（只列与问题相关的）
- 边界条件：是否“只在某分辨率/某字体/某 MTU/某波特率/某温度/某电源”下发生？

---

## 停手规则（Stop Rules）

- 无复现、无日志、无法确认平台/版本：不要输出最终修复，只输出“要补齐的信息 + 排查清单”。
- 修复涉及写 flash/修改分区/改并发 owner：先给最小冒烟闭环与回滚方案，再进入实现细节。
- 多个根因都解释得通：先加观测点（日志/计数器/抓包）缩小假设空间，再改代码。

---


## 一句话结论

当出现：

- `lv_binfont_create()` 成功
- `lv_font_get_glyph_dsc()` / `glyph probe` 显示 `found=1`，metrics 正常
- 但屏幕仍然不显示文字

高度怀疑：**binfont 文件的位图 payload（或压缩格式）与当前 LVGL 运行时 loader 解码不兼容**，而不是挂载/路径/UI 绘制逻辑问题。

---

## 先确认：你是不是卡在“LVGL FS 没开”的前置条件

对话里揭示了一个容易忽略的前置条件：

- `lv_binfont_create()` 内部走的是 LVGL 的 `lv_fs_open()`
- 如果 `sdkconfig` 里没打开 `CONFIG_LV_USE_FS_STDIO`（或等价 LVGL FS 驱动），即使 `/res` 在 ESP-IDF 侧已经挂载成功，LVGL 也可能“读不到”

表现：

- 你能看到 `resources_fs: mounted resources at /res`
- 但 `font_manager` 仍然 `use fallback font for /res/fonts/...`

修法：先把 LVGL FS 驱动打开并确保 **实际生效**（不是只改 defaults）。

相关说明见：

- `spec/iot/device-ui/resource-filesystem-playbook.md`

---

## 典型症状：文件可读 + metrics 正常 + 依然不显示

对话里出现过的关键证据链：

- `vfs probe ok: /res/fonts/ui_sans_16.bin`（文件确实可读）
- `loaded font: /res/fonts/ui_sans_16.bin`（创建成功）
- `glyph probe U+8D44: found=1 ... box=... line_h=...`（字形描述正常）

但屏幕仍然空白，说明：

- “字形索引/尺寸信息”是对的（所以 `found=1`）
- “真正画到屏幕的像素数据”可能有问题（payload 为空/解码失败/压缩格式不匹配）

---

## 最小修复路线（对话验证有效）

先用**无压缩/无预过滤**的 binfont 做对照验证：

- `--format bin`
- `--no-compress`
- `--no-prefilter`
- `--bpp 4`（验证阶段优先可读性）

对话里结论非常明确：重新生成并覆盖 4 个字体文件后，屏幕立刻恢复显示，根因基本锁定为“旧资源文件生成参数/压缩兼容性问题”。

---

## 如果你想进一步“证据级”确认（可选，偏底层）

对话里给了一个很硬核但很有效的判断思路：在 `lv_binfont_create()` 成功后，针对一个已知 glyph：

1. 调 `lv_font_get_glyph_dsc(...)` 拿到 glyph 信息
2. 直接检查 `glyph_bitmap` 对应内存区域是否明显异常（例如全 0）

判断：

- raw bytes 本身就全 0/异常：更像 binfont payload 问题
- raw bytes 非 0 但仍空：再去查压缩解码路径/压缩格式兼容性

> 这类检查需要你对 LVGL 字体内部结构足够熟悉；第一阶段通常不必做到这一步。

---

## 验收标准

- `/res/fonts/*.bin` 可读、`loaded font` 出现
- UI 能实际显示中文/数字（不再是“有对象但画不出来”）
- 若恢复显示只依赖“关闭压缩/预过滤”，就把“压缩兼容性”作为单独后续任务，不要继续混着查挂载/路径

