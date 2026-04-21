# Playbook：动态 App 开发工作流与约束（编译→注册→上传→运行）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-OLED-----.md`）中 DynamicLink 工具链、编译 GUI、创建新 App 步骤与约束表。
>
> 目标：让“写一个新动态 App 并跑起来”具备稳定流程，并把最容易踩的限制（8KB/固定地址/无标准库/按键事件枚举）写成明确清单。

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


## 关键约束（先记住这几个就能少踩 80% 坑）

- **代码 + 全局数据总大小**：< 8KB
- **装载 RAM 地址固定**：`0x20002000`
- **标准库不可用**：不依赖 `printf/malloc` 等（按对话约束）
- **按键事件枚举**：8 种
  - `UP_SHORT/UP_LONG`
  - `DOWN_SHORT/DOWN_LONG`
  - `OK_SHORT/OK_LONG`
  - `BACK_SHORT/BACK_LONG`
- **每 App 独立数据存储空间**：约 16KB/App（对话描述：每个 App 独立 Flash 空间）

> 与链接脚本的硬约束配套：见 `spec/iot/guides/stm32-dynamic-app-linker-script-contract.md`。

---

## 目录与产物约定（语义层）

- 源码：`tools/DynamicLink/src/*.c`
- 链接脚本：`tools/DynamicLink/config/app.ld`
- 编译工具：`tools/DynamicLink/tools/app_compiler_gui.py`（或 `.bat` 启动）
- 编译产物：
  - `.elf`：中间产物
  - `.bin`：最终产物（用于上传）
- App 清单：`tools/apps/app_library.json`
  - 编译工具可自动注册新 App 条目

---

## 创建新 App 的推荐步骤

1. 在 `tools/DynamicLink/src/` 新建 `my_app.c`
   - 参考 `hello_app.c`（最小模板）或现有示例 App
2. 运行编译工具（GUI 或 `.bat`）
   - 选择 `my_app`
   - 点击“编译选中的 App”
3. 产物生成与注册
   - 自动生成 `tools/apps/my_app.bin`
   - 自动更新 `tools/apps/app_library.json`
4. 上传到 MCU
   - 打开 PC 端上传工具
   - 选择串口、选择 `my_app.bin`、选择槽位（0-7）
5. MCU 端运行
   - 进入“动态 App 列表”
   - 选择已安装 App，宿主加载到 `0x20002000`
   - 调用 `app_init/app_update/app_draw/app_on_key`（或等价生命周期）

---

## 编译参数建议（与 8KB 目标一致）

对话中给出的关键编译选项（示例）：

- `-O2`：开启优化（体积/性能平衡点）
- `-ffunction-sections -fdata-sections`：配合链接器做段裁剪
- `-nostdlib -nostartfiles`：避免引入标准库/启动代码
- `-T app.ld`：强制使用动态 App 的链接脚本

---

## 验收标准

- 编译通过，`bin` 生成成功且已写入 `app_library.json`
- `arm-none-eabi-size` 显示总占用 < 8KB
- 上传后能在 MCU 端进入、响应按键、正常绘制（无随机 HardFault）

