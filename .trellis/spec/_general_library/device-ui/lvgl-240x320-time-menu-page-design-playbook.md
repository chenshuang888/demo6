# Playbook：240×320 小屏（竖屏）LVGL 时间页 + 菜单页的“耐看”布局范式

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中对 `page_time.c` / `page_menu.c` 的 UI 重写总结（配色、卡片布局、背光条目、BLE 状态展示、中文→英文回退）。
>
> 目标：在资源有限的小屏上，用一套可复用的设计 token + 组件范式，快速做出“不丑、能扩展”的基础 UI。

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

**用“深色背景 + 卡片层级 + 少量强调色”的套路，配合“顶栏 + 内容卡 + 列表卡”，能在 240×320 上获得稳定的观感与可扩展性。**

---

## 设计 token（对话中的示例）

- 背景：深色（偏紫/偏蓝皆可）
- 强调色：青绿（用于时间、按钮按下态、连接状态等）
- 组件形态：卡片（圆角 + 轻阴影/描边）

> token 的价值：后续新增页面（Weather/Music/About）能保持一致观感，不会“每页一套审美”。

---

## 时间页（page_time）布局要点

推荐结构：

- 顶部：日期 + 星期（可选）+ 右上角菜单图标（建议用 `LV_SYMBOL_LIST`）
- 中部：大号时间（强调色）
- 下部：两张卡片：
  - `TIME`：Hour/Min 两组 `[-][+]`
  - `DATE`：Year/Month/Day 三组 `[-][+]`

对话中收敛的细节：

- 英文场景下更推荐“标签在上、按钮在下”的两行布局，避免横排挤压：
  - `Hour` / `Min` 上方，按钮在其下
  - `Year` / `Month` / `Day` 同理

---

## 菜单页（page_menu）布局要点

推荐结构：

- 顶栏：`← Menu`（返回）
- 列表卡片：4 个条目（左图标 + 文本 + 右侧状态/箭头）

对话中出现的条目模板（可复用）：

- `Bluetooth`：右侧显示 `Connected`（绿）/ `Off`（灰），不可点
- `Backlight`：右侧显示百分比（25/50/75/100），点击循环切档
  - 依赖底层 API：`lcd_panel_set_backlight/get_backlight`
- `About`：跳转到 About 页面（比 msgbox 更可扩展）
- `Back to Clock`：返回时间页

---

## 中文显示不出来时的回退策略（务实）

对话中的现实情况：

- Montserrat 系列字体只覆盖 ASCII，不含中文

两条路线：

1) **短期**：把 UI 字符串全部换成英文（先把框架跑通）  
2) **长期**：引入中文字体（TTF 子集 / binfont），再恢复中文

> 字体工程化落地参考：`spec/iot/device-ui/font-strategy-playbook.md`、`spec/iot/device-ui/tiny-ttf-subset-playbook.md`。

---

## BLE 状态与 UI 的解耦展示

推荐：页面 `update()` 每周期读取“连接状态快照”，只做渲染更新。

跨线程通信的边界参考：

- `spec/iot/guides/nimble-ui-thread-communication-contract.md`

---

## 验收标准

- 时间页/菜单页在 240×320 竖屏上不拥挤、信息层级清晰
- 背光条目真实生效（不是只改 UI）
- BLE 连接状态能稳定刷新（不出现一直 Off 的“寄存器缓存”假象）

