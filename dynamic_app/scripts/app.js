// Dynamic App —— 闹钟页面（华为风格复刻，240×320 适配）
//
// 本文件分两段：
//   §1. VDOM 框架（通用，可复用）
//   §2. 闹钟业务
//
// 设计原则：
//   - VDOM 节点 = { type, props, children, _parent }，靠 props.id 寻址
//   - 在最外层 root container 调一次 sys.ui.attachRootListener，之后
//     所有子按钮的点击都由 LVGL 冒泡到 root，C 侧的 on_lv_root_event
//     拿到被点对象的 id 字符串，通过事件队列回到 JS 侧由 dispatcher
//     沿 _parent 链上爬触发 onClick。
//   - 整页只挂 1 次 native cb；子节点都不调 sys.ui.* 的事件 API。
//   - 状态变化 → 显式调 vdom.set(id, partialProps) 或 vdom.destroy/mount
//
// 约束：esp-mquickjs 仅支持 ES5。

// ============================================================================
// §1. VDOM 框架
// ============================================================================

var VDOM = (function () {

    var nodes = {};
    var autoSeq = 0;

    var FONT_MAP  = { text: sys.font.TEXT, title: sys.font.TITLE, huge: sys.font.HUGE };
    var ALIGN_MAP = {
        tl: sys.align.TOP_LEFT,    tm: sys.align.TOP_MID,    tr: sys.align.TOP_RIGHT,
        lm: sys.align.LEFT_MID,    c:  sys.align.CENTER,     rm: sys.align.RIGHT_MID,
        bl: sys.align.BOTTOM_LEFT, bm: sys.align.BOTTOM_MID, br: sys.align.BOTTOM_RIGHT
    };

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

    // 事件类型 → hook 名（与 C 侧 dynamic_app_ui_event_type_t 数值对齐）
    var HOOK_NAME = { 1: 'onClick', 2: 'onPress', 3: 'onDrag', 4: 'onRelease' };

    // 事件冒泡 dispatcher
    //
    //   流程：用户操作 → root listener 入队 { type, dx, dy, node_id }
    //         → C drain 调 dispatcher(id, type, dx, dy)
    //         → 沿 _parent 链上爬，每经过一个 vnode 检查对应 hook
    //         → handler 收到 ev = { target, currentTarget, dx, dy, stopPropagation() }
    //         → handler 返回 false 或调 ev.stopPropagation() 终止冒泡
    function dispatch(startId, type, dx, dy) {
        var node = nodes[startId];
        if (!node) return;
        var hook = HOOK_NAME[type];
        if (!hook) return;
        var stopped = false;
        var ev = {
            target: startId,
            currentTarget: null,
            dx: dx | 0,
            dy: dy | 0,
            stopPropagation: function () { stopped = true; }
        };
        var cur = node;
        while (cur) {
            if (typeof cur.props[hook] === 'function') {
                ev.currentTarget = cur.props.id;
                var ret = cur.props[hook](ev);
                if (ret === false || stopped) return;
            }
            cur = cur._parent;
        }
    }

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

        nodes[id] = node;
        node._mounted = true;

        var i;
        for (i = 0; i < node.children.length; i++) {
            node.children[i]._parent = node;
            mount(node.children[i], id);
        }
        return node;
    }

    function find(id) { return nodes[id] || null; }

    function set(id, patch) {
        var node = nodes[id];
        if (!node) { sys.log("VDOM.set: id not found: " + id); return; }
        var k;
        for (k in patch) {
            if (!patch.hasOwnProperty(k)) continue;
            node.props[k] = patch[k];
        }
        if (patch.text !== undefined) sys.ui.setText(id, "" + patch.text);
        applyStyle(id, patch);
    }

    function destroy(id) {
        var node = nodes[id];
        if (!node) return;
        var kids = node.children.slice();
        var i;
        for (i = 0; i < kids.length; i++) {
            if (kids[i].props && kids[i].props.id) destroy(kids[i].props.id);
        }
        var parent = node._parent;
        if (parent && parent.children) {
            var idx = parent.children.indexOf(node);
            if (idx >= 0) parent.children.splice(idx, 1);
        }
        sys.ui.destroy(id);
        delete nodes[id];
    }

    return { h: h, mount: mount, find: find, set: set, destroy: destroy, dispatch: dispatch };
})();

sys.__setDispatcher(function (id, type, dx, dy) { VDOM.dispatch(id, type, dx, dy); });

var h = VDOM.h;

