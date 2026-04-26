// Dynamic App —— 计算器（240×320）v2
//
// 相比 v1 的改动：
//   1) 双行 display：上面小灰字 expr（"12 × 3"），下面大字 display
//   2) 按下反馈：onPress 拉亮 / onRelease 复位（用 native style 即时反馈）
//   3) 布局参数化：DISP_H / BTN_H / GAP / N_COLS 一次定义，每行 Y 累加
//   4) 配色调整：数字键浅一档、运算符更醒目，等号绿色
//   5) 0 占两格用 BTN_W*2+GAP 横向占位
//
// 不变：架构沿用 alarm.js 那套 VDOM；不持久化。
// 约束：esp-mquickjs 仅支持 ES5。

// ============================================================================
// §1. VDOM 框架（拷自 alarm.js，保持 app 完全独立）
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
        if (props.flex !== undefined)
            sys.ui.setStyle(id, sys.style.FLEX,
                props.flex === 'row' ? 1 : 0, 0, 0, 0);
        if (props.font !== undefined) {
            var f = FONT_MAP[props.font];
            if (f === undefined) f = sys.font.TEXT;
            sys.ui.setStyle(id, sys.style.FONT, f, 0, 0, 0);
        }
        if (props.shadow !== undefined) {
            var sh = props.shadow;
            sys.ui.setStyle(id, sys.style.SHADOW,
                sh[0] | 0, sh[1] | 0, sh[2] | 0, 0);
        }
        if (props.gap !== undefined) {
            var g = props.gap;
            sys.ui.setStyle(id, sys.style.GAP, g[0] | 0, g[1] | 0, 0, 0);
        }
        if (props.scrollable !== undefined) {
            sys.ui.setStyle(id, sys.style.SCROLLABLE,
                props.scrollable ? 1 : 0, 0, 0, 0);
        }
    }

    var HOOK_NAME = {
        1: 'onClick', 2: 'onPress', 3: 'onDrag',
        4: 'onRelease', 5: 'onLongPress'
    };

    function dispatch(startId, type, dx, dy) {
        var node = nodes[startId];
        if (!node) return;
        var hook = HOOK_NAME[type];
        if (!hook) return;
        var stopped = false;
        var ev = {
            target: startId, currentTarget: null,
            dx: dx | 0, dy: dy | 0,
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
        if (!node) return;
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

    var STYLE_KEYS = ['bg', 'fg', 'radius', 'size', 'align',
                      'pad', 'flex', 'font',
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

        if (newP.text !== undefined && newP.text !== oldP.text) {
            sys.ui.setText(id, "" + newP.text);
        }
        for (i = 0; i < STYLE_KEYS.length; i++) {
            k = STYLE_KEYS[i];
            if (newP[k] !== undefined && !shallowEq(oldP[k], newP[k])) {
                patch[k] = newP[k];
                hasPatch = true;
            }
        }
        if (hasPatch) applyStyle(id, patch);

        newP.id = id;
        oldNode.props = newP;
    }

    function diffNode(oldNode, newNode, parentId) {
        var id = oldNode.props.id;
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
        var toRemove = [];
        for (i = 0; i < oldKids.length; i++) {
            var rid = oldKids[i].props.id;
            if (rid && !seen[rid]) toRemove.push(rid);
        }
        for (i = 0; i < toRemove.length; i++) destroy(toRemove[i]);
        oldNode.children = resultKids;
    }

    function render(rootDesc, parentId) {
        if (!rootDesc || !rootDesc.props || !rootDesc.props.id) return null;
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
// §2. 计算器业务
// ============================================================================
//
// 状态机：
//   display       当前屏幕显示的字符串
//   prev          上一个操作数（数字 string）；null 表示无
//   op            上一次按下的运算符 '+' '-' '×' '÷'；null 表示无
//   expr          顶部历史行（"12 × " / "12 × 3 ="），UI 提示用
//   justEval      刚按过 '='：下一个数字键应清屏重输
//   newEntry      下一个数字键应清屏重输（按运算符后置 true）

// ---- 配色 ----
var COL_BG       = 0x14101F;   // 整体底色
var COL_DISP_BG  = 0x0A0816;   // 显示区底
var COL_DISP_FG  = 0xFFFFFF;   // 大字
var COL_EXPR_FG  = 0x8B8AA8;   // 历史小字
var COL_NUM      = 0x4A4368;   // 数字键 (浅一档紫)
var COL_NUM_HI   = 0x6C638F;   // 数字键按下
var COL_FN       = 0x3A3454;   // C/±/% 功能键 (深一档)
var COL_FN_HI    = 0x534B73;
var COL_OP       = 0xF59E0B;   // 运算符 (橙)
var COL_OP_HI    = 0xFCD34D;   // 运算符按下/选中态
var COL_EQ       = 0x10B981;   // 等号 (绿)
var COL_EQ_HI    = 0x34D399;
var COL_TEXT     = 0xF1ECFF;
var COL_TEXT_DK  = 0xFFFFFF;

// ---- 布局常量（一次定义，UI 跟着变） ----
var SCR_W        = 240;
var SCR_H        = 320;
// 总高校核：DISP_H + GAP + 5*(BTN_H+GAP) = 70+5 + 5*49 = 320 ✓
var DISP_H       = 70;          // 双行 display 高度
var BTN_H        = 44;
var GAP          = 5;
var N_COLS       = 4;
var SIDE_PAD     = 8;
var BTN_W        = ((SCR_W - SIDE_PAD * 2 - GAP * (N_COLS - 1)) / N_COLS) | 0;  // 52

var state = {
    display:  "0",
    prev:     null,
    op:       null,
    expr:     "",
    justEval: false,
    newEntry: false
};

function fmt(n) {
    if (!isFinite(n)) return "Err";
    var s = "" + n;
    if (s.length > 12) {
        s = n.toPrecision(9);
        if (s.indexOf('e') < 0 && s.indexOf('.') >= 0) {
            s = s.replace(/0+$/, '').replace(/\.$/, '');
        }
    }
    return s;
}

function compute(a, b, op) {
    a = parseFloat(a); b = parseFloat(b);
    if (op === '+') return a + b;
    if (op === '-') return a - b;
    if (op === '×') return a * b;
    if (op === '÷') return b === 0 ? NaN : a / b;
    return b;
}

function pressDigit(d) {
    if (state.justEval) {
        state.display = d;
        state.expr = "";
        state.justEval = false;
        state.newEntry = false;
        return;
    }
    if (state.newEntry || state.display === "0") {
        state.display = d;
        state.newEntry = false;
    } else {
        if (state.display.length < 12) state.display += d;
    }
}

function pressDot() {
    if (state.justEval) {
        state.display = "0.";
        state.expr = "";
        state.justEval = false;
        state.newEntry = false;
        return;
    }
    if (state.newEntry) {
        state.display = "0.";
        state.newEntry = false;
        return;
    }
    if (state.display.indexOf('.') < 0) state.display += ".";
}

function pressOp(op) {
    if (state.prev !== null && state.op !== null && !state.newEntry) {
        var r = compute(state.prev, state.display, state.op);
        state.display = fmt(r);
    }
    state.prev = state.display;
    state.op = op;
    state.expr = state.display + " " + op;
    state.newEntry = true;
    state.justEval = false;
}

function pressEq() {
    if (state.op === null || state.prev === null) return;
    var r = compute(state.prev, state.display, state.op);
    state.expr = state.prev + " " + state.op + " " + state.display + " =";
    state.display = fmt(r);
    state.prev = null;
    state.op = null;
    state.justEval = true;
    state.newEntry = false;
}

function pressClear() {
    state.display = "0";
    state.prev = null;
    state.op = null;
    state.expr = "";
    state.justEval = false;
    state.newEntry = false;
}

function pressSign() {
    if (state.display === "0" || state.display === "Err") return;
    if (state.display.charAt(0) === '-') {
        state.display = state.display.substring(1);
    } else {
        state.display = '-' + state.display;
    }
}

function pressPercent() {
    var n = parseFloat(state.display);
    state.display = fmt(n / 100);
}

function onKey(e) {
    var k = e.currentTarget.substring(2);   // "k_<label>"
    if (k === 'C')      pressClear();
    else if (k === '±') pressSign();
    else if (k === '%') pressPercent();
    else if (k === '=') pressEq();
    else if (k === '+' || k === '-' || k === '×' || k === '÷') pressOp(k);
    else if (k === '.') pressDot();
    else                pressDigit(k);
    rerender();
}

// 按下 / 抬起：直接旁路 native 调样式，不走 rerender，反馈立刻
//
// 注意：rerender() 之后这些瞬时 bg 会被 view() 里的 base 色覆盖回去
// （diff 检测到 bg 变化），所以 onClick 触发 rerender 同时也"复位"了高亮。
// 唯一需要手动复位的是"按下后没触发 click"（手指划走）的场景，
// 此时 onRelease 会被调用，所以兜底放在 onRelease。

function onPressBtn(e) {
    var id = e.currentTarget;
    var k = id.substring(2);
    var hi;
    if (k === '=') hi = COL_EQ_HI;
    else if (k === '+' || k === '-' || k === '×' || k === '÷') hi = COL_OP_HI;
    else if (k === 'C' || k === '±' || k === '%') hi = COL_FN_HI;
    else hi = COL_NUM_HI;
    sys.ui.setStyle(id, sys.style.BG_COLOR, hi, 0, 0, 0);
}

function onReleaseBtn(e) {
    var id = e.currentTarget;
    var k = id.substring(2);
    var base = baseColorOf(k);
    sys.ui.setStyle(id, sys.style.BG_COLOR, base, 0, 0, 0);
}

function baseColorOf(k) {
    if (k === '=') return COL_EQ;
    if (k === '+' || k === '-' || k === '×' || k === '÷') {
        return (state.op === k && state.newEntry) ? COL_OP_HI : COL_OP;
    }
    if (k === 'C' || k === '±' || k === '%') return COL_FN;
    return COL_NUM;
}

// ---- view ----

function btn(label, w) {
    if (w === undefined) w = BTN_W;
    var color = baseColorOf(label);
    return h('button', {
        id: "k_" + label,
        size: [w, BTN_H],
        bg: color,
        radius: 14,
        onClick: onKey,
        onPress: onPressBtn,
        onRelease: onReleaseBtn
    }, [
        h('label', { id: "kl_" + label, text: label, fg: COL_TEXT,
                     font: 'title', align: ['c', 0, 0] })
    ]);
}

// 一行四个等宽按钮的容器
function row(yIdx, items) {
    var y = DISP_H + GAP + yIdx * (BTN_H + GAP);
    return h('panel', {
        id: "row_" + yIdx,
        size: [-100, BTN_H],
        align: ['tm', 0, y],
        bg: COL_BG,
        flex: 'row',
        gap: [0, GAP],
        pad: [SIDE_PAD, 0, 0, 0]
    }, items);
}

function view() {
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
            // ---- 显示区：一个 panel 装两行 label ----
            h('panel', { id: "disp", size: [-100, DISP_H], align: ['tm', 0, 0],
                         bg: COL_DISP_BG, scrollable: false }, [
                h('label', { id: "exprText", text: state.expr || " ",
                             fg: COL_EXPR_FG, font: 'text',
                             align: ['tr', -14, 8] }),
                h('label', { id: "dispText", text: state.display,
                             fg: COL_DISP_FG, font: 'huge',
                             align: ['br', -14, -8] })
            ]),
            // ---- 5 行按钮 ----
            row(0, [ btn('C'), btn('±'), btn('%'), btn('÷') ]),
            row(1, [ btn('7'), btn('8'), btn('9'), btn('×') ]),
            row(2, [ btn('4'), btn('5'), btn('6'), btn('-') ]),
            row(3, [ btn('1'), btn('2'), btn('3'), btn('+') ]),
            // ---- 末行：0 占两格 ----
            h('panel', { id: "row_4", size: [-100, BTN_H],
                         align: ['tm', 0, DISP_H + GAP + 4 * (BTN_H + GAP)],
                         bg: COL_BG, flex: 'row', gap: [0, GAP],
                         pad: [SIDE_PAD, 0, 0, 0] }, [
                btn('0', BTN_W * 2 + GAP),
                btn('.'),
                btn('=')
            ])
        ]);
}

function rerender() { VDOM.render(view(), null); }

// ============================================================================
// §3. 启动
// ============================================================================

sys.log("calc: build start");
rerender();
sys.ui.attachRootListener("appRoot");
sys.log("calc: build done");
