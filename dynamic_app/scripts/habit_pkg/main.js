// ============================================================================
// Dynamic App —— Habit（习惯打卡看板）
//
// 多视图 + 重 saveState 综合体：
//   listView   —— 习惯列表：每行 [图标] 名称 + 连续天数 + 7 天热力点 + 今日打卡按钮
//   detailView —— 单习惯详情：大字号 streak / 累计 / 最长记录 / 7×N 热力图
//   addView    —— 新建习惯：从一组预设里挑（name + symbol）
//
// 持久化（sys.app.saveState）：
//   {
//     habits: [{
//       id: "h1", name: "喝水", icon: " ",   // utf-8 symbol
//       hist: { "20260429": 1, ... },        // 打卡集合
//       created: <uptime ms>
//     }, ...],
//     today: "20260429",                     // 当前会话视为"今天"，按 uptime 推进
//     nextId: 4
//   }
//
// "今天"如何确定：
//   设备没 RTC 也无所谓，本 app 用一个内部计数：
//     - 首次启动写 todayDay=0，开机时间 t0 = uptimeMs
//     - dayOf(uptime) = todayDay + floor((uptime - t0) / DAY_MS)
//   长按"今日已打卡"标签可手动切到下一天（演示用），保证看得到热力图变化。
//
// 约束：ES5；单写者持久化（saveState 在 UI 任务里同步调用，OK）
// ============================================================================

var APP = "habit";
var ble = makeBle(APP);  // 不发协议，仅留接口

// ---- 配色（沿用项目深紫青绿） ----
var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_CARD_HI  = 0x2F2752;
var COL_TEXT     = 0xF1ECFF;
var COL_DIM      = 0x9B94B5;
var COL_OFF      = 0x4A4368;
var COL_ACCENT   = 0x06B6D4;   // 青绿
var COL_OK       = 0x10B981;
var COL_WARN     = 0xF59E0B;
var COL_DANGER   = 0xEF4444;
var COL_HEADER   = 0x100C1A;

// ---- 常量 ----
var DAY_MS = 24 * 3600 * 1000;
var HEATMAP_DAYS = 28;     // 4 周
var HEATMAP_COLS = 7;
var HEATMAP_ROWS = 4;
var HEATMAP_SIZE = 18;
var HEATMAP_GAP  = 4;

var PRESETS = [
    { icon: sys.symbols.AUDIO,     name: "喝水"     },
    { icon: sys.symbols.PLAY,      name: "运动"     },
    { icon: sys.symbols.EYE_OPEN,  name: "阅读"     },
    { icon: sys.symbols.BELL,      name: "早起"     },
    { icon: sys.symbols.BARS,      name: "记账"     },
    { icon: sys.symbols.SETTINGS,  name: "整理"     },
    { icon: sys.symbols.LIST,      name: "复盘"     },
    { icon: sys.symbols.IMAGE,     name: "拍照"     }
];

// ============================================================================
// §1. 状态
// ============================================================================

var t0 = sys.time.uptimeMs();

var state = {
    view:    "list",   // list | detail | add
    habits:  [],
    todayN:  0,        // 整数日索引（0 = 首次启动那天）
    nextId:  1,
    open:    null,
    msg:     "",
    msgUntil:0
};

// ============================================================================
// §2. 持久化
// ============================================================================

function saveAll() {
    sys.app.saveState({
        habits: state.habits,
        todayN: state.todayN,
        nextId: state.nextId
    });
}

function loadAll() {
    var s = sys.app.loadState();
    if (!s) return;
    if (s.habits && s.habits.length !== undefined) state.habits = s.habits;
    if (typeof s.todayN === 'number') state.todayN = s.todayN;
    if (typeof s.nextId === 'number') state.nextId = s.nextId;
}

// ============================================================================
// §3. 工具：日期 / streak / 热力图
// ============================================================================

function currentDay() {
    var elapsed = sys.time.uptimeMs() - t0;
    return state.todayN + Math.floor(elapsed / DAY_MS);
}

function dayKey(n) {
    // 内部不依赖真实日期，用 "D000123" 形式
    return "d" + n;
}

function isCheckedOn(habit, n) {
    return !!(habit.hist && habit.hist[dayKey(n)]);
}

function streakOf(habit) {
    var d = currentDay();
    var s = 0;
    while (isCheckedOn(habit, d) && s < 9999) { s += 1; d -= 1; }
    return s;
}

