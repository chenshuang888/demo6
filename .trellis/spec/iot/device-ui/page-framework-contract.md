# Contract：页面框架契约（PageDescriptor_t 生命周期与职责）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-----1.md`）“OLED页面级架构 - 详细分析报告”。
>
> 目标：把“页面怎么写才算标准、怎么扩展不会乱”的约定写清楚，方便持续加页面/加功能而不崩。

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


## 适用范围

- 非 LVGL 的 OLED 页面框架（或类似“每帧 update+draw”的 UI 框架）
- 以 `PageDescriptor_t` 描述页面生命周期并由框架调度

---

## 一句话原则

**页面必须按统一生命周期实现：init（一次）/ enter / update / draw / key_event / exit；页面私有状态必须内聚在页面模块内。**

---

## PageDescriptor_t（标准定义）

对话中整理的结构体示意：

```c
typedef struct {
    const char *name;              // 页面名称（用于调试）
    PageInitFunc init;             // 初始化函数（可选，NULL表示无需初始化）
    PageEnterFunc enter;           // 进入回调（可选）
    PageUpdateFunc update;         // 更新回调（可选）
    PageDrawFunc draw;             // 绘制回调（必须）
    PageKeyEventFunc key_event;    // 按键事件回调（必须）
    PageExitFunc exit;             // 退出回调（可选）
    uint8_t is_initialized;        // 内部标志：是否已初始化（框架管理）
} PageDescriptor_t;
```

---

## 生命周期语义（必须遵守）

- `init`：仅第一次加载页面时执行，用于一次性初始化（缓存、计算、资源准备）
- `enter`：每次进入页面执行，用于恢复/刷新状态
- `update`：每帧执行，处理逻辑更新（可选）
- `draw`：每帧执行，负责屏幕刷新（必须）
- `key_event`：处理输入事件（必须）
- `exit`：退出页面时执行（可选）

---

## 页面私有状态（强约束）

对话里的标准例子是：

- 用 `static` 私有变量缓存页面状态（例如 `cached_fw_size`）
- 不要把页面状态散落到框架或其它模块里

---

## 推荐实现风格（标准模板）

对话中 `page_about.c` 被总结为“最简单、最标准”的模板型页面：

- 文件顶部：私有变量
- 按生命周期函数顺序实现（init→enter→update→draw→key_event→exit）
- draw 里使用 widget 库（例如标题栏），以及 OLED API 绘制内容

---

## 验收标准

- 新增页面严格按 PageDescriptor_t 约定实现，不出现“页面一半逻辑写在别的模块”
- 进入/退出/按键响应流程可预测（不会因为缺某个回调导致异常）

