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
        if (props.shadow !== undefined) {
            // [color, width, ofsY]，例：[0x000000, 12, 4]
            var sh = props.shadow;
            sys.ui.setStyle(id, sys.style.SHADOW,
                sh[0] | 0, sh[1] | 0, sh[2] | 0, 0);
        }
        if (props.gap !== undefined) {
            // [rowGap, colGap]
            var g = props.gap;
            sys.ui.setStyle(id, sys.style.GAP,
                g[0] | 0, g[1] | 0, 0, 0);
        }
        if (props.scrollable !== undefined) {
            sys.ui.setStyle(id, sys.style.SCROLLABLE,
                props.scrollable ? 1 : 0, 0, 0, 0);
        }
    }

    // 事件类型 → hook 名（与 C 侧 dynamic_app_ui_event_type_t 数值对齐）
    var HOOK_NAME = {
        1: 'onClick', 2: 'onPress', 3: 'onDrag',
        4: 'onRelease', 5: 'onLongPress'
    };

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

    // ------------------ diff (L2: same-position props + sibling add/remove) ------------------
    //
    // 约定：同位置节点的 id 必须稳定（业务负责）。
    // 不支持 keyed reorder：children 重排会被识别成"删旧、加新"。
    //
    // 视觉字段一旦在 view() 里出现就会按 newProps 写入；如果某次 view() 不再带某字段，
    // 框架不会"清除"它（LVGL 没简单 reset 的 API）。业务总是显式列出依赖状态。

    var STYLE_KEYS = ['bg', 'fg', 'radius', 'size', 'align',
                      'pad', 'borderBottom', 'flex', 'font',
                      'shadow', 'gap', 'scrollable'];

    function shallowEq(a, b) {
        if (a === b) return true;
        if (a == null || b == null) return false;
        if (typeof a !== 'object' || typeof b !== 'object') return false;
        if (a.length === undefined || b.length === undefined) return false;
        if (a.length !== b.length) return false;
        var i;
        for (i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
    }

    function diffProps(oldNode, newNode) {
        var id = oldNode.props.id;
        var oldP = oldNode.props;
        var newP = newNode.props;
        var patch = {};
        var hasPatch = false;
        var i, k;

        // text 单独处理（走 setText 而非 setStyle）
        if (newP.text !== undefined && newP.text !== oldP.text) {
            sys.ui.setText(id, "" + newP.text);
        }

        // 样式字段：变化才下发
        for (i = 0; i < STYLE_KEYS.length; i++) {
            k = STYLE_KEYS[i];
            if (newP[k] !== undefined && !shallowEq(oldP[k], newP[k])) {
                patch[k] = newP[k];
                hasPatch = true;
            }
        }
        if (hasPatch) applyStyle(id, patch);

        // 事件钩子 + 其余字段：直接接管 props（dispatch 现取，自动拿到最新闭包）
        newP.id = id;
        oldNode.props = newP;
    }

    function diffNode(oldNode, newNode, parentId) {
        var id = oldNode.props.id;

        // type 不同：destroy 旧 + mount 新（同 id）；调用方负责把返回值放回 children
        if (oldNode.type !== newNode.type) {
            var parent = oldNode._parent;
            destroy(id);
            newNode._parent = parent;
            mount(newNode, parentId);
            return newNode;
        }

        diffProps(oldNode, newNode);
        diffChildren(oldNode, newNode, id);
        return oldNode;
    }

    function diffChildren(oldNode, newNode, parentId) {
        var oldKids = oldNode.children;
        var newKids = newNode.children || [];

        var oldById = {};
        var i;
        for (i = 0; i < oldKids.length; i++) {
            var oid = oldKids[i].props.id;
            if (oid) oldById[oid] = oldKids[i];
        }

        var resultKids = [];
        var seen = {};

        for (i = 0; i < newKids.length; i++) {
            var nk = newKids[i];
            var nid = nk.props.id;
            if (nid && oldById[nid]) {
                seen[nid] = true;
                var kept = diffNode(oldById[nid], nk, parentId);
                resultKids.push(kept);
            } else {
                nk._parent = oldNode;
                mount(nk, parentId);
                resultKids.push(nk);
            }
        }

        // 删除 newKids 里没再出现的
        // 注意 destroy 会从 parent.children splice 自己，但我们紧接着用 resultKids 覆盖
        var toRemove = [];
        for (i = 0; i < oldKids.length; i++) {
            var rid = oldKids[i].props.id;
            if (rid && !seen[rid]) toRemove.push(rid);
        }
        for (i = 0; i < toRemove.length; i++) destroy(toRemove[i]);

        oldNode.children = resultKids;
    }

    // 顶层入口：首次调用等价于 mount；之后比对 + 增量更新
    function render(rootDesc, parentId) {
        if (!rootDesc || !rootDesc.props || !rootDesc.props.id) {
            sys.log("VDOM.render: root must have id");
            return null;
        }
        var existing = nodes[rootDesc.props.id];
        if (!existing) return mount(rootDesc, parentId || null);
        diffNode(existing, rootDesc, parentId || null);
        return existing;
    }

    return {
        h: h, mount: mount, find: find, set: set, destroy: destroy,
        dispatch: dispatch, render: render
    };
})();