function longestStreak(habit) {
    if (!habit.hist) return 0;
    var keys = [];
    for (var k in habit.hist) if (habit.hist.hasOwnProperty(k)) keys.push(k);
    keys.sort();
    var best = 0, cur = 0, last = -2;
    for (var i = 0; i < keys.length; i++) {
        var n = parseInt(keys[i].slice(1), 10);
        if (n === last + 1) cur += 1; else cur = 1;
        if (cur > best) best = cur;
        last = n;
    }
    return best;
}

function totalDays(habit) {
    var c = 0;
    if (!habit.hist) return 0;
    for (var k in habit.hist) if (habit.hist.hasOwnProperty(k)) c += 1;
    return c;
}

function toggleToday(habit) {
    var d = currentDay();
    if (!habit.hist) habit.hist = {};
    var k = dayKey(d);
    if (habit.hist[k]) delete habit.hist[k];
    else habit.hist[k] = 1;
    saveAll();
}

// ============================================================================
// §4. 提示条
// ============================================================================

function flash(text, ms) {
    state.msg = text;
    state.msgUntil = sys.time.uptimeMs() + (ms || 1500);
    if (state.view === 'list') renderList();
}

// ============================================================================
// §5. 通用控件
// ============================================================================

function header(title, hasBack) {
    var kids = [
        h('label', { id: 'hdrTitle', text: title,
                     fg: COL_TEXT, font: 'title', align: ['lm', hasBack ? 44 : 12, 0] })
    ];
    if (hasBack) {
        kids.push(h('button', {
            id: 'hdrBack', size: [32, 28], radius: 14,
            bg: COL_CARD, align: ['lm', 6, 0],
            onClick: function () { go('list'); }
        }, [
            h('label', { id: 'hdrBackL', text: sys.symbols.LEFT,
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]));
    }
    var d = currentDay();
    kids.push(h('label', {
        id: 'hdrDay', text: "D" + d,
        fg: COL_DIM, font: 'text', align: ['rm', -10, 0]
    }));
    return h('panel', { id: 'hdr', size: [-100, 38], bg: COL_HEADER,
                        align: ['tm', 0, 0] }, kids);
}

function statusBar() {
    var now = sys.time.uptimeMs();
    var show = state.msg && now < state.msgUntil;
    return h('label', {
        id: 'statusMsg',
        text: show ? state.msg : "",
        fg: COL_OK, font: 'text', align: ['tm', 0, 42]
    });
}

// ============================================================================
// §6. listView：习惯列表
// ============================================================================

function heatRow(habit) {
    // 只画最近 7 天的横排小点（详情里有 28 天矩阵）
    var d = currentDay();
    var dots = [];
    for (var i = 6; i >= 0; i--) {
        var n = d - i;
        var on = isCheckedOn(habit, n);
        dots.push(h('panel', {
            id: 'dot_' + habit.id + '_' + i,
            size: [10, 10], radius: 5,
            bg: on ? COL_OK : COL_OFF,
            align: ['lm', 12 + (6 - i) * 14, 0]
        }));
    }
    return h('panel', {
        id: 'row_dots_' + habit.id,
        size: [110, 14], bg: COL_CARD,
        align: ['rm', -90, 0]
    }, dots);
}

function habitRow(habit, idx) {
    var d = currentDay();
    var checked = isCheckedOn(habit, d);
    var streak  = streakOf(habit);

    return h('panel', {
        id: 'row_' + habit.id,
        size: [-100, 56], bg: COL_CARD, radius: 10,
        align: ['tm', 0, 92 + idx * 64],
        pad: [8, 8, 8, 8],
        onClick: function () { state.open = habit; go('detail'); }
    }, [
        h('label', { id: 'r_ic_' + habit.id, text: habit.icon || sys.symbols.LIST,
                     fg: COL_ACCENT, font: 'title', align: ['lm', 8, -8] }),
        h('label', { id: 'r_nm_' + habit.id, text: habit.name,
                     fg: COL_TEXT,   font: 'title', align: ['lm', 36, -8] }),
        h('label', { id: 'r_st_' + habit.id,
                     text: (streak > 0 ? (sys.symbols.PLAY + " " + streak + "天") : "未开始"),
                     fg: streak > 0 ? COL_WARN : COL_DIM,
                     font: 'text', align: ['lm', 36, 12] }),
        heatRow(habit),
        h('button', {
            id: 'r_btn_' + habit.id,
            size: [56, 32], radius: 16,
            bg: checked ? COL_OK : COL_ACCENT,
            align: ['rm', -8, 0],
            onClick: function (ev) {
                ev.stopPropagation();
                toggleToday(habit);
                renderList();
                flash(checked ? "已取消打卡" : "打卡 +1 ", 1200);
            }
        }, [
            h('label', { id: 'r_btn_l_' + habit.id,
                         text: checked ? "✓" : "打卡",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function listView() {
    var kids = [
        header("习惯打卡", false),
        statusBar()
    ];

    if (state.habits.length === 0) {
        kids.push(h('label', {
            id: 'empty',
            text: "还没有习惯，点右下 + 新建",
            fg: COL_DIM, font: 'text', align: ['c', 0, 0]
        }));
    } else {
        for (var i = 0; i < state.habits.length; i++) {
            kids.push(habitRow(state.habits[i], i));
        }
    }

    // 浮动新建按钮（右下）
    kids.push(h('button', {
        id: 'fab', size: [54, 54], radius: 27,
        bg: COL_ACCENT, align: ['br', -12, -12],
        onClick: function () { go('add'); }
    }, [
        h('label', { id: 'fabL', text: "+",
                     fg: COL_TEXT, font: 'huge', align: ['c', 0, -2] })
    ]));

    // 长按推进一天（演示热力图）
    kids.push(h('button', {
        id: 'nextDay', size: [54, 28], radius: 14,
        bg: COL_CARD, align: ['bl', 12, -25],
        onLongPress: function () {
            state.todayN += 1;
            saveAll();
            flash("→ 下一天", 900);
            renderList();
        }
    }, [
        h('label', { id: 'nextDayL', text: "+1天",
                     fg: COL_DIM, font: 'text', align: ['c', 0, 0] })
    ]));

    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                         scrollable: false }, kids);
}

// ============================================================================
// §7. detailView：单习惯详情 + 热力图
// ============================================================================

function heatGrid(habit) {
    var d = currentDay();
    var cells = [];
    // 4 行 × 7 列；列 = 周内位（0=最早），右下角 = 今天
    for (var i = 0; i < HEATMAP_DAYS; i++) {
        var n = d - (HEATMAP_DAYS - 1 - i);
        var col = i % HEATMAP_COLS;
        var row = Math.floor(i / HEATMAP_COLS);
        var on = isCheckedOn(habit, n);
        var isToday = (n === d);
        cells.push(h('panel', {
            id: 'hm_' + i,
            size: [HEATMAP_SIZE, HEATMAP_SIZE],
            radius: 4,
            bg: on ? COL_OK : COL_OFF,
            align: ['tl',
                col * (HEATMAP_SIZE + HEATMAP_GAP),
                row * (HEATMAP_SIZE + HEATMAP_GAP)],
            borderBottom: isToday ? 2 : 0
        }));
    }
    var w = HEATMAP_COLS * HEATMAP_SIZE + (HEATMAP_COLS - 1) * HEATMAP_GAP;
    var hgt = HEATMAP_ROWS * HEATMAP_SIZE + (HEATMAP_ROWS - 1) * HEATMAP_GAP;
    return h('panel', {
        id: 'heatmap',
        size: [w + 4, hgt + 4],
        bg: COL_CARD,
        align: ['c', 0, 30],
        pad: [2, 2, 2, 2]
    }, cells);
}

function statBlock(id, label, value, color, x, y) {
    return h('panel', {
        id: id, size: [88, 56], radius: 8, bg: COL_CARD,
        align: ['tm', x, y], pad: [4, 4, 4, 4]
    }, [
        h('label', { id: id + 'V', text: "" + value,
                     fg: color, font: 'huge', align: ['tm', 0, 2] }),
        h('label', { id: id + 'L', text: label,
                     fg: COL_DIM, font: 'text', align: ['bm', 0, -2] })
    ]);
}

function detailView() {
    var hb = state.open;
    if (!hb) { go('list'); return listView(); }

    var streak = streakOf(hb);
    var total  = totalDays(hb);
    var best   = longestStreak(hb);

    var kids = [
        header(hb.icon + "  " + hb.name, true),
        statBlock('stCur',  "连续",  streak, COL_OK,     -94, 50),
        statBlock('stTot',  "累计",  total,  COL_ACCENT,   0, 50),
        statBlock('stBest', "最长",  best,   COL_WARN,    94, 50),
        heatGrid(hb),
        h('label', {
            id: 'hint',
            text: "下方按钮：今日打卡 / 删除习惯",
            fg: COL_DIM, font: 'text', align: ['bm', 0, -64]
        }),
        h('button', {
            id: 'detCheck', size: [120, 40], radius: 20,
            bg: isCheckedOn(hb, currentDay()) ? COL_OK : COL_ACCENT,
            align: ['bm', -70, -16],
            onClick: function () {
                toggleToday(hb);
                go('detail');
            }
        }, [
            h('label', { id: 'detCheckL',
                         text: isCheckedOn(hb, currentDay())
                             ? "✓ 已打卡" : "打卡今日",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] })
        ]),
        h('button', {
            id: 'detDel', size: [80, 40], radius: 20,
            bg: COL_DANGER, align: ['bm', 70, -16],
            onLongPress: function () {
                var idx = state.habits.indexOf(hb);
                if (idx >= 0) state.habits.splice(idx, 1);
                state.open = null;
                saveAll();
                flash("已删除", 1200);
                go('list');
            }
        }, [
            h('label', { id: 'detDelL', text: "长按删除",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ];

    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                         scrollable: false }, kids);
}

// ============================================================================
// §8. addView：从预设新建
// ============================================================================

function addView() {
    var kids = [ header("新建习惯", true) ];

    var perRow = 4;
    var cellW = 56, cellH = 60, gap = 4;
    var totalW = perRow * cellW + (perRow - 1) * gap;
    var startX = -((totalW - cellW) / 2);

    for (var i = 0; i < PRESETS.length; i++) {
        var p = PRESETS[i];
        var row = Math.floor(i / perRow);
        var col = i % perRow;
        var x = startX + col * (cellW + gap);
        var y = 60 + row * (cellH + gap);

        (function (preset) {
            kids.push(h('button', {
                id: 'pre_' + preset.name,
                size: [cellW, cellH], radius: 10,
                bg: COL_CARD,
                align: ['tm', x, y],
                onClick: function () {
                    if (state.habits.length >= 12) {
                        flash("最多 12 个", 1500);
                        return;
                    }
                    state.habits.push({
                        id:      "h" + state.nextId,
                        name:    preset.name,
                        icon:    preset.icon,
                        hist:    {},
                        created: sys.time.uptimeMs()
                    });
                    state.nextId += 1;
                    saveAll();
                    flash("已添加 " + preset.name, 1200);
                    go('list');
                }
            }, [
                h('label', { id: 'pre_i_' + preset.name, text: preset.icon,
                             fg: COL_ACCENT, font: 'title', align: ['tm', 0, 6] }),
                h('label', { id: 'pre_n_' + preset.name, text: preset.name,
                             fg: COL_TEXT,   font: 'text',  align: ['bm', 0, -4] })
            ]));
        })(p);
    }

    kids.push(h('label', {
        id: 'addHint',
        text: "点一下即添加",
        fg: COL_DIM, font: 'text', align: ['bm', 0, -16]
    }));

    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                         scrollable: false }, kids);
}

// ============================================================================
// §9. 路由 & 渲染
// ============================================================================

var rootMounted = false;

function pickView() {
    if (state.view === 'detail') return detailView();
    if (state.view === 'add')    return addView();
    return listView();
}

function go(view) {
    if (state.view === view && rootMounted) return;
    state.view = view;
    // 整树重建：不同视图差异太大，diff 不划算
    if (rootMounted) {
        VDOM.destroy('appRoot');
        rootMounted = false;
    }
    VDOM.mount(pickView(), null);
    rootMounted = true;
    sys.ui.attachRootListener('appRoot');
}

function renderList() {
    if (state.view !== 'list') return;
    if (rootMounted) {
        VDOM.destroy('appRoot');
        rootMounted = false;
    }
    VDOM.mount(listView(), null);
    rootMounted = true;
    sys.ui.attachRootListener('appRoot');
}

// ============================================================================
// §10. 启动
// ============================================================================

sys.log("habit: boot");
loadAll();
go('list');
sys.log("habit: ready, " + state.habits.length + " habits");
