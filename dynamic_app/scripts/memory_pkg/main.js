// ============================================================================
// Dynamic App —— Memory（记忆翻牌游戏）
//
// 资源（apps/memory/assets/）：
//   back.bin   —— 卡牌背面（同一张图）
//   p1.bin     —— 图案 1
//   p2.bin     —— 图案 2
//   p3.bin     —— 图案 3
//   p4.bin     —— 图案 4   （4 对 = 8 张牌，2×4 网格）
//
// 视图：
//   menuView   —— 标题 + 最佳成绩 + 开始按钮 + 难度提示
//   gameView   —— 顶栏（步数 / 用时）+ 4×2 牌组 + 重新开始
//   winView    —— 庆祝面板（步数 / 用时 / 是否破纪录）+ 再来一局/返回
//
// 持久化：
//   { best: { steps, ms } } —— 最佳成绩
//
// 玩法：
//   - 点牌翻开。同时翻 2 张：
//       * 配对成功：永久亮起
//       * 不配对：800ms 后自动翻回
//   - 全部配对 → winView
// ============================================================================

var APP = "memory";

// 配色
var COL_BG     = 0x14101F;
var COL_HEADER = 0x100C1A;
var COL_CARD   = 0x231C3A;
var COL_TEXT   = 0xF1ECFF;
var COL_DIM    = 0x9B94B5;
var COL_OK     = 0x10B981;
var COL_WARN   = 0xF59E0B;
var COL_ACCENT = 0x06B6D4;
var COL_DANGER = 0xEF4444;

var PAIRS = ["p1.bin", "p2.bin", "p3.bin", "p4.bin"];
var BACK  = "back.bin";

// 网格：4 列 × 2 行
var COLS = 4;
var ROWS = 2;
var CELL = 48;
var GAP  = 8;

// ============================================================================
// §1. 状态
// ============================================================================

var state = {
    view:    "menu",       // menu | game | win
    cards:   [],           // [{ src, matched, open }]
    flipped: [],           // 当前翻开的索引（最多 2）
    steps:   0,
    startMs: 0,
    elapsed: 0,
    locking: false,        // 翻 2 张不匹配时短暂锁定
    timerId: 0,
    best:    null          // { steps, ms } | null
};

// ============================================================================
// §2. 持久化
// ============================================================================

function saveBest() {
    sys.app.saveState({ best: state.best });
}

function loadBest() {
    var s = sys.app.loadState();
    if (s && s.best) state.best = s.best;
}

// ============================================================================
// §3. 工具
// ============================================================================

