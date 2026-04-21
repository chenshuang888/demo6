# Playbook：新建 ESP32-S3 + LVGL 工程并接入移植的 `drivers/`（保证可编译）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）从“只移植了 LVGL 相关 drivers 文件夹”到“工程能编译”的完整落地路径。
>
> 目标：下次遇到同样场景（新建工程 + 仅拷贝 drivers）时，不再在 `CMakeLists`/依赖名/managed component 上反复踩坑。

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


## 适用范围

- 新建 ESP-IDF 工程（ESP32-S3）
- 从旧项目移植了一个 `drivers/` 文件夹，里面包含：
  - LCD 初始化
  - Touch 初始化
  - LVGL port（`lvgl_port_init()`、`lvgl_port_task_handler()` 等）
- 希望“你自己编译时能直接过”（AI 不替你跑 build）

---

## 最小必需文件改动清单

### 1) `sdkconfig.defaults`

用途：给“新生成的 sdkconfig”提供默认值（注意：不会自动覆盖已有 `sdkconfig`）。

对话里首先落的“最小必要项”是增大 main task 栈，避免 LVGL 初始化/渲染栈不够：

- `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`
- `CONFIG_MAIN_TASK_STACK_SIZE=8192`

> 若工程里已存在 `sdkconfig`：改 defaults 后要 `idf.py reconfigure` 才会生效。

### 2) 把 `drivers/` 注册成 ESP-IDF 组件：`drivers/CMakeLists.txt`

目标：

- 把 `drivers/*.c` 编译进来
- 把 drivers 依赖声明完整（缺一个头文件组件就会炸）

对话中出现过的依赖点（按“真实 include 用到啥就 requires 啥”）：

- `esp_lcd`
- `lvgl`
- `esp_timer`（`esp_timer.h`）
- **驱动组件名按 IDF 版本选**：见 `spec/iot/firmware/esp-idf-component-names-by-version-pitfall.md`
- 触摸（若 IDF 不自带）：见下方 `idf_component.yml`

### 3) 根 `CMakeLists.txt`：把组件目录加进扫描路径

```cmake
set(EXTRA_COMPONENT_DIRS drivers)
```

### 4) `main/CMakeLists.txt`：声明 main 组件依赖

- 至少依赖 `drivers`
- 需要持久化/系统组件时再加 `nvs_flash`（纯 UI demo 不强制）

### 5) `main/idf_component.yml`：用组件管理器引入外部依赖（LVGL/触摸）

当工程里没有 `components/lvgl` 时，用 managed component 是最快路径：

```yml
dependencies:
  lvgl/lvgl: "^9.0.0"
```

如果你的 IDF 版本不自带触摸组件（对话里在 5.2.6 遇到过），再加：

```yml
dependencies:
  espressif/esp_lcd_touch: "^1.2.1"
  espressif/esp_lcd_touch_ft5x06: "^1.1.0"
```

> 若你必须离线编译：把这些组件源码放到 `components/`，不要依赖 managed component 拉取。

### 6) `main/main.c`：最小 UI demo（按钮点击计数 +1）

推荐结构（语义）：

- 初始化必要底座（如 `lvgl_port_init()`）
- 创建 label + button
- UI 逻辑尽量放 UI 任务里（避免 main 线程长期跑 UI）

关于“UI 任务 vs main 线程”的落地建议，可配合：

- `spec/iot/device-ui/lvgl-page-router-minimal-contract.md`

---

## 高概率踩坑清单（强制先排）

- tick 自定义导致 `Arduino.h`：见 `spec/iot/firmware/lvgl-kconfig-tick-custom-arduino-include-pitfall.md`
- LVGL 主版本与 port API 不匹配（8 vs 9）：见 `spec/iot/firmware/lvgl-version-api-mismatch-managed-component-pitfall.md`
- 字体未启用导致 `lv_font_montserrat_xx` 未定义：见 `spec/iot/firmware/lvgl-font-kconfig-and-sdkconfig-defaults-pitfall.md`

---

## 验收标准

- `idf.py reconfigure` 后 CMake 可配置完成（无 missing component）
- `idf.py build` 通过（无 Arduino.h / LVGL API 不匹配 / 缺字体符号）
- 屏幕能显示 UI，点击按钮数字 +1（基本交互链路跑通）

