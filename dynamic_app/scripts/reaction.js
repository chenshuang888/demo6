// ============================================================================
// Dynamic App —— Reaction（反应力测试）
//
// 压测：onPress/onRelease 时序、uptimeMs 毫秒精度、fs 排行榜读改写、
//       视图状态机（idle → wait → go → result → leaderboard）
//
// 玩法：
//   - 按"开始"，屏幕红色"准备…"
//   - 1.5~4 秒后变绿色"GO!"，立刻点屏幕，记录反应时（ms）
//   - 提前点 = 抢跑（不计入排行）
//   - 排行榜保留最好 10 次，落到 fs 的 lb.txt（一行一个 ms 数字）
// ============================================================================

var APP_NAME = "reaction";
var LB_FILE  = "lb.txt";
var LB_MAX   = 10;

var COL_BG     = 0x0F172A;
var COL_TEXT   = 0xF1F5F9;
var COL_DIM    = 0x94A3B8;
var COL_RED    = 0xDC2626;
var COL_GREEN  = 0x16A34A;
var COL_BLUE   = 0x3B82F6;
var COL_GOLD   = 0xEAB308;
var COL_CARD   = 0x1E293B;

// 状态机
var ST_IDLE   = 0;
var ST_WAIT   = 1;   // 红屏，倒计时中
var ST_GO     = 2;   // 绿屏，等点击
var ST_RESULT = 3;
var ST_LB     = 4;
var ST_EARLY  = 5;   // 抢跑

var state = {
    st:     ST_IDLE,
    waitTimerStart: 0,
    waitTimerDur:   0,    // ms，下一次绿屏到来的偏移
    goAtMs:         0,    // 真正变绿的时刻
    lastMs:         0,    // 本次反应时
    leaderboard:    []    // 升序，[ms,ms,...]
};

function rand(min, max) { return min + (Math.random() * (max - min) | 0); }

// ---- 排行榜 IO ----

function loadLB() {
    var raw = sys.fs.read(LB_FILE);
    if (!raw) return;
    var parts = raw.split("\n");
    var arr = [];
    var i;
    for (i = 0; i < parts.length && arr.length < LB_MAX; i++) {
        var s = parts[i];
        if (!s) continue;
        var n = parseInt(s, 10);
        if (!isNaN(n) && n > 0 && n < 5000) arr.push(n);
    }
    arr.sort(function (a, b) { return a - b; });
    state.leaderboard = arr;
}

function saveLB() {
    var lines = [];
    var i;
    for (i = 0; i < state.leaderboard.length; i++) {
        lines.push("" + state.leaderboard[i]);
    }
    var text = lines.join("\n");
    if (text.length > 180) text = text.substring(0, 180);
    sys.fs.write(LB_FILE, text);
}

function recordResult(ms) {
    state.leaderboard.push(ms);
    state.leaderboard.sort(function (a, b) { return a - b; });
    if (state.leaderboard.length > LB_MAX) {
        state.leaderboard = state.leaderboard.slice(0, LB_MAX);
    }
    saveLB();
}

// ---- 控制流 ----

function startRound() {
    state.st = ST_WAIT;
    state.waitTimerStart = sys.time.uptimeMs();
    state.waitTimerDur   = rand(1500, 4000);
    state.goAtMs         = state.waitTimerStart + state.waitTimerDur;
    rerender();
}

function onScreenPress() {
    var now = sys.time.uptimeMs();

    if (state.st === ST_IDLE || state.st === ST_RESULT || state.st === ST_EARLY) {
        startRound();
        return;
    }
    if (state.st === ST_WAIT) {
        // 抢跑
        state.st = ST_EARLY;
        rerender();
        return;
    }
    if (state.st === ST_GO) {
        var dt = now - state.goAtMs;
        state.lastMs = dt;
        recordResult(dt);
        state.st = ST_RESULT;
        rerender();
        return;
    }
    if (state.st === ST_LB) {
        state.st = ST_IDLE;
        rerender();
        return;
    }
}

// 后台 tick：检查 wait → go 的切换
function tick() {
    if (state.st === ST_WAIT) {
        if (sys.time.uptimeMs() >= state.goAtMs) {
            state.st = ST_GO;
            rerender();
        }
    }
}

// ---- 视图 ----

