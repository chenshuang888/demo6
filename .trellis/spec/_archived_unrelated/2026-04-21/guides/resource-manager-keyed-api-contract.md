# Contract：资源系统的三层模型（归属域 + 资源 key + 物理存储），对外按“我要什么”而不是“资源类型”

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中 demo5 关于资源体系的关键纠偏：`font/icon/image` 不是资源体系的一级分类，它们更像消费场景/加载方式；资源系统对外应稳定为“归属域 + 资源 key（语义名）”，物理目录与文件格式属于内部实现细节。
>
> 目标：避免把阶段性实现（`font_manager/image_manager/...`）误当成最终公共 API；让工程能从 IoT bring-up 阶段平滑演进到“多 app + 多资源类型 + 可扩展”的形态。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 一句话结论

- 对外（app/UI 层）只应该看到：**资源 key（语义名字）**，而不是路径/目录结构/文件格式。
- 对内（实现层）可以按 loader 分类（font/image/text/blob…），但 loader 分类不应主导外部资源模型。

---

## 为什么 `font / icon / image` 不应该是“一级分类”

`font/icon/image` 更像是：

- 当前的消费场景
- 或加载方式

同一种物理类型（例如 image）会承载很多不同语义（背景图、app 图标、动画帧、皮肤预览等）。因此资源体系更合理的入口是“语义 key”，而不是“image 目录 / icon 目录”。

---

## 推荐的三层模型

### 第一层：归属域（最稳定）

- `public`
- `private/<app_id>`

### 第二层：资源标识（resource key，语义名字）

示例：

- `icon.main`
- `screen.home.background`
- `config.default`
- `theme.dark.palette`
- `anim.boot.frames`

原则：app 只感知这一层。

### 第三层：物理存储（内部实现细节）

物理落盘未来可能是：

- `/res/public/...`
- `/res/apps/<app_id>/...`

也可能演进为 OTA 包/压缩包/外置存储等。  
原则：这层不对 app 暴露。

---

## `resource_manager` 的职责边界（当前最合适的 4 条）

`resource_manager` 是“资源边界层”，不是“万能资源平台”。当前最合适定成四件事：

1. **统一资源注册**：把 app 的资源声明/清单收口到一个入口（静态注册优先）。
2. **统一资源查询**：对外按 key 查询，不暴露路径/目录结构。
3. **统一资源适配**：把资源喂给不同消费端（如 LVGL 字体/图片/文本/二进制）。
4. **屏蔽底层存储细节**：LittleFS/分区/目录结构变更不影响 app 层。

---

## 明确“不负责”的事（避免过度设计）

- 不负责第一版就自动推导“所有 key → path 的映射规则”并强行固定（容易定错、成本高）。
- 不负责一上来就做“运行时 JSON manifest 解析平台”（复杂且容易走偏）。
- 不负责把所有资源都做成统一“对象化资源系统”（先保证稳定边界与最小闭环）。

manifest 的分阶段落地见：`spec/iot/guides/resource-manifest-staged-adoption-playbook.md`。

---

## 对外 API 形态建议：围绕“我要什么”

示例（语义表达，命名可调整）：

```c
resource_manager_apply_visual(app_id, "icon.main", obj);
resource_manager_get_font(app_id, "font.title", &font_out);
resource_manager_read_blob(app_id, "config.default", dst, dst_size, &len_out);
resource_manager_open_stream(app_id, "model.keyword", &stream_out);
```

关键点：外部传入的是 resource key，而不是路径/目录结构/强绑定 `font/icon/image` 总分类。

---

## 内部实现建议：先“统一收口”，再“内部收编”

短期现实：底层仍需要知道怎么处理不同资源：

- 字体如何转成 `lv_font_t *`
- 图片如何喂给 `lv_image_set_src`
- JSON/二进制 blob 如何读取

推荐折中：

- 对外：只暴露 `resource_manager`（按 key）
- 对内：保留 `font_loader` / `image_loader` / `blob_loader` / `text_loader`
- 现有 `font_manager/image_manager/icon_manager` 允许作为内部实现复用，逐步降级

---

## `app_paths.c` 的处理建议（收敛路径规则）

对话里明确：`app_paths` 如果只是“路径规则集中器”，它不应该再是公共概念。

两种选项：

- 方案 A：保留文件，但只作为 `resource_manager.c` 的内部 helper（不导出给 app/launcher）
- 方案 B（更推荐）：直接并入 `resource_manager.c`（减少概念、边界更清晰）

推荐的两步改造（风险最小）：

1. 第一步：先做“公共收口”
   - 新增 `resource_manager` 对外统一 API
   - 内部先复用现有 manager/loader
2. 第二步：再做“内部收编”
   - `font_manager/image_manager/icon_manager` 逐步降级为内部模块
   - `app_paths` 并入 `resource_manager`

---

## 与 `app_manager` 的边界（避免职责打架）

- `app_manager` 管“身份与生命周期”（有哪些 app、当前是谁、打开/返回、context）
- `resource_manager` 管“资源与加载”（icon 在哪、字体怎么拿、图片怎么喂、配置怎么读）

---

## 最小对外 API 覆盖面（从真实调用反推）

基于对话中对“当前真实调用模式”的归纳，`resource_manager` 第一版对外至少要覆盖这 3 类：

1. **资源系统初始化**
   - 确保文件系统/资源后端 ready
   - 允许降级（失败不致命时走 fallback）
2. **图片类资源的 UI 直接 apply**
   - 给定 UI 对象 + 资源 key，直接完成设置（适配 `lv_image_set_src` 等）
3. **字体类资源的句柄/指针获取**
   - 按资源 key 返回 `const lv_font_t *`（或等价句柄）

更像“内部能力/可选扩展”的两类（建议预留，但不强行纳入第一版公共 API）：

4. key → locator/path 的解析输出（给内部 loader 使用即可）
5. raw blob / stream 读取（格式探测、完整性校验、自定义 decoder、缓存/版本校验等）

---

## 验收标准（反复用）

- app/UI 层不再拼路径访问资源（没有硬编码 `/res/...`）。
- 物理目录结构调整时，只改 `resource_manager` / loader，app 代码不动。
- 能明确回答：“这个问题属于 app_manager 还是 resource_manager？”
