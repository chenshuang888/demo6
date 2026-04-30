# UI Mockups —— 浏览器原型先行工作流

> 任何 UI 改动**先在 HTML 里定稿**，再翻译成 LVGL。迭代速度从分钟级降到秒级。

---

## 为什么要这套东西

LVGL 改一次 → 编译 ~30s → 烧录 ~30s → 看效果 → 不满意 → 重写
**每轮 ~2 分钟**，一晚上 30 个版本。

HTML 改一次 → 浏览器 Cmd+R → 即时看效果
**每轮 ~2 秒**，一晚上 1000 个版本。

定稿后再翻译成 LVGL（~10 分钟）。**总成本 = 1× 翻译，省去 N×编译烧录**。

---

## 目录结构

```
ui_mockups/
├── README.md           ← 本文件
├── _shared/
│   ├── tokens.css      ← 复刻 app/ui/ui_tokens.h 的颜色/字号/间距
│   └── frame.css       ← 240×320 屏幕模拟器 + 2x 显示
└── <page-name>/
    ├── v1.html         ← 当前实现的镜像（baseline）
    ├── v2.html         ← 改动尝试
    └── ...
```

---

## 工作流

### 1. 开 baseline

新页面改造前，先把**当前 LVGL 实现**镜像成 `v1.html`，存档作对比基准。

### 2. 在 HTML 上迭代

复制 v1 → v2/v3/v4，浏览器看效果，秒级反馈。**只用 tokens.css 里的变量**，不要乱用颜色/字号——否则翻译不到 LVGL。

### 3. 定稿

挑一个最满意的版本，告诉 AI "翻译 vN.html 到 LVGL"。

### 4. 同步 token

如果 mockup 阶段改了 `tokens.css`（比如新增一个颜色），**记得同步改 `app/ui/ui_tokens.h`**，否则两边会漂移。

---

## 怎么打开

直接双击 `*.html`，浏览器里就能看（不需要 server）。

VSCode 用户推荐装 **Live Server** 插件，改完文件浏览器自动刷新。

---

## 必须遵守的"LVGL 兼容规则"

HTML 漂亮但翻译不出来 = 白忙。所以画原型时严格遵守以下规则：

| 规则 | 原因 |
|---|---|
| 屏幕容器固定 240×320 | 物理尺寸，不能变 |
| **只用 tokens.css 里定义的颜色** | 现有字体/调色板才能映射到 LVGL |
| 字号只用 14 / 16 / 24 / 48 | LVGL 字体只有这几档 |
| 间距只用 4/8/12/16/24/32（8 的倍数） | 跟 ui_tokens.h `UI_SP_*` 对齐 |
| 圆角只用 6/10/14 | 跟 `UI_R_*` 对齐 |
| **禁止 box-shadow** | LVGL shadow 嵌入式性能差 |
| **禁止 linear-gradient** | LVGL 渐变性能差 |
| **禁止 SVG filter / backdrop-filter** | LVGL 不支持模糊/光效 |
| 不要写 hover/transition CSS | 设备没有鼠标 hover；动画走 LVGL anim 系统 |
| 中文用现有字体（霞鹜文楷） | CSS 里 fallback 到本地字体即可 |

**违反规则的 mockup 会让你浪费 2 倍时间** —— 先在浏览器画出漂亮的，然后发现 LVGL 做不到，再回去重画。

---

## 示例：从 mockup 到 LVGL

```html
<!-- v2.html 里这样写 -->
<div class="card">
  <span class="kv-key">湿度</span>
  <span class="kv-value">45%</span>
</div>
```

翻译到 LVGL：

```c
lv_obj_t *card = ui_card(parent);
lv_obj_t *val;
ui_kv_row(card, "湿度", "45%", &val, false);
```

**每一个 mockup 的 class 都对应一个 LVGL widget**。这是 mockup 系统能稳定工作的基础。
