// Dynamic App —— Timers 页面（VDOM + Root Delegation）
//
// 本文件分两段：
//   §1. VDOM 框架（通用，可复用）
//   §2. Timers 业务（用 VDOM 写）
//
// 设计原则：
//   - VDOM 节点 = { type, props, children, _parent }，靠 props.id 寻址
//   - 在最外层 root container 调一次 sys.ui.attachRootListener，之后
//     所有子按钮的点击都由 LVGL 冒泡到 root，C 侧的 on_lv_root_click
//     拿到被点对象的 id 字符串，通过事件队列回到 JS 侧由 dispatcher
//     沿 _parent 链上爬触发 onClick。
//   - 整页只挂 1 次 native cb；子节点都不调 sys.ui.* 的事件 API。
//   - 状态变化 → 显式调 vdom.set(id, partialProps)
//
// 约束：esp-mquickjs 仅支持 ES5。

// ============================================================================
// §1. VDOM 框架
// ============================================================================

var VDOM = (function () {

    // 节点 registry：id -> vnode
    var nodes = {};
    var autoSeq = 0;

    // 字体/对齐/样式常量映射（避免到处写 sys.style.XXX）
    var FONT_MAP  = { text: sys.font.TEXT, title: sys.font.TITLE, huge: sys.font.HUGE };
    var ALIGN_MAP = {
        tl: sys.align.TOP_LEFT,    tm: sys.align.TOP_MID,    tr: sys.align.TOP_RIGHT,
        lm: sys.align.LEFT_MID,    c:  sys.align.CENTER,     rm: sys.align.RIGHT_MID,
        bl: sys.align.BOTTOM_LEFT, bm: sys.align.BOTTOM_MID, br: sys.align.BOTTOM_RIGHT
    };

    // h(type, props, children) -> vnode
    //   type: 'panel' | 'button' | 'label'
    //   props: { id, text, onClick, ...style props }
    //   children: array of vnodes (可省略)
    function h(type, props, children) {
        return {
            type: type,
            props: props || {},
            children: children || [],
            _parent: null,
            _mounted: false
        };
    }

    function autoId(type) {
        autoSeq += 1;
        return "_" + type + "_" + autoSeq;
    }

    // ---- 把 props 翻译成 sys.ui.setStyle 调用 ----
    function applyStyle(id, props) {
        if (props.bg !== undefined)
            sys.ui.setStyle(id, sys.style.BG_COLOR, props.bg, 0, 0, 0);
        if (props.fg !== undefined)
            sys.ui.setStyle(id, sys.style.TEXT_COLOR, props.fg, 0, 0, 0);
        if (props.radius !== undefined)
            sys.ui.setStyle(id, sys.style.RADIUS, props.radius, 0, 0, 0);
        if (props.size !== undefined)
            sys.ui.setStyle(id, sys.style.SIZE,
                props.size[0] | 0, props.size[1] | 0, 0, 0);
        if (props.align !== undefined) {
            var a = props.align;
            var atype = ALIGN_MAP[a[0]];
            if (atype === undefined) atype = sys.align.CENTER;
            sys.ui.setStyle(id, sys.style.ALIGN,
                atype, a[1] | 0, a[2] | 0, 0);
        }
        if (props.pad !== undefined) {
            var p = props.pad;
            sys.ui.setStyle(id, sys.style.PAD,
                p[0] | 0, p[1] | 0, p[2] | 0, p[3] | 0);
        }
        if (props.borderBottom !== undefined)
            sys.ui.setStyle(id, sys.style.BORDER_BOTTOM, props.borderBottom, 0, 0, 0);
        if (props.flex !== undefined)
            sys.ui.setStyle(id, sys.style.FLEX,
                props.flex === 'row' ? 1 : 0, 0, 0, 0);
        if (props.font !== undefined) {
            var f = FONT_MAP[props.font];
            if (f === undefined) f = sys.font.TEXT;
            sys.ui.setStyle(id, sys.style.FONT, f, 0, 0, 0);
        }
    }

    // ---- 事件冒泡 dispatcher ----
    //
    //   流程：用户点击 LVGL 对象 → root listener 入队 node_id
    //         → C drain 调 dispatcher → dispatch(targetId)
    //         → 从 target 沿 _parent 链上爬，每经过一个 vnode 检查 props.onClick
    //         → handler 收到 event 对象 { target, currentTarget, stopPropagation() }
    //         → handler 返回 false 或调 e.stopPropagation() 终止冒泡
    function dispatch(startId) {
        var node = nodes[startId];
        if (!node) return;
        var stopped = false;
        var ev = {
            target: startId,
            currentTarget: null,
            stopPropagation: function () { stopped = true; }
        };
        var cur = node;
        while (cur) {
            if (typeof cur.props.onClick === 'function') {
                ev.currentTarget = cur.props.id;
                var ret = cur.props.onClick(ev);
                if (ret === false || stopped) return;
            }
            cur = cur._parent;
        }
    }

    // ---- 挂载一棵子树到 parentId（parentId 可为 null = root）----
    function mount(node, parentId) {
        if (node._mounted) return node;

        var id = node.props.id;
        if (!id) { id = autoId(node.type); node.props.id = id; }

        if (node.type === 'panel')       sys.ui.createPanel(id, parentId || null);
        else if (node.type === 'button') sys.ui.createButton(id, parentId || null);
        else if (node.type === 'label')  sys.ui.createLabel(id, parentId || null);
        else { sys.log("VDOM: unknown type " + node.type); return node; }

        if (node.props.text !== undefined) sys.ui.setText(id, "" + node.props.text);
        applyStyle(id, node.props);
        // 不为每个有 onClick 的节点单独 attach。事件统一由 root listener
        // 捕获，dispatcher 沿 _parent 链遍历到该节点的 props.onClick。

        nodes[id] = node;
        node._mounted = true;

        var i;
        for (i = 0; i < node.children.length; i++) {
            node.children[i]._parent = node;
            mount(node.children[i], id);
        }
        return node;
    }

    // ---- 查找节点 ----
    function find(id) { return nodes[id] || null; }

    // ---- 局部更新 props（不改 type/children/onClick/id） ----
    //   set(id, { text: "...", fg: 0x..., ... })
    function set(id, patch) {
        var node = nodes[id];
        if (!node) { sys.log("VDOM.set: id not found: " + id); return; }
        var k;
        for (k in patch) {
            if (!patch.hasOwnProperty(k)) continue;
            node.props[k] = patch[k];
        }
        if (patch.text !== undefined) sys.ui.setText(id, "" + patch.text);
        // 重新走一遍 applyStyle —— 只有 patch 里包含的 style 项会调 setStyle
        applyStyle(id, patch);
    }

    return { h: h, mount: mount, find: find, set: set, dispatch: dispatch };
})();

