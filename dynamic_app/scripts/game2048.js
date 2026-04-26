// Dynamic App —— 2048
//
// 玩法：
//   - 4x4 格子，滑动合并相同数字的方块（2+2=4，4+4=8，... 2048 胜利）
//   - 每次有效滑动后随机生成一个 2 (90%) 或 4 (10%) 在空格里
//   - 滑动方向由 onDrag 的 dx/dy 大小判定
//
// 持久化：board + score + best
//
// UI：
//   顶部 36px：score | best | new game
//   网格：240 宽 - 16 边距 = 224；4 格 + 5 间距 → 间距 6, 格 49 → 4*49+5*6=226 ≈ ok
//                调成 cell=48 gap=6 → 4*48+5*6 = 222，左右各留 9
//   底部留几行做 hint
//
// 约束 ES5。

// ============================================================================
// §1. VDOM 框架（拷自 alarm.js）
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
// §2. 配色 (经典 2048 配色微调，深色背景版)
// ============================================================================

var COL_BG       = 0x1A1530;
var COL_GRID     = 0x352B55;
var COL_CELL_EMP = 0x2B2347;
var COL_TEXT_DK  = 0x1F1B30;     // 浅色块上的字
var COL_TEXT_LT  = 0xF1ECFF;     // 深色块上的字
var COL_TOP      = 0x231C3A;
var COL_PRIMARY  = 0x06B6D4;
var COL_TEXT     = 0xF1ECFF;
var COL_MUTED    = 0x8B8AA8;

// value → bg/fg
var TILE_COLORS = {
    0:    [COL_CELL_EMP, COL_TEXT],
    2:    [0xEEE4DA,    COL_TEXT_DK],
    4:    [0xEDE0C8,    COL_TEXT_DK],
    8:    [0xF2B179,    COL_TEXT_LT],
    16:   [0xF59563,    COL_TEXT_LT],
    32:   [0xF67C5F,    COL_TEXT_LT],
    64:   [0xF65E3B,    COL_TEXT_LT],
    128:  [0xEDCF72,    COL_TEXT_LT],
    256:  [0xEDCC61,    COL_TEXT_LT],
    512:  [0xEDC850,    COL_TEXT_LT],
    1024: [0xEDC53F,    COL_TEXT_LT],
    2048: [0xEDC22E,    COL_TEXT_LT],
    4096: [0x3C3A32,    COL_TEXT_LT],
    8192: [0x3C3A32,    COL_TEXT_LT]
};

function tileBg(v) { var t = TILE_COLORS[v]; return t ? t[0] : 0x3C3A32; }
function tileFg(v) { var t = TILE_COLORS[v]; return t ? t[1] : COL_TEXT_LT; }
function tileFont(v) {
    if (v >= 1024) return 'text';   // 4 位数用小字才放得下 50px 格子
    if (v >= 100)  return 'title';
    return 'title';                  // 2 位数 / 个位数也用 title
}

// ============================================================================
// §3. 状态 + 持久化
// ============================================================================

function emptyBoard() {
    return [ [0,0,0,0], [0,0,0,0], [0,0,0,0], [0,0,0,0] ];
}

function defaultState() {
    var s = { board: emptyBoard(), score: 0, best: 0, gameOver: false, won: false };
    spawn(s); spawn(s);
    return s;
}

function loadInit() {
    var raw = sys.app.loadState();
    if (!raw) return defaultState();
    try {
        var saved = JSON.parse(raw);
        if (!saved || !saved.board || !saved.board.length) return defaultState();
        if (saved.best === undefined) saved.best = 0;
        if (saved.score === undefined) saved.score = 0;
        if (saved.gameOver === undefined) saved.gameOver = false;
        if (saved.won === undefined) saved.won = false;
        return saved;
    } catch (e) { return defaultState(); }
}

var state = loadInit();

function persist() { sys.app.saveState(JSON.stringify(state)); }

// ============================================================================
// §4. 游戏逻辑
// ============================================================================

function spawn(s) {
    var empties = [];
    for (var r = 0; r < 4; r++) for (var c = 0; c < 4; c++) {
        if (s.board[r][c] === 0) empties.push([r, c]);
    }
    if (empties.length === 0) return false;
    var pick = empties[(Math.random() * empties.length) | 0];
    s.board[pick[0]][pick[1]] = Math.random() < 0.9 ? 2 : 4;
    return true;
}

// 把单行向左挤压并合并，返回 { row, gained, moved }
function compressRow(row) {
    var src = [];
    for (var i = 0; i < 4; i++) if (row[i] !== 0) src.push(row[i]);
    var out = [], gained = 0;
    var i2 = 0;
    while (i2 < src.length) {
        if (i2 + 1 < src.length && src[i2] === src[i2 + 1]) {
            var merged = src[i2] * 2;
            out.push(merged);
            gained += merged;
            i2 += 2;
        } else {
            out.push(src[i2]);
            i2 += 1;
        }
    }
    while (out.length < 4) out.push(0);

    var moved = false;
    for (i = 0; i < 4; i++) if (out[i] !== row[i]) { moved = true; break; }
    return { row: out, gained: gained, moved: moved };
}

function reverseRow(row) {
    return [row[3], row[2], row[1], row[0]];
}

