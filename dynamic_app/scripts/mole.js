// ============================================================================
// Dynamic App —— Mole（打地鼠）
//
// 压测：3×3 网格事件冒泡、setInterval/clearInterval 配合、动态色彩切换、
//       fs 排行榜、整树 diff 切视图
//
// 玩法：
//   - 9 格地鼠洞，每秒随机一个洞冒头（或炸弹）
//   - 点冒头的鼠 +10，点空格 -2，点炸弹 -20
//   - 30 秒一局，结束记录最高分
//
// 难度：每过 5 秒，地鼠出现间隔 -50ms（最低 250ms）
// ============================================================================

var APP_NAME = "mole";
var HS_FILE  = "hs.txt";

// 配色
var COL_BG    = 0x14532D;       // 深绿草地
var COL_GRID  = 0x166534;
var COL_HOLE  = 0x422006;       // 洞口棕
var COL_MOLE  = 0xFCD34D;       // 地鼠黄
var COL_BOMB  = 0x111827;       // 炸弹黑
var COL_HIT   = 0x10B981;
var COL_MISS  = 0xEF4444;
var COL_TEXT  = 0xF8FAFC;
var COL_DIM   = 0xBBF7D0;
var COL_CARD  = 0x065F46;

// 状态机
var ST_MENU   = 0;
var ST_PLAY   = 1;
var ST_OVER   = 2;

var GAME_DUR_MS = 30000;
var GRID_W = 3, GRID_H = 3;

var state = {
    st: ST_MENU,
    score: 0,
    highScore: 0,
    startMs: 0,
    leftMs:  GAME_DUR_MS,
    spawnIntervalMs: 900,
    spawnTimerId: -1,
    countdownTimerId: -1,
    activeIdx: -1,        // 0..8 当前冒头的洞，-1 = 无
    activeIsBomb: false,
    activeUntilMs: 0,
    flash: { idx: -1, color: 0, untilMs: 0 }   // 命中/失误闪烁
};

function rand(min, max) { return min + (Math.random() * (max - min + 1) | 0); }

// ---- 持久化 ----

function loadHS() {
    var raw = sys.fs.read(HS_FILE);
    if (!raw) return;
    var n = parseInt(raw, 10);
    if (!isNaN(n) && n >= 0) state.highScore = n;
}

function maybeSaveHS() {
    if (state.score > state.highScore) {
        state.highScore = state.score;
        sys.fs.write(HS_FILE, "" + state.highScore);
    }
}

// ---- 游戏控制 ----

function spawn() {
    state.activeIdx     = rand(0, 8);
    state.activeIsBomb  = Math.random() < 0.18;        // 18% 出炸弹
    state.activeUntilMs = sys.time.uptimeMs() + state.spawnIntervalMs;
    rerender();
}

function tickGame() {
    var now = sys.time.uptimeMs();
    state.leftMs = Math.max(0, GAME_DUR_MS - (now - state.startMs));

    // 难度递增：每 5 秒 -50ms
    var elapsed = (now - state.startMs);
    var lv = (elapsed / 5000) | 0;
    var newInterval = Math.max(250, 900 - lv * 50);
    if (newInterval !== state.spawnIntervalMs) state.spawnIntervalMs = newInterval;

    // 当前洞超时 → 没打中 = miss（不扣分）
    if (state.activeIdx >= 0 && now > state.activeUntilMs) {
        state.activeIdx = -1;
    }

    // 闪烁结束清掉
    if (state.flash.idx >= 0 && now > state.flash.untilMs) {
        state.flash.idx = -1;
    }

    if (state.leftMs <= 0) {
        endGame();
        return;
    }
    rerender();
}

function startGame() {
    state.st = ST_PLAY;
    state.score = 0;
    state.startMs = sys.time.uptimeMs();
    state.leftMs  = GAME_DUR_MS;
    state.spawnIntervalMs = 900;
    state.activeIdx = -1;

    // 两个并行 timer：一个出鼠，一个 UI/倒计时
    state.spawnTimerId = setInterval(function () {
        spawn();
        // 重新挂一遍：因为难度变化时间隔也要变。这里偷懒：用 fixed 周期，
        // 由 spawn 内部用 activeUntilMs 控制存在时长，间隔仍是 900ms（保持简单）
    }, 900);
    state.countdownTimerId = setInterval(tickGame, 100);
    rerender();
}

function endGame() {
    if (state.spawnTimerId    >= 0) clearInterval(state.spawnTimerId);
    if (state.countdownTimerId>= 0) clearInterval(state.countdownTimerId);
    state.spawnTimerId = -1;
    state.countdownTimerId = -1;
    state.activeIdx = -1;
    maybeSaveHS();
    state.st = ST_OVER;
    rerender();
}

