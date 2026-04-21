# Bootstrap: IoT Spec Fit Check

> **[执行记录]** 2026-04-21：Phase 1-5 全部执行完毕。
> - Phase 1：归档 54 条（52 计划 + 2 孤儿文件）到 `.trellis/spec/_archived_unrelated/2026-04-21/`
> - Phase 2：5 条差异条目开头插入"本项目适配说明"块
> - Phase 3：新增 7 条本项目独有 spec（`firmware/` 3 条、`device-ui/` 2 条、`protocol/` 2 条）
> - Phase 4：6 个 `index.md` 精简 + 新增条目分组到"本项目专属"小节
> - Phase 5：断链扫描 0 / 孤儿文件 0 / 88 条链接全有效
> - 最终有效条目 87 条 + 7 个 index.md，task.json.meta.verification 有完整数字
> - 原 prd.md 的 checkbox 清单保留如下，作为执行过程的参考审计路径

---

## 原 Fit Check 执行清单（参考）

> 原模板是 fullstack web 项目的 backend/frontend guidelines，不适用于 ESP32-S3 固件 + LVGL UI + Python 桌面伴侣的嵌入式场景。
> 本任务改为：**对已导入的 `.trellis/spec/iot/` 知识库做一次 Fit Check**，按本项目实际删除无关条目、适配部分条目、补齐本项目独有知识。
>
> 判断依据来自首次 Fit Check 会话（2026-04-21），统计（按本 prd.md 清单逐条计）：共 138 条左右，直接可用 ~64、需适配 5、Reference 保留 ~6、需删除 52；另需新增 7 条本项目独有条目。

---

## 项目基线（Fit Check 判断依据）

| 维度 | 实际情况 |
|------|---------|
| 板型 | ESP32-S3 N16R8（16MB QIO Flash + 8MB OCT PSRAM）|
| SDK | ESP-IDF v5.4.3 |
| UI | LVGL 9.5 + ST7789 240×320 SPI + FT5x06 I2C 触摸（**非 FT6336U**）|
| 字体 | Tiny TTF 运行时渲染 + 中文子集 EMBED_FILES（**非 binfont，非 LittleFS**）|
| 显示缓冲 | 40 行部分刷新（**非全屏双缓冲 PSRAM**）|
| BLE | NimBLE 4.2（**非 5.0**），5 个自定义 service（UUID `8a5c000x`）+ 标准 CTS |
| PC 端 | Python + bleak + customtkinter + winsdk |
| 分层 | `main / app / framework / drivers / services / tools` |
| 无 | STM32、ASR-Pro 语音、UART 业务、WiFi、HTTP、OTA、LittleFS、多 App、动态 App、Web、KVM |

---

## 先做这件事（0）：Import from Existing

本项目已经有成熟的"非 spec 形式"沉淀，先把它们抽取进 spec：

- [ ] `README.md` — 已详细描述硬件/软件架构，作为 Fit Check 与新增条目的权威来源
- [ ] `AGENTS.md` — Trellis 注入说明（无需抽取内容，作为交叉引用）
- [ ] 全局 `~/.claude/CLAUDE.md` — 项目偏好（Windows Python 用 `python` 不用 `python3`、奥卡姆剃刀、八荣八耻）
- [ ] 会话内 memory `MEMORY.md` — 已有 5 条项目级 memory，对应 7 条新增 spec 的草稿

---

## Phase 1：删除与项目明显无关的条目（共 52 条）

> 建议做法：整体移到 `.trellis/spec/_archived_unrelated/2026-04-21/` 而非 `rm`，便于反悔；每移一批立即更新对应目录的 `index.md` 把链接去掉。

### 1.1 firmware/（删 9）

- [ ] STM32 系列（2）
  - [ ] `stm32-hal-dma-enabled-but-unused-and-nvic-priority-playbook.md`
  - [ ] `stm32-usart-dma-dual-layer-transport-playbook.md`