// ============================================================================
// §2. 闹钟业务
// ============================================================================
//
// 屏幕 240×320。布局（自上而下）：
//   - header        高 36：标题"闹钟" + 右上 ☰ 装饰
//   - clockArea     高 80：当前时间大字（HH:MM:SS）+ 上方时段标签
//   - statusLine    高 20：'已开启 N 个闹钟' / '所有闹钟已关闭'
//   - listArea      140：闹钟卡片列表
//   - fabArea       44：底部居中圆形 +
//
// 卡片结构（swipe-to-reveal）：
//   row_<seq>  panel    ← flex 项，固定 [-100, 56]，做"轨道"，clip 出对外可视区
//     ├─ del_<seq>   button  ← 红色"删除"按钮，绝对定位贴 row 右边 80×56
//     └─ alarm_<seq> button  ← 卡片本体，align 起点 ['lm', 0, 0]
//                              手势期间按 swipeX 平移；阈值过则吸附到 -80
//                              露出底下的删除按钮
//
// 视图切换：
//   listView 和 editView 是两套子树。listView 静态保留；
//   编辑时 mount editView，按返回销毁 editView。

var COLOR_BG          = 0x1A1530;
var COLOR_HEADER      = 0x2D2640;
var COLOR_CARD        = 0x2D2640;
var COLOR_TEXT        = 0xF1ECFF;
var COLOR_TEXT_DIM    = 0x9088A8;
var COLOR_TEXT_OFF    = 0x6B6480;
var COLOR_ACCENT      = 0x007DFF;
var COLOR_SWITCH_OFF  = 0x4A4360;
var COLOR_KNOB        = 0xFFFFFF;
var COLOR_DANGER      = 0xEF4444;

var SWIPE_REVEAL_PX   = 80;    // 露出删除按钮宽度
var SWIPE_THRESHOLD   = 30;    // 松手时位移超过这个就吸附到 open

// ---- 闹钟数据 ----
var alarms = [
    { tag: "清晨", time: "6:55", sub: "闹钟，每天",   on: false },
    { tag: "早上", time: "8:20", sub: "早上好，每天", on: true  },
    { tag: "早上", time: "8:30", sub: "早上好，每天", on: false }
];
var nextAlarmSeq = 100;

// 卡片手势状态：seq -> { phase: 'rest'|'dragging'|'open', x: 当前位移 }
var cardSwipe = {};

function alarmRowId(seq)     { return "row_"   + seq; }
function alarmCardId(seq)    { return "alarm_" + seq; }
function alarmDelId(seq)     { return "del_"   + seq; }
function alarmSwitchId(seq)  { return "sw_"    + seq; }
function alarmKnobId(seq)    { return "knob_"  + seq; }
function alarmTimeId(seq)    { return "tm_"    + seq; }

function findIndexBySeq(seq) {
    var i;
    for (i = 0; i < alarms.length; i++) {
        if (alarms[i].seq === seq) return i;
    }
    return -1;
}

// ---- 状态行 ----
function refreshStatus() {
    var n = 0;
    var i;
    for (i = 0; i < alarms.length; i++) if (alarms[i].on) n++;
    var msg = (n === 0) ? "所有闹钟已关闭" : ("已开启 " + n + " 个闹钟");
    VDOM.set("statusLine", { text: msg });
}

// ---- 单卡视觉刷新 ----
function refreshCard(seq) {
    var idx = findIndexBySeq(seq);
    if (idx < 0) return;
    var on = alarms[idx].on;
    VDOM.set(alarmTimeId(seq),   { fg: on ? COLOR_TEXT : COLOR_TEXT_OFF });
    VDOM.set(alarmSwitchId(seq), { bg: on ? COLOR_ACCENT : COLOR_SWITCH_OFF });
    VDOM.set(alarmKnobId(seq),   { align: ['lm', on ? 18 : 2, 0] });
}

// ---- 卡片位移工具 ----
function setCardX(seq, x) {
    cardSwipe[seq].x = x;
    VDOM.set(alarmCardId(seq), { align: ['lm', x, 0] });
}

// 收起所有露出的卡片（点新卡片或回弹时用）
function closeAllExcept(exceptSeq) {
    var k;
    for (k in cardSwipe) {
        if (!cardSwipe.hasOwnProperty(k)) continue;
        if (k === String(exceptSeq)) continue;
        if (cardSwipe[k].phase === 'open') {
            cardSwipe[k].phase = 'rest';
            setCardX(parseInt(k, 10), 0);
        }
    }
}

// ---- 手势 hooks ----
function onCardPress(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);  // alarm_<seq>
    if (!cardSwipe[seq]) cardSwipe[seq] = { phase: 'rest', x: 0 };
    // 起始 phase：从 rest 进 dragging；若已 open，按住时也允许继续左右拖
    cardSwipe[seq].phase = 'dragging';
}

