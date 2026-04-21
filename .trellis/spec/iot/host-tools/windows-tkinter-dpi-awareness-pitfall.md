# Pitfall：Windows 高 DPI 缩放下 Tkinter 窗口“内容被挤出/按钮消失”

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中 PC 端 GUI 的真实问题：同一套 Tkinter 布局在不同 DPI/缩放下，底部按钮被挤出屏幕；通过声明 DPI awareness 一次性解决。
>
> 目标：让“GUI 显示不全”这类问题能快速归因到系统缩放，而不是反复猜“布局写法/库质量/屏幕大小”。

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


## 典型症状

- 代码写的是 `720x480` / `700` 高度，但实际显示像被“放大”了一遍：底部按钮/提示永远看不到。
- 允许纵向拉伸后才勉强能看到，体验很差。

---

## 根因（对话里的关键解释）

**Tkinter 默认不声明 DPI awareness**。在 Windows 125%/150% 缩放下，系统会对“不 DPI-aware 的进程”做二次缩放：

- 你以为画了 700px 高度
- 系统按 150% 把它放大成 1050px
- 屏幕装不下 → 内容被挤出

这就是“常见软件不会这样”的原因：它们通常显式声明了 DPI awareness（Qt/Electron/WPF 往往内置）。

---

## 标准修复：启动时声明 DPI awareness（必须在创建 `Tk()` 之前）

最小可用片段（Windows 专用）：

```python
import ctypes

def enable_dpi_awareness():
    try:
        # Per-monitor DPI aware（推荐）
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        try:
            # 旧接口兜底
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass
```

调用顺序（必须）：

1) `enable_dpi_awareness()`
2) `root = Tk()` / `root = customtkinter.CTk()`

---

## 验收标准

- 同一台机器把系统缩放改成 125%/150% 后，GUI 仍能完整显示关键按钮（不需要拖拽窗口作为“兜底”）。
- 逻辑不变：只修显示，不引入业务行为变化。

