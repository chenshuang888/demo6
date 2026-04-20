# ESP32-S3 桌面伴侣 (Desktop Companion)

基于 ESP-IDF v5.4.3 + LVGL v9.5.0 的 ESP32-S3 触摸屏桌面伴侣。通过 BLE 与 PC（Python 工具）双向联动，实现时间同步、天气/通知推送、正在播放副屏、反向控制面板等一体化体验。

## 功能概览

| 页面 | 功能 | BLE 方向 |
|------|------|---------|
| 时间 | 实时时钟 + 手动调节（时/分、年/月/日） | PC → ESP (CTS 0x1805) |
| 菜单 | 页面导航、背光亮度设置、BLE 状态 | — |
| 天气 | 当前温度/最高最低/湿度/描述/城市 | PC → ESP (自定义 0x8a5c0001) |
| 通知 | 滚动列表，保存最近 10 条（类别+优先级） | PC → ESP (自定义 0x8a5c0003) |
| 音乐 | 正在播放曲目 + 艺术家 + 进度条 + 播放状态 | PC → ESP (自定义 0x8a5c0007) |
| 控制 | 屏上按钮触发 PC 动作（锁屏/静音/上下曲/播放暂停） | ESP → PC (自定义 0x8a5c0005) |
| 关于 | 版本信息 | — |

## 硬件配置

### 板型
- **MCU**: ESP32-S3 N16R8（Xtensa LX7 双核 @ 240MHz）
- **Flash**: 16MB, **QIO** @ 80MHz（不是 Octal！）
- **PSRAM**: 8MB, **Octal** @ 80MHz
- **LCD**: ST7789, 240×320, SPI @ 40MHz, RGB565
- **触摸**: FT5x06 电容屏, I2C @ 400kHz
- **蓝牙**: NimBLE 4.2（非 5.0）

> ⚠️ N16R8 的 Flash 是 QIO 而非 Octal，`CONFIG_ESPTOOLPY_OCT_FLASH` 必须保持 **OFF**，否则会因时序冲突进入 boot 循环。

### 引脚

| 外设 | 引脚 | 外设 | 引脚 |
|------|------|------|------|
| LCD_SCK | GPIO12 | TOUCH_SCL | GPIO18 |
| LCD_MOSI | GPIO11 | TOUCH_SDA | GPIO17 |
| LCD_CS | GPIO10 | TOUCH_RST | GPIO15 |
| LCD_DC | GPIO9 | TOUCH_INT | GPIO16 |
| LCD_RST | GPIO8 | LCD_BL | GPIO14 (LEDC PWM) |

引脚定义全部集中在 `drivers/board_config.h`。

## 软件架构

### 目录结构

```
demo6/
├── main/                     # 启动入口（app_main）
├── app/                      # 应用层
│   ├── app_main.c           # UI 任务创建 + 页面注册
│   ├── app_fonts.c          # Tiny TTF 字体初始化
│   ├── fonts/               # 嵌入 TTF（中文子集 ~3.4MB）
│   └── pages/               # 7 个页面（独立 screen + callbacks）
├── framework/                # 轻量页面路由器
│   └── page_router.c/h      # create/destroy/update 生命周期
├── drivers/                  # 硬件抽象层
│   ├── lcd_panel.c/h        # ST7789 + 背光 PWM
│   ├── touch_ft5x06.c/h     # FT5x06 I2C 触摸
│   ├── lvgl_port.c/h        # LVGL 移植（双缓冲 40 行）
│   └── ble_driver.c/h       # NimBLE 协议栈 + GAP
├── services/                 # 业务服务层
│   ├── persist.c/h          # NVS KV / blob 统一封装
│   ├── settings_store.c/h   # 背光 + 系统时间持久化
│   ├── ble_conn.c/h         # BLE 连接状态中转（避免循环依赖）
│   ├── time_service/manager       # Current Time Service
│   ├── weather_service/manager    # 天气推送
│   ├── notify_service/manager     # 通知推送（10 条环形缓冲 + 落盘）
│   ├── media_service/manager      # 正在播放（带 esp_timer 进度插值）
│   └── control_service            # ESP → PC 按钮事件（NOTIFY）
├── tools/                    # PC 端 Python 伴侣
│   ├── desktop_companion.py    # 音乐 + 控制合并版（winsdk）
│   ├── ble_time_sync.py        # 时间/天气/通知 GUI（CustomTkinter）
│   ├── control_panel_client.py # 仅控制面板
│   ├── media_publisher.py      # 仅音乐推送
│   ├── gen_font_subset.py      # TTF 子集生成
│   └── requirements.txt
├── docs/                     # 各阶段开发日志
├── partitions.csv            # 自定义分区（16MB）
└── sdkconfig.defaults
```

### 分层依赖

```
main
 └─► app ──┐
           │  page_router
           ├─► framework
           │
           ├─► drivers ──► services (ble_conn 中转)
           │   (ble_driver 上报连接状态)
           │
           └─► services
               persist → settings_store / notify_manager
               *_service (GATT, BLE host 线程)
                    └── *_manager (UI 线程单写)
```

关键约束：**drivers → services 单向依赖**。连接状态由 `ble_conn` 中转层托管，防止 services 反向依赖 drivers。

### Service / Manager 解耦模式

每个数据通道分为两半：

- **`*_service.c`** — GATT 接入层，跑在 NimBLE host 线程，只负责校验长度后 `push()` 入队。
- **`*_manager.c`** — 数据管理层，UI 线程独占消费（`process_pending`），更新快照、维护版本号。

BLE 回调从不阻塞、从不访问 LVGL、从不写 NVS。队列满时丢弃最旧数据（天气/通知/媒体越新越好）。对需要持久化的 manager（notify、settings），由 UI 线程按 dirty + 防抖策略 `tick_flush` 单写落盘，避免多线程写 NVS。

