// Dynamic App —— Timers 页面（MVP）
//
// 功能：
//   - 最多 6 个 timer slot，固定预分配
//   - 点 "+" 进入设置页，±按钮调分钟(±1) / 秒(±10)
//   - Save 回列表，slot 填入 triggerAtMs；每秒 tick 更新剩余时间
//   - 到期弹出 modal（一次只弹一个），Dismiss 后回列表并释放该 slot
//
// 约束：esp-mquickjs 仅支持 ES5，不要 const/let/arrow function/模板字符串。
//
// 没有 destroy/hide 接口，所以显隐用 ALIGN 把 panel 偏到屏幕外 (X_OFFSCREEN)。

var Style = sys.style;
var Align = sys.align;
var Font  = sys.font;
var Sym   = sys.symbols;

// ---------- 配色 ----------
var COLOR_BG         = 0x1A1530;
var COLOR_CARD       = 0x2D2640;
var COLOR_CARD_ALT   = 0x3A3354;
var COLOR_ACCENT     = 0x06B6D4; // 青绿
var COLOR_TEXT       = 0xF1ECFF;
var COLOR_DIM        = 0x6B6480; // 空 slot 灰字
var COLOR_ACTIVE     = 0x10B981; // 活跃 timer 绿
var COLOR_DANGER     = 0xEF4444; // modal 红

// 屏外坐标：用 ALIGN(CENTER, X_OFFSCREEN, 0) 把整个 panel 推出可视区
var X_OFFSCREEN = 1000;

var SLOT_COUNT = 6;

// ---------- 状态 ----------
var view = 'list';           // 'list' | 'set' | 'modal'
var firingSlot = -1;

// 新建草稿
var draftMin = 5;
var draftSec = 0;

// slots[i] = { active: false, triggerAtMs: 0 }
var slots = [];
(function () {
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        slots.push({ active: false, triggerAtMs: 0 });
    }
})();

// ---------- helpers ----------
function panel(id, parent)  { sys.ui.createPanel(id, parent || null);  return id; }
function button(id, parent) { sys.ui.createButton(id, parent || null); return id; }
function label(id, parent, text) {
    sys.ui.createLabel(id, parent || null);
    if (text !== undefined) sys.ui.setText(id, text);
    return id;
}
function st(id, key, a, b, c, d) {
    sys.ui.setStyle(id, key,
        a | 0,
        b === undefined ? 0 : (b | 0),
        c === undefined ? 0 : (c | 0),
        d === undefined ? 0 : (d | 0));
}
function pad2(n) {
    n = n | 0;
    if (n < 0) n = 0;
    if (n < 10) return "0" + n;
    return "" + n;
}
function fmtMS(totalSec) {
    totalSec = totalSec | 0;
    if (totalSec < 0) totalSec = 0;
    var m = (totalSec / 60) | 0;
    var s = totalSec - m * 60;
    if (m > 99) m = 99;
    return pad2(m) + ":" + pad2(s);
}

// ---------- 视图切换 ----------
// 把指定视图居中显示，其余推到屏外。
function showView(name) {
    view = name;
    st("listPanel",  Style.ALIGN, Align.CENTER, name === 'list'  ? 0 : X_OFFSCREEN, 0);
    st("setPanel",   Style.ALIGN, Align.CENTER, name === 'set'   ? 0 : X_OFFSCREEN, 0);
    st("modalPanel", Style.ALIGN, Align.CENTER, name === 'modal' ? 0 : X_OFFSCREEN, 0);
}

// ---------- 构建：根容器 + 三个 panel ----------
sys.log("timers: build start");

// root 背景色（root 已由 C 侧 create，size 由页面决定，这里只刷背景）
// 注意：root 本身未注册到 registry，不能直接 setStyle。
// 所以三个 panel 各自全屏 + 带背景色。

// ---- List Panel ----
panel("listPanel", null);
st("listPanel", Style.SIZE, -100, -100);
st("listPanel", Style.BG_COLOR, COLOR_BG);
st("listPanel", Style.FLEX, 0); // column

// header（标题 + 号按钮）
panel("hdr", "listPanel");
st("hdr", Style.SIZE, -100, 40);
st("hdr", Style.BG_COLOR, COLOR_CARD);

label("hdrTitle", "hdr", "Timers");
st("hdrTitle", Style.TEXT_COLOR, COLOR_TEXT);
st("hdrTitle", Style.FONT, Font.TITLE);
st("hdrTitle", Style.ALIGN, Align.LEFT_MID, 14, 0);

