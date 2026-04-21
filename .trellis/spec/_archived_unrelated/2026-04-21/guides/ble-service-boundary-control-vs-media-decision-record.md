# Decision Record：`control_service` 与 `media_service` 是否合并（建议不合并）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中用户提出“control/media 是否同一业务、能否合并”，以及给出的语义边界判断与折中方案。
>
> 目标：避免“为了少一个文件/少一个 UUID”做出错误合并，导致协议语义混乱、后续扩展反而更痛苦。

---

## 结论

**建议不合并。**

---

## 关键理由（对话中的语义边界）

- `control_service` 的语义是“输入设备事件通道”（ESP → PC，Notify）：
  - 当前按钮包含 Lock/Mute 等系统级操作，并不只服务音乐
  - 未来还可能扩展亮度、快捷键面板、窗口切换等
- `media_service` 的语义是“媒体信息展示通道”（PC → ESP，Write）：
  - 负责曲目/歌手/进度等显示数据

两个方向、职责、寿命都不同，合并会让抽象变糊。

---

## 如果强烈想合并（折中）

可以合成一个 `companion_service`（单个 Service UUID），但保留两个 characteristic：

- 一个 `control_event`（Notify，ESP → PC）
- 一个 `media_payload`（Write，PC → ESP）

这样只是“BLE 注册层面的收拢”，业务逻辑仍是两套；收益通常很小。