function shuffle(arr) {
    for (var i = arr.length - 1; i > 0; i--) {
        var j = Math.floor(Math.random() * (i + 1));
        var t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
    return arr;
}

function newDeck() {
    var deck = [];
    for (var i = 0; i < PAIRS.length; i++) {
        deck.push({ src: PAIRS[i], matched: false, open: false });
        deck.push({ src: PAIRS[i], matched: false, open: false });
    }
    return shuffle(deck);
}

function fmtTime(ms) {
    var sec = Math.floor(ms / 1000);
    var m = Math.floor(sec / 60);
    var s = sec % 60;
    return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
}

function isBetter(a, b) {
    if (!b) return true;
    if (a.steps !== b.steps) return a.steps < b.steps;
    return a.ms < b.ms;
}

// ============================================================================
// §4. 计时器（gameView 用，1Hz 刷新顶栏）
// ============================================================================

function startTimer() {
    if (state.timerId) clearInterval(state.timerId);
    state.startMs = sys.time.uptimeMs();
    state.elapsed = 0;
    state.timerId = setInterval(function () {
        if (state.view !== 'game') return;
        state.elapsed = sys.time.uptimeMs() - state.startMs;
        VDOM.set('topTime', { text: fmtTime(state.elapsed) });
    }, 1000);
}

function stopTimer() {
    if (state.timerId) { clearInterval(state.timerId); state.timerId = 0; }
}

// ============================================================================
// §5. 翻牌逻辑
// ============================================================================

function cardId(i) { return "card_" + i; }

function refreshCard(i) {
    var c = state.cards[i];
    var src = (c.open || c.matched) ? c.src : BACK;
    VDOM.set(cardId(i), { src: src });
}

function checkWin() {
    for (var i = 0; i < state.cards.length; i++) {
        if (!state.cards[i].matched) return false;
    }
    return true;
}

function onCardClick(i) {
    if (state.locking) return;
    var c = state.cards[i];
    if (c.matched || c.open) return;

    c.open = true;
    refreshCard(i);
    state.flipped.push(i);

    if (state.flipped.length === 2) {
        state.steps += 1;
        VDOM.set('topSteps', { text: "步数 " + state.steps });

        var a = state.cards[state.flipped[0]];
        var b = state.cards[state.flipped[1]];
        if (a.src === b.src) {
            a.matched = true; b.matched = true;
            state.flipped = [];
            if (checkWin()) {
                stopTimer();
                var rec = { steps: state.steps, ms: state.elapsed };
                if (isBetter(rec, state.best)) {
                    state.best = rec;
                    saveBest();
                    go('win', { broken: true });
                } else {
                    go('win', { broken: false });
                }
            }
        } else {
            state.locking = true;
            var idxs = state.flipped;
            state.flipped = [];
            var tid = setInterval(function () {
                clearInterval(tid);
                a.open = false; b.open = false;
                refreshCard(idxs[0]);
                refreshCard(idxs[1]);
                state.locking = false;
            }, 800);
        }
    }
}

// ============================================================================
// §6. 通用 header
// ============================================================================

function header(title, hasBack) {
    var kids = [
        h('label', { id: 'hdrT', text: title,
                     fg: COL_TEXT, font: 'title',
                     align: ['lm', hasBack ? 44 : 12, 0] })
    ];
    if (hasBack) {
        kids.push(h('button', {
            id: 'hdrBack', size: [32, 28], radius: 14,
            bg: COL_CARD, align: ['lm', 6, 0],
            onClick: function () { go('menu'); }
        }, [
            h('label', { id: 'hdrBackL', text: sys.symbols.LEFT,
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]));
    }
    return h('panel', { id: 'hdr', size: [-100, 38], bg: COL_HEADER,
                        align: ['tm', 0, 0] }, kids);
}

// ============================================================================
// §7. menuView
// ============================================================================

function menuView() {
    var bestText = state.best
        ? ("最佳：" + state.best.steps + " 步 / " + fmtTime(state.best.ms))
        : "还没有记录";

    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                        scrollable: false }, [
        header("记忆翻牌", false),
        h('label', { id: 'subTitle', text: "4 对图案，配对全部牌",
                     fg: COL_DIM, font: 'text', align: ['tm', 0, 60] }),
        h('panel', {
            id: 'bestCard', size: [200, 70], radius: 12, bg: COL_CARD,
            align: ['c', 0, -30], pad: [8, 8, 8, 8]
        }, [
            h('label', { id: 'bestL', text: "🏆 最佳成绩",
                         fg: COL_WARN, font: 'text',  align: ['tm', 0, 4] }),
            h('label', { id: 'bestV', text: bestText,
                         fg: COL_TEXT, font: 'title', align: ['bm', 0, -6] })
        ]),
        h('button', {
            id: 'startBtn', size: [160, 50], radius: 25,
            bg: COL_ACCENT, align: ['c', 0, 60],
            onClick: function () { go('game'); }
        }, [
            h('label', { id: 'startL', text: "开 始",
                         fg: COL_TEXT, font: 'huge', align: ['c', 0, 0] })
        ]),
        h('button', {
            id: 'resetBtn', size: [80, 30], radius: 15,
            bg: COL_CARD, align: ['bm', 0, -16],
            onLongPress: function () {
                state.best = null;
                saveBest();
                go('menu');
            }
        }, [
            h('label', { id: 'resetL', text: "长按清纪录",
                         fg: COL_DIM, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

// ============================================================================
// §8. gameView
// ============================================================================

function gameView() {
    var kids = [
        header("第 1 关", true),
        h('label', { id: 'topSteps', text: "步数 0",
                     fg: COL_DIM, font: 'text', align: ['tl', 56, 12] }),
        h('label', { id: 'topTime',  text: "00:00",
                     fg: COL_DIM, font: 'text', align: ['tr', -12, 12] })
    ];

    // 牌组居中
    var totalW = COLS * CELL + (COLS - 1) * GAP;
    var totalH = ROWS * CELL + (ROWS - 1) * GAP;
    var startX = -((totalW - CELL) / 2);
    var startY = 60;

    for (var i = 0; i < state.cards.length; i++) {
        var col = i % COLS;
        var row = Math.floor(i / COLS);
        var x = startX + col * (CELL + GAP);
        var y = startY + row * (CELL + GAP);
        var c = state.cards[i];
        var src = (c.open || c.matched) ? c.src : BACK;

        (function (idx) {
            kids.push(h('panel', {
                id: 'cellBg_' + idx,
                size: [CELL, CELL], radius: 6,
                bg: c.matched ? COL_OK : COL_CARD,
                align: ['tm', x, y],
                onClick: function () { onCardClick(idx); }
            }, [
                h('image', {
                    id: cardId(idx),
                    src: src,
                    align: ['c', 0, 0]
                })
            ]));
        })(i);
    }

    kids.push(h('button', {
        id: 'restartBtn', size: [120, 36], radius: 18,
        bg: COL_CARD, align: ['bm', 0, -10],
        onLongPress: function () {
            startGame();
            go('game');
        }
    }, [
        h('label', { id: 'restartL', text: "长按重新发牌",
                     fg: COL_DIM, font: 'text', align: ['c', 0, 0] })
    ]));

    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                        scrollable: false }, kids);
}

// ============================================================================
// §9. winView
// ============================================================================

function winView(extra) {
    var broken = !!(extra && extra.broken);
    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                        scrollable: false }, [
        header("通关 ", true),
        h('label', { id: 'winBig', text: broken ? "新纪录" : "完成",
                     fg: broken ? COL_WARN : COL_OK,
                     font: 'huge', align: ['c', 0, -50] }),
        h('panel', {
            id: 'winCard', size: [200, 80], radius: 12, bg: COL_CARD,
            align: ['c', 0, 20], pad: [8, 8, 8, 8]
        }, [
            h('label', { id: 'winSteps',
                         text: "步数 " + state.steps,
                         fg: COL_TEXT, font: 'title', align: ['tm', 0, 4] }),
            h('label', { id: 'winTime',
                         text: "用时 " + fmtTime(state.elapsed),
                         fg: COL_TEXT, font: 'title', align: ['bm', 0, -4] })
        ]),
        h('button', {
            id: 'againBtn', size: [120, 40], radius: 20,
            bg: COL_ACCENT, align: ['bm', -70, -16],
            onClick: function () { startGame(); go('game'); }
        }, [
            h('label', { id: 'againL', text: "再来一局",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ]),
        h('button', {
            id: 'backBtn', size: [80, 40], radius: 20,
            bg: COL_CARD, align: ['bm', 70, -16],
            onClick: function () { go('menu'); }
        }, [
            h('label', { id: 'backL', text: "返回",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

// ============================================================================
// §10. 路由
// ============================================================================

var rootMounted = false;
var lastWinExtra = null;

function go(view, extra) {
    state.view = view;
    if (view === 'win') lastWinExtra = extra || null;

    if (rootMounted) { VDOM.destroy('appRoot'); rootMounted = false; }
    var tree;
    if (view === 'game')      tree = gameView();
    else if (view === 'win')  tree = winView(lastWinExtra);
    else                      tree = menuView();
    VDOM.mount(tree, null);
    rootMounted = true;
    sys.ui.attachRootListener('appRoot');
}

function startGame() {
    state.cards   = newDeck();
    state.flipped = [];
    state.steps   = 0;
    state.locking = false;
    startTimer();
}

// ============================================================================
// §11. 启动
// ============================================================================

sys.log("memory: boot");
loadBest();
go('menu');
sys.log("memory: ready");