- [ ] ASR-Pro / CI130X 系列（5）
  - [ ] `asrpro-cm_read_codec-300ms-timeout-30s-delay-pitfall.md`
  - [ ] `asrpro-outside-streaming-playback-playbook.md`
  - [ ] `asrpro-outside-write-stream-blocking-deadlock-pitfall.md`
  - [ ] `asrpro-pcm-passthrough-decoder-params-semantics-pitfall.md`
  - [ ] `asrpro-tts-hoarse-dma-block-size-alignment-pitfall.md`
- [ ] UART DMA 系列（2；本项目无 UART 业务）
  - [ ] `uart-mode-routing-contract.md`
  - [ ] `uart-dma-circular-idle-ht-tc-ringbuffer-playbook.md`

### 1.2 device-ui/（删 10）

- [ ] binfont 路线（3；本项目走 Tiny TTF）
  - [ ] `binfont-load-success-but-no-render-pitfall.md`
  - [ ] `lvgl-binfont-create-playbook.md`
  - [ ] `lv-font-conv-binfont-generation-playbook.md`
- [ ] 文件系统资源（2；本项目无文件系统）
  - [ ] `image-filesystem-bin-resource-playbook.md`
  - [ ] `resource-filesystem-playbook.md`
- [ ] 与本项目交互模式不符（2）
  - [ ] `small-screen-text-input-methods-playbook.md`（无输入需求）
  - [ ] `bottom-swipe-back-gesture-nav-bar-removal-playbook.md`（菜单页导航非手势）
- [ ] 与架构不符（3）
  - [ ] `lvgl-chat-ui-flex-layout-and-input-mode-playbook.md`（非聊天 UI）
  - [ ] `lvgl-app-shell-app-manager-layering-playbook.md`（单 App 7 页，非多 App）
  - [ ] `wououi-page-ui-framework-architecture-playbook.md`（STM32 OLED 框架）

### 1.3 protocol/（删 6）

- [ ] `asrpro-uplink-pcm-stream-contract.md`
- [ ] `handwrite-suggest-chat-thin-client-contract.md`
- [ ] `kvm-tcp-jpeg-frame-and-touch-contract.md`
- [ ] `parser-serial-framing-and-dispatch-contract.md`
- [ ] `downloader-module-layering-refactor-playbook.md`
- [ ] `downloader-protocol-contract.md`

### 1.4 ota/（删 4；STM32 专用）

- [ ] `stm32-bootloader-oled-progress-display-playbook.md`
- [ ] `stm32-ota-partition-contract.md`
- [ ] `stm32-ota-uart-protocol-v3-contract.md`
- [ ] `stm32-ota-uart-upload-playbook.md`

> 保留：`index.md`、`system-update-playbook.md`（将来加 OTA 时作为参考）

### 1.5 host-tools/（删 9）

- [ ] `host-protocol-stack-layering-playbook.md`（串口协议栈）
- [ ] `multi-cloud-asr-adapter-playbook.md`
- [ ] `nodejs-uploader-frontend-backend-split-playbook.md`
- [ ] `pc-serial-vad-asr-llm-tts-conversation-loop-playbook.md`
- [ ] `pinyin-server-3-tier-matching-ai-fallback-playbook.md`
- [ ] `server-ai-helpers-stream-and-json-array-playbook.md`
- [ ] `shared-js-dom-dependency-pitfall.md`
- [ ] `shared-js-implicit-global-deps-pitfall.md`
- [ ] `web-tools-new-page-integration-checklist.md`

### 1.6 iot/guides/（删 14）

- [ ] 多 App / 动态 App / Launcher（6）
  - [ ] `app-manager-lifecycle-contract.md`
  - [ ] `app-manager-minimal-api-playbook.md`
  - [ ] `launcher-dynamic-app-list-playbook.md`
  - [ ] `dynamic-app-appapi-abi-contract.md`
  - [ ] `dynamic-app-dev-workflow-and-constraints-playbook.md`
  - [ ] `stm32-dynamic-app-linker-script-contract.md`
