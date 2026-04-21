# Playbook：字体架构（中文/英文数字/图标分离）与“图标+文字同一标题”的落地方案

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里围绕 Flash 字体体积、删冗余字体、以及 FontAwesome 图标恢复的整套迁移与复盘。

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

小屏 IoT UI 的字体建议拆成三类：

- **中文**：单独的大中文像素字库（占大头，控制字符集）
- **英文/数字**：小而清晰的西文字体（如 Inter/Montserrat，主要用于数字/英文）
- **图标**：FontAwesome（12px/18px 等）

并且接受一个现实：**LVGL 的 `label` 一次只能设置一个 font**；标题里要“图标+文字”通常用 **双 label**（推荐）或做 **fallback font**。

---

## Flash 账本（先看清楚再优化）

对话里的一个典型统计：

- 字体总占用：约 4.8MB
  - `font_zpix_*_ext`：约 4.5MB（>90%）
  - `font_inter_*`：约 275KB
  - `font_zpix_12`：约 61KB（未使用，可删）

结论：**真正的大头是中文字库的字符集规模**，删掉一些小字体只能省几十 KB。

---

## 立刻收益：删除未使用字体（安全清理路径）

对话里给出的“删一个字体文件要同时改 3 处”的经验非常实用：

1. 从 `main/CMakeLists.txt` 移除编译项
2. 删除对应 `.c` 文件
3. 从头文件（例如 `font_inter.h` 这类注册头）移除声明

原则：**只删文件不删声明 / 只删声明不删 CMake** 都会留下隐患（编译/链接/重复符号/误引用）。

---

## 未来收益：裁剪中文字符集（决定 2MB+ 的节省）

对话里提到的可选优化：

- 把中文字库从约 7000 字裁剪到常用 3500 字
- 可节省约 2.3MB Flash

落地建议：

- 把“字符集文件 + 生成流水线”固化到仓库（可复现）
- 参考既有字体流水线文档（如字符集与生成 pipeline 的 playbook）

---

## 图标恢复：FontAwesome 与标题混排问题

对话里遇到的核心限制：

- **同一个 `lv_label_t` 无法同时使用“图标字体”和“中文字体”**

### 方案 A：双 label（推荐）

思路：图标一个 label，文字一个 label，并排布局。

```c
// icon label
lv_obj_t *icon = lv_label_create(parent);
lv_label_set_text(icon, LV_SYMBOL_BELL);
lv_obj_set_style_text_font(icon, &font_awesome_12, 0);

// text label
lv_obj_t *text = lv_label_create(parent);
lv_label_set_text(text, " 时钟");
lv_obj_set_style_text_font(text, &font_zpix_cn_12, 0);
```

优点：简单、稳定、可控。

### 方案 B：fallback font（可选但复杂）

思路：让主字体找不到 glyph 时自动回退到另一个字体链。

缺点：需要字体生成/配置配合，工程复杂度更高，不适合作为第一方案。

---

## 验收标准

- 图标字体与中文字体同时可用：
  - Launcher 图标恢复（18px）
  - 标题/按钮图标可用（12px）
- 不出现缺字/方块（至少覆盖常用 UI 文本 + 图标集）
- Flash 变化可解释：
  - 删除未使用字体会立即下降
  - 裁剪字符集能带来 MB 级收益