sys.__setDispatcher(function (id, type, dx, dy) { VDOM.dispatch(id, type, dx, dy); });

var h = VDOM.h;

// ============================================================================
// §2. 闹钟业务（声明式 view + rerender）
// ============================================================================
//
// 数据流：state -> view(state) -> VDOM.render(树) -> diff -> 调原语
//
// 业务只写：
//   1) state（普通对象）
//   2) view(state)（纯函数，描述当前状态对应的 UI 长啥样）
//   3) 事件回调里改 state，最后调 rerender()
//
// 例外：手势拖动 (onCardDrag) 因为每帧都触发，保留命令式 VDOM.set 旁路，
//      避免每帧重 render 整棵树。释放/点击时再走 rerender 收尾。
//
// 屏幕 240×320。布局（自上而下）：
//   - header     36   "闹钟" + ☰
//   - clockArea  80   时段 + HH:MM:SS（huge）
//   - statusBar  20   "已开启 N 个闹钟" / "所有闹钟已关闭"
//   - list       140  闹钟卡片列表
//   - fab        44   底部居中圆形 +
//
// 卡片三层（swipe-to-reveal）：
//   row_<seq>   轨道
//     ├─ del_<seq>    底层红色删除按钮
//     └─ alarm_<seq>  上层卡片本体，align lm x 0；x 由 cardSwipe[seq] 决定

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

// ---- 全局 state ----
//
// 持久化策略：
//   - 启动时 sys.app.loadState() 拿到 JSON 字符串，没数据返回 null
//   - 只持久化"业务状态"：alarms / nextSeq
//   - UI 临时态（editSeq / clockPeriod / clockMain）每次启动都重置成默认
//   - 改 state 的事件回调末尾调一次 persist()，立即落盘

var DEFAULT_ALARMS = [
    { seq: 0, tag: "清晨", time: "6:55", sub: "闹钟，每天",   on: false },
    { seq: 1, tag: "早上", time: "8:20", sub: "早上好，每天", on: true  },
    { seq: 2, tag: "早上", time: "8:30", sub: "早上好，每天", on: false }
];

function loadState() {
    var raw = sys.app.loadState();
    if (!raw) return null;
    try { return JSON.parse(raw); }
    catch (e) { sys.log("loadState: bad JSON, ignored"); return null; }
}

var saved = loadState();
var state = {
    alarms:      saved && saved.alarms  ? saved.alarms  : DEFAULT_ALARMS,
    nextSeq:     saved && saved.nextSeq ? saved.nextSeq : 100,
    editSeq:     -1,
    clockPeriod: "上午",
    clockMain:   "00:00:00"
};

function persist() {
    sys.app.saveState(JSON.stringify({
        alarms:  state.alarms,
        nextSeq: state.nextSeq
    }));
}

// 卡片手势状态：seq -> { phase: 'rest'|'dragging'|'open', x: 当前位移 }
// 不放进 state —— 这是临时 UI 态，不该触发 rerender
var cardSwipe = {};

function alarmRowId(seq)     { return "row_"   + seq; }
function alarmCardId(seq)    { return "alarm_" + seq; }
function alarmDelId(seq)     { return "del_"   + seq; }
function alarmSwitchId(seq)  { return "sw_"    + seq; }
function alarmKnobId(seq)    { return "knob_"  + seq; }
function alarmTimeId(seq)    { return "tm_"    + seq; }

function findIndexBySeq(seq) {
    var i;
    for (i = 0; i < state.alarms.length; i++) {
        if (state.alarms[i].seq === seq) return i;
    }
    return -1;
}

function statusMsg() {
    var n = 0, i;
    for (i = 0; i < state.alarms.length; i++) if (state.alarms[i].on) n++;
    return (n === 0) ? "所有闹钟已关闭" : ("已开启 " + n + " 个闹钟");
}

// ---- 关闭其他露出卡片（命令式旁路：临时 UI 态收敛，不重 render）----
function closeAllExcept(exceptSeq) {
    var k;
    for (k in cardSwipe) {
        if (!cardSwipe.hasOwnProperty(k)) continue;
        if (k === String(exceptSeq)) continue;
        if (cardSwipe[k].phase === 'open') {
            cardSwipe[k].phase = 'rest';
            cardSwipe[k].x = 0;
            VDOM.set(alarmCardId(parseInt(k, 10)), { align: ['lm', 0, 0] });
        }
    }
}

