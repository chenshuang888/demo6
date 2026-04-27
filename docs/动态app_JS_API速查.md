# 动态 App JS API 速查

> 适用：基于本固件 dynamic_app runtime 的 JS app 开发
> 引擎：esp-mquickjs（**仅 ES5**，无箭头函数 / let / const / class / Promise）
>
> **重要**：runtime 在每次启动 app 之前会自动 eval 一段标准库 `prelude.js`，
> 暴露 `VDOM` / `h` / `makeBle` 三个全局符号。业务脚本无需 import / 拷模板，
> 直接用即可。本速查列出 prelude 提供的"上层 API"和固件提供的"下层 sys.* API"。

---

## 0. 全局对象一览

```
// ── prelude 自动注入（推荐使用） ──
VDOM                         // 声明式 UI 框架
  ├─ h(type, props, children)  // 造 vnode
  ├─ mount(node, parentId)     // 挂载（parentId=null 表 root container）
  ├─ render(node, parentId)    // diff + 增量更新（首次等同 mount）
  ├─ set(id, patch)            // 局部 patch 单节点
  ├─ find(id)                  // 取 vnode
  ├─ destroy(id)               // 销毁子树
  └─ dispatch(...)             // 内部用，不调
h                            // VDOM.h 的短名
makeBle(appName)             // → ble 对象（见 §5）

// ── 固件原生（每个 app 都有） ──
sys
├─ log(msg)
├─ ui.*                      // 命令式绘图原语（一般不直接用，VDOM 已包好）
├─ time.uptimeMs() / uptimeStr()
├─ app.saveState() / loadState() / eraseState()
├─ ble.send / onRecv / isConnected      ⚠️ 不直接用，请走 makeBle
├─ symbols.*                 // LVGL 内置图标字面量
├─ style.*                   // setStyle key 枚举（VDOM 内部用）
├─ align.*                   // 对齐方式枚举
└─ font.*                    // TEXT / TITLE / HUGE

setInterval(fn, ms)          // 周期回调，返 id
clearInterval(id)            // 取消
```

---

## 1. VDOM —— 声明式 UI（推荐用法）

### 1.1 三件事

```js
// 1) 描述 UI（一棵 vnode 树）
var tree = h('panel', { id: 'root', size: [-100, -100], bg: 0x14101F }, [
    h('label', { id: 'lbl', text: 'Hello',
                 fg: 0xFFFFFF, font: 'title',
                 align: ['c', 0, -10] }),
    h('button', { id: 'btn', size: [120, 40],
                   bg: 0x06B6D4, radius: 20,
                   align: ['c', 0, 30],
                   onClick: function (ev) { sys.log('clicked'); } }, [
        h('label', { id: 'btnL', text: 'Go',
                     fg: 0x000814, align: ['c', 0, 0] })
    ])
]);

// 2) 挂上去（首次走 mount）
VDOM.mount(tree, null);
sys.ui.attachRootListener('root');   // 整页只调一次

// 3) 改字段（任选一种）
VDOM.set('lbl', { text: 'World', fg: 0xF59E0B });   // 单节点局部改
// 或者重建整树 + diff:
VDOM.render(buildTreeFromState(state), null);
```

### 1.2 节点类型

| type | 说明 |
|---|---|
| `panel`  | 容器（lv_obj） |
| `label`  | 文本 |
| `button` | 按钮（与 panel 类似，但有按下视觉反馈） |

### 1.3 props 全集

