# AI 智能交互手表｜详细技术文档（固件 + 上位机 + 语音链路）

这份文档面向“想看懂/想二次开发”的读者：按 **系统结构 → 关键数据结构 → 协议与数据流 → 运行时机制 → 开发扩展点** 的顺序，把这个大项目的亮点串起来。

如果你只想快速跑起来，请看根目录 `README.md`。

---

## 1. 总体架构（端到端）

这个项目不是单一 MCU 固件，而是一个“完整系统”：

```
                 ┌───────────────────────────────┐
                 │           PC 用户端            │
                 │  Web(静态Vue) + Node(串口服务) │
                 │  - 应用商店/安装/卸载          │
                 │  - OTA 文件夹扫描 + 自动更新   │
                 │  - DeepSeek function calling  │
                 └───────────────┬───────────────┘
                                 │  USART3 @ 921600
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                       STM32F103 主控（Application）              │
│  UI 框架 + 页面系统 + 动态App容器 + 存储/OTA + 串口协议栈         │
│                                                                 │
│  串口栈：Transport(DMA+IDLE) → Parser(CMD路由) → Dialog(超时) → 业务│
│                                                                 │
│  语音透传：USART1(ASRPRO) ↔ USART3(PC)（透传模式）               │
└───────────────┬─────────────────────────────────────────────────┘
                │  USART1 @ 921600
                ▼
      ┌──────────────────────────┐
      │     ASRPRO 语音模块       │
      │  唤醒词/录音/播放 OUTSIDE │
      └──────────────────────────┘
```

核心设计思想：**STM32 既是 UI 设备，也是“协议路由器 + 语音数据中继”**。PC 端承担“重活”（商店/AI/云端识别/合成）。

---

## 2. 固件部分（application部分）

### 2.1 启动与页面注册

主入口：`application部分/Core/Src/main.c`

初始化顺序（高度体现“分层架构”）：

1. HAL/CubeMX 外设初始化（GPIO/TIM/USART1/USART3）
2. **Transport_Init / Parser_Init**：串口搬运层 & 解析层上线
3. Key/UIEvent/UI 初始化
4. UI 页面注册（表盘/菜单/秒表/倒计时/闹钟/设置/动态App/商店/系统更新等）
5. W25Q64 初始化 + AppStorage / OTA_Storage 初始化
6. `OTA_Storage_BackupRunningFW()`：自动备份“当前运行固件”到 OTA Slot 0（版本一致会跳过）
7. `Service_Init()`：业务层注册 CMD handler
8. `SimpleRunner_Init()`：动态 App 运行器初始化（对外仍叫 SimpleRunner，内部转发到 NativeRunner）
9. `scheduler_init()`：启动调度器；主循环中 `scheduler_run()` 驱动各模块 tick
10. `OTA_Storage_ConfirmFirmware()`：通知 Bootloader 本固件已成功启动（用于回滚机制）

### 2.2 UI 框架：页面栈 + 生命周期

页面接口约定（见 `application部分/Myapp/Core/ui_page.h` 与 `application部分/docs/README_PROJECT.md`）：

- `init`：首次进入前调用一次
- `enter`：每次进入页面调用
- `update`：逻辑更新（按帧）
- `draw`：渲染（按帧）
- `key_event`：按键事件
- `exit`：退出页面

框架亮点：

- **页面栈**：天然支持“返回上一页 / 回主页”
- **事件驱动**：Key 驱动与 UI 解耦（防抖/长短按识别后再映射为 UI 事件）
- **可扩展控件库**：列表项、标题栏、进度条等（见 `application部分/docs/UI_FRAMEWORK_README.md`）

### 2.3 动态 App（原生机器码加载）

当前实现路线是“原生机器码动态加载”，关键点：

- **加载地址**：RAM 固定 `0x20002000`（见 `application部分/Myapp/VM/native_runner.c`）
- **大小限制**：最大 8KB
- **入口约定**：App 前 16 字节是 4 个函数指针（init/update/draw/on_key）
- **系统 API**：固件在 RAM 固定地址 `0x20000100` 放置 `AppAPI_t` 函数指针表（见 `application部分/Myapp/DynamicLink/app_api.c` 与 `application部分/tools/DynamicLink/README.md`）

