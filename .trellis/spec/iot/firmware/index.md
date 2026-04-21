# 固件（ESP-IDF / FreeRTOS / 存储 / 并发）

> 本目录聚焦：任务划分、并发模型、持久化（NVS）、性能与可靠性。

## 复用安全（必读）

- 复用任何条目前先做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（硬件/SDK/并发 owner 不同会直接翻车）

## 文档

- `./nvs-single-writer-contract.md`：NVS 持久化的"单写者"契约（跨任务/跨回调都要遵守）。
- `./nvs-concurrency-pitfall.md`：多线程直接写 NVS 的典型坑：症状→根因→修复→预防→验收。
- `./nvs-persist-settings-store-layering-playbook.md`：NVS 持久化的分层落地（`persist/settings_store/notify_manager`）+ 写入策略（立即/周期/防抖）。
- `./esp-idf-requires-vs-priv-requires-transitive-header-pitfall.md`：公共头暴露依赖导致 `nvs.h` 找不到：`REQUIRES` vs `PRIV_REQUIRES` 的传递性坑。
- `./esp-idf-project-baseline-checklist.md`：ESP-IDF 新项目的基线盘点清单（先摸清模板态/入口/组件/环境）。
- `./esp-idf-component-deps-minimal-playbook.md`：ESP-IDF 组件依赖声明的最小化策略（先最小集合，缺哪个补哪个）。
- `./esp-idf-component-names-by-version-pitfall.md`：ESP-IDF 组件名随版本变化的坑（`esp_driver_i2c` vs `driver`，以及 `esp_lcd_touch` 是否自带）。
- `./ble-conn-shared-state-to-avoid-component-cycle-playbook.md`：为主动 Notify 提供 `conn_handle` 的共享状态模块（`ble_conn`），避免 `services` 反向依赖 `drivers` 形成组件循环依赖。
- `./esp-idf-lvgl-drivers-bootstrap-playbook.md`：新建工程 + 只移植 `drivers/` 时的最小落地路径（CMakeLists/sdkconfig.defaults/managed components）。
- `./esp32s3-n16r8-flash-psram-partition-lvgl-malloc-bringup-playbook.md`：ESP32-S3 N16R8 硬件基线迁移（QIO flash + OCT PSRAM + NimBLE 外部内存 + 自定义分区 + 必做清理/擦除）。
- `./esp-idf-littlefs-integration-playbook.md`：ESP-IDF 集成 LittleFS（依赖坐标 vs 组件名、分区镜像打包、挂载 API）。
- `./esp32s3-16mb-flash-8mb-psram-config-migration-playbook.md`：把工程迁移到 ESP32-S3 16MB Flash + 8MB PSRAM（Octal）基线（以 demo3 为参考，避免盲抄不开机）。
- `./partition-table-planning-16mb-flash-resources-storage-playbook.md`：16MB Flash 的分区表规划（`factory` 3MB + `resources` 4MB + `storage`，以及 label/重排失效等约束）。
- `./sdkconfig-defaults-playbook.md`：用 `sdkconfig.defaults` 固化关键配置，并验证实际生效值。
- `./sdkconfig-defaults-windows-gbk-decoding-pitfall.md`：Windows 上 `sdkconfig.defaults` 被按 GBK 读取导致 `UnicodeDecodeError`（避免中文注释/UTF-8 内容）。
- `./esp32s3-mspi-timing-unsupported-flash-psram-freq-combo-pitfall.md`：MSPI timing 表缺失：Flash/PSRAM 频率组合不受支持导致编译失败（先回到 80/80）。
- `./lvgl-kconfig-tick-custom-arduino-include-pitfall.md`：LVGL Kconfig tick 配置导致 `Arduino.h` 被 include 的坑（ESP-IDF 直接炸）。
- `./lvgl-version-api-mismatch-managed-component-pitfall.md`：LVGL 组件版本与 port API 不匹配（8.x vs 9.x）的坑。
- `./lvgl-font-kconfig-and-sdkconfig-defaults-pitfall.md`：字体未启用导致 `lv_font_montserrat_xx` 未定义，以及 defaults 改了不生效的真实问题。
- `./lvgl-memory-management-checklist.md`：LVGL 内存管理与稳定性清单（页面生命周期/样式/删除对象时机）。
- `./async-candidate-fetcher-seq-drop-stale-playbook.md`：异步候选请求（/pinyin、/suggest）用 `seq` 丢弃过期响应，防止乱序覆盖。
- `./http-streaming-utf8-and-lvgl-thread-bridge-playbook.md`：HTTP 流式文本的 UTF-8 边界处理 + 后台缓冲到 LVGL 线程安全渲染的桥接方式（本项目无 HTTP，作 reference 保留，桥接思路用于 service/manager 模式）。
- `./http-response-buffer-truncation-cjson-null-whitescreen-pitfall.md`：HTTP 响应被固定缓冲截断导致 `cJSON_Parse` 失败（白屏/崩溃）的高频坑位与修复路线（本项目无 HTTP，作 reference 保留）。
- `./lvgl-allocator-and-binfont-oom-hardening-playbook.md`：LVGL allocator 选择与 OOM 加固（本项目走 `LV_USE_CLIB_MALLOC=y` + Tiny TTF，不用 binfont——允许 binfont 章节仅作参考）。
- `./notify-manager-ringbuffer-version-playbook.md`：`notify_manager`（10 条环形缓冲 + version 去重 + BLE→UI 入队）稳定落地方案（回调不阻塞，UI 按需刷新）。
- `./toolchain-internal-compiler-error-pitfall.md`：GCC `internal compiler error` 的优先排查路径（先排工具链/IDF 配套）。
- `./werror-macro-and-deprecated-api-pitfall.md`：`-Werror` 下宏/弃用 API 的高频失败点与修复（含 `esp_check.h`、`esp_lcd_touch_get_data`）。
- `./kconfgen-idf-python-env-mismatch-pitfall.md`：`kconfgen` 崩溃（MenuNode/help 等）优先排查 IDF 与 Python env 混用。
- `./esp32-wifi-low-latency-tuning-playbook.md`：ESP32 Wi‑Fi 低延迟/高吞吐调参（禁用省电 + buffer/mailbox/IRAM 优化），适用于实时流场景（本项目走 BLE，作 reference 保留）。
- `./freertos-task-architecture-smell-checklist.md`：FreeRTOS 任务架构味道自检（优先级/栈/同步/分层），用于提前发现"能跑但会爆炸"的结构问题。

### 本项目专属（demo6）

- `./esp32s3-n16r8-qio-flash-oct-psram-pitfall.md`：N16R8 的 Flash 是 QIO 不是 OCT，`ESPTOOLPY_OCT_FLASH=y` 会 boot loop（必须保持 OFF）。
- `./sdkconfig-defaults-regen-pitfall.md`：改 `sdkconfig.defaults` 后必须删 `sdkconfig` 再 build，否则 defaults 不生效。
- `./nimble-mem-external-psram-playbook.md`：`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` 让 NimBLE 堆走 PSRAM，释放内部 RAM 给 DMA/LVGL。
