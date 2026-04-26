// Dynamic App —— 计算器（240×320）
//
// 架构沿用闹钟那套：
//   §1. VDOM 框架（与 alarm.js 完全一致；将来可拆共享）
//   §2. 计算器业务（state + view + rerender）
//
// 没有持久化：计算器属于"用完即丢"语义，重启从 0 开始更符合直觉。
//
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
//   justEval      刚按过 '='：下一个数字键应清屏重输；下一个运算符以当前
//                 display 为左操作数继续
//   newEntry      下一个数字键应清屏重输（按运算符后置 true）

var COLOR_BG       = 0x1A1530;
var COLOR_DISPLAY  = 0x0F0B20;
var COLOR_TEXT     = 0xF1ECFF;
var COLOR_NUM      = 0x3B3458;   // 数字键背景
var COLOR_FN       = 0x55507A;   // C/±/% 功能键
var COLOR_OP       = 0xF59E0B;   // 运算符键（橙色）
var COLOR_OP_PRESS = 0xFCD34D;   // 运算符按下/被选中态
var COLOR_EQ       = 0x10B981;   // 等号键（绿色）

var state = {
    display:  "0",
    prev:     null,
    op:       null,
    justEval: false,
    newEntry: false
};

function fmt(n) {
    // 截掉无意义尾零；保留最多 9 位
    if (!isFinite(n)) return "Err";
    var s = "" + n;
    if (s.length > 12) {
        s = n.toPrecision(9);
        // toPrecision 可能产生科学计数 1.234e+10，去掉无意义尾 0
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
    // 已有 prev + op 且未 newEntry → 先把当前式算掉再链式
    if (state.prev !== null && state.op !== null && !state.newEntry) {
        var r = compute(state.prev, state.display, state.op);
        state.display = fmt(r);
    }
    state.prev = state.display;
    state.op = op;
    state.newEntry = true;
    state.justEval = false;
}

function pressEq() {
    if (state.op === null || state.prev === null) return;
    var r = compute(state.prev, state.display, state.op);
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

// ---- view ----

function btn(label, color, w, h_, isOp) {
    return h('button', {
        id: "k_" + label,
        size: [w, h_],
        bg: (isOp && state.op === label && state.newEntry) ? COLOR_OP_PRESS : color,
        radius: 12,
        onClick: onKey
    }, [
        h('label', { id: "kl_" + label, text: label, fg: COLOR_TEXT,
                     font: 'title', align: ['c', 0, 0] })
    ]);
}

// 240 宽，按钮 4 列：4*52 + 5*4 间距 = 208 + 20 = 228，左右各留 6
// 高度：display 80 + 5行*52 + 6*4 间距 = 80 + 260 + 24 = 364 > 320
// 缩到 4 行（合并最后一行）+ 按钮 48：4*48+5*4=212；display 60 + 5*48 + 6*4=324
// 改方案：display 60，按钮 46，间距 4，5 行 = 60 + 5*46 + 6*4 = 60 + 230 + 24 = 314 ✓

var BTN = 46;
var GAP = 4;

function viewRow(items, y) {
    return h('panel', {
        id: "row_" + y, size: [-100, BTN], align: ['tm', 0, y],
        bg: COLOR_BG, flex: 'row', gap: [0, GAP],
        pad: [(240 - 4 * BTN - 3 * GAP) / 2, 0, 0, 0]
    }, items);
}

function view() {
    var rowY0 = 60 + GAP;             // 64
    var rowY1 = rowY0 + BTN + GAP;    // 114
    var rowY2 = rowY1 + BTN + GAP;    // 164
    var rowY3 = rowY2 + BTN + GAP;    // 214
    var rowY4 = rowY3 + BTN + GAP;    // 264

    return h('panel', { id: "appRoot", size: [-100, -100], bg: COLOR_BG,
                         scrollable: false }, [
        // 显示区
        h('panel', { id: "disp", size: [-100, 60], align: ['tm', 0, 0],
                     bg: COLOR_DISPLAY }, [
            h('label', { id: "dispText", text: state.display,
                         fg: COLOR_TEXT, font: 'huge',
                         align: ['rm', -12, 0] })
        ]),
        viewRow([
            btn('C',  COLOR_FN, BTN, BTN, false),
            btn('±',  COLOR_FN, BTN, BTN, false),
            btn('%',  COLOR_FN, BTN, BTN, false),
            btn('÷',  COLOR_OP, BTN, BTN, true)
        ], rowY0),
        viewRow([
            btn('7',  COLOR_NUM, BTN, BTN, false),
            btn('8',  COLOR_NUM, BTN, BTN, false),
            btn('9',  COLOR_NUM, BTN, BTN, false),
            btn('×',  COLOR_OP,  BTN, BTN, true)
        ], rowY1),
        viewRow([
            btn('4',  COLOR_NUM, BTN, BTN, false),
            btn('5',  COLOR_NUM, BTN, BTN, false),
            btn('6',  COLOR_NUM, BTN, BTN, false),
            btn('-',  COLOR_OP,  BTN, BTN, true)
        ], rowY2),
        viewRow([
            btn('1',  COLOR_NUM, BTN, BTN, false),
            btn('2',  COLOR_NUM, BTN, BTN, false),
            btn('3',  COLOR_NUM, BTN, BTN, false),
            btn('+',  COLOR_OP,  BTN, BTN, true)
        ], rowY3),
        // 末行：0 占两列宽（含间距） + . + =
        h('panel', { id: "row_last", size: [-100, BTN], align: ['tm', 0, rowY4],
                     bg: COLOR_BG, flex: 'row', gap: [0, GAP],
                     pad: [(240 - 4 * BTN - 3 * GAP) / 2, 0, 0, 0] }, [
            btn('0',  COLOR_NUM, BTN * 2 + GAP, BTN, false),
            btn('.',  COLOR_NUM, BTN, BTN, false),
            btn('=',  COLOR_EQ,  BTN, BTN, false)
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
