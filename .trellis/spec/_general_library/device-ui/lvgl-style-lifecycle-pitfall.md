# Pitfall：LVGL 样式对象生命周期错误导致“第 3 次进页面卡死/崩溃”

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）关于页面样式复用的排障与方案选择。

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


## 一句话结论

**不要用“全局静态 style + 初始化一次”去绑定“会被销毁重建的页面对象”。要么每次重建样式，要么把样式变成页面实例成员，跟页面同生共死。**

---

## 触发条件

- 页面会被销毁（例如 `lv_obj_del(screen)`）然后再次创建
- 样式对象 `lv_style_t` 是全局静态变量
- 用 `s_styles_initialized` 这类 flag 只初始化一次

典型代码形态（对话中的示例）：

```c
static lv_style_t s_style_btn;
static bool s_styles_initialized = false;

static void init_styles(void) {
    if (s_styles_initialized) return;
    lv_style_init(&s_style_btn);
    s_styles_initialized = true;
}
```

---

## 现象（可观测信号）

- 第 1 次进入页面正常
- 第 2 次可能仍正常
- 第 3 次开始出现卡死/死循环/崩溃（表现不稳定）

---

## 根因（对话中的解释要点）

LVGL 样式系统内部可能维护：

1. 引用计数
2. 样式关联对象列表
3. 内部状态标志

当页面对象删除时：

- `lv_obj_del(screen)` 会删对象
- 但样式对象本身仍是全局存在的
- 如果样式内部还残留对已删除对象的关联信息，后续 `lv_obj_add_style()` 可能触发访问野指针 → 卡死/崩溃

根本原因总结：

**样式对象是“全局生命周期”，但它绑定的 UI 对象是“临时生命周期”，两者不匹配。**

---

## 修复方案（两种都可，按项目风格选）

### 方案 1：每次进入页面重新初始化样式（简单、推荐起步）

核心做法：页面销毁时重置初始化标志，下次进入重新 `lv_style_init()`。

```c
static void page_time_destroy(void) {
    s_styles_initialized = false;
}
```

### 方案 2：把样式对象变成页面实例成员（对话中偏好的方案）

核心做法：把 `lv_style_t` 放进页面 `ui` 结构体中，随页面一起创建/销毁。

示意结构（对话中的思路）：

```c
typedef struct {
    lv_obj_t *screen;
    // ...
    lv_style_t style_btn;
    lv_style_t style_btn_pressed;
} page_time_ui_t;
```

---

## 预防（让它不再发生）

- 规则：**凡是页面会 destroy/recreate，就不要把 style 当“永生对象”复用**
- 结构：页面模块统一采用 `page_xxx_ui_t` 保存 UI 资源（对象 + 样式），退出时统一释放/重置

---

## 验收标准

- 连续进入/退出目标页面 10 次，不出现卡死/崩溃
- 内存监控下堆使用稳定（不出现随进出次数持续增长）

