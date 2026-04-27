// Dynamic App —— Weather (动态版)
//
// 双端协议（详见 docs/动态app双端通信协议.md + tools/providers/weather_provider.py）：
//   ESP → PC   {to: "weather", type: "req"}                  // 启动 / 用户刷新
//   ESP ← PC   {to: "weather", type: "data",  body: {...}}   // 完整快照
//   ESP ← PC   {to: "weather", type: "error", body: {msg}}   // 拉取失败
//
// body.data 字段：
//   temp_c, temp_min, temp_max, humidity, code, city, desc, ts
//
// 屏上：
//   - 顶部：城市 + 状态条（loading / ok / error / no PC）
//   - 中间：大温度 + min/max
//   - 中下：天气 code 文本 + 湿度
//   - 底部：刷新按钮（短按 = 走缓存；长按 = 强制刷新）
//
// 框架：VDOM / h / makeBle 由 prelude.js 提供。约束 ES5。

var ble = makeBle("weather");

// ---- 配色 ----
var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_TEXT     = 0xF1ECFF;
var COL_MUTED    = 0x8B8AA8;
var COL_OK       = 0x10B981;
var COL_WAIT     = 0xF59E0B;
var COL_ERR      = 0xEF4444;
var COL_PRIMARY  = 0x06B6D4;

// ---- 状态 ----
var state = {
    status: "idle",          // "idle" | "loading" | "ok" | "error"
    data:   null,
    errMsg: null,
    lastReqMs: 0
};

var REQ_MIN_INTERVAL_MS = 1500;  // 防连点
var REQ_TIMEOUT_MS = 8000;       // 自动超时
var pendingTimer = null;

function clearPending() {
    if (pendingTimer !== null) {
        clearInterval(pendingTimer);
        pendingTimer = null;
    }
}

function requestData(force) {
    var now = sys.time.uptimeMs();
    if (now - state.lastReqMs < REQ_MIN_INTERVAL_MS) return;
    state.lastReqMs = now;

    if (!ble.isConnected()) {
        state.status = "error";
        state.errMsg = "no PC";
        refreshUI();
        return;
    }

    state.status = "loading";
    refreshUI();
    ble.send("req", force ? { force: true } : undefined);

    clearPending();
    var startMs = sys.time.uptimeMs();
    pendingTimer = setInterval(function () {
        if (state.status !== "loading") {
            clearPending();
            return;
        }
        if (sys.time.uptimeMs() - startMs > REQ_TIMEOUT_MS) {
            state.status = "error";
            state.errMsg = "timeout";
            clearPending();
            refreshUI();
        }
    }, 500);
}

// ---- UI ----
function buildUI() {
    var tree = h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                             scrollable: false }, [
        h('label', { id: 'city', text: '...', fg: COL_TEXT, font: 'title',
                     align: ['tm', 0, 8] }),

        h('panel', { id: 'stBox', size: [-100, 24], align: ['tm', 0, 36],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'stVal', text: 'starting...',
                         fg: COL_WAIT, font: 'text', align: ['c', 0, 0] })
        ]),

        h('label', { id: 'temp', text: '--°', fg: COL_TEXT, font: 'huge',
                     align: ['tm', 0, 70] }),
        h('label', { id: 'mm', text: '-- / --', fg: COL_MUTED, font: 'text',
                     align: ['tm', 0, 130] }),
        h('label', { id: 'desc', text: '...', fg: COL_PRIMARY, font: 'title',
                     align: ['tm', 0, 158] }),
        h('label', { id: 'hum', text: 'humidity --%', fg: COL_MUTED, font: 'text',
                     align: ['tm', 0, 192] }),
        h('label', { id: 'ts', text: '', fg: COL_MUTED, font: 'text',
                     align: ['tm', 0, 218] }),

        h('button', { id: 'btnRefresh', size: [120, 36], align: ['bm', 0, -16],
                       bg: COL_PRIMARY, radius: 18,
                       onClick: onRefreshClick,
                       onLongPress: onRefreshLongPress }, [
            h('label', { id: 'btnRefreshL', text: 'Refresh',
                         fg: 0x000814, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
    VDOM.mount(tree, null);
}

function statusInfo() {
    if (state.status === "loading") return ["loading...", COL_WAIT];
    if (state.status === "error")   return ["err: " + (state.errMsg || "?"), COL_ERR];
    if (state.status === "ok")      return ["ok", COL_OK];
    return ["idle", COL_MUTED];
}

function pad2(n) { return n < 10 ? "0" + n : "" + n; }

function fmtUnix(ts) {
    if (!ts) return "";
    var sec = ts | 0;
    var hh = ((sec / 3600) | 0) % 24;
    var m  = ((sec / 60) | 0) % 60;
    return "(at " + pad2(hh) + ":" + pad2(m) + " UTC)";
}

function refreshUI() {
    var st = statusInfo();
    VDOM.set('stVal', { text: st[0], fg: st[1] });

    var d = state.data;
    if (d) {
        VDOM.set('city', { text: d.city || "Unknown" });
        VDOM.set('temp', { text: (Math.round(d.temp_c * 10) / 10) + "°" });
        VDOM.set('mm',
            { text: (Math.round(d.temp_min) | 0) + "° / " + (Math.round(d.temp_max) | 0) + "°" });
        VDOM.set('desc', { text: d.desc || d.code || "Unknown" });
        VDOM.set('hum',  { text: "humidity " + (d.humidity | 0) + "%" });
        VDOM.set('ts',   { text: fmtUnix(d.ts) });
    } else if (state.status !== "loading") {
        VDOM.set('temp', { text: '--°' });
    }
}

// ---- 事件 ----
function onRefreshClick(_e)     { requestData(false); }
function onRefreshLongPress(_e) { requestData(true); }

ble.on('data', function (msg) {
    var b = msg.body || {};
    state.data = {
        temp_c:   typeof b.temp_c   === 'number' ? b.temp_c   : 0,
        temp_min: typeof b.temp_min === 'number' ? b.temp_min : 0,
        temp_max: typeof b.temp_max === 'number' ? b.temp_max : 0,
        humidity: typeof b.humidity === 'number' ? b.humidity : 0,
        code:     b.code || "unknown",
        city:     b.city || "?",
        desc:     b.desc || "",
        ts:       b.ts   || 0
    };
    state.status = "ok";
    state.errMsg = null;
    clearPending();
    refreshUI();
});

ble.on('error', function (msg) {
    state.status = "error";
    state.errMsg = (msg.body && msg.body.msg) ? ("" + msg.body.msg).substring(0, 30) : "?";
    clearPending();
    refreshUI();
});

// ---- 启动 ----
sys.log("weather: build start");
buildUI();
sys.ui.attachRootListener('appRoot');
refreshUI();
requestData(false);
sys.log("weather: build done");
