// ============================================================================
// tictactoe_pkg —— 井字棋（动态 app 平台测试 #2：人机对战，需配对 PC 插件）
//
// 协议（与 tools/plugins/tictactoe/plugin.py 对偶）：
//   ESP → PC  { type:"move",  body:{ r, c } }     玩家落子（X）
//   ESP → PC  { type:"reset" }                    请求新局
//   ESP → PC  { type:"hello" }                    上线握手（启动时 + 重连时）
//   PC → ESP  { type:"ready" }                    AI 已就绪
//   PC → ESP  { type:"move",  body:{ r, c } }     AI 落子（O）
//   PC → ESP  { type:"reset_ack" }                AI 同意重开
//
// 设备端始终是 X，先手。AI 是 O，被动响应。
// 棋盘 3×3，state 取值：'' | 'X' | 'O'。
//
// 状态：
//   bootstrap → waiting_ai → your_turn → ai_thinking → your_turn → ... →
//     win | lose | draw  （结束态后只接受"再来一局"按钮）
// ============================================================================

var APP_NAME = "tictactoe_pkg";
var T = UI.T;
var I = UI.I;

// --- BLE ----------------------------------------------------------------
var ble = makeBle(APP_NAME);

// --- 状态 ---------------------------------------------------------------
var board = ['', '', '', '', '', '', '', '', ''];   // 9 格
var phase = 'bootstrap';    // bootstrap / waiting_ai / your_turn / ai_thinking / win / lose / draw
var lastWin = null;         // 三连线索引数组（高亮用）
var aiReadyOnce = false;    // 收到过 ready 才认为对端可用

// --- 工具 ---------------------------------------------------------------
function emptyBoard() {
    for (var i = 0; i < 9; i++) board[i] = '';
    lastWin = null;
}

var WIN_LINES = [
    [0,1,2],[3,4,5],[6,7,8],
    [0,3,6],[1,4,7],[2,5,8],
    [0,4,8],[2,4,6]
];

function checkWin() {
    for (var i = 0; i < WIN_LINES.length; i++) {
        var L = WIN_LINES[i];
        var a = board[L[0]];
        if (a && a === board[L[1]] && a === board[L[2]]) return { who: a, line: L };
    }
    for (var j = 0; j < 9; j++) if (!board[j]) return null;   // 还有空位
    return { who: '_draw_', line: null };
}

function statusText() {
    if (phase === 'bootstrap')    return '等待 PC 上线...';
    if (phase === 'waiting_ai')   return 'AI 准备中...';
    if (phase === 'your_turn')    return '你的回合（X）';
    if (phase === 'ai_thinking')  return 'AI 思考中...';
    if (phase === 'win')          return '🏆 你赢了！';
    if (phase === 'lose')         return '💀 AI 赢了';
    if (phase === 'draw')         return '🤝 平局';
    return phase;
}

function statusColor() {
    if (phase === 'win')  return T.C_OK;
    if (phase === 'lose') return T.C_ERR;
    if (phase === 'draw') return T.C_WARN;
    if (phase === 'your_turn') return T.C_ACCENT;
    return T.C_TEXT_MUTED;
}

// --- 渲染 ---------------------------------------------------------------
function cellId(i)  { return 'c' + i; }
function cellLblId(i) { return 'cl' + i; }

function refreshCells() {
    for (var i = 0; i < 9; i++) {
        var v = board[i];
        var inWin = lastWin && lastWin.line && lastWin.line.indexOf(i) >= 0;
        var fg = T.C_TEXT;
        if (v === 'X') fg = inWin ? T.C_OK  : T.C_ACCENT;
        if (v === 'O') fg = inWin ? T.C_ERR : T.C_TEXT_DIM;
        VDOM.set(cellLblId(i), { text: v || ' ', fg: fg });
    }
}

function refreshStatus() {
    VDOM.set('statusLbl', { text: statusText(), fg: statusColor() });
}

function refreshAll() {
    refreshCells();
    refreshStatus();
}

// --- 操作 ---------------------------------------------------------------
function tryUserMove(idx) {
    if (phase !== 'your_turn') return;
    if (board[idx]) return;     // 占用
    board[idx] = 'X';
    var r = (idx / 3) | 0, c = idx % 3;
    ble.send('move', { r: r, c: c });
    var w = checkWin();
    if (w) {
        endGame(w);
        return;
    }
    phase = 'ai_thinking';
    refreshAll();
}

