# Playbook：把图片做成文件系统里的 LVGL `.bin` 资源并显示（`/res/images/*.bin` + `lv_image_set_src`）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中从“先做最小可见图片示例”推进到“把较大图片放到文件系统，让 LVGL 解码显示”的完整讨论：为什么优先 `.bin`，为什么不先上 PNG/JPG，以及如何用 `image_manager` 维持 UI 不碰路径。
>
> 目标：在不引入额外图片解码依赖的前提下，先把“文件系统图片显示链路”跑通，并把常见花屏/像素堆积问题的排查路径写清楚。

---
## 上下文签名（Context Signature，必填）

> 目的：避免“场景相似但背景不同”时照搬实现细节。

- 目标平台：ESP32/ESP32‑S3/STM32/…（按项目填写）
- SDK/版本：ESP-IDF x.y / LVGL 9.x / STM32 HAL / …（按项目填写）
- 外设/链路：UART/BLE/Wi‑Fi/TCP/UDP/屏幕/触摸/外置 Flash/…（按项目填写）
- 资源约束：Flash/RAM/PSRAM，是否允许双缓冲/大缓存（按项目填写）
- 并发模型：回调在哪个线程/任务触发；谁是 owner（UI/协议/存储）（按项目填写）

---

## 不变式（可直接复用）

- 先跑通最小闭环（冒烟）再叠功能/优化，避免不可定位
- 只直接复用“原则与边界”，实现细节必须参数化
- 必须可观测：日志/计数/错误码/抓包等至少一种证据链

---

## 参数清单（必须由当前项目提供/确认）

> 关键参数缺失时必须停手，先做 Fit Check：`spec/iot/guides/spec-reuse-safety-playbook.md`

- 常量/边界：magic、分辨率、slot 大小、最大 payload、buffer 大小等（按项目填写）
- 时序策略：超时/重试/节奏/窗口/幂等（按项目填写）
- 存储语义：写入位置、校验策略、激活/回滚策略（如适用）（按项目填写）

---

## 可替换点（示例映射，不得照搬）

- 本文若出现文件名/目录名/参数示例值：一律视为“示例”，必须先做角色映射再落地
- 角色映射建议：transport/codec/protocol/ui/persist 的 owner 边界先明确

---

## 停手规则（Stop Rules）

- 上下文签名关键项不匹配（平台/版本/外设/资源/并发模型）时，禁止照搬实施步骤
- 关键参数缺失（端序/magic/分辨率/slot/payload_size/超时重试等）时，禁止给最终实现
- 缺少可执行的最小冒烟闭环（无法验收）时，禁止继续叠功能

---


## 一句话结论

在你当前工程配置下，最稳的路线是：

1. 把输入图片（png/jpg 只是素材）转换成 **LVGL `.bin` 图片资源**
2. 放进 `resources/images/`
3. 通过 LittleFS 打包并挂载到 `/res`
4. UI 用 `lv_image_set_src(img, "/res/images/xxx.bin")` 显示

不要一上来折腾 PNG/JPG 解码器开关。

---

## 为什么推荐 `.bin`（而不是 PNG/JPG）

对话里的关键判断依据是：工程当前已经启用 `CONFIG_LV_USE_IMAGE`，但 PNG/JPG/BMP 等解码器开关并未启用。

因此：

- **现在就能直接做**：`.bin` 图片（走 LVGL 的 bin decoder）
- **不建议现在直接做**：PNG/JPG 文件显示（需要额外开 decoder，引入更多 flash/RAM/耗时/复杂度）

---

## LVGL 侧“这条路为什么通”

对话里指出：LVGL 自带 bin decoder，并且支持文件源：

- 数据源：文件（`LV_IMAGE_SRC_FILE`）
- 格式：`*.bin`

因此“文件系统图片 → LVGL bin decoder → image widget”链路在 LVGL 9 里是成立的。

---

## 大图片要注意的点（ESP32 现实约束）

对话里强调：能显示 ≠ 适合随便上超大图。建议：

- 图片尺寸尽量接近实际显示尺寸（240×320 就别塞 1080p 再缩）
- 优先 RGB565
- 少透明通道
- 少做复杂缩放/旋转

---

## 最小落地形态（建议同时做两层）

### 1) 运行时资源（文件系统）

- `resources/images/xxx.bin`（构建期打包进 `resources` 分区）
- 运行时路径：`/res/images/xxx.bin`

### 2) `image_manager`（UI 解耦）

对话里推荐的形态是让 UI 不直接写路径：

- `image_manager_get_path(role)`
- 或 `image_manager_apply(lv_obj_t *image, role)` 内部做 `lv_image_set_src(image, path)`

并且要理解一个关键差异：

- 字体：`lv_binfont_create()` 会创建 `lv_font_t*`，属于“对象常驻复用”
- 图片：当前更像“把路径绑定到 LVGL 对象”，图片的解码/缓存主要由 LVGL image decoder/cache 决定

因此第一版的 `image_manager` 更像“路径路由器/注册表”，不必一上来做重缓存框架。

---

## 常见坑：显示成“像素堆积/花屏”

对话里这类问题的核心结论是：通常不是“挂载/路径”问题，而更像是：

- `.bin` 资源的**格式/颜色格式/字节序**与当前 LVGL 显示链路不匹配

建议的优先级（原则）：

1. 先做一个“最小验证图”（小尺寸、已知色块），验证 `.bin`→显示链路是否正确
2. 再用官方/可靠的转换器生成 `.bin`，避免“自定义脚本生成但格式不一致”
3. 避免直接把原始 RGB565 dump 当成 LVGL `.bin`（除非你明确知道 LVGL `.bin` 头与布局要求）

---

## 验收标准

- `/res/images/xxx.bin` 能被 `lv_image_set_src` 正常显示（不花屏）
- UI 不直接写路径（通过 `image_manager` role/API）
- 大图场景下 RAM/刷新性能可接受（至少不 crash、不明显卡死）

