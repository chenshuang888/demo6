// Dynamic App —— 2048
// 框架：VDOM / h / makeBle 由 prelude.js 提供。约束 ES5。

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