function applyAiMove(r, c) {
    if (phase !== 'ai_thinking') {
        sys.log('tictactoe: stray ai move ignored, phase=' + phase);
        return;
    }
    var idx = (r | 0) * 3 + (c | 0);
    if (idx < 0 || idx >= 9 || board[idx]) {
        sys.log('tictactoe: illegal ai move r=' + r + ' c=' + c);
        return;
    }
    board[idx] = 'O';
    var w = checkWin();
    if (w) { endGame(w); return; }
    phase = 'your_turn';
    refreshAll();
}

function endGame(w) {
    if (w.who === 'X')          phase = 'win';
    else if (w.who === 'O')     phase = 'lose';
    else                         phase = 'draw';
    lastWin = w;
    refreshAll();
}

function actReset() {
    if (phase === 'bootstrap' || phase === 'waiting_ai') {
        UI.toast('AI 还未就绪', 800);
        return;
    }
    emptyBoard();
    ble.send('reset');
    phase = 'your_turn';   // 玩家先手
    refreshAll();
    UI.toast('新一局开始', 600);
}

// --- BLE 收消息 ---------------------------------------------------------
ble.on('ready', function (msg) {
    aiReadyOnce = true;
    if (phase === 'bootstrap' || phase === 'waiting_ai') {
        emptyBoard();
        phase = 'your_turn';
        refreshAll();
    }
});

ble.on('move', function (msg) {
    var body = msg && msg.body;
    if (!body) return;
    applyAiMove(body.r, body.c);
});

ble.on('reset_ack', function (msg) {
    /* 信息性，AI 也认账了 */
});

ble.onError(function (e) { sys.log('tictactoe: ble err: ' + e); });

// --- 页面 ---------------------------------------------------------------
function buildBoard() {
    var rows = [];
    for (var r = 0; r < 3; r++) {
        var cells = [];
        for (var c = 0; c < 3; c++) {
            var idx = r * 3 + c;
            cells.push(buildCell(idx));
        }
        rows.push(h('panel', {
            size: [-100, 52], flex: 'row',
            flexAlign: ['center', 'center', 'center'], gap: [0, 6]
        }, cells));
    }
    return h('panel', {
        size: [-100, UI.SIZE_CONTENT], flex: 'col', gap: [4, 0]
    }, rows);
}

function buildCell(idx) {
    return h('button', {
        id: cellId(idx),
        size: [50, 50],
        bg: T.C_PANEL, radius: 10,
        border: { color: T.C_BORDER, width: 1, side: 'full', opa: 128 },
        pressedBg: { color: T.C_ACCENT, opa: 60 },
        onClick: function () { tryUserMove(idx); }
    }, [
        h('label', {
            id: cellLblId(idx),
            text: ' ', fg: T.C_TEXT, font: 'title',
            align: ['c', 0, 0]
        })
    ]);
}

function buildHome() {
    return h('panel', {
        bg: T.C_BG, flex: 'col', gap: [10, 0], pad: [12, 8, 12, 8],
        flexAlign: ['center', 'center', 'center']
    }, [
        h('label', {
            id: 'statusLbl', text: statusText(),
            fg: statusColor(), font: 'title'
        }),
        buildBoard(),
        UI.pillBtn({
            id: 'btnReset',
            text: '重新开始',
            w: 130, h: 34,
            onClick: actReset
        })
    ]);
}

Router.define('home', buildHome);

// --- 启动 ---------------------------------------------------------------
// 反复发 hello 直到 PC 端 ready；ready 后清掉 interval。
var helloT = null;
function bootHandshake() {
    ble.send('hello');
    helloT = setInterval(function () {
        if (aiReadyOnce) {
            clearInterval(helloT);
            helloT = null;
            return;
        }
        if (phase === 'bootstrap') {
            phase = 'waiting_ai';
            refreshAll();
        }
        ble.send('hello');
    }, 2000);
}

sys.log('tictactoe: start');
Router.start('home');
bootHandshake();
sys.log('tictactoe: ready');