| 字段 | 类型 | 例 |
|---|---|---|
| `id` | string | `'root'`（**必填**，全局唯一） |
| `text` | string | `'Hello'`（仅 label / button 内部 label） |
| `bg` | int 0xRRGGBB | `0x14101F` |
| `fg` | int 0xRRGGBB | `0xFFFFFF` |
| `radius` | px | `8` |
| `size` | `[w, h]` | `[120, 40]` 或 `[-100, -100]`（负数=百分比） |
| `align` | `[mode, dx, dy]` | `['c', 0, 0]` 居中；模式见下 |
| `pad` | `[L, T, R, B]` | `[8, 4, 8, 4]` |
| `borderBottom` | int color | `0x333333` |
| `flex` | `'row'` / `'col'` | flex 方向 |
| `gap` | `[rowGap, colGap]` | `[6, 0]` |
| `font` | `'text'` / `'title'` / `'huge'` | |
| `shadow` | `[color, width, ofsY]` | `[0x000000, 12, 4]` |
| `scrollable` | bool | panel 是否可滚 |
| `onClick` / `onPress` / `onDrag` / `onRelease` / `onLongPress` | fn(ev) | 见 §1.5 |

### 1.4 align 模式

```
'tl'(top-left)  'tm'(top-mid)  'tr'(top-right)
'lm'(left-mid)  'c' (center)   'rm'(right-mid)
'bl'            'bm'           'br'
```

### 1.5 事件回调

```js
function onClick(ev) {
    ev.target;          // 实际被点的子节点 id
    ev.currentTarget;   // 当前 hook 所在节点 id
    ev.dx, ev.dy;       // onDrag 时的位移（其它事件为 0）
    ev.stopPropagation();
    return false;       // 等价 stopPropagation + 终止冒泡
}
```

事件**沿 `_parent` 链冒泡**（最深处的节点先），所以可以在父节点上挂 `onClick` 给一组子按钮统一处理（看 alarm.js 的 `onCardClick`）。

### 1.6 命令式旁路（高频更新）

每次 `render()` 走整树 diff；如果某字段每帧都变（如 30Hz 进度条、拖动位移），用 `VDOM.set` 单点改更省 CPU：

```js
// rerender 整树（按需）
function rerender() { VDOM.render(view(state), null); }

// 单点旁路（高频）
VDOM.set('progFg', { size: [w, 6] });
```

---

## 2. sys.ui —— 命令式绘图（一般不直接用）

> **VDOM 已经包好了所有这些原语**，只在做特殊优化时才直接调。

```js
sys.ui.createPanel(id, parentId)
sys.ui.createLabel(id, parentId)
sys.ui.createButton(id, parentId)
sys.ui.setText(id, "...")
sys.ui.setStyle(id, sys.style.<KEY>, a, b, c, d)
sys.ui.destroy(id)
sys.ui.attachRootListener(id)         // 在最外层 panel 上调一次！
```

> **必须**在你的 root panel mount 完之后调一次 `sys.ui.attachRootListener(rootId)`，否则按钮不响应（VDOM 不会自动调，因为它不知道哪个是 root）。

---

## 3. sys.time —— 时间

```js
var ms = sys.time.uptimeMs();        // 开机至今毫秒
var s  = sys.time.uptimeStr();       // "00:01:23"
```

⚠️ **没有真实墙钟**。要做闹钟/日历需要业务自己通过 BLE 从 PC 同步。

---

## 4. sys.app —— 每个 app 独享的持久化

```js
sys.app.saveState(jsonString);       // → bool
var raw = sys.app.loadState();       // → string | null（首次返 null）
sys.app.eraseState();                // → bool
```

- 存到 NVS namespace `dynapp`，key 为当前 app 名
- 单条上限 4KB
- 跨 app 互相不可见
- JS 自己 `JSON.stringify` / `JSON.parse`，C 不解析

---

## 5. makeBle —— BLE 路由（推荐入口）

```js
var ble = makeBle("myapp");          // 在脚本顶部调一次

ble.on("data", function (msg) {       // 注册 type 回调
    sys.log(JSON.stringify(msg.body));
});
ble.onAny(function (msg) {            // 收到任何"给我的"消息（不含 ping）
    sys.log("any: " + msg.type);
});
ble.onError(function (raw) {          // JSON 解析失败回调
    sys.log("bad json: " + raw);
});

ble.send("req", { force: true });     // 发 { from:"myapp", type:"req", body:{...} }
ble.isConnected();                    // → bool
ble.appName;                          // 'myapp'
```