button("addBtn", "hdr");
st("addBtn", Style.SIZE, 36, 32);
st("addBtn", Style.BG_COLOR, COLOR_ACCENT);
st("addBtn", Style.RADIUS, 8);
st("addBtn", Style.ALIGN, Align.RIGHT_MID, -8, 0);

label("addBtnLbl", "addBtn", "+");
st("addBtnLbl", Style.TEXT_COLOR, COLOR_TEXT);
st("addBtnLbl", Style.FONT, Font.TITLE);
st("addBtnLbl", Style.ALIGN, Align.CENTER, 0, 0);

sys.ui.onClick("addBtn", function () {
    if (view !== 'list') return;
    draftMin = 5; draftSec = 0;
    refreshDraft();
    showView('set');
});

// slot 容器（承载 6 行）。固定高度 = 6×32 = 192，避免 -100 把 hdr 挤掉。
panel("list", "listPanel");
st("list", Style.SIZE, -100, 192);
st("list", Style.BG_COLOR, COLOR_CARD);
st("list", Style.FLEX, 0);

(function buildSlots() {
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        var row = "slot_" + i;
        button(row, "list");
        st(row, Style.SIZE, -100, 32);
        st(row, Style.BG_COLOR, COLOR_CARD);
        st(row, Style.BORDER_BOTTOM, COLOR_CARD_ALT);

        var ic = row + "_ic";
        label(ic, row, Sym.BELL);
        st(ic, Style.TEXT_COLOR, COLOR_DIM);
        st(ic, Style.FONT, Font.TEXT);
        st(ic, Style.ALIGN, Align.LEFT_MID, 12, 0);

        var tm = row + "_t";
        label(tm, row, "--:--");
        st(tm, Style.TEXT_COLOR, COLOR_DIM);
        st(tm, Style.FONT, Font.TEXT);
        st(tm, Style.ALIGN, Align.LEFT_MID, 40, 0);

        var state = row + "_s";
        label(state, row, "Empty");
        st(state, Style.TEXT_COLOR, COLOR_DIM);
        st(state, Style.FONT, Font.TEXT);
        st(state, Style.ALIGN, Align.RIGHT_MID, -14, 0);
    }
})();

// ---- Set Panel ----
panel("setPanel", null);
st("setPanel", Style.SIZE, -100, -100);
st("setPanel", Style.BG_COLOR, COLOR_BG);
st("setPanel", Style.ALIGN, Align.CENTER, X_OFFSCREEN, 0);

label("setTitle", "setPanel", "New Timer");
st("setTitle", Style.TEXT_COLOR, COLOR_TEXT);
st("setTitle", Style.FONT, Font.TITLE);
st("setTitle", Style.ALIGN, Align.TOP_MID, 0, 10);

// 分钟列
button("minPlus", "setPanel");
st("minPlus", Style.SIZE, 44, 34);
st("minPlus", Style.BG_COLOR, COLOR_CARD);
st("minPlus", Style.RADIUS, 8);
st("minPlus", Style.ALIGN, Align.CENTER, -48, -48);
label("minPlusL", "minPlus", "+");
st("minPlusL", Style.TEXT_COLOR, COLOR_ACCENT);
st("minPlusL", Style.FONT, Font.TITLE);
st("minPlusL", Style.ALIGN, Align.CENTER, 0, 0);

label("minVal", "setPanel", "05");
st("minVal", Style.TEXT_COLOR, COLOR_TEXT);
st("minVal", Style.FONT, Font.HUGE);
st("minVal", Style.ALIGN, Align.CENTER, -48, 0);

button("minMinus", "setPanel");
st("minMinus", Style.SIZE, 44, 34);
st("minMinus", Style.BG_COLOR, COLOR_CARD);
st("minMinus", Style.RADIUS, 8);
st("minMinus", Style.ALIGN, Align.CENTER, -48, 48);
label("minMinusL", "minMinus", "-");
st("minMinusL", Style.TEXT_COLOR, COLOR_ACCENT);
st("minMinusL", Style.FONT, Font.TITLE);
st("minMinusL", Style.ALIGN, Align.CENTER, 0, 0);

label("minLbl", "setPanel", "Min");
st("minLbl", Style.TEXT_COLOR, COLOR_DIM);
st("minLbl", Style.FONT, Font.TEXT);
st("minLbl", Style.ALIGN, Align.CENTER, -48, 80);