- [ ] 不适用领域（2）
  - [ ] `audio-denoise-offline-pipeline-playbook.md`
  - [ ] `embedded-web-layering-guide.md`
- [ ] KVM（2）
  - [ ] `wireless-kvm-downlink-raw-vs-jpeg-decision-record.md`
  - [ ] `wireless-kvm-thin-client-architecture-playbook.md`
- [ ] 资源管理器+文件系统（3；本项目无）
  - [ ] `resource-manager-keyed-api-contract.md`
  - [ ] `resource-manifest-staged-adoption-playbook.md`
  - [ ] `device-filesystem-layout-resources-vs-data-playbook.md`
- [ ] 多 App 基础（1；单 App 不需要）
  - [ ] `embedded-app-framework-foundations-playbook.md`

### 1.7 辅助清理

- [ ] `.trellis/spec/iot/_extracted/` — 上一轮会话的蒸馏 backlog 工具产物（12000+ 行对话切块 + Python 脚本），不是 spec 正文。移到 `.trellis/_backlog/` 或直接 gitignore
- [ ] `.trellis/spec/iot/_extracted_v1_20260420/` — 旧版本同上

---

## Phase 2：需要适配的条目（5 条）

这几条思路可用，但某个关键细节与本项目不同，需要就地改写或新增 decision-record 说明选择。

- [ ] `device-ui/st7789-ft6336u-lvgl9-bringup-playbook.md`
      → **改**：触摸 IC 改为 **FT5x06**（不是 FT6336U），pinmap 按 `drivers/board_config.h`，I2C 地址按 `drivers/touch_ft5x06.c`
- [ ] `device-ui/lvgl-fullscreen-double-buffer-psram-render-mode-full-playbook.md`
      → **补**：新增 `device-ui/lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`，记录"本项目选 40 行部分刷新的取舍"（内存 38KB vs 全屏 PSRAM 方案）
- [ ] `device-ui/font-manager-contract.md`
      → **说明**：本项目没有独立 font_manager，字体在 `app/app_fonts.c` 直接初始化。该 contract 作为"将来拆 font_manager 时的参考"保留
- [ ] `firmware/lvgl-allocator-and-binfont-oom-hardening-playbook.md`
      → **改**：删掉 binfont 部分，保留 allocator 选择思路（对应 `CONFIG_LV_USE_CLIB_MALLOC=y` + `SPIRAM_MALLOC_ALWAYSINTERNAL`）
- [ ] `firmware/http-streaming-utf8-and-lvgl-thread-bridge-playbook.md`
      → **说明**：HTTP 部分不适用，但"后台任务→LVGL 线程桥接"思路与本项目 service/manager 模式同源，作为 reference 保留

---

## Phase 3：新增本项目独有条目（7 条）

memory 里已有碎片，现在把它们升级为正式 spec。每条按 `_templates/` 里对应模板写。

- [ ] `firmware/esp32s3-n16r8-qio-flash-oct-psram-pitfall.md`
      - 类型：pitfall
      - 来源：memory `project_esp32s3_n16r8_hardware.md`
      - 要点：`ESPTOOLPY_OCT_FLASH` 必须 OFF（Flash 是 QIO），否则 boot 循环；PSRAM 是 OCT 8MB@80M
- [ ] `firmware/sdkconfig-defaults-regen-pitfall.md`
      - 类型：pitfall
      - 来源：memory `feedback_sdkconfig_defaults_regen.md`
      - 要点：改 defaults 必须删 sdkconfig，ESP-IDF 只在 sdkconfig 缺失时从 defaults 生成
