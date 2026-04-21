# Contract：自定义 BLE Notification Service（WRITE + 136 字节 payload packed）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“PC 端 GUI 推送通知 → ESP32 存 10 条环形缓冲 → 通知页查看”的协议收敛设计（固定长度二进制 + packed struct + 严格长度校验）。
>
> 目标：让通知推送变成可验收的跨端契约：PC 端/固件逐字节对齐，错一字节直接拒绝，避免“看似成功但内容乱了”的隐蔽失败。

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

**通知推送用自定义 GATT 服务 + 固定 136 字节 packed payload（小端）；Characteristic 只开放 WRITE，PC 端只能写入不能读回。**

---

## UUID 约定（对话中的设计）

- Service UUID：`8a5c0003-0000-4aef-b87e-4fa1e0c7e0f6`
- Characteristic UUID：`8a5c0004-0000-4aef-b87e-4fa1e0c7e0f6`
  - 属性：WRITE（PC → ESP32 推送）

---

## Payload：`notification_payload_t`（136 bytes，必须 packed）

对话里给的预算是“10 条通知，总约 1.4KB（136B×10）”。推荐把 payload 设计为固定长度，避免动态分配与 UTF-8 变长导致的跨端复杂度。

推荐结构（显式保留 2 字节 `_reserved`，确保总长度 136）：

```c
typedef enum {
    NOTIFY_CAT_GENERIC  = 0,
    NOTIFY_CAT_MESSAGE  = 1,
    NOTIFY_CAT_EMAIL    = 2,
    NOTIFY_CAT_CALL     = 3,
    NOTIFY_CAT_CALENDAR = 4,
    NOTIFY_CAT_SOCIAL   = 5,
    NOTIFY_CAT_NEWS     = 6,
    NOTIFY_CAT_ALERT    = 7,
} notify_category_t;

typedef struct {
    uint32_t ts;              // unix timestamp（秒）
    uint8_t  category;        // notify_category_t
    uint8_t  priority;        // 0=low 1=normal 2=high（建议）
    uint16_t _reserved;       // 置 0；用于未来扩展/对齐
    char     title[32];       // UTF-8 bytes，末尾以 '\0' 结束；不足补 0
    char     body[96];        // UTF-8 bytes，末尾以 '\0' 结束；不足补 0
} __attribute__((packed)) notification_payload_t;  // 136 bytes
```

验收标准：

- `sizeof(notification_payload_t) == 136`（固件编译期静态断言/日志确认）。
- WRITE 回调必须做长度校验；不等于 136 直接返回 `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`（或等价错误码）。

---

## PC 端打包（Python `struct.pack`）

建议格式串（小端）：

```python
struct.pack("<IBBH32s96s", ts, category, priority, 0, title_bytes, body_bytes)
```

注意点：

- 字符串是 **固定字节数**，必须做“按字节截断 + 末尾补 `\\0` + 不足补 0”。
- 若要支持中文，固件侧必须启用对应字体；否则 UI 只能显示 ASCII（对话里明确：Montserrat 默认只覆盖拉丁字符，中文会方块）。

---

## 验收标准（端到端）

- PC 连续推送 15 条：设备侧只保留最新 10 条（环形丢旧）。
- 推送 payload 字节数错误时：固件日志可见“invalid len”，并拒绝写入（不写入环形缓冲）。
- UI 不应每帧重建整个列表：需有 `version` / “数据变更计数器”做去重（见 `notify_manager` 设计）。