// 把 dispatcher 注册给 C 侧（GCRef 持有）。
// 选这种方式是因为 esp-mquickjs 顶层 `this` 为 undefined，拿不到全局对象。
sys.__setDispatcher(function (id) { VDOM.dispatch(id); });

var h = VDOM.h;

// ============================================================================
// §2. Timers 业务页面
// ============================================================================

// ---- 配色 ----
var COLOR_BG       = 0x1A1530;
var COLOR_CARD     = 0x2D2640;
var COLOR_CARD_ALT = 0x3A3354;
var COLOR_ACCENT   = 0x06B6D4;
var COLOR_TEXT     = 0xF1ECFF;
var COLOR_DIM      = 0x6B6480;
var COLOR_ACTIVE   = 0x10B981;
var COLOR_DANGER   = 0xEF4444;

var X_OFFSCREEN = 1000;
var SLOT_COUNT  = 6;

// ---- 状态 ----
var view = 'list';            // 'list' | 'set' | 'modal'
var firingSlot = -1;
var draftMin = 5;
var draftSec = 0;
var slots = [];
(function () {
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        slots.push({ active: false, triggerAtMs: 0 });
    }
})();

// ---- 视图切换：靠 align 把非当前 panel 推到屏外 ----
function showView(name) {
    view = name;
    VDOM.set("listPanel",  { align: ['c', name === 'list'  ? 0 : X_OFFSCREEN, 0] });
    VDOM.set("setPanel",   { align: ['c', name === 'set'   ? 0 : X_OFFSCREEN, 0] });
    VDOM.set("modalPanel", { align: ['c', name === 'modal' ? 0 : X_OFFSCREEN, 0] });
}

// ---- 工具 ----
function pad2(n) {
    n = n | 0; if (n < 0) n = 0;
    return n < 10 ? "0" + n : "" + n;
}
function fmtMS(totalSec) {
    totalSec = totalSec | 0; if (totalSec < 0) totalSec = 0;
    var m = (totalSec / 60) | 0;
    var s = totalSec - m * 60;
    if (m > 99) m = 99;
    return pad2(m) + ":" + pad2(s);
}
function clampDraft() {
    if (draftMin < 0)  draftMin = 0;
    if (draftMin > 99) draftMin = 99;
    if (draftSec < 0)  draftSec = 0;
    if (draftSec > 59) draftSec = 59;
}
function refreshDraft() {
    clampDraft();
    VDOM.set("minVal", { text: pad2(draftMin) });
    VDOM.set("secVal", { text: pad2(draftSec) });
}

