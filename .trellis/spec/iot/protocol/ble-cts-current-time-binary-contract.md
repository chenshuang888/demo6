# Contract：BLE CTS（0x1805/0x2A2B）Current Time 二进制格式（10 字节，packed 对齐）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中 CTS（Current Time Service）打包/解包与 day_of_week 映射说明。
>
> 目标：PC（Python/bleak）与 ESP32（NimBLE）对“当前时间特征值”的字节布局达成单一事实来源，避免 padding/字节序/星期映射错位。

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

**该特征值 payload 固定 10 字节；ESP32 结构体必须 `packed`；PC 端用 `struct.pack("<HBBBBBBBB", ...)` 小端严格对齐。**

---

## 服务/特征 UUID（标准）

- Service：`0x1805`（Current Time Service）
- Characteristic：`0x2A2B`（Current Time）
- 权限（典型）：READ / WRITE（可选 NOTIFY，按你的实现）

---

## MCU 端结构体（必须 packed）

```c
typedef struct {
    uint16_t year;        // 2
    uint8_t  month;       // 1
    uint8_t  day;         // 1
    uint8_t  hour;        // 1
    uint8_t  minute;      // 1
    uint8_t  second;      // 1
    uint8_t  day_of_week; // 1
    uint8_t  fractions256;// 1
    uint8_t  adjust_reason;//1
} __attribute__((packed)) ble_cts_current_time_t;
```

关键点：

- **必须 `packed`**：否则编译器可能为了对齐插 padding（payload 变 12 字节，PC 解包就错位）。

---

## PC 端（Python）打包格式

对话中使用的配方：

- `CTS_STRUCT = "<HBBBBBBBB"`

含义：

- `<`：小端（与 ESP32 CPU 一致）
- `H`：2 字节无符号（year）
- `B`：1 字节无符号（其余字段）

示例：

```python
data = struct.pack(
    "<HBBBBBBBB",
    2026, 4, 18,
    12, 34, 56,
    5, 0, 0,
)
```

---

## day_of_week 映射（最容易错的点）

CTS 规定（标准）：

- `0` = unknown
- `1` = 周一 … `7` = 周日

常见语言差异：

- Python `datetime.weekday()`：`0=周一 … 6=周日` → 需要 `+1`
- C `tm_wday`：`0=周日 … 6=周六` → 需要把 `0` 映射成 `7`

对话中的 C 端示例（语义）：

```c
cts_time->day_of_week = (timeinfo.tm_wday == 0) ? 7 : timeinfo.tm_wday;
```

---

## 验收标准

- PC 端写入后，ESP32 端解包字段全对（年/月/日/时/分/秒/星期不串位）
- payload 长度始终为 10（不因编译器/平台差异漂移）

