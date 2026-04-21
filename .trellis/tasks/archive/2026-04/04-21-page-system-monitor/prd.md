# PRD：PC 系统监控卡片

## 背景 / 目标

在设备上新增一个"系统监控"页面，显示 PC 端的 CPU / 内存 / 磁盘 / 电池 / 上下行网速 / CPU 温度 / 开机时长。与现有 weather / notify / media 一样走 "PC WRITE → ESP" 模式，完全复用 service/manager 解耦和奇偶 UUID 配对规则。

## BLE 协议

- Service UUID: `8a5c0009-0000-4aef-b87e-4fa1e0c7e0f6`
- Char UUID: `8a5c000a-0000-4aef-b87e-4fa1e0c7e0f6`（WRITE）
- 按 `spec/iot/protocol/ble-custom-uuid-allocation-decision-record.md` 奇偶规则分配

## Payload 结构（16 字节，`<BBBBBBhIHH` packed）

| 字段 | 类型 | 语义 |
|------|------|------|
| cpu_percent | u8 | CPU 0-100 |
| mem_percent | u8 | 内存 0-100 |
| disk_percent | u8 | 磁盘（C 盘）0-100 |
| battery_percent | u8 | 电池 0-100；255 = 无电池 |
| battery_charging | u8 | 0=未充电 / 1=充电中 / 255=无电池 |
| _reserved | u8 | 对齐 |
| cpu_temp_cx10 | i16 | CPU 温度 ×10；-32768 = 不可用 |
| uptime_sec | u32 | PC 开机秒数 |
| net_down_kbps | u16 | 下行 KB/s |
| net_up_kbps | u16 | 上行 KB/s |

## REQUEST id

按 `esp-to-pc-notify-request-pattern-playbook.md` 规则，control_service 增加：
- `CONTROL_REQUEST_SYSTEM = 2`

进入 `page_system` 时自动发一次，触发 PC 立即推送一帧（打破常规 2 秒间隔）。

## 文件清单

**新增：**
- `services/system_service.{c,h}` — GATT 接入
- `services/system_manager.{c,h}` — UI 线程消费
- `app/pages/page_system.{c,h}` — UI

**修改：**
- `services/CMakeLists.txt` — 加源文件
- `drivers/ble_driver.c` — 调 `system_service_init()`
- `framework/page_router.h` — 加 `PAGE_SYSTEM`
- `app/app_main.c` — 注册 page + init manager
- `app/pages/page_menu.c` — 加菜单入口 "System"
- `services/control_service.h` — 加 `CONTROL_REQUEST_SYSTEM`
- `tools/desktop_companion.py` — 加 `publish_system_info` 协程（psutil）
- `tools/requirements.txt` — 确认 psutil

## UI 布局（240×320）

```
┌────────────────────────────┐
│ < Back        System       │  40px
├────────────────────────────┤
│ ┌──────────────────────┐   │
│ │ CPU           68%    │   │
│ │ ████████████░░░░░░   │   │
│ │ MEM           54%    │   │
│ │ ██████████░░░░░░░░   │   │   ~180px 进度卡
│ │ DISK          78%    │   │
│ │ ███████████████░░░   │   │
│ │ BAT   52% ⚡         │   │
│ │ ██████████░░░░░░░░   │   │
│ └──────────────────────┘   │
│ ┌──────────────────────┐   │
│ │ Uptime   12h 34m     │   │
│ │ ↓120 ↑8 KB/s         │   │   ~80px 信息卡
│ │ CPU Temp  65.2°C     │   │
│ └──────────────────────┘   │
└────────────────────────────┘
```

配色沿用项目深紫 + 青绿；进度条超过 80% 时变橙色警告（CPU/MEM/DISK）。

## 验收

- PC 端运行 `desktop_companion.py` 后 ESP 侧菜单 → System，进入后 2 秒内第一屏数据填满
- 数值每 2 秒更新，数字有变化时刷新
- 页面退出后 publisher 继续后台推（不需要暂停）
- 未连接时页面显示占位 "--"，不崩
- CPU 温度不可用（部分 PC 读不到）时显示 "--"

## 实现风险 / 停手

- psutil 在 Windows 某些虚拟机读不到 CPU 温度——已用 i16 -32768 做无效哨兵
- 2 秒间隔如果和 media_publisher 同时推会挤 BLE 带宽——system_payload 16 字节远小于 media 92 字节，实测风险低
- PC 端磁盘遍历慢——只取 C 盘 fixed，非 `psutil.disk_partitions()` 遍历
