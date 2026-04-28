// ============================================================================
// Dynamic App —— Aquarium（像素水族箱）
//
// 压测：高频 setInterval (~10fps) + 多 panel 同时 VDOM.set + fs 持久化
//
// 玩法：
//   - 屏幕里 5 条小鱼自由游来游去（panel + label 当鱼身）
//   - 点空白处 = 撒鱼食（出现一个食物点，鱼会被吸过去吃）
//   - 顶部显示饱食度，每 30s 减 1，吃一口加 8
//   - 饱食度落 fs，断电也保留（重启时按"离线了多少秒"补扣）
//
// 容量：5 鱼 × 14B id + 状态 → 内存 < 1KB
// ============================================================================

var APP_NAME = "aquarium";

// ---- 配色 ----
var COL_BG     = 0x0B1929;   // 深海蓝
var COL_BG2    = 0x102B44;
var COL_TEXT   = 0xE0F2FE;
var COL_DIM    = 0x7DD3FC;
var COL_FOOD   = 0xFBBF24;
var COL_HUNGRY = 0xEF4444;
var COL_OK     = 0x10B981;

var FISH_COLORS = [0xF87171, 0xFBBF24, 0x34D399, 0x60A5FA, 0xC084FC];
var FISH_GLYPHS = ["><>", "<><", ">o>", "<o<", "><>"];

// ---- 区域 ----
var AREA_W = 240, AREA_H = 240;     // 鱼可活动区
var AREA_TOP = 40;                   // 顶部 40px 留给 HUD

// ---- 状态 ----
var state = {
    fishes: [],
    food:   null,            // { x, y, ttl } | null
    fed:    50,              // 0..100 饱食度
    lastTickMs: 0,
    lastSavedFed: 50,
    savedAtMs:  0
};

function rand(min, max) { return min + (Math.random() * (max - min + 1) | 0); }

function makeFish(i) {
    return {
        id: "f" + i,
        x:  rand(20, AREA_W - 40),
        y:  rand(AREA_TOP + 10, AREA_TOP + AREA_H - 30),
        vx: (Math.random() < 0.5 ? -1 : 1) * (1 + Math.random() * 1.5),
        vy: (Math.random() - 0.5) * 0.6,
        col: FISH_COLORS[i % FISH_COLORS.length],
        glyph: FISH_GLYPHS[i % FISH_GLYPHS.length]
    };
}

function initFishes() {
    state.fishes = [];
    for (var i = 0; i < 5; i++) state.fishes.push(makeFish(i));
}

// ============================================================================
// 持久化（只存饱食度 + 保存时刻）
// ============================================================================

function loadFed() {
    var raw = sys.app.loadState();
    if (!raw) return;
    var p; try { p = JSON.parse(raw); } catch (e) { return; }
    if (p && typeof p.fed === "number") {
        state.fed = Math.max(0, Math.min(100, p.fed | 0));
        // 离线饥饿：每 30s 掉 1
        if (typeof p.atMs === "number") {
            // 用 uptimeMs 没法跨重启对比；这里就当存档当时的设备时间不可知，
            // 干脆每次启动一律扣 5（演示语义即可）
            state.fed = Math.max(0, state.fed - 5);
        }
    }
}

function maybeSave() {
    // 5 秒一次；变化 ≥ 3 才落，避免狂写 NVS
    var now = sys.time.uptimeMs();
    if (now - state.savedAtMs < 5000) return;
    if (Math.abs(state.fed - state.lastSavedFed) < 3) return;
    sys.app.saveState(JSON.stringify({ fed: state.fed, atMs: now }));
    state.lastSavedFed = state.fed;
    state.savedAtMs   = now;
}

// ============================================================================
// 物理 tick
// ============================================================================

