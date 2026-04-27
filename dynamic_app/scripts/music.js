// Dynamic App —— Music (动态版)
//
// 双端协议（详见 tools/providers/media_provider.py）：
//   ESP → PC   {to: "music", type: "req"}                       // 启动 / 用户刷新
//   ESP ← PC   {to: "music", type: "state", body: {...}}        // 完整快照
//   ESP ← PC   {to: "music", type: "no_session"}                // PC 无媒体会话
//   ESP → PC   {to: "music", type: "btn",   body: {id: "prev"|"play"|"next"}}
//
// state body:
//   playing  bool, position int(秒), duration int(秒),
//   title string, artist string, ts int(unix sec)
//
// 进度条：
//   PC 不必每秒推。我们收到 state 后记 (posBase, lastUpdateMs)，
//   30Hz 心跳用 sys.time.uptimeMs() 插值前进。播放/暂停/曲目变都重置基准。
//
// 框架：VDOM / h / makeBle 由 prelude.js 提供。约束 ES5。

var ble = makeBle("music");

// ---- 配色 ----
var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_TEXT     = 0xF1ECFF;
var COL_MUTED    = 0x8B8AA8;
var COL_PRIMARY  = 0x06B6D4;
var COL_OK       = 0x10B981;
var COL_WAIT     = 0xF59E0B;
var COL_ERR      = 0xEF4444;
var COL_PROG_BG  = 0x352B55;
var COL_PROG_FG  = 0x06B6D4;

// ---- 状态 ----
var state = {
    status:   "connecting",   // "connecting" | "no_pc" | "no_session" | "playing" | "paused"
    playing:  false,
    posBase:  0,              // 上一次收到 state 时的 position (秒)
    durSec:   0,              // 总时长 (秒)
    title:    "",
    artist:   "",
    lastUpdateMs: 0
};

function currentPos() {
    if (!state.playing) return state.posBase;
    var dt = (sys.time.uptimeMs() - state.lastUpdateMs) / 1000;
    var p = state.posBase + dt;
    if (state.durSec > 0 && p > state.durSec) p = state.durSec;
    return p;
}

// ---- UI ----
var PROG_TRACK_W  = 200;
var PROG_TRACK_H  = 6;
var PROG_FILL_MAX = PROG_TRACK_W;

function buildUI() {
    var tree = h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                             scrollable: false }, [
        h('panel', { id: 'stBox', size: [-100, 24], align: ['tm', 0, 6],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'stVal', text: 'connecting...',
                         fg: COL_WAIT, font: 'text', align: ['c', 0, 0] })
        ]),

        h('label', { id: 'title', text: '(no track)', fg: COL_TEXT, font: 'title',
                     align: ['tm', 0, 44] }),
        h('label', { id: 'artist', text: '', fg: COL_MUTED, font: 'text',
                     align: ['tm', 0, 80] }),

        h('panel', { id: 'progBg', size: [PROG_TRACK_W, PROG_TRACK_H],
                     align: ['tm', 0, 130], bg: COL_PROG_BG, radius: 3,
                     scrollable: false }, [
            h('panel', { id: 'progFg', size: [0, PROG_TRACK_H],
                         align: ['lm', 0, 0], bg: COL_PROG_FG, radius: 3,
                         scrollable: false })
        ]),

        h('label', { id: 'tCur', text: '0:00', fg: COL_MUTED, font: 'text',
                     align: ['tm', -90, 144] }),
        h('label', { id: 'tDur', text: '0:00', fg: COL_MUTED, font: 'text',
                     align: ['tm',  90, 144] }),

        h('button', { id: 'btnPrev', size: [60, 50], align: ['bm', -75, -16],
                       bg: COL_CARD, radius: 25, onClick: onPrev }, [
            h('label', { id: 'btnPrevL', text: '<<', fg: COL_TEXT, font: 'title',
                         align: ['c', 0, 0] })
        ]),
        h('button', { id: 'btnPlay', size: [70, 56], align: ['bm', 0, -13],
                       bg: COL_PRIMARY, radius: 28, onClick: onPlayPause }, [
            h('label', { id: 'btnPlayL', text: '>', fg: 0x000814, font: 'title',
                         align: ['c', 0, 0] })
        ]),
        h('button', { id: 'btnNext', size: [60, 50], align: ['bm',  75, -16],
                       bg: COL_CARD, radius: 25, onClick: onNext }, [
            h('label', { id: 'btnNextL', text: '>>', fg: COL_TEXT, font: 'title',
                         align: ['c', 0, 0] })
        ])
    ]);
    VDOM.mount(tree, null);
}

