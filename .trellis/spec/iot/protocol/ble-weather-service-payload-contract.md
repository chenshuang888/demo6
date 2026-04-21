# Contract：自定义 BLE Weather Service（UUID + 68 字节 payload packed）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“PC 端 open-meteo → BLE write → ESP32 UI 天气页”的协议收敛设计（自定义 UUID + packed struct + Python `struct.pack`）。
>
> 目标：把天气推送做成“可重复/可验收”的跨端契约：PC 端脚本与固件严格对齐字节布局，避免 padding、避免 JSON 解析开销。

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

**用自定义 GATT 服务 + 固定字段 packed struct（小端）传天气；payload 约 68 字节，要求 MTU ≥ 100（对话给的经验值）。**

---

## UUID 约定（对话中的设计）

- Service UUID：`8a5c0001-0000-4aef-b87e-4fa1e0c7e0f6`
- Characteristic UUID：`8a5c0002-0000-4aef-b87e-4fa1e0c7e0f6`
  - 属性：WRITE（PC → ESP32 推送）

---

## Payload：`weather_payload_t`（MCU 端必须 packed）

```c
typedef struct {
    int16_t  temp_c_x10;        // 当前温度×10（235=23.5°C）
    int16_t  temp_min_x10;      // 今日最低×10
    int16_t  temp_max_x10;      // 今日最高×10
    uint8_t  humidity;          // 湿度 %
    uint8_t  weather_code;      // 0=Unknown 1=Clear 2=Cloudy 3=Overcast
                               // 4=Rain 5=Snow 6=Fog 7=Thunder
    uint32_t updated_at;        // unix timestamp（PC 查询到的时间）
    char     city[24];          // UTF-8，`\0` 结尾（不足填 0）
    char     description[32];   // UTF-8，`\0` 结尾（不足填 0）
} __attribute__((packed)) weather_payload_t;
```

---

## `weather_code` 语义（推荐做“归类压缩”）

来源对话强调：PC 端拿到气象服务（如 open-meteo）返回的 WMO 天气代码后，**不要把几十种 code 原样搬到固件**，而是先在 PC 端做归类压缩，让固件只认识少量“语义码”（UI 易做、颜色映射稳定、协议更长期）。

推荐（与对话一致）的 8 类枚举（`0..7`）：

- `0 Unknown`
- `1 Clear`
- `2 Cloudy`
- `3 Overcast`
- `4 Rain`
- `5 Snow`
- `6 Fog`
- `7 Thunder`

验收要点：

- UI 颜色映射只依赖这 8 类，避免“新增 WMO code → 固件又要改一遍颜色表”的耦合。
- `weather_code` 不做“显示文本”的唯一来源；`description` 才负责最终人类可读文本（但字体受限时可降级）。

---

## PC 端打包格式（Python）

对话中给出的严格对齐配方：

```python
struct.pack("<hhhBBI24s32s", ...)
```

含义：

- `<`：小端
- `h h h`：3× int16
- `B B`：2× uint8
- `I`：uint32
- `24s`：城市（固定 24 字节）
- `32s`：描述（固定 32 字节）

> C 端 `packed` + Python `struct.pack` 必须同时满足，才能做到“无 padding 的固定长度”。

---

## `updated_at` 的推荐用法（UI 去重 / 防闪烁）

对话里明确用 `updated_at` 作为“内容指纹”，让 UI 在高频 `update()`（例如 10ms 一次）时避免重复重绘：

- 页面侧缓存 `last_updated_at`。
- 若 `w->updated_at == last_updated_at`，直接 `return`（不更新 label / 不重算布局）。

这样能避免：

- 10ms 刷一次同样数据导致的无意义 `lv_label_set_text()` 调用；
- 文本重排/重绘造成的可见闪烁。

---

## 为什么不用 JSON（对话中的取舍理由）

- BLE 带宽小：二进制能省很多字节（相比 JSON 通常节省 30–60%）
- ESP32 端无需 JSON 解析器（减少依赖与 CPU）
- 字段固定、验收清晰

---

## 验收标准

- PC 推送后，ESP32 端解析字段全部正确（温度/湿度/城市/描述不乱码不串位）
- payload 长度恒定（不随编译器/平台产生 padding 漂移）
- MTU 过小时能给出可理解的失败信号（至少在日志中可观测）