行为：
- 自动按 `to` 字段过滤（不是给我的丢掉）
- 自动应答 `ping` → `pong`，不打扰业务
- handler 抛异常会被 catch 住 + sys.log，不会崩 app

> ⚠️ 每个 app 调一次 `makeBle` 即可。再调会**覆盖**底层 onRecv，旧 helper 失效。

详见 [`docs/动态app双端通信协议.md`](./动态app双端通信协议.md)。

---

## 6. setInterval / clearInterval

```js
var id = setInterval(function () { ... }, 100);    // 每 100ms 一次
clearInterval(id);
```

约束：
- 最多 8 个并发 interval
- 没有 `setTimeout`，单次延时自己用 setInterval + 第一次后 clear
- 高频回调（< 50ms）注意不要每帧整树 rerender，用 `VDOM.set` 旁路

---

## 7. sys.symbols —— LVGL 内置图标

```js
var p = sys.symbols.PLAY;
sys.ui.setText(id, sys.symbols.PLAY + " Play");
// 或在 VDOM 里：
h('label', { id: 'x', text: sys.symbols.BLUETOOTH + ' BT' })
```

可用图标见固件 `dynamic_app_natives.c::dynamic_app_natives_bind` 的 `symbols` 段。

---

## 8. sys.font —— 字体

| 常量 | 用途 |
|---|---|
| `sys.font.TEXT` (0) | 默认正文 |
| `sys.font.TITLE` (1) | 标题中等 |
| `sys.font.HUGE` (2) | 超大数字（时钟/温度） |

VDOM 里用字符串 `'text'` / `'title'` / `'huge'` 即可。

---

## 9. 常见坑

| 问题 | 原因 | 解决 |
|---|---|---|
| 屏上空白 | 忘了 `sys.ui.attachRootListener` | mount 完根 panel 后调一次 |
| 按钮不响应 | 同上 | 同上 |
| 卡 menu 不切 | 脚本 build 超 800ms | 简化或拆成 lazy build |
| 高频 setText 闪烁 | 每帧整树 rerender | 高频字段改用 `VDOM.set` 单点 |
| BLE send 一直返 false | PC 没 start_notify 订阅 tx | 确认 PC 端 SDK 跑起来 |
| ble.on 不回调 | 自己又调了一次 `makeBle("xxx")` 覆盖 | 每个 app 只调一次 makeBle |
| `SyntaxError: catch variable already exists` | 同函数内多个 `catch (e)` 同名 | 用 `eParse` / `eAny` 等不同名字 |
| TLSF assert 重启 | 加 native 但没改 `DYNAMIC_APP_EXTRA_NATIVE_COUNT` | 同步改宏 |

---

## 10. 完整最小 app 模板

```js
// hello.js —— 一个按钮，按一下给 PC 发消息

var ble = makeBle("hello");

VDOM.mount(h('panel', { id: 'root', size: [-100, -100], bg: 0x14101F }, [
    h('label', { id: 'hi', text: 'Hello, dynamic world',
                 fg: 0xFFFFFF, font: 'title',
                 align: ['c', 0, -20] }),
    h('button', { id: 'btn', size: [120, 40],
                   bg: 0x06B6D4, radius: 20,
                   align: ['c', 0, 30],
                   onClick: function () {
                       ble.send('greet', { ts: sys.time.uptimeMs() });
                       VDOM.set('hi', { text: 'Sent!' });
                   }}, [
        h('label', { id: 'btnL', text: 'Greet PC',
                     fg: 0x000814, align: ['c', 0, 0] })
    ])
]), null);
sys.ui.attachRootListener('root');

ble.on('reply', function (msg) {
    sys.log('PC said: ' + JSON.stringify(msg.body));
    VDOM.set('hi', { text: 'Got reply' });
});

sys.log('hello: ready');
```

完整业务，**24 行**。框架那 348 行 VDOM + makeBle 全部由 prelude 自动注入。