function viewIdle() {
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('label', { id: "title", text: "反应力测试",
                     fg: COL_TEXT, font: 'title', align: ['tm', 0, 30] }),
        h('label', { id: "hint",
                     text: "屏幕变绿瞬间点击\n抢跑或慢半拍都不算",
                     fg: COL_DIM, font: 'text', align: ['tm', 0, 70] }),
        h('button', { id: "start", size: [180, 60], radius: 30,
                      bg: COL_BLUE, align: ['c', 0, 20],
                      onClick: onScreenPress }, [
            h('label', { id: "startL", text: "开始",
                         fg: COL_TEXT, font: 'huge', align: ['c', 0, 0] })
        ]),
        h('button', { id: "lbBtn", size: [120, 36], radius: 18,
                      bg: COL_CARD, align: ['bm', 0, -16],
                      onClick: function () { state.st = ST_LB; rerender(); } }, [
            h('label', { id: "lbBtnL", text: "排行榜 (" + state.leaderboard.length + ")",
                         fg: COL_DIM, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function viewWait() {
    return h('button', { id: "appRoot", size: [-100, -100], bg: COL_RED,
                          radius: 0, onClick: onScreenPress }, [
        h('label', { id: "wT", text: "准备…",
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, -20] }),
        h('label', { id: "wS", text: "等屏幕变绿再点",
                     fg: COL_TEXT, font: 'text', align: ['c', 0, 30] })
    ]);
}

function viewGo() {
    return h('button', { id: "appRoot", size: [-100, -100], bg: COL_GREEN,
                          radius: 0, onClick: onScreenPress }, [
        h('label', { id: "gT", text: "GO!",
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, 0] })
    ]);
}

function viewEarly() {
    return h('button', { id: "appRoot", size: [-100, -100], bg: COL_RED,
                          radius: 0, onClick: onScreenPress }, [
        h('label', { id: "eT", text: "抢跑了！",
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, -20] }),
        h('label', { id: "eS", text: "点屏幕重来",
                     fg: COL_TEXT, font: 'text', align: ['c', 0, 30] })
    ]);
}

function rankBadge(ms) {
    if (ms < 220) return "顶级反射";
    if (ms < 280) return "电竞水准";
    if (ms < 350) return "正常人类";
    if (ms < 450) return "稍慢";
    return "再练练";
}

function viewResult() {
    var lb = state.leaderboard;
    var best = lb.length > 0 ? lb[0] : state.lastMs;
    var msg  = (state.lastMs <= best) ? "新纪录！" : "最好 " + best + " ms";
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('label', { id: "rT", text: rankBadge(state.lastMs),
                     fg: COL_GOLD, font: 'title', align: ['tm', 0, 24] }),
        h('label', { id: "rMs", text: state.lastMs + " ms",
                     fg: COL_GREEN, font: 'huge', align: ['c', 0, -20] }),
        h('label', { id: "rBest", text: msg,
                     fg: COL_DIM, font: 'text', align: ['c', 0, 30] }),
        h('button', { id: "rAgain", size: [140, 50], radius: 25,
                      bg: COL_BLUE, align: ['bm', -80, -20],
                      onClick: onScreenPress }, [
            h('label', { id: "rAgainL", text: "再来",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ]),
        h('button', { id: "rLb", size: [140, 50], radius: 25,
                      bg: COL_CARD, align: ['bm', 80, -20],
                      onClick: function () { state.st = ST_LB; rerender(); } }, [
            h('label', { id: "rLbL", text: "排行榜",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ])
    ]);
}

function viewLB() {
    var rows = [];
    var i;
    if (state.leaderboard.length === 0) {
        rows.push(h('label', { id: "lbEmpty", text: "（还没有记录）",
                                fg: COL_DIM, font: 'text',
                                align: ['c', 0, 0] }));
    } else {
        for (i = 0; i < state.leaderboard.length; i++) {
            var ms = state.leaderboard[i];
            var medal = (i === 0) ? "🥇" : (i === 1) ? "🥈" : (i === 2) ? "🥉" : ("#" + (i + 1));
            rows.push(h('panel', { id: "lbR" + i, size: [-100, 28], bg: COL_CARD,
                                    radius: 6 }, [
                h('label', { id: "lbRk" + i, text: medal,
                             fg: COL_GOLD, font: 'text', align: ['lm', 12, 0] }),
                h('label', { id: "lbMs" + i, text: ms + " ms",
                             fg: COL_TEXT, font: 'text', align: ['rm', -12, 0] })
            ]));
        }
    }

    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('label', { id: "lbT", text: "🏆 排行榜",
                     fg: COL_GOLD, font: 'title', align: ['tm', 0, 12] }),
        h('panel', { id: "lbList", size: [-100, 200], bg: COL_BG,
                     align: ['tm', 0, 50], pad: [10, 4, 10, 4],
                     gap: [4, 0], flex: 'col', scrollable: true }, rows),
        h('button', { id: "lbBack", size: [140, 40], radius: 20,
                      bg: COL_BLUE, align: ['bm', 0, -16],
                      onClick: function () { state.st = ST_IDLE; rerender(); } }, [
            h('label', { id: "lbBackL", text: "返回",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function view() {
    if (state.st === ST_WAIT)   return viewWait();
    if (state.st === ST_GO)     return viewGo();
    if (state.st === ST_EARLY)  return viewEarly();
    if (state.st === ST_RESULT) return viewResult();
    if (state.st === ST_LB)     return viewLB();
    return viewIdle();
}

function rerender() { VDOM.render(view(), null); }

// ---- 启动 ----

sys.log("reaction: build start");
loadLB();
rerender();
sys.ui.attachRootListener("appRoot");
setInterval(tick, 30);  // 30ms 检测 wait→go，保证视觉反应及时
sys.log("reaction: lb size=" + state.leaderboard.length);
