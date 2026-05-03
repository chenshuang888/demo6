// ============================================================================
// pomodoro_pkg —— 番茄钟（动态 app 平台测试 #1：纯本地，无 PC 配合）
//
// 验证目标：
//   新增此 app 不动任何 C 代码 / prelude.js / app/* / tools/companion/*。
//   产物：本目录 main.js + manifest.json + 跑一次上传脚本即可。
//
// 状态机：
//   idle   ─ 按"开始" → focus
//   focus  ─ 倒计时归 0 → 自动 pause + toast，按"开始"进入 short_break / long_break
//   *_break ─ 倒计时归 0 → 自动 pause + toast，按"开始"回 focus
//
//   每完成 settings.longEvery 个 focus 后下一次休息 = long_break
//   "跳过"按钮：直接结束当前阶段（不算完成番茄）
//   "重置"按钮：回 idle，已完成数清零
//
// 持久化：
//   仅存 settings 和 done（已完成番茄数）。当前阶段 / 剩余秒数不存 ——
//   重启后从 idle 开始更符合直觉（避免"昨晚关机时还差 3s 的番茄"诡异恢复）。
// ============================================================================

var T = UI.T;
var I = UI.I;

// --- 默认配置 + 状态 -----------------------------------------------------
var settings = {
    focusMin:    25,
    shortMin:    5,
    longMin:     15,
    longEvery:   4
};

var runtime = {
    phase:       'idle',     // idle / focus / short_break / long_break
    remainSec:   0,
    running:     false,
    done:        0           // 累计完成的 focus 个数
};

var tickT = null;            // setInterval id

