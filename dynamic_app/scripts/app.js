// Dynamic App MVP (MicroQuickJS) —— Layer1 widget + Layer2 onClick
//
// 目标：在 dynamic_app 页面里 1:1 复刻"菜单页"的卡片+列表项视觉。
// 所有 UI 操作均通过 sys.ui.* 异步入队，UI 线程 drain 时调用 LVGL。
//
// 注意：esp-mquickjs 仅支持 ES5，不要用 const/let/arrow function。

var Style = sys.style;
var Align = sys.align;
var Font  = sys.font;
var Sym   = sys.symbols;

// 颜色（与 page_menu.c 保持一致）
var COLOR_CARD     = 0x2D2640;
var COLOR_CARD_ALT = 0x3A3354;
var COLOR_ACCENT   = 0x06B6D4;
var COLOR_TEXT     = 0xF1ECFF;

sys.log("dynamic app: building menu-like UI");

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

function makeItem(parent, id, icon, text) {
    var item = button(id, parent);
    // 高 50px、宽 100%（用 -100 表示 lv_pct(100)）
    st(item, Style.SIZE, -100, 50);
    st(item, Style.BORDER_BOTTOM, COLOR_CARD_ALT);

    var ic = label(id + "_ic", item, icon);
    st(ic, Style.TEXT_COLOR, COLOR_ACCENT);
    st(ic, Style.FONT, Font.TITLE);
    st(ic, Style.ALIGN, Align.LEFT_MID, 14, 0);

    var tx = label(id + "_t", item, text);
    st(tx, Style.TEXT_COLOR, COLOR_TEXT);
    st(tx, Style.FONT, Font.TEXT);
    st(tx, Style.ALIGN, Align.LEFT_MID, 48, 0);

    sys.ui.onClick(item, (function (cid) {
        return function () { sys.log("click " + cid); };
    })(id));
    return item;
}

// ---------- 卡片 + 7 个列表项 ----------
var card = panel("card", null);
st(card, Style.SIZE, -100, -100);
st(card, Style.BG_COLOR, COLOR_CARD);
st(card, Style.RADIUS, 12);
st(card, Style.FLEX, 0);   // 0 = column

makeItem(card, "bt", Sym.BLUETOOTH, "Bluetooth");
makeItem(card, "bl", Sym.EYE_OPEN,  "Backlight");
makeItem(card, "tm", Sym.SETTINGS,  "Time & Date");
makeItem(card, "wt", Sym.IMAGE,     "Weather");
makeItem(card, "nt", Sym.BELL,      "Notifications");
makeItem(card, "ms", Sym.AUDIO,     "Music");
makeItem(card, "sy", Sym.BARS,      "System");

sys.log("dynamic app: build done");
