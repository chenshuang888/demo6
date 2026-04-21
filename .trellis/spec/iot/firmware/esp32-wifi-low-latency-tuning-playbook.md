# Playbook：ESP32 Wi‑Fi 低延迟/高吞吐调参（UDP/TCP 实时流场景）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo1.md`）中的 Wi‑Fi/LwIP 调参建议清单（实时屏幕流/音视频/控制链路共通）。

---

## 上下文签名（Context Signature，必填）

- 目标平台：ESP32/ESP32‑S3 等（ESP‑IDF 5.x）
- 场景：实时流（UDP 分片帧、TCP JPEG 帧流、低延迟交互/控制）
- 风险等级：中到高（会显著增加内存占用与功耗；调错会引发 OOM/卡顿/不稳定）
- 观测手段：必须能记录吞吐、丢包、端到端延迟/抖动（否则不要盲调）

---

## 不变式（可直接复用）

- 先基线 → 再最小改动 → 再逐项加：避免“改多了不知道谁生效”
- 没有证据不要乱调：调参要能回滚，要能对比
- 实时链路优先：为“可预测延迟”牺牲部分省电/部分极限内存

---

## 参数清单（必须由当前项目提供/确认）

- 目标指标：端到端延迟上限/抖动上限/帧率/吞吐目标
- 链路形态：UDP/TCP 比例、包大小、发送节奏、是否存在突发
- 内存预算：可接受的 RAM/IRAM 额外占用上限（否则别把 buffer 拉满）
- 省电策略：是否允许禁用 Wi‑Fi 省电（`WIFI_PS_NONE`）
- 回滚策略：当 OOM/不稳定时回退顺序与最小可用配置

---

## 停手规则（Stop Rules）

- 你没有“基线数据”（延迟/丢包/吞吐）就不要改 5+ 项参数
- 你不知道当前工程是否真的有高频包/突发，就不要把 mailbox/buffer 一把拉满
- 修改后无法观测指标变化，就不要继续叠参数（先补观测）

---

## 目标

在“实时流 + 交互”的场景下（如无线 KVM、UDP 帧流、TCP JPEG 帧流）获得更稳定的吞吐与更低的抖动：

- 减少因为省电/缓冲不足导致的延迟尖刺
- 让 lwIP mailbox 不因为峰值入包被打爆

---

## 前置条件

- ESP-IDF 5.x
- 你的工程确实在跑高频网络包（否则不建议上来就把 buffer 全拉满）

---

## 设计边界（别过度调参）

- 这是“实时链路”专用配置；它会增加内存占用与功耗。
- 没有证据（吞吐/丢包/时延）不要乱改一堆；先改最关键的 2-3 项并验证。

---

## 实施步骤（建议顺序）

### 1) 先禁用 Wi‑Fi 省电（低延迟必做）

在成功连上 STA 后调用：

- `esp_wifi_set_ps(WIFI_PS_NONE);`

否则容易出现“延迟不稳定 / 首包慢 / 抖动大”。

### 2) 调整 Wi‑Fi buffer（最关键）

按对话建议的方向（示例值，按内存与实际压测再微调）：

- `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=16`
- `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=64`
- `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64`

### 3) AMPDU 窗口增大（吞吐稳定性）

- `CONFIG_ESP_WIFI_RX_BA_WIN=32`
- `CONFIG_ESP_WIFI_TX_BA_WIN=32`

### 4) lwIP mailbox（UDP/TCP 高峰入包）

- `CONFIG_LWIP_UDP_RECVMBOX_SIZE=64`
- `CONFIG_LWIP_TCP_RECVMBOX_SIZE=64`
- `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=64`

并可提高 TCPIP 任务优先级（对话建议）：

- `CONFIG_LWIP_TCPIP_TASK_PRIO=23`

### 5) IRAM 优化（可选，但对 RX 有帮助）

若你对 IRAM 预算有余量，可考虑开启：

- `CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=y`（会增加约 7.4KB IRAM，占用换性能）

### 6) CPU 频率（可选）

对话建议在极限性能需求下使用：

- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`

---

## 验证顺序（必须可执行）

1) 基线：在不调参时跑 1 次你的关键场景（记录吞吐/丢包/端到端延迟）
2) 最小改动：只做“禁用省电 + buffer 调整”，复测
3) 再逐项加入：AMPDU → lwIP mailbox → IRAM → CPU，避免“改多了也不知道谁生效”

---

## 常见问题（快速排障）

- 现象：吞吐很高但交互延迟尖刺严重
  - 优先检查：是否禁用了省电 `WIFI_PS_NONE`
  - 再检查：mailbox 是否过小导致排队

- 现象：内存/IRAM 不够导致编译或运行异常
  - 回退顺序：`EXTRA_IRAM_OPT` → buffer 数量 → mailbox size