// 冒号
label("colon", "setPanel", ":");
st("colon", Style.TEXT_COLOR, COLOR_TEXT);
st("colon", Style.FONT, Font.HUGE);
st("colon", Style.ALIGN, Align.CENTER, 0, 0);

// 秒列
button("secPlus", "setPanel");
st("secPlus", Style.SIZE, 44, 34);
st("secPlus", Style.BG_COLOR, COLOR_CARD);
st("secPlus", Style.RADIUS, 8);
st("secPlus", Style.ALIGN, Align.CENTER, 48, -48);
label("secPlusL", "secPlus", "+");
st("secPlusL", Style.TEXT_COLOR, COLOR_ACCENT);
st("secPlusL", Style.FONT, Font.TITLE);
st("secPlusL", Style.ALIGN, Align.CENTER, 0, 0);

label("secVal", "setPanel", "00");
st("secVal", Style.TEXT_COLOR, COLOR_TEXT);
st("secVal", Style.FONT, Font.HUGE);
st("secVal", Style.ALIGN, Align.CENTER, 48, 0);

button("secMinus", "setPanel");
st("secMinus", Style.SIZE, 44, 34);
st("secMinus", Style.BG_COLOR, COLOR_CARD);
st("secMinus", Style.RADIUS, 8);
st("secMinus", Style.ALIGN, Align.CENTER, 48, 48);
label("secMinusL", "secMinus", "-");
st("secMinusL", Style.TEXT_COLOR, COLOR_ACCENT);
st("secMinusL", Style.FONT, Font.TITLE);
st("secMinusL", Style.ALIGN, Align.CENTER, 0, 0);

label("secLbl", "setPanel", "Sec");
st("secLbl", Style.TEXT_COLOR, COLOR_DIM);
st("secLbl", Style.FONT, Font.TEXT);
st("secLbl", Style.ALIGN, Align.CENTER, 48, 80);

// 底部按钮
button("cancelBtn", "setPanel");
st("cancelBtn", Style.SIZE, 90, 36);
st("cancelBtn", Style.BG_COLOR, COLOR_CARD);
st("cancelBtn", Style.RADIUS, 8);
st("cancelBtn", Style.ALIGN, Align.BOTTOM_LEFT, 14, -14);
label("cancelL", "cancelBtn", "Cancel");
st("cancelL", Style.TEXT_COLOR, COLOR_TEXT);
st("cancelL", Style.FONT, Font.TEXT);
st("cancelL", Style.ALIGN, Align.CENTER, 0, 0);

button("saveBtn", "setPanel");
st("saveBtn", Style.SIZE, 90, 36);
st("saveBtn", Style.BG_COLOR, COLOR_ACCENT);
st("saveBtn", Style.RADIUS, 8);
st("saveBtn", Style.ALIGN, Align.BOTTOM_RIGHT, -14, -14);
label("saveL", "saveBtn", "Save");
st("saveL", Style.TEXT_COLOR, COLOR_TEXT);
st("saveL", Style.FONT, Font.TEXT);
st("saveL", Style.ALIGN, Align.CENTER, 0, 0);

function clampDraft() {
    if (draftMin < 0)  draftMin = 0;
    if (draftMin > 99) draftMin = 99;
    if (draftSec < 0)  draftSec = 0;
    if (draftSec > 59) draftSec = 59;
}
function refreshDraft() {
    clampDraft();
    sys.ui.setText("minVal", pad2(draftMin));
    sys.ui.setText("secVal", pad2(draftSec));
}

sys.ui.onClick("minPlus",  function () { if (view !== 'set') return; draftMin += 1;  refreshDraft(); });
sys.ui.onClick("minMinus", function () { if (view !== 'set') return; draftMin -= 1;  refreshDraft(); });
sys.ui.onClick("secPlus",  function () { if (view !== 'set') return; draftSec += 10; refreshDraft(); });
sys.ui.onClick("secMinus", function () { if (view !== 'set') return; draftSec -= 10; refreshDraft(); });

sys.ui.onClick("cancelBtn", function () {
    if (view !== 'set') return;
    showView('list');
});