function onCardDrag(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    if (!s || s.phase !== 'dragging') return;
    var nx = s.x + e.dx;
    if (nx > 0)               nx = 0;
    if (nx < -SWIPE_REVEAL_PX) nx = -SWIPE_REVEAL_PX;
    setCardX(seq, nx);
}

function onCardRelease(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    if (!s || s.phase !== 'dragging') return;
    if (s.x <= -SWIPE_THRESHOLD) {
        // 吸附到 open
        s.phase = 'open';
        setCardX(seq, -SWIPE_REVEAL_PX);
        closeAllExcept(seq);
    } else {
        s.phase = 'rest';
        setCardX(seq, 0);
    }
}

function onCardClick(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    // 如果当前处于 open 状态，点击卡片本体只是收起，不进编辑
    if (s && s.phase === 'open') {
        s.phase = 'rest';
        setCardX(seq, 0);
        return;
    }
    // 收起其他露出的卡片
    closeAllExcept(seq);
    // 进入编辑页
    openEditView(seq);
}

function onSwitchClick(e) {
    var seq = parseInt(e.currentTarget.substring(3), 10);
    var idx = findIndexBySeq(seq);
    if (idx < 0) return false;
    alarms[idx].on = !alarms[idx].on;
    refreshCard(seq);
    refreshStatus();
    return false;   // 阻止冒泡到卡片
}

function onDelClick(e) {
    var seq = parseInt(e.currentTarget.substring(4), 10);  // del_<seq>
    var idx = findIndexBySeq(seq);
    if (idx < 0) return false;
    alarms.splice(idx, 1);
    delete cardSwipe[seq];
    VDOM.destroy(alarmRowId(seq));
    refreshStatus();
    sys.log("alarm removed: seq=" + seq);
    return false;
}

function onAddClick() {
    var a = {
        tag: "上午", time: "9:00", sub: "闹钟，仅一次", on: true,
        seq: nextAlarmSeq++
    };
    alarms.push(a);
    mountAlarmRow(a);
    refreshCard(a.seq);
    refreshStatus();
    sys.log("alarm added: seq=" + a.seq);
}

// ---- 卡片工厂 ----
function makeAlarmRow(a) {
    var seq = a.seq;
    return h('panel', {
        id: alarmRowId(seq),
        size: [-100, 56],
        bg: COLOR_BG
    }, [
        // 红色删除按钮在底层，贴右
        h('button', {
            id: alarmDelId(seq),
            size: [SWIPE_REVEAL_PX, 56],
            bg: COLOR_DANGER,
            radius: 14,
            align: ['rm', 0, 0],
            onClick: onDelClick
        }, [
            h('label', { id: "delL_" + seq, text: "删除", fg: COLOR_TEXT,
                         font: 'text', align: ['c', 0, 0] })
        ]),
        // 卡片本体在上层，初始覆盖整宽 align lm 0
        h('button', {
            id: alarmCardId(seq),
            size: [-100, 56],
            bg: COLOR_CARD,
            radius: 14,
            align: ['lm', 0, 0],
            onPress:   onCardPress,
            onDrag:    onCardDrag,
            onRelease: onCardRelease,
            onClick:   onCardClick
        }, [
            h('label', { id: "tag_" + seq, text: a.tag, fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['lm', 14, -10] }),
            h('label', { id: alarmTimeId(seq), text: a.time, fg: COLOR_TEXT,
                         font: 'title', align: ['lm', 42, -10] }),
            h('label', { id: "sub_" + seq, text: a.sub, fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['lm', 14, 14] }),
            h('button', {
                id: alarmSwitchId(seq),
                size: [36, 22],
                radius: 11,
                bg: COLOR_SWITCH_OFF,
                align: ['rm', -12, 0],
                onClick: onSwitchClick
            }, [
                h('label', { id: alarmKnobId(seq), text: " ", bg: COLOR_KNOB,
                             size: [16, 16], radius: 8,
                             align: ['lm', 2, 0] })
            ])
        ])
    ]);
}

function mountAlarmRow(a) {
    VDOM.mount(makeAlarmRow(a), "list");
    cardSwipe[a.seq] = { phase: 'rest', x: 0 };
}