// --- 工具 ----------------------------------------------------------------
function clampInt(v, lo, hi) {
    v = v | 0;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function fmtMSS(sec) {
    sec = Math.max(0, sec | 0);
    var m = (sec / 60) | 0;
    var s = sec % 60;
    return (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
}

// --- 持久化 --------------------------------------------------------------
(function loadState() {
    var raw = sys.app.loadState();
    if (!raw) return;
    try {
        var s = JSON.parse(raw);
        if (s && s.settings) {
            if (typeof s.settings.focusMin  === 'number') settings.focusMin  = clampInt(s.settings.focusMin,  1, 90);
            if (typeof s.settings.shortMin  === 'number') settings.shortMin  = clampInt(s.settings.shortMin,  1, 30);
            if (typeof s.settings.longMin   === 'number') settings.longMin   = clampInt(s.settings.longMin,   5, 60);
            if (typeof s.settings.longEvery === 'number') settings.longEvery = clampInt(s.settings.longEvery, 2, 8);
        }
        if (typeof s.done === 'number') runtime.done = Math.max(0, s.done | 0);
    } catch (e) { sys.log('pomodoro: bad state'); }
})();

function saveState() {
    sys.app.saveState(JSON.stringify({ settings: settings, done: runtime.done }));
}

// --- 阶段控制 ------------------------------------------------------------
function nextRestPhase() {
    if (runtime.done > 0 && (runtime.done % settings.longEvery) === 0) return 'long_break';
    return 'short_break';
}

function phaseSeconds(phase) {
    if (phase === 'focus')       return settings.focusMin * 60;
    if (phase === 'short_break') return settings.shortMin * 60;
    if (phase === 'long_break')  return settings.longMin  * 60;
    return 0;
}

function phaseLabel(phase) {
    if (phase === 'focus')       return '专注';
    if (phase === 'short_break') return '短休';
    if (phase === 'long_break')  return '长休';
    return '待机';
}

function phaseColor(phase) {
    if (phase === 'focus')       return T.C_ERR;
    if (phase === 'short_break') return T.C_OK;
    if (phase === 'long_break')  return T.C_INFO;
    return T.C_TEXT_MUTED;
}

function startPhase(phase) {
    runtime.phase     = phase;
    runtime.remainSec = phaseSeconds(phase);
    runtime.running   = true;
    refreshHome();
    ensureTick();
}

// --- 计时主循环 ----------------------------------------------------------
function ensureTick() {
    if (tickT) return;
    tickT = setInterval(onTick, 1000);
}

function stopTick() {
    if (tickT) { clearInterval(tickT); tickT = null; }
}

function onTick() {
    if (!runtime.running) return;
    runtime.remainSec--;
    if (runtime.remainSec <= 0) {
        runtime.running = false;
        if (runtime.phase === 'focus') {
            runtime.done++;
            saveState();
            UI.toast('专注完成！按开始进入休息', 1500);
            runtime.phase     = nextRestPhase();
            runtime.remainSec = phaseSeconds(runtime.phase);
        } else if (runtime.phase === 'short_break' || runtime.phase === 'long_break') {
            UI.toast('休息结束！按开始进入专注', 1500);
            runtime.phase     = 'focus';
            runtime.remainSec = phaseSeconds('focus');
        }
        stopTick();
    }
    refreshHome();
}

// --- 操作 ----------------------------------------------------------------
function actStartPause() {
    if (runtime.phase === 'idle') {
        startPhase('focus');
        return;
    }
    if (runtime.running) {
        runtime.running = false;
        stopTick();
    } else {
        runtime.running = true;
        ensureTick();
    }
    refreshHome();
}

function actSkip() {
    if (runtime.phase === 'idle') return;
    runtime.running = false;
    stopTick();
    if (runtime.phase === 'focus') {
        runtime.phase     = nextRestPhase();
        runtime.remainSec = phaseSeconds(runtime.phase);
        UI.toast('已跳过本次专注', 800);
    } else {
        runtime.phase     = 'focus';
        runtime.remainSec = phaseSeconds('focus');
        UI.toast('已跳过休息', 800);
    }
    refreshHome();
}

function actReset() {
    UI.modal({
        title:   '重置番茄钟？',
        body:    '清零本日完成数并停止计时。',
        action0: '取消',
        action1: '确定',
        onResult: function (idx) {
            if (idx !== 1) return;
            runtime.running   = false;
            runtime.phase     = 'idle';
            runtime.remainSec = 0;
            runtime.done      = 0;
            stopTick();
            saveState();
            refreshHome();
        }
    });
}

// --- 页面：home ----------------------------------------------------------
function refreshHome() {
    var displaySec = runtime.phase === 'idle' ? settings.focusMin * 60 : runtime.remainSec;
    VDOM.set('phaseLbl',  { text: phaseLabel(runtime.phase),
                            fg:   phaseColor(runtime.phase) });
    VDOM.set('timeLbl',   { text: fmtMSS(displaySec) });
    VDOM.set('btnRunLbl', { text: runtime.running ? '暂停' : '开始' });
    VDOM.set('doneLbl',   { text: '已完成 ' + runtime.done + ' 个番茄' });
}

function startPauseBtn() {
    return h('button', {
        id: 'btnRun',
        size: [120, 44],
        bg: T.C_ACCENT, radius: 12,
        pressedBg: { color: 0x000000, opa: 38 },
        onClick: actStartPause
    }, [
        h('label', {
            id: 'btnRunLbl',
            text: runtime.running ? '暂停' : '开始',
            fg: T.C_PANEL, font: 'title', align: ['c', 0, 0]
        })
    ]);
}

function buildHome() {
    return h('panel', {
        bg: T.C_BG, flex: 'col', gap: [10, 0], pad: [0, 16, 0, 16],
        flexAlign: ['start', 'center', 'center']
    }, [
        UI.statusBar({ title: '番茄钟' }),
        h('label', {
            id: 'phaseLbl',
            text: phaseLabel(runtime.phase),
            fg:   phaseColor(runtime.phase),
            font: 'title'
        }),
        h('label', {
            id: 'timeLbl',
            text: fmtMSS(runtime.phase === 'idle'
                ? settings.focusMin * 60
                : runtime.remainSec),
            fg: T.C_TEXT, font: 'huge'
        }),
        h('label', {
            id: 'doneLbl',
            text: '已完成 ' + runtime.done + ' 个番茄',
            fg: T.C_TEXT_MUTED, font: 'text'
        }),
        startPauseBtn(),
        h('panel', {
            size: [-100, 38], flex: 'row',
            flexAlign: ['evenly', 'center', 'center'], gap: [0, 6]
        }, [
            UI.pillBtn({ text: '跳过', w: 64, h: 32,
                         bg: T.C_PANEL_HI, fg: T.C_TEXT,
                         onClick: actSkip }),
            UI.pillBtn({ text: '重置', w: 64, h: 32,
                         bg: T.C_PANEL_HI, fg: T.C_TEXT,
                         onClick: actReset }),
            UI.pillBtn({ text: '设置', w: 64, h: 32,
                         bg: T.C_PANEL_HI, fg: T.C_TEXT,
                         onClick: function () { Router.push('settings'); } })
        ])
    ]);
}

// --- 页面：settings ------------------------------------------------------
function adjusterRow(label, valueId, suffix, getCur, dec, inc) {
    return h('panel', {
        size: [-100, 50], flex: 'row',
        flexAlign: ['between', 'center', 'center'], pad: [12, 0, 12, 0]
    }, [
        h('label', { text: label, fg: T.C_TEXT, font: 'text' }),
        h('panel', {
            size: [140, 32], flex: 'row',
            flexAlign: ['between', 'center', 'center'], gap: [0, 6]
        }, [
            h('button', {
                size: [32, 28], bg: T.C_PANEL_HI, radius: 14,
                pressedBg: { color: T.C_ACCENT, opa: 80 },
                onClick: function () {
                    dec();
                    VDOM.set(valueId, { text: getCur() + suffix });
                }
            }, [ h('label', { text: '−', fg: T.C_TEXT, font: 'title',
                              align: ['c', 0, -2] }) ]),
            h('label', { id: valueId, text: getCur() + suffix,
                         fg: T.C_TEXT, font: 'text', align: ['c', 0, 0] }),
            h('button', {
                size: [32, 28], bg: T.C_PANEL_HI, radius: 14,
                pressedBg: { color: T.C_ACCENT, opa: 80 },
                onClick: function () {
                    inc();
                    VDOM.set(valueId, { text: getCur() + suffix });
                }
            }, [ h('label', { text: '+', fg: T.C_TEXT, font: 'title',
                              align: ['c', 0, -2] }) ])
        ])
    ]);
}

function topbar(title) {
    return h('panel', {
        size: [-100, 36], bg: T.C_PANEL,
        flex: 'row', flexAlign: ['start', 'center', 'center'],
        pad: [4, 2, 12, 2], gap: [0, 8],
        border: { color: T.C_BORDER, width: 1, side: 'bottom', opa: 102 }
    }, [
        h('button', {
            size: [40, 28], bg: T.C_PANEL, radius: 6,
            pressedBg: { color: T.C_PANEL_HI, opa: 255 },
            onClick: function () { Router.pop(); }
        }, [
            h('label', { text: I.CHEVRON_LEFT, fg: T.C_ACCENT,
                         font: 'icon24', align: ['c', 0, 0] })
        ]),
        h('label', { text: title, fg: T.C_TEXT, font: 'title' })
    ]);
}

function buildSettings() {
    return h('panel', { bg: T.C_BG, flex: 'col', scrollable: true, gap: [4, 0] }, [
        topbar('设置'),
        UI.card({ pad: [0, 0, 0, 0] }, [
            adjusterRow('专注时长',  'sFocus', ' 分',
                function () { return settings.focusMin; },
                function () { settings.focusMin = clampInt(settings.focusMin - 5, 1, 90); },
                function () { settings.focusMin = clampInt(settings.focusMin + 5, 1, 90); }),
            adjusterRow('短休时长',  'sShort', ' 分',
                function () { return settings.shortMin; },
                function () { settings.shortMin = clampInt(settings.shortMin - 1, 1, 30); },
                function () { settings.shortMin = clampInt(settings.shortMin + 1, 1, 30); }),
            adjusterRow('长休时长',  'sLong', ' 分',
                function () { return settings.longMin; },
                function () { settings.longMin = clampInt(settings.longMin - 5, 5, 60); },
                function () { settings.longMin = clampInt(settings.longMin + 5, 5, 60); }),
            adjusterRow('长休频率',  'sEvery', ' 个',
                function () { return settings.longEvery; },
                function () { settings.longEvery = clampInt(settings.longEvery - 1, 2, 8); },
                function () { settings.longEvery = clampInt(settings.longEvery + 1, 2, 8); })
        ]),
        UI.card({}, [
            h('label', {
                text: '每完成"长休频率"个专注后会自动安排长休。修改在下次开始时生效。',
                fg: T.C_TEXT_DIM, font: 'text'
            })
        ])
    ]);
}

// --- 注册 + 生命周期 -----------------------------------------------------
Router.define('home',     buildHome);
Router.define('settings', buildSettings);

Router.onLeave('settings', function () {
    saveState();
    sys.log('pomodoro: settings saved (focus=' + settings.focusMin
            + ' short=' + settings.shortMin
            + ' long=' + settings.longMin
            + ' every=' + settings.longEvery + ')');
});

Router.onEnter('home', function () {
    refreshHome();
});

sys.log('pomodoro: start');
Router.start('home');
sys.log('pomodoro: ready');