// ---- 刷新 ----
function fmtMs(sec) {
    sec = sec | 0;
    if (sec < 0) sec = 0;
    var m = (sec / 60) | 0;
    var s = sec % 60;
    return m + ":" + (s < 10 ? "0" + s : "" + s);
}

function statusInfo() {
    if (state.status === "no_pc")      return ["no PC", COL_ERR];
    if (state.status === "connecting") return ["connecting...", COL_WAIT];
    if (state.status === "no_session") return ["no music app", COL_MUTED];
    if (state.status === "playing")    return ["playing", COL_OK];
    if (state.status === "paused")     return ["paused", COL_WAIT];
    return ["?", COL_MUTED];
}

function refreshStatic() {
    var st = statusInfo();
    VDOM.set('stVal', { text: st[0], fg: st[1] });
    VDOM.set('title',  { text: state.title  || '(no track)' });
    VDOM.set('artist', { text: state.artist || '' });
    VDOM.set('tDur',   { text: fmtMs(state.durSec) });
    VDOM.set('btnPlayL', { text: state.playing ? '||' : '>' });
}

// 30Hz 仅刷"动的"东西：进度条 + 当前时间
function refreshProg() {
    var p = currentPos();
    VDOM.set('tCur', { text: fmtMs(p) });
    var w = 0;
    if (state.durSec > 0) {
        w = (p / state.durSec) * PROG_FILL_MAX;
        if (w < 0) w = 0;
        if (w > PROG_FILL_MAX) w = PROG_FILL_MAX;
    }
    VDOM.set('progFg', { size: [w | 0, PROG_TRACK_H] });
}

// ---- 事件 ----
function onPrev(_e) { ble.send('btn', { id: 'prev' }); }
function onNext(_e) { ble.send('btn', { id: 'next' }); }
function onPlayPause(_e) {
    // 乐观更新：本地立刻翻转，不等 PC 回 state
    state.playing = !state.playing;
    state.posBase = currentPos();
    state.lastUpdateMs = sys.time.uptimeMs();
    state.status = state.playing ? "playing" : "paused";
    refreshStatic();
    ble.send('btn', { id: 'play' });
}

ble.on('state', function (msg) {
    var b = msg.body || {};
    state.playing      = !!b.playing;
    state.posBase      = (b.position | 0);
    state.durSec       = (b.duration | 0);
    state.title        = b.title  || '';
    state.artist       = b.artist || '';
    state.lastUpdateMs = sys.time.uptimeMs();
    state.status       = state.playing ? "playing" : "paused";
    refreshStatic();
    refreshProg();
});

ble.on('no_session', function (_msg) {
    state.status = "no_session";
    state.playing = false;
    state.posBase = 0;
    state.durSec = 0;
    state.title = "";
    state.artist = "";
    refreshStatic();
    refreshProg();
});

// 心跳：连接状态 + 进度条插值
function tick() {
    if (!ble.isConnected()) {
        if (state.status !== "no_pc") {
            state.status = "no_pc";
            refreshStatic();
        }
        return;
    }
    if (state.status === "connecting" || state.status === "no_pc") {
        state.status = "no_session";
        refreshStatic();
        ble.send('req');
    }
    refreshProg();
}

// ---- 启动 ----
sys.log("music: build start");
buildUI();
sys.ui.attachRootListener('appRoot');
refreshStatic();
refreshProg();

if (ble.isConnected()) ble.send('req');

setInterval(tick, 33);
sys.log("music: build done");