// dir: 'L' 'R' 'U' 'D'
function move(dir) {
    var b = state.board;
    var anyMoved = false, totalGained = 0;
    var r, c, row, res;

    if (dir === 'L' || dir === 'R') {
        for (r = 0; r < 4; r++) {
            row = b[r];
            if (dir === 'R') row = reverseRow(row);
            res = compressRow(row);
            if (dir === 'R') res.row = reverseRow(res.row);
            b[r] = res.row;
            if (res.moved) anyMoved = true;
            totalGained += res.gained;
        }
    } else {
        // U/D：取列 → compress → 写回
        for (c = 0; c < 4; c++) {
            row = [b[0][c], b[1][c], b[2][c], b[3][c]];
            if (dir === 'D') row = reverseRow(row);
            res = compressRow(row);
            if (dir === 'D') res.row = reverseRow(res.row);
            for (r = 0; r < 4; r++) b[r][c] = res.row[r];
            if (res.moved) anyMoved = true;
            totalGained += res.gained;
        }
    }

    if (!anyMoved) return false;
    state.score += totalGained;
    if (state.score > state.best) state.best = state.score;
    if (!state.won) {
        for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) {
            if (b[r][c] >= 2048) { state.won = true; break; }
        }
    }
    spawn(state);
    if (!hasMove()) state.gameOver = true;
    return true;
}

function hasMove() {
    var b = state.board;
    for (var r = 0; r < 4; r++) for (var c = 0; c < 4; c++) {
        if (b[r][c] === 0) return true;
        if (c < 3 && b[r][c] === b[r][c+1]) return true;
        if (r < 3 && b[r][c] === b[r+1][c]) return true;
    }
    return false;
}

function newGame() {
    state.board = emptyBoard();
    state.score = 0;
    state.gameOver = false;
    state.won = false;
    spawn(state); spawn(state);
}

// ============================================================================
// §5. 事件
// ============================================================================

function onBoardDrag(e) {
    var dx = e.dx, dy = e.dy;
    if (state.gameOver) return;
    var dir;
    if (Math.abs(dx) > Math.abs(dy)) {
        dir = dx > 0 ? 'R' : 'L';
    } else {
        dir = dy > 0 ? 'D' : 'U';
    }
    if (move(dir)) {
        persist();
        rerender();
    }
}

function onNewGame() {
    newGame();
    persist();
    rerender();
}

// ============================================================================
// §6. view
// ============================================================================

var SCR_W = 240, SCR_H = 320;
var GRID_PAD = 6;          // 网格内部间距
var CELL = 50;             // cell 边长
var GRID_SIZE = CELL*4 + GRID_PAD*5;   // 50*4+6*5 = 230
var GRID_X = (SCR_W - GRID_SIZE) / 2;  // 5
var GRID_Y = 50;           // 顶部留 50 给 score bar

function topBar() {
    return h('panel', {
        id: 'topBar', size: [-100, 40], align: ['tm', 0, 4],
        bg: COL_BG, scrollable: false
    }, [
        h('panel', {
            id: 'scoreBox', size: [70, 36], align: ['lm', 8, 0],
            bg: COL_TOP, radius: 8, scrollable: false
        }, [
            h('label', { id: 'scoreLbl', text: 'SCORE', fg: COL_MUTED,
                         font: 'text', align: ['tm', 0, 2] }),
            h('label', { id: 'scoreVal', text: '' + state.score, fg: COL_TEXT,
                         font: 'text', align: ['bm', 0, -2] })
        ]),
        h('panel', {
            id: 'bestBox', size: [70, 36], align: ['lm', 84, 0],
            bg: COL_TOP, radius: 8, scrollable: false
        }, [
            h('label', { id: 'bestLbl', text: 'BEST', fg: COL_MUTED,
                         font: 'text', align: ['tm', 0, 2] }),
            h('label', { id: 'bestVal', text: '' + state.best, fg: COL_TEXT,
                         font: 'text', align: ['bm', 0, -2] })
        ]),
        h('button', {
            id: 'btnNew', size: [76, 36], align: ['rm', -8, 0],
            bg: COL_PRIMARY, radius: 8, onClick: onNewGame
        }, [
            h('label', { id: 'btnNewL', text: 'New', fg: 0x000814,
                         font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function tile(r, c) {
    var v = state.board[r][c];
    var x = GRID_PAD + c * (CELL + GRID_PAD);
    var y = GRID_PAD + r * (CELL + GRID_PAD);
    var tileKids = [];
    if (v > 0) {
        tileKids.push(h('label', {
            id: 'tl_' + r + '_' + c, text: '' + v,
            fg: tileFg(v), font: tileFont(v), align: ['c', 0, 0]
        }));
    }
    return h('panel', {
        id: 't_' + r + '_' + c,
        size: [CELL, CELL], align: ['tl', x, y],
        bg: tileBg(v), radius: 6, scrollable: false
    }, tileKids);
}

function viewBoard() {
    var kids = [];
    for (var r = 0; r < 4; r++) for (var c = 0; c < 4; c++) {
        kids.push(tile(r, c));
    }
    return h('panel', {
        id: 'board', size: [GRID_SIZE, GRID_SIZE],
        align: ['tl', GRID_X, GRID_Y],
        bg: COL_GRID, radius: 10, scrollable: false,
        onDrag: onBoardDrag
    }, kids);
}

function bottomBar() {
    var msg;
    if (state.gameOver) msg = 'Game Over - tap New';
    else if (state.won)  msg = '2048 reached! Keep going.';
    else                 msg = 'Swipe to move';
    var color = state.gameOver ? 0xEF4444 :
                state.won      ? 0xEDC22E : COL_MUTED;
    return h('label', {
        id: 'hint', text: msg, fg: color, font: 'text',
        align: ['bm', 0, -8]
    });
}

function view() {
    return h('panel', {
        id: 'appRoot', size: [-100, -100], bg: COL_BG, scrollable: false
    }, [ topBar(), viewBoard(), bottomBar() ]);
}

function rerender() { VDOM.render(view(), null); }

// ============================================================================
// §7. 启动
// ============================================================================

sys.log("2048: build start");
rerender();
sys.ui.attachRootListener("appRoot");
sys.log("2048: build done score=" + state.score + " best=" + state.best);