为什么这是亮点：

- 不需要解释器 → 性能接近“内置应用”
- 固定地址 API 表 → App 无需重定位/无需复杂链接脚本处理（更像“插件”）
- 外部 Flash 存储槽位 → 安装/卸载简单，配合商店可扩展

> 历史上曾设计过“字节码虚拟机”，文档保留在 `application部分/docs/虚拟机系统README.md` 等；目前 `SimpleRunner_*` API 对外保持不变，内部已转发到 `NativeRunner_*`（见 `application部分/Myapp/VM/simple_runner.c`）。

---

## 3. Bootloader（bootloader部分）：OTA + 回滚保护

Bootloader 入口：`bootloader部分/Core/Src/main.c`

### 3.1 内部 Flash 分区（STM32 64KB）

- Bootloader：`0x08000000` ~ `0x08001FFF`（8KB）
- Application：`0x08002000` ~ `0x0800FFFF`（56KB）

Application 会设置 `SCB->VTOR = 0x08002000` 以重定向中断向量表。

### 3.2 外部 Flash 分区（W25Q64 8MB）

（以 Bootloader 注释为准）

- APP 槽位区：`0x000000` ~ `0x0FFFFF`
- OTA 固件槽区：`0x100000` 起，每槽位 64KB（元数据 4KB + 数据 60KB）
- 配置区：`0x7FFC00`（W25Q64 末尾 1KB）用于 OTA 标志、激活槽位、回滚检查标志、重试计数

### 3.3 回滚机制（IWDG + Confirm）

启动新固件后：

- Bootloader 置“回滚检查标志”，并在“首次启动窗口”启用 IWDG
- Application 初始化完成后调用 `OTA_Storage_ConfirmFirmware()` 清除检查标志
- 若新固件崩溃/卡死导致 IWDG 复位 → Bootloader 增加重试计数 → 超过阈值后自动回滚到备份固件（Slot 0）

这是典型“生产级 OTA”思路：**先把新固件跑起来并让它自己确认**。

---

## 4. 串口协议与通信架构（MCU ↔ PC）

### 4.1 帧格式

双端统一帧格式（见 `application部分/docs/串口通信框架详解.md` 与 `application部分/用户端/server/protocol.js`）：

```
| 0x55 0xAA | CMD (1B) | payload (0~256B) | 0xA5 0x5A |
```

响应约定：payload 首字节为 `STATUS`（0=OK，1=ERR），随后为 data。

### 4.2 MCU 侧：四层架构（Transport/Parser/Dialog/Services）

强烈建议阅读：`application部分/docs/ARCHITECTURE.md`

关键设计要点：

- Transport：DMA Circular RX + UART IDLE 断帧（大流量下载也不“中断风暴”）
- Parser：帧头/帧尾校验 + CMD 注册表分发（幂等注册）
- Dialog：为“多帧交互”提供统一超时管理（全局单对话槽位，契合单片机模型）
- Services：APP/OTA 上传、槽位管理、商店客户端、时间同步、AI 单向命令等

### 4.3 语音透传模式（USART1 ↔ USART3）

实现文件：`application部分/Myapp/Protocol/transport.c`

机制：

- 正常模式下，Transport 在 USART1 侧检测到 **2 字节唤醒信号 `0xAA 0xBB`** 时：
  1. 把 `0xAA 0xBB` 转发给 PC（USART3）
  2. 立即切换为 **透传模式**（TP_MODE_PASSTHROUGH）
- 透传模式下：
  - `IDLE` / `DMA Half` / `DMA Full` 回调都会触发 `_Passthrough_Forward()`，把新到数据从一端 DMA 缓冲“线性化”后发到另一端
  - 超时无数据则回到正常模式

这使得 STM32 能在不改动 PC 脚本/语音模块协议的情况下，充当“硬件级的全双工中继”。

