# 主机端工具（PC/Web/Electron/串口）

> 本目录聚焦：PC 端 BLE/串口、GUI、协议对齐与主机侧 Windows 兼容性。
>
> 本项目 PC 端使用 `bleak`（BLE）+ `customtkinter`（GUI）+ `winsdk`（Windows 媒体会话）构建桌面伴侣 `tools/desktop_companion.py` 及 `tools/ble_time_sync.py`。

## 复用安全（必读）

- 复用任何条目前先做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（路径/模块名多为示例，先做角色映射再复用）

## 文档

- `./windows-tkinter-dpi-awareness-pitfall.md`：Windows 高 DPI 缩放下 Tkinter 窗口"内容被挤出/按钮消失"的根因与标准修复（声明 DPI awareness）。
- `./customtkinter-sidebar-navigation-playbook.md`：用 CustomTkinter + 侧边栏导航重构"一屏堆叠"的脚本 GUI（Home/Time/Weather/Notify 分页）。
- `./windows-smtc-winsdk-media-session-integration-playbook.md`：Windows SMTC（winsdk）媒体会话集成：兼容性边界、QQ 音乐网页版坑位、事件去重、requirements 版本约束。
- `./desktop-companion-bleak-multiplex-playbook.md`：`desktop_companion.py` 一连接承载 control notify + media write（`--dry-run`、自动重连、payload 去重）。
- `./utf8-fixed-size-truncate-pitfall.md`：二进制协议固定长度字段的 UTF-8 安全截断（避免 `�`）。