- [ ] `firmware/nimble-mem-external-psram-playbook.md`
      - 类型：playbook
      - 来源：`sdkconfig.defaults` line 78 注释
      - 要点：`BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` 让 BLE 堆走 PSRAM，释放内部 RAM
- [ ] `device-ui/tiny-ttf-plus-fontawesome-fallback-playbook.md`
      - 类型：playbook
      - 来源：`README.md` 中文字体节 + `app/app_fonts.c`
      - 要点：Tiny TTF 中文子集主字体 + `lv_font_montserrat_14/20` fallback 解 `LV_SYMBOL_*` 不在 CJK 子集问题；`CONFIG_LV_USE_CLIB_MALLOC=y` 让 glyph cache 自动落 PSRAM
- [ ] `device-ui/lvgl-40row-partial-buffer-vs-fullscreen-decision-record.md`
      - 类型：decision-record（Phase 2 已列，此处重复跟踪）
      - 要点：为什么选 40 行部分刷新（约 38KB）不走全屏双缓冲 PSRAM
- [ ] `protocol/esp-to-pc-notify-request-pattern-playbook.md`
      - 类型：playbook
      - 来源：memory `feedback_esp_reverse_request_pattern.md`
      - 要点：ESP 主动向 PC 要数据时复用 `control_service` NOTIFY + REQUEST id，不新建 GATT service，不切 Central
- [ ] `protocol/ble-custom-uuid-allocation-decision-record.md`
      - 类型：decision-record
      - 来源：`README.md` BLE 协议节
      - 要点：`8a5c000x` 前缀分配方案（weather 01/02、notify 03/04、control 05/06、media 07/08；奇偶对 = 对端读/写）

---

## Phase 4：更新各 index.md

每删一条、每新增一条，都要同步改对应 `index.md` 的链接列表。

- [ ] `.trellis/spec/iot/firmware/index.md`（删 9 行 + 新增 3 行）
- [ ] `.trellis/spec/iot/device-ui/index.md`（删 10 行 + 新增 2 行）
- [ ] `.trellis/spec/iot/protocol/index.md`（删 6 行 + 新增 2 行）
- [ ] `.trellis/spec/iot/ota/index.md`（删 4 行）
- [ ] `.trellis/spec/iot/host-tools/index.md`（删 9 行）
- [ ] `.trellis/spec/iot/guides/index.md`（删 14 行）

---

## Phase 5：回归验收

- [ ] 跑一次 `python ./.trellis/scripts/get_context.py`，确认 session-start 注入的 guidelines 索引不再指向已删条目
- [ ] `spec/iot/` 下的每条 `index.md` 文中提到的链接文件都实际存在（可用 shell 脚本批量校验）
- [ ] 按 `guides/spec-reuse-safety-playbook.md` 对新增 7 条各做一次自审（每条是否包含"适用边界"与"反例"）
- [ ] 按 `guides/anti-regression-acceptance-checklist.md` 走一遍

---

## 完成后

```bash
# Windows 用 python 不是 python3
python ./.trellis/scripts/add_session.py \
  --title "IoT Spec Fit Check 适配完成" \
  --commit <hash> \
  --summary "删 55 / 适配 5 / 新增 7 / 更新 6 个 index.md"

python ./.trellis/scripts/task.py finish
python ./.trellis/scripts/task.py archive 00-bootstrap-guidelines
```

---

## 执行原则

1. **先移动后删除**：所有删除操作先移到 `_archived_unrelated/2026-04-21/`，Phase 5 验收通过再考虑真删
2. **Fit Check 后再复用**：即便标记"直接可用"的条目，写代码前仍需按 `guides/spec-reuse-safety-playbook.md` 做一次自检
3. **新增条目不抄外部**：Phase 3 的 7 条必须基于本项目代码 / README / memory，不要从搜索结果拼凑
4. **先读后写**：编辑 `index.md` 前必先 Read，避免覆盖中间改动
5. **AI 不执行 `git commit`**：所有改动由人类审阅后提交