---

## 5. PC 用户端（Web + Node 串口服务）

目录：`application部分/用户端/`

### 5.1 Node 后端

目录：`application部分/用户端/server/`

职责：

- 串口管理与帧解析（`serial_manager.js`）
- 协议编解码（`protocol.js`，与 MCU `protocol.h` 对称）
- 统一上传（`upload_service.js`，Start→Data→End，APP/OTA 共用）
- 应用商店服务（`store_service.js`）
- OTA 服务（`ota_service.js`：扫描固件目录、版本检查、MCU 请求更新时自动上传）
- AI Agent（`ai_agent.js`）：DeepSeek function calling → tool_calls → 下发串口命令

### 5.2 Web 前端

目录：`application部分/用户端/web/`

特性：

- 连接串口、查看已装应用、卸载
- 应用商店浏览/安装（内置进度条）
- AI 对话 UI（显示 tool_call 气泡，便于调试模型的“动作决策”）

---

## 6. 语音交互（ASRPRO + PC 脚本）

### 6.1 脚本与用途

- 一句话识别：`语音交互部分/音频识别/serial_realtime_asr.py`
- 串口播放（TTS → PCM → 模块播放）：`语音交互部分/音频播放/tts_serial_play.py`
- 完整对话：`语音交互部分/语音对话/voice_assistant.py`

### 6.2 处理流水线（PC 端）

以 `voice_assistant.py` 为例，核心链路：

1. 串口接收 32kHz PCM（模块输出）
2. 降采样到 16kHz（云识别与播放统一采样率）
3. 高通滤波（去低频噪声）
4. VAD 分句（能量比阈值 + 静默判定 + pre-roll）
5. 整句降噪（OLA 谱减法）+ 归一化
6. 腾讯云一句话识别（ASR）
7. DeepSeek 对话生成（LLM，可选）
8. Windows TTS 合成 → 重采样到 16kHz → **对齐到 256 样本倍数**
9. 握手后把 PCM 流式发回模块播放

相关的“踩坑复盘/根因分析”在 `语音交互部分/文档存放/` 中非常完整，尤其是：

- `ASRPRO_OUTSIDE播放开发踩坑记录.md`
- `ASRPRO_串口音频播放_调试记录.md`
- `噪音问题根因分析.md`

### 6.3 开源密钥管理

本仓库已将 Tencent/DeepSeek Key 改为 **环境变量读取**：

- `TENCENT_SECRET_ID`
- `TENCENT_SECRET_KEY`
- `DEEPSEEK_API_KEY`

未配置 DeepSeek Key 时，`voice_assistant.py` 会回退为“复述你说的话”的离线 Demo，便于开源演示与快速验证链路。

---

## 7. 扩展点清单（最适合二次开发的地方）

### 固件

- 新页面：按 `PageDescriptor_t` 模板新增，并在 `application部分/Core/Src/main.c` 注册
- 新串口命令：在 `application部分/Myapp/Protocol/protocol.h` 增加 CMD；在对应 Service `Parser_Register()` 注册处理器
- 新动态 App 能力：在 `AppAPI_t` 增加系统调用，并在 `application部分/tools/DynamicLink` 里同步暴露

### 上位机

- 扩展商店：扩展 `application部分/tools/apps/app_library.json` 元数据；增加更多 `.bin` App
- 增加手表“状态上报”：让 MCU 主动上报当前页面/亮度/电量等，AI 就能做“相对控制”（例如“调暗一点”）

---

## 8. 进一步阅读（文档地图）

固件文档（`application部分/docs/`）量很大，但质量很高，建议按顺序：

1. `README_PROJECT.md`（总览 + 内存分区 + OTA + 商店 + 协议）
2. `UI_FRAMEWORK_README.md`（UI 框架用法）
3. `串口通信框架详解.md` / `串口通信四层架构 — 数据流全链路详解.md`（通信体系）
4. `ai_cmd_impl.md` + `deepseek_ai_agent.md`（AI 控制链路）
