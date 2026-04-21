# Contract：自定义 BLE Media Service（WRITE + 92 字节 media_payload packed）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“音乐副屏：PC 订阅 Windows 媒体会话（SMTC）→ BLE write 到 ESP32 → page_music 本地插值进度条”的协议收敛设计。
>
> 目标：把“歌名/歌手/播放状态/进度”做成可验收的跨端契约：字段固定、长度固定、去重策略明确，避免 UI 频繁重绘与 BLE 流量浪费。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 一句话结论

**Media 信息用自定义 GATT 服务 + 固定 92 字节 packed payload（小端）；PC 端只在“状态变化/周期校准”时推送，ESP32 本地插值让进度条丝滑。**

---

## UUID 约定（对话中的实现草案）

- Service UUID：`8a5c0007-0000-4aef-b87e-4fa1e0c7e0f6`
- Characteristic UUID：`8a5c0008-0000-4aef-b87e-4fa1e0c7e0f6`
  - 属性：WRITE（PC → ESP32 推送）

---

## Payload：`media_payload_t`（92 bytes，必须 packed）

对话里最初估算成 90B，随后纠正：实际需要 92B（PC 端 `struct.pack` 对齐为准）。

推荐结构（显式保留一个 16-bit 字段，保证总长度 92）：

```c
typedef struct {
    uint8_t  playing;        // 0=paused, 1=playing
    uint8_t  _reserved8;     // 置 0
    int16_t  position_sec;   // 当前进度（秒；-1 表示未知）
    int16_t  duration_sec;   // 总时长（秒；-1 表示未知）
    uint16_t _reserved16;    // 置 0；用于对齐/未来扩展
    uint32_t sample_ts;      // PC 侧采样时刻（unix 秒），用于调试/可观测
    char     title[48];      // UTF-8 bytes，末尾 '\0'；不足补 0
    char     artist[32];     // UTF-8 bytes，末尾 '\0'；不足补 0
} __attribute__((packed)) media_payload_t;  // 92 bytes
```

> 对话里明确建议不加 `album`：屏幕窄，收益低；想扩字段优先走版本化/新 characteristic，而不是把 payload 无限膨胀。

---

## PC 端打包（Python `struct.pack`）

对话给出的格式串（小端）：

```python
struct.pack("<BBhhHI48s32s", playing, 0, position, duration, 0, sample_ts, title_bytes, artist_bytes)
```

验收点：

- 固件端必须校验长度==92；不等则拒绝。
- UTF-8 固定长度字段必须“安全截断”（不能在多字节字符中间截断，否则显示成 `�`）。

---

## 进度条策略（强烈推荐：设备端本地插值）

对话强调：PC 不要每秒推一次（浪费带宽 + 触发重绘）。

推荐策略：

- PC 端只在以下时机推送：
  - 曲目切换
  - 播放/暂停变化
  - seek
  - 每 10 秒一次“校准推送”（兜底）
- ESP32 收到 payload 时记录：
  - `received_position_sec`
  - `received_esp_time`（例如 `esp_timer_get_time()`）
- UI 每帧计算显示值：
  - `playing=1`：`displayed = received_position + (now - received_time)`
  - `playing=0`：`displayed = received_position`

并做边界处理：

- 若 `position_sec < 0` 或 `duration_sec < 0`：隐藏/禁用进度条
- clamp 到 `[0, duration_sec]`

---

## 验收标准

- PC 推送一条固定 payload，设备侧能立即显示 `title/artist` 且进度每秒前进。
- 任意一次曲目切换：只触发一次有效 push（需要“payload 去重”，见主机端工具文档）。
- 中文文本不出现 `�`（UTF-8 截断正确）。