## 关键技术

### 中文字体 —— Tiny TTF 运行时渲染

LVGL 自带的 CJK 字库仅 1118 字，覆盖不全。项目改为：
- 用 `tools/gen_font_subset.py` 扫描源码提取用到的汉字，生成 `srhs_sc_subset.ttf`（~3.4MB）
- 通过 `EMBED_FILES` 嵌入固件镜像
- 启动时 `lv_tiny_ttf_create_data_ex` 创建两个字体（14px 正文 / 16px 标题）
- Fallback 链挂 `lv_font_montserrat_14/20`，解决 FontAwesome 图标（`LV_SYMBOL_*`）不在 CJK 子集里的问题
- 开启 `LV_USE_CLIB_MALLOC` 让大块 glyph cache 自动落到 PSRAM

### 内存布局

- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` — 小分配走内部 RAM（性能），大块自动进 PSRAM
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` — 保留 32KB 给 DMA / ISR 等不能用 PSRAM 的场景
- `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` — BLE 堆走 PSRAM，释放内部 RAM
- LVGL 双缓冲：240×40×2×2 ≈ 38KB

### 分区表（16MB）

```
nvs       0x9000    24KB        WiFi/BT 配对、settings、notifications 快照
phy_init  0xf000    4KB         RF 校准
factory   0x10000   6MB         主应用（含嵌入 TTF）
storage   auto      ~9.9MB      SPIFFS 预留（未来字体/OTA）
```

## 快速开始

### 编译固件

```bash
# 1. 激活 ESP-IDF 环境
. $HOME/esp/esp-idf/export.sh            # Linux/macOS
%USERPROFILE%\esp\esp-idf\export.bat    # Windows

# 2. 编译
idf.py set-target esp32s3
idf.py build

# 3. 烧录（Windows）
idf.py -p COM3 flash monitor
```

> 修改 `sdkconfig.defaults` 后必须删除 `sdkconfig` 再重新编译——ESP-IDF 只在 `sdkconfig` 缺失时才从 defaults 生成。

### PC 端工具

```bash
cd tools
pip install -r requirements.txt

# 一体化（推荐）：音乐副屏 + 控制面板
python desktop_companion.py

# 单独：时间同步 / 天气 / 通知 GUI
python ble_time_sync.py
```

依赖：`bleak`（BLE）、`customtkinter`（GUI）、`winsdk`（Windows 媒体会话，仅 Win）、`requests`（天气 API）。

### 重新生成中文字体子集

当新增中文文案时：

```bash
python tools/gen_font_subset.py
idf.py build   # 重新嵌入
```

脚本会扫描源代码，提取所有出现的汉字，生成最小子集 TTF，大幅压缩固件体积。

## BLE 协议约定

所有自定义特征值的 payload 均为 `__attribute__((packed))` 结构体，PC 端 Python 用 `struct.pack` 严格对齐：

| UUID 片段 | 方向 | 结构 | 字节 |
|-----------|------|------|------|
| 0x2A2B (标准 CTS) | PC → ESP | `<HBBBBBBBB` | 10 |
| 8a5c0002 weather | PC → ESP | `<hhhBBI24s32s` | 68 |
| 8a5c0004 notify | PC → ESP | `<IBB2x32s96s` | 136 |
| 8a5c0008 media | PC → ESP | `<BBhhHI48s32s` | 92 |
| 8a5c0006 control | ESP → PC | `<BBBBhH` | 8 (NOTIFY) |

设备广播名：**ESP32-S3-DEMO**。

## 调试技巧

**查看 LVGL 内存**：
```c
lv_mem_monitor_t mon;
lv_mem_monitor(&mon);
ESP_LOGI(TAG, "LVGL mem: %u/%u  frag=%d%%",
         mon.used_size, mon.total_size, mon.frag_pct);
```

**擦 NVS 复位**：
```bash
idf.py -p COM3 erase-flash
```

**强制配对重来**：通过 `persist_erase_namespace("nimble_bond")` 或直接 `erase-flash`。

## 已知限制 / 待优化

- 通知页面长列表滚动偶发掉帧（LVGL 9.5 已知回归）
- 音乐进度条依赖 PC 端 10s 兜底推送；短于 1s 的 seek 可能看不到
- 触摸灵敏度未做校准，依赖 FT5x06 出厂默认

## 依赖

| 组件 | 版本 | 用途 |
|------|------|------|
| ESP-IDF | v5.4.3+ | SDK |
| lvgl/lvgl | ^9.0.0 | GUI |
| espressif/esp_lcd_touch_ft5x06 | ^1.0.0 | 触摸驱动 |
| NimBLE (内置) | — | BLE 协议栈 |
| bleak | ≥0.22 | PC 端 BLE |
| customtkinter | 5.x | PC 端 GUI |
| winsdk | ≥1.0.0b10 | Windows 媒体会话（可选） |

## 版本历史

- **v0.6 (2026-04-20)** — TTF 运行时渲染中文，取代 LVGL CJK 内置字库
- **v0.5** — Media service（正在播放副屏）、Control service（反向按钮事件）、desktop_companion 合并客户端
- **v0.4** — NVS 持久化：背光、系统时间、通知环形缓冲
- **v0.3** — Notification service + 通知页面，UI 布局/逻辑分离
- **v0.2** — Weather service + 天气页面，PC 端 CustomTkinter GUI
- **v0.1** — 驱动移植：ST7789 + FT5x06 + LVGL 9.x，时间调节页面

详细过程见 `docs/` 目录各阶段日志。

## 许可证

仅供学习参考。