// ---- 手势 hooks ----
function onCardPress(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    if (!cardSwipe[seq]) cardSwipe[seq] = { phase: 'rest', x: 0 };
    cardSwipe[seq].phase = 'dragging';
}

function onCardDrag(e) {
    // 每帧触发：走命令式旁路，不重 render
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    if (!s || s.phase !== 'dragging') return;
    var nx = s.x + e.dx;
    if (nx > 0)               nx = 0;
    if (nx < -SWIPE_REVEAL_PX) nx = -SWIPE_REVEAL_PX;
    s.x = nx;
    VDOM.set(alarmCardId(seq), { align: ['lm', nx, 0] });
}

function onCardRelease(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    if (!s || s.phase !== 'dragging') return;
    var dragged = (s.x !== 0);
    if (s.x <= -SWIPE_THRESHOLD) {
        s.phase = 'open';
        s.x = -SWIPE_REVEAL_PX;
        VDOM.set(alarmCardId(seq), { align: ['lm', -SWIPE_REVEAL_PX, 0] });
        closeAllExcept(seq);
    } else {
        s.phase = 'rest';
        s.x = 0;
        VDOM.set(alarmCardId(seq), { align: ['lm', 0, 0] });
    }
    // LVGL 在小于 gesture 阈值时 release 后还会发 CLICKED；
    // 我们这边业务已知"刚才拖过"，标记一下，紧跟着的 click 直接吞掉。
    if (dragged) s.suppressClick = true;
}

function onCardClick(e) {
    var seq = parseInt(e.currentTarget.substring(6), 10);
    var s = cardSwipe[seq];
    if (s && s.suppressClick) {
        s.suppressClick = false;
        return;
    }
    if (s && s.phase === 'open') {
        s.phase = 'rest';
        s.x = 0;
        VDOM.set(alarmCardId(seq), { align: ['lm', 0, 0] });
        return;
    }
    closeAllExcept(seq);
    state.editSeq = seq;
    rerender();
}

function onCardLongPress(e) {
    // 长按直接吸附到 open，露出删除按钮（提供滑动之外的另一条路径）
    var seq = parseInt(e.currentTarget.substring(6), 10);
    if (!cardSwipe[seq]) cardSwipe[seq] = { phase: 'rest', x: 0 };
    cardSwipe[seq].phase = 'open';
    cardSwipe[seq].x = -SWIPE_REVEAL_PX;
    VDOM.set(alarmCardId(seq), { align: ['lm', -SWIPE_REVEAL_PX, 0] });
    closeAllExcept(seq);
}

function onSwitchClick(e) {
    var seq = parseInt(e.currentTarget.substring(3), 10);
    var idx = findIndexBySeq(seq);
    if (idx < 0) return false;
    state.alarms[idx].on = !state.alarms[idx].on;
    persist();
    rerender();
    return false;
}

function onDelClick(e) {
    var seq = parseInt(e.currentTarget.substring(4), 10);
    var idx = findIndexBySeq(seq);
    if (idx < 0) return false;
    state.alarms.splice(idx, 1);
    delete cardSwipe[seq];
    persist();
    rerender();
    sys.log("alarm removed: seq=" + seq);
    return false;
}

function onAddClick() {
    var seq = state.nextSeq++;
    state.alarms.push({
        seq: seq, tag: "上午", time: "9:00",
        sub: "闹钟，仅一次", on: true
    });
    persist();
    rerender();
    sys.log("alarm added: seq=" + seq);
}

function closeEditView() {
    state.editSeq = -1;
    rerender();
}

// ============================================================================
// §3. view —— 纯函数，描述 state 对应的 UI 长什么样
// ============================================================================

function viewAlarmRow(a) {
    var seq = a.seq;
    return h('panel', {
        id: alarmRowId(seq),
        size: [-100, 56],
        bg: COLOR_BG
    }, [
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
        h('button', {
            id: alarmCardId(seq),
            size: [-100, 56],
            bg: COLOR_CARD,
            radius: 14,
            shadow: [0x000000, 10, 3],
            // 跟随当前手势位移：rerender 时不会把已露出的卡片瞬移回 0
            align: ['lm', (cardSwipe[seq] && cardSwipe[seq].x) || 0, 0],
            onPress:     onCardPress,
            onDrag:      onCardDrag,
            onRelease:   onCardRelease,
            onClick:     onCardClick,
            onLongPress: onCardLongPress
        }, [
            h('label', { id: "tag_" + seq, text: a.tag, fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['lm', 14, -10] }),
            h('label', { id: alarmTimeId(seq), text: a.time,
                         fg: a.on ? COLOR_TEXT : COLOR_TEXT_OFF,
                         font: 'title', align: ['lm', 42, -10] }),
            h('label', { id: "sub_" + seq, text: a.sub, fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['lm', 14, 14] }),
            h('button', {
                id: alarmSwitchId(seq),
                size: [36, 22],
                radius: 11,
                bg: a.on ? COLOR_ACCENT : COLOR_SWITCH_OFF,
                align: ['rm', -12, 0],
                onClick: onSwitchClick
            }, [
                h('label', { id: alarmKnobId(seq), text: " ", bg: COLOR_KNOB,
                             size: [16, 16], radius: 8,
                             align: ['lm', a.on ? 18 : 2, 0] })
            ])
        ])
    ]);
}

