# Pitfall：`lv_screen_load()` 不带 `auto_del` 导致 screen 泄漏（反复切屏内存持续增长）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo3.md`）里对 App Manager / 锁屏流程的代码审查：屏幕生命周期管理存在 bug，导致“短期不崩、长期必崩”的慢性泄漏。

---
## 上下文签名（Context Signature）

> 这是“坑点复盘（Pitfall）”。症状相似不代表根因相同。
> 如果无法对齐本节上下文与证据：**禁止**直接给“最终修复实现”，只能给排查路径与最小验证闭环。

- 目标平台：ESP32/ESP32-S3/STM32/…
- SDK/版本：ESP-IDF x.y / LVGL x.y / HAL 版本 / …
- 关键外设：LCD/Touch/I2C/SPI/UART/Wi‑Fi/BLE/…
- 资源约束：Flash/RAM/是否有 PSRAM / heap 策略
- 并发模型：谁是 single-writer？哪些回调/中断上下文？

---

## 证据最小集（必须补齐，否则只给排查清单）

- 复现步骤：最短 3~5 步
- 关键日志：至少 10 行（含时间戳/线程/错误码）
- 关键配置：`sdkconfig`/分区表/LVGL 配置/驱动配置（只列与问题相关的）
- 边界条件：是否“只在某分辨率/某字体/某 MTU/某波特率/某温度/某电源”下发生？

---

## 停手规则（Stop Rules）

- 无复现、无日志、无法确认平台/版本：不要输出最终修复，只输出“要补齐的信息 + 排查清单”。
- 修复涉及写 flash/修改分区/改并发 owner：先给最小冒烟闭环与回滚方案，再进入实现细节。
- 多个根因都解释得通：先加观测点（日志/计数器/抓包）缩小假设空间，再改代码。

---


## 症状

- 反复“开关 App / 锁屏→PIN→桌面”后：
  - 内存占用持续增长（尤其是 PSRAM/heap）
  - 运行一段时间后出现白屏、卡顿或随机崩溃
- 典型特征：**短期没事（8MB PSRAM 看起来很宽裕）**，但随着交互次数增加逐渐恶化。

---

## 根因（两个点经常同时出现）

### 1) 使用 `lv_screen_load()`（不自动删除旧 screen）

`lv_screen_load()` 本身不会帮你删旧 screen；如果你没有显式 delete，旧 screen 上的 LVGL 对象会一直留在堆里。

### 2) “只清指针不删对象”的死代码

对话里出现过这种典型 bug（示意）：

```c
current_app_scr = NULL;   // 先置空
if (current_app_scr) {    // 永远 false，delete 永远不执行
    lv_obj_delete(current_app_scr);
}
```

结果：每次退出 App 都泄漏一整个 screen。

---

## 修复策略（推荐顺序）

### 方案 A（优先）：统一用带 `auto_del` 的切屏 API

如果你的切屏语义是“新 screen 生效后，旧 screen 就可以销毁”，优先使用支持 `auto_del` 的 API（以 LVGL 版本为准）：

- `lv_screen_load_anim(scr, ..., auto_del=true)`（或等价接口）

要点：
- 旧 screen 的 delete 交给 LVGL，避免你自己写错时序。
- 切屏统一入口放到 App Manager（避免每个 App 各写一套）。

### 方案 B：App Manager 手动 delete（但要写对时序）

如果你的框架需要“返回栈”，一般是：
- push：保留上一屏（不删）
- pop：销毁当前屏，回到上一屏

此时建议：
- pop 时只 delete **被弹出的那一屏**
- 不要把“当前屏指针”提前置空
- 在事件回调里删除对象用 `lv_obj_delete_async()`（降低时序风险）

---

## 预防（把泄漏变成可复现、可验收）

- 在开发期加入监控：
  - 定期调用 `lv_mem_monitor()`（或等效方法）记录堆趋势
- 做一个最小“压力交互脚本”：
  - 进入/退出任意 App 10 次
  - 锁屏→PIN→桌面循环 10 次
- 每次循环后检查：
  - 堆是否回落或稳定（允许轻微波动，但不应单调增长）

---

## 验收标准

- 连续 30 分钟高频切屏/锁屏交互：
  - 不出现堆单调增长
  - 不出现白屏/卡死
  - 退出 App 后 UI 能正常回到桌面（screen 对象被正确释放）