// ---- slot 渲染 ----
function paintSlot(i) {
    var s = slots[i];
    var prefix = "slot_" + i;
    if (s.active) {
        var remainMs  = s.triggerAtMs - sys.time.uptimeMs();
        var remainSec = (remainMs + 999) / 1000 | 0;
        if (remainSec < 0) remainSec = 0;
        VDOM.set(prefix + "_ic", { fg: COLOR_ACTIVE });
        VDOM.set(prefix + "_t",  { text: fmtMS(remainSec), fg: COLOR_TEXT });
        VDOM.set(prefix + "_s",  { text: "Active", fg: COLOR_ACTIVE });
    } else {
        VDOM.set(prefix + "_ic", { fg: COLOR_DIM });
        VDOM.set(prefix + "_t",  { text: "--:--", fg: COLOR_DIM });
        VDOM.set(prefix + "_s",  { text: "Empty", fg: COLOR_DIM });
    }
}

// ---- 事件回调 ----
function onAdd() {
    if (view !== 'list') return;
    draftMin = 5; draftSec = 0;
    refreshDraft();
    showView('set');
}
function onMinPlus()  { if (view === 'set') { draftMin += 1;  refreshDraft(); } }
function onMinMinus() { if (view === 'set') { draftMin -= 1;  refreshDraft(); } }
function onSecPlus()  { if (view === 'set') { draftSec += 10; refreshDraft(); } }
function onSecMinus() { if (view === 'set') { draftSec -= 10; refreshDraft(); } }
function onCancel()   { if (view === 'set') showView('list'); }

function onSave() {
    if (view !== 'set') return;
    clampDraft();
    var totalSec = draftMin * 60 + draftSec;
    if (totalSec <= 0) { showView('list'); return; }
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        if (!slots[i].active) {
            slots[i].active = true;
            slots[i].triggerAtMs = sys.time.uptimeMs() + totalSec * 1000;
            paintSlot(i);
            showView('list');
            return;
        }
    }
    sys.log("timers: all slots full");
    showView('list');
}

function onDismiss() {
    if (view !== 'modal') return;
    if (firingSlot >= 0) {
        slots[firingSlot].active = false;
        slots[firingSlot].triggerAtMs = 0;
        paintSlot(firingSlot);
        firingSlot = -1;
    }
    showView('list');
}

// ---- 构造 slot 子树（一个工厂） ----
function makeSlotRow(idx) {
    var prefix = "slot_" + idx;
    return h('button', {
        id: prefix,
        size: [-100, 32],
        bg: COLOR_CARD,
        borderBottom: COLOR_CARD_ALT
    }, [
        h('label', { id: prefix+"_ic", text: sys.symbols.BELL, fg: COLOR_DIM,
                     font: 'text', align: ['lm', 12, 0] }),
        h('label', { id: prefix+"_t",  text: "--:--",          fg: COLOR_DIM,
                     font: 'text', align: ['lm', 40, 0] }),
        h('label', { id: prefix+"_s",  text: "Empty",          fg: COLOR_DIM,
                     font: 'text', align: ['rm', -14, 0] })
    ]);
}

// ---- 构造整棵 UI 树 ----
sys.log("timers/vdom: build start");

// 先挂一个 appRoot 容器 —— 三个视图 panel 都做它的子节点。
// 这样 attachRootListener 只需要在 appRoot 上注册一次，
// 所有按钮点击都能冒泡到这里被捕获。
VDOM.mount(
    h('panel', { id: "appRoot", size: [-100,-100], bg: COLOR_BG }),
    null
);

// List 视图
var slotRows = [];
(function () { var i; for (i = 0; i < SLOT_COUNT; i++) slotRows.push(makeSlotRow(i)); })();

VDOM.mount(
    h('panel', { id: "listPanel", size: [-100,-100], bg: COLOR_BG, flex: 'col' }, [
        h('panel', { id: "hdr", size: [-100, 40], bg: COLOR_CARD }, [
            h('label',  { id: "hdrTitle", text: "Timers", fg: COLOR_TEXT,
                          font: 'title', align: ['lm', 14, 0] }),
            h('button', { id: "addBtn", size: [36,32], bg: COLOR_ACCENT, radius: 8,
                          align: ['rm', -8, 0], onClick: onAdd }, [
                h('label', { id: "addBtnLbl", text: "+", fg: COLOR_TEXT,
                             font: 'title', align: ['c', 0, 0] })
            ])
        ]),
        h('panel', { id: "list", size: [-100, 192], bg: COLOR_CARD, flex: 'col' }, slotRows)
    ]),
    "appRoot"
);

