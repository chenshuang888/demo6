# 固件（ESP-IDF / FreeRTOS / 存储 / 并发）

> 本目录聚焦：**本项目（ESP32-S3 N16R8 + NimBLE 4.2 + LVGL 9.5）**的固件硬规则。
> 通用 ESP-IDF / LVGL / WiFi / HTTP 等经验已移到 `.trellis/spec/_general_library/firmware/`。

## 复用安全（必读）

- 复用前做 Fit Check：`../guides/spec-reuse-safety-playbook.md`

## 硬件基线（N16R8 板型）

- `./esp32s3-n16r8-qio-flash-oct-psram-pitfall.md`：**最重要**的板型坑 —— Flash 是 QIO 不是 OCT，`ESPTOOLPY_OCT_FLASH=y` 会 boot loop
- `./esp32s3-n16r8-flash-psram-partition-lvgl-malloc-bringup-playbook.md`：N16R8 完整硬件基线（QIO flash + OCT PSRAM + NimBLE 外部内存 + 自定义分区 + 烧录清理）
- `./sdkconfig-defaults-regen-pitfall.md`：改 `sdkconfig.defaults` 后必须删 `sdkconfig` 再 build
- `./nimble-mem-external-psram-playbook.md`：`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` 让 NimBLE 堆走 PSRAM

## 持久化（NVS）

- `./nvs-single-writer-contract.md`：NVS 持久化的"单写者"契约
- `./nvs-concurrency-pitfall.md`：多线程直接写 NVS 的典型坑
- `./nvs-persist-settings-store-layering-playbook.md`：本项目 `persist / settings_store / notify_manager` 的分层落地 + 写入策略（立即/周期/防抖）

## 本项目核心模块

- `./ble-conn-shared-state-to-avoid-component-cycle-playbook.md`：`ble_conn` 共享连接句柄，避免 `services` 反向依赖 `drivers` 组件循环依赖
- `./notify-manager-ringbuffer-version-playbook.md`：`notify_manager` 10 条环形缓冲 + version 去重 + BLE→UI 入队

## 任务架构

- `./freertos-task-architecture-smell-checklist.md`：FreeRTOS 任务优先级/栈/同步/分层 smell 自检（本项目每次加 service 或 task 前查一下）