function viewEditView() {
    var idx = findIndexBySeq(state.editSeq);
    if (idx < 0) return null;
    var a = state.alarms[idx];
    return h('panel', { id: "editView", size: [-100, -100], bg: COLOR_BG,
                         align: ['tl', 0, 0], scrollable: false }, [
        h('panel', { id: "editHdr", size: [-100, 36], bg: COLOR_HEADER,
                     align: ['tm', 0, 0] }, [
            h('button', { id: "editBack", size: [60, 36], bg: COLOR_HEADER,
                          align: ['lm', 0, 0],
                          onClick: closeEditView }, [
                h('label', { id: "editBackL", text: sys.symbols.LEFT,
                             fg: COLOR_TEXT, font: 'text',
                             align: ['c', 0, 0] })
            ]),
            h('label', { id: "editTitle", text: "编辑闹钟", fg: COLOR_TEXT,
                         font: 'title', align: ['c', 0, 0] }),
            h('button', { id: "editDone", size: [60, 36], bg: COLOR_HEADER,
                          align: ['rm', 0, 0],
                          onClick: closeEditView }, [
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
    ]);
}

function view() {
    var rowKids = [];
    var i;
    for (i = 0; i < state.alarms.length; i++) {
        rowKids.push(viewAlarmRow(state.alarms[i]));
    }

    var rootKids = [
        h('panel', { id: "header", size: [-100, 36], bg: COLOR_HEADER,
                     align: ['tm', 0, 0] }, [
            h('label', { id: "headerTitle", text: "闹钟", fg: COLOR_TEXT,
                         font: 'title', align: ['lm', 14, 0] }),
            h('label', { id: "headerMore", text: sys.symbols.BARS,
                         fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['rm', -14, 0] })
        ]),
        h('panel', { id: "clockArea", size: [-100, 80], bg: COLOR_BG,
                     align: ['tm', 0, 36] }, [
            h('label', { id: "clockPeriod", text: state.clockPeriod,
                         fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['c', -82, -2] }),
            h('label', { id: "clockMain", text: state.clockMain, fg: COLOR_TEXT,
                         font: 'huge', align: ['c', 14, 0] })
        ]),
        h('panel', { id: "statusBar", size: [-100, 20], bg: COLOR_BG,
                     align: ['tm', 0, 116] }, [
            h('label', { id: "statusLine", text: statusMsg(),
                         fg: COLOR_TEXT_DIM,
                         font: 'text', align: ['c', 0, 0] })
        ]),
        h('panel', { id: "list", size: [-100, 132], bg: COLOR_BG, flex: 'col',
                     align: ['tm', 0, 136],
                     pad: [8, 4, 8, 4],
                     gap: [6, 0],
                     scrollable: true }, rowKids),
        h('button', { id: "fab", size: [44, 44], radius: 22, bg: COLOR_ACCENT,
                      align: ['bm', 0, -8], onClick: onAddClick }, [
            h('label', { id: "fabL", text: "+", fg: COLOR_TEXT,
                         font: 'huge', align: ['c', 0, -4] })
        ])
    ];

    if (state.editSeq >= 0) {
        var ev = viewEditView();
        if (ev) rootKids.push(ev);
    }

    return h('panel', { id: "appRoot", size: [-100, -100], bg: COLOR_BG,
                         scrollable: false },
             rootKids);
}

function rerender() { VDOM.render(view(), null); }

// ---- 时钟 tick ----
function pad2(n) { n = n | 0; return n < 10 ? "0" + n : "" + n; }
function tickClock() {
    var totalSec = (sys.time.uptimeMs() / 1000) | 0;
    var s = totalSec % 60;
    var m = (totalSec / 60 | 0) % 60;
    var hh = (totalSec / 3600 | 0) % 24;
    state.clockPeriod = (hh < 6) ? "凌晨"
                      : (hh < 12) ? "上午"
                      : (hh < 13) ? "中午"
                      : (hh < 18) ? "下午"
                      : "晚上";
    state.clockMain = pad2(hh) + ":" + pad2(m) + ":" + pad2(s);
    rerender();
}

// ============================================================================
// §4. 启动
// ============================================================================

sys.log("alarm: build start");

rerender();
sys.ui.attachRootListener("appRoot");

tickClock();
setInterval(tickClock, 1000);

sys.log("alarm: build done");