sys.ui.onClick("saveBtn", function () {
    if (view !== 'set') return;
    clampDraft();
    var totalSec = draftMin * 60 + draftSec;
    if (totalSec <= 0) { showView('list'); return; }
    // 找空 slot
    var i;
    for (i = 0; i < SLOT_COUNT; i++) {
        if (!slots[i].active) {
            slots[i].active = true;
            slots[i].triggerAtMs = sys.time.uptimeMs() + totalSec * 1000;
            paintSlot(i);
            showView('list');
            return;
        }
    }
    // 没空位就忽略
    sys.log("timers: all slots full");
    showView('list');
});

// ---- Modal Panel ----
panel("modalPanel", null);
st("modalPanel", Style.SIZE, -100, -100);
st("modalPanel", Style.BG_COLOR, COLOR_CARD);
st("modalPanel", Style.ALIGN, Align.CENTER, X_OFFSCREEN, 0);

label("modalIcon", "modalPanel", Sym.BELL);
st("modalIcon", Style.TEXT_COLOR, COLOR_DANGER);
st("modalIcon", Style.FONT, Font.HUGE);
st("modalIcon", Style.ALIGN, Align.CENTER, 0, -50);

label("modalTitle", "modalPanel", "Time's up!");
st("modalTitle", Style.TEXT_COLOR, COLOR_TEXT);
st("modalTitle", Style.FONT, Font.TITLE);
st("modalTitle", Style.ALIGN, Align.CENTER, 0, 0);

label("modalSub", "modalPanel", "--:--");
st("modalSub", Style.TEXT_COLOR, COLOR_DIM);
st("modalSub", Style.FONT, Font.TEXT);
st("modalSub", Style.ALIGN, Align.CENTER, 0, 28);

button("dismissBtn", "modalPanel");
st("dismissBtn", Style.SIZE, 120, 40);
st("dismissBtn", Style.BG_COLOR, COLOR_DANGER);
st("dismissBtn", Style.RADIUS, 10);
st("dismissBtn", Style.ALIGN, Align.CENTER, 0, 60);
label("dismissL", "dismissBtn", "Dismiss");
st("dismissL", Style.TEXT_COLOR, COLOR_TEXT);
st("dismissL", Style.FONT, Font.TEXT);
st("dismissL", Style.ALIGN, Align.CENTER, 0, 0);

sys.ui.onClick("dismissBtn", function () {
    if (view !== 'modal') return;
    if (firingSlot >= 0) {
        slots[firingSlot].active = false;
        slots[firingSlot].triggerAtMs = 0;
        paintSlot(firingSlot);
        firingSlot = -1;
    }
    showView('list');
});

// ---------- slot 渲染 ----------
function paintSlot(i) {
    var row = "slot_" + i;
    var tm = row + "_t";
    var state = row + "_s";
    var ic = row + "_ic";
    var s = slots[i];
    if (s.active) {
        var remainMs = s.triggerAtMs - sys.time.uptimeMs();
        var remainSec = (remainMs + 999) / 1000 | 0; // ceil
        if (remainSec < 0) remainSec = 0;
        sys.ui.setText(tm, fmtMS(remainSec));
        sys.ui.setText(state, "Active");
        st(ic, Style.TEXT_COLOR, COLOR_ACTIVE);
        st(tm, Style.TEXT_COLOR, COLOR_TEXT);
        st(state, Style.TEXT_COLOR, COLOR_ACTIVE);
    } else {
        sys.ui.setText(tm, "--:--");
        sys.ui.setText(state, "Empty");
        st(ic, Style.TEXT_COLOR, COLOR_DIM);
        st(tm, Style.TEXT_COLOR, COLOR_DIM);
        st(state, Style.TEXT_COLOR, COLOR_DIM);
    }
}

// ---------- 初始化显示 ----------
showView('list');
(function () { var i; for (i = 0; i < SLOT_COUNT; i++) paintSlot(i); })();

// ---------- tick：每秒更新 + 检查到期 ----------
setInterval(function () {
    var now = sys.time.uptimeMs();
    var i;
    // 先刷新活跃 slot 的剩余显示
    for (i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].active) paintSlot(i);
    }
    // 找一个到期的（只触发第一个，其它下一秒再处理）
    if (view !== 'modal') {
        for (i = 0; i < SLOT_COUNT; i++) {
            if (slots[i].active && slots[i].triggerAtMs <= now) {
                firingSlot = i;
                sys.ui.setText("modalSub", "Slot " + (i + 1));
                showView('modal');
                break;
            }
        }
    }
}, 1000);

sys.log("timers: build done");