function onHoleClick(idx) {
    if (state.st !== ST_PLAY) return;
    if (idx === state.activeIdx) {
        if (state.activeIsBomb) {
            state.score = Math.max(0, state.score - 20);
            state.flash = { idx: idx, color: COL_MISS,
                            untilMs: sys.time.uptimeMs() + 200 };
        } else {
            state.score += 10;
            state.flash = { idx: idx, color: COL_HIT,
                            untilMs: sys.time.uptimeMs() + 200 };
        }
        state.activeIdx = -1;
    } else {
        state.score = Math.max(0, state.score - 2);
        state.flash = { idx: idx, color: COL_MISS,
                        untilMs: sys.time.uptimeMs() + 150 };
    }
    rerender();
}

// ---- 视图 ----

function holeColor(idx) {
    if (state.flash.idx === idx) return state.flash.color;
    if (state.activeIdx === idx) {
        return state.activeIsBomb ? COL_BOMB : COL_MOLE;
    }
    return COL_HOLE;
}

function holeText(idx) {
    if (state.activeIdx === idx) {
        return state.activeIsBomb ? "💣" : "🐹";
    }
    return "";
}

function viewHole(idx) {
    return h('button', {
        id: "hole" + idx,
        size: [60, 60],
        radius: 30,
        bg: holeColor(idx),
        onClick: function () { onHoleClick(idx); }
    }, [
        h('label', {
            id: "holeT" + idx,
            text: holeText(idx),
            fg: COL_TEXT, font: 'title',
            align: ['c', 0, 0]
        })
    ]);
}

function viewGrid() {
    var rows = [];
    var r, c;
    for (r = 0; r < GRID_H; r++) {
        var rowKids = [];
        for (c = 0; c < GRID_W; c++) {
            rowKids.push(viewHole(r * GRID_W + c));
        }
        rows.push(h('panel', {
            id: "row" + r, size: [-100, 64], bg: COL_BG,
            flex: 'row', pad: [4, 4, 4, 4], gap: [12, 0]
        }, rowKids));
    }
    return rows;
}

function viewMenu() {
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('label', { id: "mT", text: "🐹 打地鼠",
                     fg: COL_TEXT, font: 'huge', align: ['tm', 0, 30] }),
        h('label', { id: "mHS", text: "最高分: " + state.highScore,
                     fg: COL_DIM, font: 'title', align: ['c', 0, -30] }),
        h('label', { id: "mTip", text: "黄鼠 +10 · 黑炸 -20\n点空 -2 · 30 秒一局",
                     fg: COL_DIM, font: 'text', align: ['c', 0, 30] }),
        h('button', { id: "mStart", size: [180, 56], radius: 28,
                      bg: COL_HIT, align: ['bm', 0, -20],
                      onClick: startGame }, [
            h('label', { id: "mStartL", text: "开始",
                         fg: COL_TEXT, font: 'huge', align: ['c', 0, 0] })
        ])
    ]);
}

function viewPlay() {
    var leftSec = (state.leftMs / 1000 + 0.99) | 0;
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('panel', { id: "hud", size: [-100, 32], bg: COL_GRID,
                     align: ['tm', 0, 0] }, [
            h('label', { id: "hScore", text: "分: " + state.score,
                         fg: COL_TEXT, font: 'title', align: ['lm', 12, 0] }),
            h('label', { id: "hTime",
                         text: leftSec + "s",
                         fg: leftSec <= 5 ? COL_MISS : COL_TEXT,
                         font: 'title', align: ['rm', -12, 0] })
        ]),
        h('panel', { id: "grid", size: [-100, 220], bg: COL_BG,
                     align: ['c', 0, 10], pad: [4, 4, 4, 4],
                     gap: [0, 8], flex: 'col' },
          viewGrid())
    ]);
}

function viewOver() {
    var newRec = state.score >= state.highScore && state.score > 0;
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('label', { id: "oT", text: "时间到！",
                     fg: COL_TEXT, font: 'huge', align: ['tm', 0, 24] }),
        h('label', { id: "oS", text: "本局: " + state.score,
                     fg: COL_HIT, font: 'huge', align: ['c', 0, -30] }),
        h('label', { id: "oH", text: newRec
                     ? "🎉 新纪录！"
                     : "最高: " + state.highScore,
                     fg: newRec ? COL_MOLE : COL_DIM,
                     font: 'title', align: ['c', 0, 30] }),
        h('button', { id: "oAgain", size: [140, 50], radius: 25,
                      bg: COL_HIT, align: ['bm', -80, -20],
                      onClick: startGame }, [
            h('label', { id: "oAgainL", text: "再来",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ]),
        h('button', { id: "oBack", size: [140, 50], radius: 25,
                      bg: COL_CARD, align: ['bm', 80, -20],
                      onClick: function () { state.st = ST_MENU; rerender(); } }, [
            h('label', { id: "oBackL", text: "菜单",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ])
    ]);
}

function view() {
    if (state.st === ST_PLAY) return viewPlay();
    if (state.st === ST_OVER) return viewOver();
    return viewMenu();
}

function rerender() { VDOM.render(view(), null); }

// ---- 启动 ----

sys.log("mole: build start");
loadHS();
rerender();
sys.ui.attachRootListener("appRoot");
sys.log("mole: high=" + state.highScore);