// Set 视图
VDOM.mount(
    h('panel', { id: "setPanel", size: [-100,-100], bg: COLOR_BG,
                 align: ['c', X_OFFSCREEN, 0] }, [
        h('label', { id: "setTitle", text: "New Timer", fg: COLOR_TEXT,
                     font: 'title', align: ['tm', 0, 10] }),
        // Minutes column
        h('button', { id: "minPlus", size: [44,34], bg: COLOR_CARD, radius: 8,
                      align: ['c', -48, -48], onClick: onMinPlus }, [
            h('label', { id: "minPlusL", text: "+", fg: COLOR_ACCENT,
                         font: 'title', align: ['c', 0, 0] })
        ]),
        h('label', { id: "minVal", text: "05", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', -48, 0] }),
        h('button', { id: "minMinus", size: [44,34], bg: COLOR_CARD, radius: 8,
                      align: ['c', -48, 48], onClick: onMinMinus }, [
            h('label', { id: "minMinusL", text: "-", fg: COLOR_ACCENT,
                         font: 'title', align: ['c', 0, 0] })
        ]),
        h('label', { id: "minLbl", text: "Min", fg: COLOR_DIM,
                     font: 'text', align: ['c', -48, 80] }),
        // Colon
        h('label', { id: "colon", text: ":", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 0, 0] }),
        // Seconds column
        h('button', { id: "secPlus", size: [44,34], bg: COLOR_CARD, radius: 8,
                      align: ['c', 48, -48], onClick: onSecPlus }, [
            h('label', { id: "secPlusL", text: "+", fg: COLOR_ACCENT,
                         font: 'title', align: ['c', 0, 0] })
        ]),
        h('label', { id: "secVal", text: "00", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 48, 0] }),
        h('button', { id: "secMinus", size: [44,34], bg: COLOR_CARD, radius: 8,
                      align: ['c', 48, 48], onClick: onSecMinus }, [
            h('label', { id: "secMinusL", text: "-", fg: COLOR_ACCENT,
                         font: 'title', align: ['c', 0, 0] })
        ]),
        h('label', { id: "secLbl", text: "Sec", fg: COLOR_DIM,
                     font: 'text', align: ['c', 48, 80] }),
        // Bottom buttons
        h('button', { id: "cancelBtn", size: [90,36], bg: COLOR_CARD, radius: 8,
                      align: ['bl', 14, -14], onClick: onCancel }, [
            h('label', { id: "cancelL", text: "Cancel", fg: COLOR_TEXT,
                         font: 'text', align: ['c', 0, 0] })
        ]),
        h('button', { id: "saveBtn", size: [90,36], bg: COLOR_ACCENT, radius: 8,
                      align: ['br', -14, -14], onClick: onSave }, [
            h('label', { id: "saveL", text: "Save", fg: COLOR_TEXT,
                         font: 'text', align: ['c', 0, 0] })
        ])
    ]),
    "appRoot"
);

// Modal 视图
VDOM.mount(
    h('panel', { id: "modalPanel", size: [-100,-100], bg: COLOR_CARD,
                 align: ['c', X_OFFSCREEN, 0] }, [
        h('label', { id: "modalIcon",  text: sys.symbols.BELL, fg: COLOR_DANGER,
                     font: 'huge',  align: ['c', 0, -50] }),
        h('label', { id: "modalTitle", text: "Time's up!",     fg: COLOR_TEXT,
                     font: 'title', align: ['c', 0, 0] }),
        h('label', { id: "modalSub",   text: "--:--",          fg: COLOR_DIM,
                     font: 'text',  align: ['c', 0, 28] }),
        h('button', { id: "dismissBtn", size: [120,40], bg: COLOR_DANGER, radius: 10,
                      align: ['c', 0, 60], onClick: onDismiss }, [
            h('label', { id: "dismissL", text: "Dismiss", fg: COLOR_TEXT,
                         font: 'text', align: ['c', 0, 0] })
        ])
    ]),
    "appRoot"
);

// 一次性挂载 root listener，所有按钮点击通过这条总干线回到 JS。
sys.ui.attachRootListener("appRoot");

// 初始化显示
showView('list');
(function () { var i; for (i = 0; i < SLOT_COUNT; i++) paintSlot(i); })();

// 每秒 tick：刷新活跃 slot + 检查到期
setInterval(function () {
    var now = sys.time.uptimeMs();
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].active) paintSlot(i);
    }
    if (view !== 'modal') {
        for (i = 0; i < SLOT_COUNT; i++) {
            if (slots[i].active && slots[i].triggerAtMs <= now) {
                firingSlot = i;
                VDOM.set("modalSub", { text: "Slot " + (i + 1) });
                showView('modal');
                break;
            }
        }
    }
}, 1000);

sys.log("timers/vdom: build done");