function stepFish(f) {
    // 鱼食吸引
    if (state.food) {
        var dx = state.food.x - f.x;
        var dy = state.food.y - f.y;
        var d2 = dx * dx + dy * dy;
        if (d2 < 14 * 14) {
            // 吃到了
            state.fed = Math.min(100, state.fed + 8);
            state.food = null;
        } else {
            var d = Math.sqrt(d2);
            f.vx = f.vx * 0.85 + (dx / d) * 0.6;
            f.vy = f.vy * 0.85 + (dy / d) * 0.4;
        }
    } else {
        // 随机微扰
        if (Math.random() < 0.05) f.vy += (Math.random() - 0.5) * 0.4;
        if (Math.random() < 0.02) f.vx *= -1;
    }

    f.x += f.vx;
    f.y += f.vy;

    // 边界反弹
    if (f.x < 8)            { f.x = 8;            f.vx = Math.abs(f.vx); }
    if (f.x > AREA_W - 32)  { f.x = AREA_W - 32;  f.vx = -Math.abs(f.vx); }
    if (f.y < AREA_TOP + 4)         { f.y = AREA_TOP + 4;         f.vy = Math.abs(f.vy); }
    if (f.y > AREA_TOP + AREA_H - 14) { f.y = AREA_TOP + AREA_H - 14; f.vy = -Math.abs(f.vy); }

    // 朝向：vx > 0 用右朝向 glyph，否则左朝向
    f.glyph = f.vx >= 0 ? "><>" : "<><";
}

function tickFishes() {
    var i;
    for (i = 0; i < state.fishes.length; i++) stepFish(state.fishes[i]);

    // 食物倒计时
    if (state.food) {
        state.food.ttl -= 1;
        if (state.food.ttl <= 0) state.food = null;
    }

    // 饥饿衰减：10fps × 300tick = 30s 掉 1
    state.lastTickMs += 100;
    if (state.lastTickMs >= 30000) {
        state.lastTickMs = 0;
        state.fed = Math.max(0, state.fed - 1);
    }
}

// ============================================================================
// 视图
// ============================================================================

function fedColor() {
    if (state.fed > 60) return COL_OK;
    if (state.fed > 25) return COL_DIM;
    return COL_HUNGRY;
}

function fedHint() {
    if (state.fed > 80) return "饱饱的";
    if (state.fed > 40) return "想吃饭";
    if (state.fed > 10) return "饿了";
    return "快饿死了 :(";
}

function viewFish(f) {
    return h('label', {
        id: f.id,
        text: f.glyph,
        fg: f.col,
        font: 'text',
        align: ['tl', f.x | 0, f.y | 0]
    });
}

function viewFood() {
    if (!state.food) return null;
    return h('label', {
        id: "food",
        text: "*",
        fg: COL_FOOD,
        font: 'title',
        align: ['tl', (state.food.x - 4) | 0, (state.food.y - 8) | 0]
    });
}

function view() {
    var kids = [
        // HUD
        h('panel', { id: "hud", size: [-100, 36], bg: COL_BG2,
                     align: ['tm', 0, 0] }, [
            h('label', { id: "hudT", text: "🐟 像素鱼缸",
                         fg: COL_TEXT, font: 'title', align: ['lm', 12, 0] }),
            h('label', { id: "hudF", text: state.fed + "/100  " + fedHint(),
                         fg: fedColor(), font: 'text', align: ['rm', -12, 0] })
        ]),
        // 水面区（点这里撒食）
        h('button', { id: "water", size: [-100, 240], bg: COL_BG,
                      align: ['tm', 0, 36], radius: 0,
                      onClick: function (ev) {
                          // ev.dx/dy 来自 root delegation，不是绝对坐标；
                          // 这里没有真坐标，就在水面随机位置撒食
                          state.food = {
                              x: rand(20, AREA_W - 20),
                              y: rand(AREA_TOP + 20, AREA_TOP + AREA_H - 20),
                              ttl: 80   // 8 秒没鱼吃就消失
                          };
                          rerender();
                      } })
    ];

    var i;
    for (i = 0; i < state.fishes.length; i++) kids.push(viewFish(state.fishes[i]));
    var f = viewFood(); if (f) kids.push(f);

    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, kids);
}

function rerender() { VDOM.render(view(), null); }

// ============================================================================
// 主循环
// ============================================================================

function tick() {
    tickFishes();
    rerender();
    maybeSave();
}

sys.log("aquarium: build start");
loadFed();
initFishes();
rerender();
sys.ui.attachRootListener("appRoot");
setInterval(tick, 100);   // 10 fps
sys.log("aquarium: 5 fish, fed=" + state.fed);
