// Dynamic App —— 秒表 + 倒计时 (Timer)
//
// 双模式 app：顶部 segmented 切换 Stopwatch / Countdown
//
// §A. Stopwatch（秒表）
//   - 大字 mm:ss.cc（cc=百分秒，30ms 刷新足够看不到跳秒）
//   - Start / Pause / Lap / Reset 四个按钮
//   - Lap 列表（可滚动），最多 20 条
//   - 持久化：当前累计时长 + lap 列表 + isRunning（重启后恢复"暂停在 X"）
//
// §B. Countdown（倒计时）
//   - 大字 mm:ss
//   - 三个常用预设：1m / 3m / 5m，可点选；点 + 自增 30s（上限 99:00）
//   - Start / Pause / Reset
//   - 到 0 时显示 "00:00 ⏰" + 闪烁 3 秒（用 bg 配合 setInterval）
//   - 持久化：preset 选择和 remainingMs（暂停时）
//
// 框架沿用 alarm.js 的 VDOM。约束 ES5。

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
        return { type: type, props: props || {}, children: children || [],
                 _parent: null, _mounted: false };
    }
    function autoId(type) { autoSeq += 1; return "_" + type + "_" + autoSeq; }

    function applyStyle(id, props) {
        if (props.bg !== undefined) sys.ui.setStyle(id, sys.style.BG_COLOR, props.bg, 0, 0, 0);
        if (props.fg !== undefined) sys.ui.setStyle(id, sys.style.TEXT_COLOR, props.fg, 0, 0, 0);
        if (props.radius !== undefined) sys.ui.setStyle(id, sys.style.RADIUS, props.radius, 0, 0, 0);
        if (props.size !== undefined)
            sys.ui.setStyle(id, sys.style.SIZE, props.size[0]|0, props.size[1]|0, 0, 0);
        if (props.align !== undefined) {
            var a = props.align;
            var atype = ALIGN_MAP[a[0]]; if (atype === undefined) atype = sys.align.CENTER;
            sys.ui.setStyle(id, sys.style.ALIGN, atype, a[1]|0, a[2]|0, 0);
        }
        if (props.pad !== undefined) {
            var p = props.pad;
            sys.ui.setStyle(id, sys.style.PAD, p[0]|0, p[1]|0, p[2]|0, p[3]|0);
        }
        if (props.flex !== undefined)
            sys.ui.setStyle(id, sys.style.FLEX, props.flex === 'row' ? 1 : 0, 0, 0, 0);
        if (props.font !== undefined) {
            var f = FONT_MAP[props.font]; if (f === undefined) f = sys.font.TEXT;
            sys.ui.setStyle(id, sys.style.FONT, f, 0, 0, 0);
        }
        if (props.shadow !== undefined) {
            var sh = props.shadow;
            sys.ui.setStyle(id, sys.style.SHADOW, sh[0]|0, sh[1]|0, sh[2]|0, 0);
        }
        if (props.gap !== undefined) {
            var g = props.gap;
            sys.ui.setStyle(id, sys.style.GAP, g[0]|0, g[1]|0, 0, 0);
        }
        if (props.scrollable !== undefined)
            sys.ui.setStyle(id, sys.style.SCROLLABLE, props.scrollable ? 1 : 0, 0, 0, 0);
    }

    var HOOK_NAME = { 1:'onClick', 2:'onPress', 3:'onDrag', 4:'onRelease', 5:'onLongPress' };

    function dispatch(startId, type, dx, dy) {
        var node = nodes[startId]; if (!node) return;
        var hook = HOOK_NAME[type]; if (!hook) return;
        var stopped = false;
        var ev = { target: startId, currentTarget: null,
                   dx: dx|0, dy: dy|0,
                   stopPropagation: function(){ stopped = true; } };
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
        nodes[id] = node; node._mounted = true;
        for (var i = 0; i < node.children.length; i++) {
            node.children[i]._parent = node;
            mount(node.children[i], id);
        }
        return node;
    }

    function set(id, patch) {
        var node = nodes[id]; if (!node) return;
        for (var k in patch) if (patch.hasOwnProperty(k)) node.props[k] = patch[k];
        if (patch.text !== undefined) sys.ui.setText(id, "" + patch.text);
        applyStyle(id, patch);
    }

    function destroy(id) {
        var node = nodes[id]; if (!node) return;
        var kids = node.children.slice();
        for (var i = 0; i < kids.length; i++) {
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

    var STYLE_KEYS = ['bg','fg','radius','size','align','pad','flex','font','shadow','gap','scrollable'];
    function shallowEq(a, b) {
        if (a === b) return true;
        if (a == null || b == null) return false;
        if (typeof a !== 'object' || typeof b !== 'object') return false;
        if (a.length === undefined || b.length === undefined) return false;
        if (a.length !== b.length) return false;
        for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
    }
    function diffProps(oldNode, newNode) {
        var id = oldNode.props.id, oldP = oldNode.props, newP = newNode.props;
        var patch = {}, hasPatch = false;
        if (newP.text !== undefined && newP.text !== oldP.text)
            sys.ui.setText(id, "" + newP.text);
        for (var i = 0; i < STYLE_KEYS.length; i++) {
            var k = STYLE_KEYS[i];
            if (newP[k] !== undefined && !shallowEq(oldP[k], newP[k])) {
                patch[k] = newP[k]; hasPatch = true;
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
        var oldKids = oldNode.children, newKids = newNode.children || [];
        var oldById = {};
        for (var i = 0; i < oldKids.length; i++) {
            var oid = oldKids[i].props.id;
            if (oid) oldById[oid] = oldKids[i];
        }
        var resultKids = [], seen = {};
        for (i = 0; i < newKids.length; i++) {
            var nk = newKids[i], nid = nk.props.id;
            if (nid && oldById[nid]) {
                seen[nid] = true;
                resultKids.push(diffNode(oldById[nid], nk, parentId));
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

    return { h: h, mount: mount, set: set, destroy: destroy,
             dispatch: dispatch, render: render };
})();

sys.__setDispatcher(function (id, type, dx, dy) { VDOM.dispatch(id, type, dx, dy); });
var h = VDOM.h;

// ============================================================================
// §2. 配色 / 布局
// ============================================================================

var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_CARD_HI  = 0x352B55;
var COL_TEXT     = 0xF1ECFF;
var COL_MUTED    = 0x8B8AA8;
var COL_PRIMARY  = 0x06B6D4;   // 青绿
var COL_PRIMARY_HI = 0x22D3EE;
var COL_SUCCESS  = 0x10B981;
var COL_DANGER   = 0xEF4444;
var COL_WARN     = 0xF59E0B;
var COL_TAB_OFF  = 0x2D2640;
var COL_DISP_BG  = 0x0A0816;

// ============================================================================
// §3. 状态 + 持久化
// ============================================================================

function defaultState() {
    return {
        mode: 'sw',                // 'sw' | 'cd'
        sw: {
            running:   false,
            startMs:   0,           // 当前段开始时刻（uptimeMs），running=true 时才有意义
            elapsedMs: 0,           // 已累计 ms（不含当前段）
            laps:      []           // 每个 lap 的累计 ms
        },
        cd: {
            running:   false,
            endMs:     0,           // 倒计时目标 uptimeMs（running=true 时）
            remainMs:  60000,       // 暂停时的剩余 ms
            totalMs:   60000,       // 当前设定的总长 ms（重置回到这）
            doneAt:    0            // 到 0 时刻（用来做 3 秒闪烁）
        }
    };
}

function loadInit() {
    var raw = sys.app.loadState();
    if (!raw) return defaultState();
    try {
        var saved = JSON.parse(raw);
        if (!saved || !saved.sw || !saved.cd) return defaultState();
        // 关键：跨重启绝不保留 running=true，否则启动时 startMs 是旧 uptimeMs，
        // 算出来的 elapsed 会暴涨。统一退化为暂停态。
        if (saved.sw.running) {
            saved.sw.running = false;
        }
        if (saved.cd.running) {
            saved.cd.running = false;
            // remainMs 已经是暂停态语义了
        }
        return saved;
    } catch (e) {
        sys.log("timer: loadState parse error -> default");
        return defaultState();
    }
}

var state = loadInit();

function persist() {
    sys.app.saveState(JSON.stringify(state));
}

// ============================================================================
// §4. 时间处理
// ============================================================================

function fmtSwTime(ms) {
    if (ms < 0) ms = 0;
    var totalCs = (ms / 10) | 0;        // 百分秒
    var cs = totalCs % 100;
    var totalSec = (totalCs / 100) | 0;
    var s = totalSec % 60;
    var m = (totalSec / 60) | 0;
    if (m > 99) m = 99;
    return pad2(m) + ":" + pad2(s) + "." + pad2(cs);
}

function fmtCdTime(ms) {
    if (ms < 0) ms = 0;
    var totalSec = Math.ceil(ms / 1000) | 0;
    var s = totalSec % 60;
    var m = (totalSec / 60) | 0;
    if (m > 99) { m = 99; s = 59; }
    return pad2(m) + ":" + pad2(s);
}

function pad2(n) { return n < 10 ? "0" + n : "" + n; }

function swCurrent() {
    var s = state.sw;
    if (s.running) return s.elapsedMs + (sys.time.uptimeMs() - s.startMs);
    return s.elapsedMs;
}

function cdCurrent() {
    var c = state.cd;
    if (c.running) {
        var rem = c.endMs - sys.time.uptimeMs();
        if (rem < 0) rem = 0;
        return rem;
    }
    return c.remainMs;
}

// ============================================================================
// §5. tick：30ms 刷一次时间 label，不重 render 整树
//
//    rerender 全树会导致 26 个 button 全 diffProps，CPU 浪费。
//    时间字段是高频低成本，直接 sys.ui.setText 更新即可。
// ============================================================================

var swTicker = null;
var cdTicker = null;
var blinkTimer = null;
var blinkOn = false;

function startSwTicker() {
    if (swTicker !== null) return;
    swTicker = setInterval(function () {
        if (state.mode !== 'sw' || !state.sw.running) return;
        sys.ui.setText("swTime", fmtSwTime(swCurrent()));
    }, 30);
}

function startCdTicker() {
    if (cdTicker !== null) return;
    cdTicker = setInterval(function () {
        if (state.mode !== 'cd') return;
        var c = state.cd;
        if (c.running) {
            var rem = c.endMs - sys.time.uptimeMs();
            if (rem <= 0) {
                // 到点
                c.running = false;
                c.remainMs = 0;
                c.doneAt = sys.time.uptimeMs();
                persist();
                rerender();        // 切换按钮文案
                startBlink();
                return;
            }
            sys.ui.setText("cdTime", fmtCdTime(rem));
        }
    }, 100);   // 倒计时 1s 精度，100ms 足够
}

function startBlink() {
    if (blinkTimer !== null) return;
    blinkOn = false;
    blinkTimer = setInterval(function () {
        blinkOn = !blinkOn;
        sys.ui.setStyle("cdTime", sys.style.TEXT_COLOR,
                        blinkOn ? COL_DANGER : COL_TEXT, 0, 0, 0);
        // 闪 6 次（3 秒）后停
        if (sys.time.uptimeMs() - state.cd.doneAt > 3000) {
            clearInterval(blinkTimer);
            blinkTimer = null;
            sys.ui.setStyle("cdTime", sys.style.TEXT_COLOR, COL_TEXT, 0, 0, 0);
        }
    }, 500);
}

function stopBlink() {
    if (blinkTimer !== null) {
        clearInterval(blinkTimer);
        blinkTimer = null;
    }
    sys.ui.setStyle("cdTime", sys.style.TEXT_COLOR, COL_TEXT, 0, 0, 0);
}

// ============================================================================
// §6. 事件
// ============================================================================

function onTabSw()   { if (state.mode === 'sw') return; state.mode = 'sw'; persist(); rerender(); }
function onTabCd()   { if (state.mode === 'cd') return; state.mode = 'cd'; stopBlink(); persist(); rerender(); }

// ---- Stopwatch ----

function onSwStartPause() {
    var s = state.sw;
    if (s.running) {
        // 暂停：把当前段累加到 elapsedMs
        s.elapsedMs = s.elapsedMs + (sys.time.uptimeMs() - s.startMs);
        s.running = false;
    } else {
        s.startMs = sys.time.uptimeMs();
        s.running = true;
    }
    persist();
    rerender();
}

function onSwLap() {
    var s = state.sw;
    if (!s.running && s.elapsedMs === 0) return;     // 0 状态不记
    if (s.laps.length >= 20) s.laps.shift();         // 最多 20 条，FIFO
    s.laps.push(swCurrent());
    persist();
    rerender();
}

function onSwReset() {
    state.sw = { running: false, startMs: 0, elapsedMs: 0, laps: [] };
    persist();
    rerender();
}

// ---- Countdown ----

function onCdStartPause() {
    var c = state.cd;
    if (c.running) {
        c.remainMs = c.endMs - sys.time.uptimeMs();
        if (c.remainMs < 0) c.remainMs = 0;
        c.running = false;
    } else {
        if (c.remainMs <= 0) return;     // 0 不能启动
        stopBlink();
        c.endMs = sys.time.uptimeMs() + c.remainMs;
        c.running = true;
    }
    persist();
    rerender();
}

function onCdReset() {
    stopBlink();
    state.cd.running = false;
    state.cd.remainMs = state.cd.totalMs;
    persist();
    rerender();
}

function onCdPreset(e) {
    var k = e.currentTarget.substring(3);   // "ps_60"
    var sec = parseInt(k, 10);
    if (isNaN(sec)) return;
    setCdTotal(sec * 1000);
}

function onCdAdd30() {
    var newTotal = state.cd.totalMs + 30000;
    if (newTotal > 99 * 60 * 1000) newTotal = 99 * 60 * 1000;
    setCdTotal(newTotal);
}

function onCdSub30() {
    var newTotal = state.cd.totalMs - 30000;
    if (newTotal < 30000) newTotal = 30000;
    setCdTotal(newTotal);
}

function setCdTotal(ms) {
    stopBlink();
    state.cd.totalMs = ms;
    state.cd.remainMs = ms;
    state.cd.running = false;
    persist();
    rerender();
}

// ============================================================================
// §7. view
// ============================================================================

var SCR_W = 240, SCR_H = 320;

function tabBtn(id, label, active, onClick) {
    return h('button', {
        id: id, size: [110, 32],
        bg: active ? COL_PRIMARY : COL_TAB_OFF,
        radius: 16, onClick: onClick
    }, [
        h('label', { id: id + "_l", text: label,
                     fg: active ? 0x000814 : COL_MUTED,
                     font: 'text', align: ['c', 0, 0] })
    ]);
}

function actionBtn(id, label, color, w, onClick) {
    return h('button', {
        id: id, size: [w, 40], bg: color, radius: 14, onClick: onClick
    }, [
        h('label', { id: id + "_l", text: label, fg: COL_TEXT,
                     font: 'text', align: ['c', 0, 0] })
    ]);
}

function viewTabs() {
    return h('panel', { id: 'tabs', size: [-100, 36], align: ['tm', 0, 6],
                         bg: COL_BG, scrollable: false, flex: 'row',
                         gap: [0, 8],
                         pad: [(SCR_W - 110*2 - 8) / 2, 0, 0, 0] }, [
        tabBtn('tab_sw', 'Stopwatch', state.mode === 'sw', onTabSw),
        tabBtn('tab_cd', 'Countdown', state.mode === 'cd', onTabCd)
    ]);
}

function lapRow(idx, ms, totalLaps) {
    // 索引展示：最新在最上面
    var no = totalLaps - idx;
    return h('panel', {
        id: 'lap_' + idx,
        size: [-100, 26], bg: idx % 2 === 0 ? COL_CARD : COL_CARD_HI,
        scrollable: false
    }, [
        h('label', { id: 'lapn_' + idx, text: '#' + pad2(no),
                     fg: COL_MUTED, font: 'text', align: ['lm', 12, 0] }),
        h('label', { id: 'lapt_' + idx, text: fmtSwTime(ms),
                     fg: COL_TEXT, font: 'text', align: ['rm', -12, 0] })
    ]);
}

function viewSw() {
    var s = state.sw;
    var displayText = fmtSwTime(swCurrent());

    var children = [];
    // 大字 display
    children.push(h('panel', {
        id: 'swDispBox', size: [-100, 76], align: ['tm', 0, 50],
        bg: COL_DISP_BG, scrollable: false, radius: 12
    }, [
        h('label', { id: 'swTime', text: displayText,
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, 0] })
    ]));

    // 三个按钮：Lap | Start/Pause | Reset
    var btnY = 138;
    var bw = 70;
    var startLabel = s.running ? 'Pause' : 'Start';
    var startCol   = s.running ? COL_WARN : COL_SUCCESS;
    children.push(h('panel', {
        id: 'swBtns', size: [-100, 40], align: ['tm', 0, btnY],
        bg: COL_BG, scrollable: false, flex: 'row', gap: [0, 8],
        pad: [(SCR_W - bw*3 - 8*2) / 2, 0, 0, 0]
    }, [
        actionBtn('btn_lap',   'Lap',      COL_CARD_HI, bw, onSwLap),
        actionBtn('btn_start', startLabel, startCol,    bw, onSwStartPause),
        actionBtn('btn_reset', 'Reset',    COL_DANGER,  bw, onSwReset)
    ]));

    // Lap 列表
    var lapY = btnY + 50;       // 188
    var lapH = SCR_H - lapY - 6; // 126
    var lapKids = [];
    // 倒序展示：最新在最上
    for (var i = s.laps.length - 1; i >= 0; i--) {
        lapKids.push(lapRow(i, s.laps[i], s.laps.length));
    }
    if (lapKids.length === 0) {
        lapKids.push(h('label', { id: 'lapEmpty', text: 'No laps',
                                   fg: COL_MUTED, font: 'text',
                                   align: ['c', 0, 0] }));
    }
    children.push(h('panel', {
        id: 'lapList', size: [-100, lapH], align: ['tm', 0, lapY],
        bg: COL_CARD, scrollable: true, flex: 'col', gap: [1, 0],
        radius: 12, pad: [0, 0, 0, 0]
    }, lapKids));

    return children;
}

function presetBtn(label, sec, isActive) {
    return h('button', {
        id: 'ps_' + sec, size: [60, 30],
        bg: isActive ? COL_PRIMARY : COL_CARD_HI,
        radius: 14, onClick: onCdPreset
    }, [
        h('label', { id: 'psl_' + sec, text: label,
                     fg: isActive ? 0x000814 : COL_TEXT,
                     font: 'text', align: ['c', 0, 0] })
    ]);
}

function viewCd() {
    var c = state.cd;
    var rem = cdCurrent();

    var children = [];
    // 大字
    children.push(h('panel', {
        id: 'cdDispBox', size: [-100, 100], align: ['tm', 0, 50],
        bg: COL_DISP_BG, scrollable: false, radius: 12
    }, [
        h('label', { id: 'cdTime', text: fmtCdTime(rem),
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, -8] }),
        h('label', { id: 'cdTotal',
                     text: 'set: ' + fmtCdTime(c.totalMs),
                     fg: COL_MUTED, font: 'text', align: ['c', 0, 28] })
    ]));

    // preset 行：1m / 3m / 5m
    var presetY = 162;
    children.push(h('panel', {
        id: 'cdPresets', size: [-100, 30], align: ['tm', 0, presetY],
        bg: COL_BG, scrollable: false, flex: 'row', gap: [0, 8],
        pad: [(SCR_W - 60*3 - 8*2) / 2, 0, 0, 0]
    }, [
        presetBtn('1 min', 60,  c.totalMs === 60000),
        presetBtn('3 min', 180, c.totalMs === 180000),
        presetBtn('5 min', 300, c.totalMs === 300000)
    ]));

    // -30 / +30 行
    var adjY = presetY + 38;
    children.push(h('panel', {
        id: 'cdAdj', size: [-100, 30], align: ['tm', 0, adjY],
        bg: COL_BG, scrollable: false, flex: 'row', gap: [0, 8],
        pad: [(SCR_W - 100*2 - 8) / 2, 0, 0, 0]
    }, [
        actionBtn('btn_sub30', '- 30 sec', COL_CARD_HI, 100, onCdSub30),
        actionBtn('btn_add30', '+ 30 sec', COL_CARD_HI, 100, onCdAdd30)
    ]));

    // start/pause + reset
    var actY = adjY + 40;
    var bw = 90;
    var startLabel = c.running ? 'Pause' : 'Start';
    var startCol   = c.running ? COL_WARN : COL_SUCCESS;
    children.push(h('panel', {
        id: 'cdActs', size: [-100, 40], align: ['tm', 0, actY],
        bg: COL_BG, scrollable: false, flex: 'row', gap: [0, 8],
        pad: [(SCR_W - bw*2 - 8) / 2, 0, 0, 0]
    }, [
        actionBtn('btn_cdstart', startLabel, startCol,   bw, onCdStartPause),
        actionBtn('btn_cdreset', 'Reset',    COL_DANGER, bw, onCdReset)
    ]));

    return children;
}

function view() {
    var modeKids = state.mode === 'sw' ? viewSw() : viewCd();
    var allKids = [ viewTabs() ].concat(modeKids);
    return h('panel', {
        id: 'appRoot', size: [-100, -100], bg: COL_BG, scrollable: false
    }, allKids);
}

function rerender() { VDOM.render(view(), null); }

// ============================================================================
// §8. 启动
// ============================================================================

sys.log("timer: build start");
rerender();
sys.ui.attachRootListener("appRoot");
startSwTicker();
startCdTicker();
sys.log("timer: build done, mode=" + state.mode);