// ---- 编辑页 ----
function openEditView(seq) {
    var idx = findIndexBySeq(seq);
    if (idx < 0) return;
    var a = alarms[idx];
    if (VDOM.find("editView")) return;   // 已开就别重复开
    VDOM.mount(
        h('panel', { id: "editView", size: [-100, -100], bg: COLOR_BG }, [
            h('panel', { id: "editHdr", size: [-100, 36], bg: COLOR_HEADER,
                         align: ['tm', 0, 0] }, [
                h('button', { id: "editBack", size: [60, 36], bg: COLOR_HEADER,
                              align: ['lm', 0, 0],
                              onClick: function () { closeEditView(); } }, [
                    h('label', { id: "editBackL", text: sys.symbols.LEFT,
                                 fg: COLOR_TEXT, font: 'text',
                                 align: ['c', 0, 0] })
                ]),
                h('label', { id: "editTitle", text: "编辑闹钟", fg: COLOR_TEXT,
                             font: 'title', align: ['c', 0, 0] }),
                h('button', { id: "editDone", size: [60, 36], bg: COLOR_HEADER,
                              align: ['rm', 0, 0],
                              onClick: function () { closeEditView(); } }, [
                    h('label', { id: "editDoneL", text: "完成",
                                 fg: COLOR_ACCENT, font: 'text',
                                 align: ['c', 0, 0] })
                ])
            ]),
            h('label', { id: "editLine1", text: a.tag + " " + a.time,
                         fg: COLOR_TEXT, font: 'huge',
                         align: ['tm', 0, 60] }),
            h('label', { id: "editLine2", text: a.sub,
                         fg: COLOR_TEXT_DIM, font: 'text',
                         align: ['tm', 0, 130] }),
            h('label', { id: "editHint", text: "（编辑功能待实现）",
                         fg: COLOR_TEXT_OFF, font: 'text',
                         align: ['tm', 0, 170] })
        ]),
        "appRoot"
    );
}

function closeEditView() {
    if (VDOM.find("editView")) VDOM.destroy("editView");
}

// ---- 时钟 tick ----
function pad2(n) { n = n | 0; return n < 10 ? "0" + n : "" + n; }
function refreshClock() {
    var totalSec = (sys.time.uptimeMs() / 1000) | 0;
    var s = totalSec % 60;
    var m = (totalSec / 60 | 0) % 60;
    var hh = (totalSec / 3600 | 0) % 24;
    var period = (hh < 6) ? "凌晨"
               : (hh < 12) ? "上午"
               : (hh < 13) ? "中午"
               : (hh < 18) ? "下午"
               : "晚上";
    if (VDOM.find("clockPeriod")) VDOM.set("clockPeriod", { text: period });
    if (VDOM.find("clockMain"))   VDOM.set("clockMain",   { text: pad2(hh) + ":" + pad2(m) + ":" + pad2(s) });
}

// ============================================================================
// §3. 构造 UI 树
// ============================================================================

sys.log("alarm: build start");

VDOM.mount(
    h('panel', { id: "appRoot", size: [-100, -100], bg: COLOR_BG }),
    null
);

VDOM.mount(
    h('panel', { id: "header", size: [-100, 36], bg: COLOR_HEADER,
                 align: ['tm', 0, 0] }, [
        h('label', { id: "headerTitle", text: "闹钟", fg: COLOR_TEXT,
                     font: 'title', align: ['lm', 14, 0] }),
        h('label', { id: "headerMore", text: sys.symbols.BARS, fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['rm', -14, 0] })
    ]),
    "appRoot"
);

VDOM.mount(
    h('panel', { id: "clockArea", size: [-100, 80], bg: COLOR_BG,
                 align: ['tm', 0, 36] }, [
        h('label', { id: "clockPeriod", text: "上午", fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['c', -82, -2] }),
        h('label', { id: "clockMain", text: "00:00:00", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 14, 0] })
    ]),
    "appRoot"
);

VDOM.mount(
    h('panel', { id: "statusBar", size: [-100, 20], bg: COLOR_BG,
                 align: ['tm', 0, 116] }, [
        h('label', { id: "statusLine", text: "...", fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['c', 0, 0] })
    ]),
    "appRoot"
);

VDOM.mount(
    h('panel', { id: "list", size: [-100, 140], bg: COLOR_BG, flex: 'col',
                 align: ['tm', 0, 136],
                 pad: [8, 4, 8, 4] }),
    "appRoot"
);

(function () {
    var i;
    for (i = 0; i < alarms.length; i++) {
        alarms[i].seq = i;
        mountAlarmRow(alarms[i]);
    }
})();

VDOM.mount(
    h('button', { id: "fab", size: [44, 44], radius: 22, bg: COLOR_ACCENT,
                  align: ['bm', 0, -8], onClick: onAddClick }, [
        h('label', { id: "fabL", text: "+", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 0, -4] })
    ]),
    "appRoot"
);

sys.ui.attachRootListener("appRoot");

refreshClock();
refreshStatus();
(function () {
    var i;
    for (i = 0; i < alarms.length; i++) refreshCard(alarms[i].seq);
})();

setInterval(refreshClock, 1000);

sys.log("alarm: build done");
