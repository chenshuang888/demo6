# Playbook：LVGL 内存分配策略与 binfont OOM 加固（避免 `lv_binfont_loader` 崩溃）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于"binfont 大字库后崩溃/分配失败""`CONFIG_LV_MEM_SIZE_KILOBYTES=64` 太小""切换 LVGL allocator 到 stdlib/ESP heap""loader 判空止血""不要只靠加大 builtin pool 试错"的一整段结论。
>
> 目标：把"必修的稳定性补丁"和"推荐的最小修复组合"写成可执行清单，保证内存紧张时系统降级为 fallback，而不是直接崩溃。

> **[本项目适配说明]**（demo6）：
> - 本项目**不用 binfont**（走 Tiny TTF + EMBED_FILES 嵌入），所以 binfont OOM 加固的"loader 判空 / fallback 切换"章节只作参考
> - 但 **allocator 选择的主干思路直接适用**：
>   - 本项目启用 `CONFIG_LV_USE_CLIB_MALLOC=y`（对应 playbook 中 "切 stdlib/ESP heap" 建议）
>   - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`：小分配走内部 RAM，大分配自动落 PSRAM（glyph cache）
>   - `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`：保留 32KB 给 DMA/ISR
> - 对 Tiny TTF，类似的 OOM 加固思路：`lv_tiny_ttf_create_data_ex` 返回 NULL 时要判空，`app_fonts_init` 如失败应日志告警但不崩溃
> - 相关：`./nimble-mem-external-psram-playbook.md`（NimBLE 也吃 PSRAM，注意综合预算）、`../device-ui/tiny-ttf-plus-fontawesome-fallback-playbook.md`
> - **复用时**：binfont 段落跳过；allocator / `LV_USE_CLIB_MALLOC` / 内存预算的不变式直接用

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

对话里最推荐的最小修复组合是：

1. **给 `lv_binfont_loader.c` 的 `lv_malloc()` 补判空并安全返回**（止血：把 crash 变成可控失败）
2. **把 LVGL allocator 从 builtin pool 切到 stdlib / ESP heap（系统堆）**（让大字库有真实可用的堆空间）
3. **保持 `app/font_manager.c` 的 fallback 逻辑不变**（上层自然接住失败）

---

## 选项对比（对话中的结论）

### 方案 1：切换 LVGL 内存分配到 stdlib / ESP heap（推荐）

结论：推荐，且属于“最小修复”范畴。

理由（对话提炼）：

- 当前 `CONFIG_LV_MEM_SIZE_KILOBYTES=64` 这种专用小池明显不够承载大字库的运行时分配需求
- 项目本身已经启用了系统堆/PSRAM 等内存体系，让 LVGL 回到系统堆更符合工程现实

风险/代价：

- 需要确认 LVGL 9 在你的 managed component 配置下对应的 Kconfig 选项名（不要猜）
- 切换后要补一轮内存验证，确认没有引入新的碎片化/约束问题

### 方案 2：增大 LVGL builtin 内存池（备选，不建议长期）

结论：可作为短期对照实验，不建议作为最终方案。

优点：

- 改动面看起来很小（只改配置）

缺点（对话强调）：

- 只是把问题往后拖：后续资源变化/碎片变化会再次踩坑
- 静态预留吞内存，性价比低（尤其在你已经有 PSRAM 的前提下）

### 方案 3：只做 loader 判空（必须做，但不够）

结论：必须做止血，但它不是完整解决方案。

原因：

- 不判空属于确定性 bug：内存不足时会空指针写导致崩溃
- 只修判空会让系统“从 crash 变成 fallback”，但如果内存策略不改，你的大字库依然可能加载不上来

### 方案 4：缩小字体规模（最后兜底）

结论：不应作为第一选择。

理由：

- 这是改产品资源规格，不是修 bug
- 你当前直观问题是 loader 空指针写 + allocator 不匹配

---

## 关键实现点（概念层，不替你直接改第三方依赖）

## 涉及文件（对话中的最小闭环）

> 下列路径用“工程相对路径”表达（对话里出现的是绝对路径）。

1. LVGL binfont loader（第三方依赖）
   - `managed_components/lvgl__lvgl/src/font/lv_binfont_loader.c`
   - 关注点：在 `load_glyph()` 附近为 `lv_malloc()` 返回值补判空，并确保错误路径能安全返回
2. 生效配置
   - `sdkconfig`（必须先改它）
   - `sdkconfig.defaults`（建议同步维护为模板，但不要指望它自动覆盖 sdkconfig）
3. 上层 fallback（用于承接失败）
   - `app/font_manager.c`
4. 资源链路（验证时必须仍正常）
   - `app/resources_fs.c`
   - `partitions.csv`（确保 `resources` 分区大小满足字库体积预算；对话中提到 `0x400000` 级别是足够的）
5. 初始化顺序（影响日志定位与是否能在 UI 建立前完成字体加载）
   - `main/main.c`（通常建议 `lvgl_port_init()` → `font_manager_init()` → `ui_app_init()`）

---

### A. loader 判空的“应有行为”

当 `lv_malloc()` 失败时，最合理的行为应该是：

- 立即返回失败（不要继续写 bitmap buffer）
- 当前 glyph 加载失败
- draw path 走“无字形”或 fallback 路径
- 若初始化阶段失败：`lv_binfont_create()` 返回 `NULL`
- 上层 `font_manager` 走 fallback（对话里已设计好）

### B. managed component 修改的维护成本

对话里提醒：`lv_binfont_loader.c` 在 `managed_components/lvgl__lvgl/...` 下，属于第三方依赖改动：

- 后续组件升级/重新 lock 需要记得重打补丁或维护 patch

---

## 验证点（按“安全性 -> 功能性 -> 资源表现”）

### 1) 安全性验证

- 不再出现 binfont loader 相关的空指针崩溃（例如某行号空指针写）
- 内存压力下启动也不应 Guru Meditation
- 分配失败时只允许出现 warning/fallback，不允许死机

### 2) 功能性验证

- 资源字体能在资源存在时加载成功（日志出现 `loaded font: ...`）
- 资源缺失/内存不足时自动 fallback（UI 仍能显示）

建议关注的“链路日志锚点”（对话中的例子）：

- `resources_fs: mounted resources at /res`
- `vfs probe ok: /res/fonts/ui_sans_16.bin` / `ui_sans_24.bin`
- 期望：`loaded font: /res/fonts/ui_sans_16.bin` / `ui_sans_24.bin`
- 若仍然是：`use fallback font for ...`，说明你只是“从 crash 变成 fallback”，但字体仍可能因为 allocator 不足而加载不上

### 3) 资源表现验证

- 放大字库（例如 3500 字）后仍可稳定运行
- 字体加载成功与否不能被 fallback 掩盖（日志必须能区分）

### 4) 负向验证（证明 fallback 真闭环）

对话里建议做一个“故意失败”的验证：

- 临时让某个字体路径无效，或准备一个故意不可加载的 binfont
- 预期：只 warning、UI 仍显示 fallback、绝不 crash
