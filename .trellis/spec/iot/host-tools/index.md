# 主机端工具（Python / bleak / winsdk / customtkinter）

> 本目录聚焦：**本项目** PC 端（`tools/desktop_companion.py` + `tools/ble_time_sync.py`）的 Windows BLE 集成要点。
> 通用 Tkinter / DPI 踩坑已移到 `.trellis/spec/_general_library/host-tools/`。

## 复用安全（必读）

- 复用前做 Fit Check：`../guides/spec-reuse-safety-playbook.md`

## 文档

- `./desktop-companion-bleak-multiplex-playbook.md`：**本项目核心** —— `desktop_companion.py` 一连接多通道服务端（media-btn notify + CTS/weather/system req + media/notify/system write）
- `./windows-smtc-winsdk-media-session-integration-playbook.md`：Windows SMTC（winsdk）媒体会话集成（QQ 音乐网页版坑位、事件去重、requirements 版本）
