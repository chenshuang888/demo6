// ============================================================================
// Dynamic App —— Dashboard（天气仪表盘）
//
// 复用现有 weather provider 协议（tools/providers/weather_provider.py）：
//   ESP → PC   {to:"weather", type:"req", body?:{force:true}}
//   ESP ← PC   {to:"weather", type:"data",  body:{temp_c, temp_min, temp_max,
//                                                  humidity, code, city, desc, ts}}
//   ESP ← PC   {to:"weather", type:"error", body:{msg}}
//
// 资源（apps/dashboard/assets/）：
//   ic_clear.bin    晴
//   ic_cloudy.bin   多云
//   ic_overcast.bin 阴
//   ic_rain.bin     雨
//   ic_snow.bin     雪
//   ic_fog.bin      雾
//   ic_thunder.bin  雷
//   ic_unknown.bin  未知
//
// 多卡片布局（240×320，去掉顶栏 38px 还有 282px 可用）：
//   顶部：标题 + BLE 状态点 + 刷新按钮
//   主卡片：大温度 + 天气图标 + city/desc
//   侧卡片：min/max + humidity
//   底部 forecast：5 个小图标横排（用过去几次缓存模拟"最近 N 次采样"）
// ============================================================================

var APP = "weather";   // 复用 weather 协议 channel
var ble = makeBle(APP);

// 配色
var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_CARD_HI  = 0x2F2752;
var COL_HEADER   = 0x100C1A;
var COL_TEXT     = 0xF1ECFF;
var COL_DIM      = 0x9B94B5;
var COL_OK       = 0x10B981;
var COL_WARN     = 0xF59E0B;
var COL_ERR      = 0xEF4444;
var COL_ACCENT   = 0x06B6D4;
var COL_HOT      = 0xEF4444;
var COL_COLD     = 0x60A5FA;
var COL_MID      = 0x10B981;

// code → 图标资源名
var ICONS = {
    clear:    "ic_clear.bin",
    cloudy:   "ic_cloudy.bin",
    overcast: "ic_overcast.bin",
    rain:     "ic_rain.bin",
    snow:     "ic_snow.bin",
    fog:      "ic_fog.bin",
    thunder:  "ic_thunder.bin",
    unknown:  "ic_unknown.bin"
};

function iconOf(code) {
    return ICONS[code] || ICONS.unknown;
}

function tempColor(t) {
    if (t === undefined || t === null) return COL_DIM;
    if (t >= 28) return COL_HOT;
    if (t <= 5)  return COL_COLD;
    return COL_MID;
}

// ============================================================================
// §1. 状态
// ============================================================================

var state = {
    status: "init",          // init | wait | ok | err | offline
    last:   null,            // 最近一次 data
    err:    "",
    history: [],             // 最近 5 次采样：[{code, ts}, ...] 用来画 forecast 横排
    refreshLockUntil: 0
};

// 持久化最近采样
function saveHist() {
    sys.app.saveState({
        history: state.history.slice(-5),
        last:    state.last
    });
}

function loadHist() {
    var s = sys.app.loadState();
    if (s && s.history && s.history.length !== undefined) {
        state.history = s.history.slice(-5);
    }
    if (s && s.last) state.last = s.last;
}

// ============================================================================
// §2. BLE 处理
// ============================================================================

function setStatus(st, err) {
    state.status = st;
    state.err = err || "";
    refreshDom();
}

function pushHistory(rec) {
    state.history.push({ code: rec.code, ts: rec.ts });
    if (state.history.length > 5) state.history.shift();
}

ble.on("data", function (msg) {
    var d = msg.body || {};
    if (d.temp_c === undefined) {
        setStatus("err", "data 字段缺失");
        return;
    }
    state.last = d;
    pushHistory(d);
    saveHist();
    setStatus("ok");
});

ble.on("error", function (msg) {
    var m = (msg.body && msg.body.msg) || "PC fetch failed";
    setStatus("err", m);
});

// ============================================================================
// §3. 操作
// ============================================================================

function requestData(force) {
    if (!ble.isConnected()) {
        setStatus("offline");
        return;
    }
    var now = sys.time.uptimeMs();
    if (now < state.refreshLockUntil) return;
    state.refreshLockUntil = now + 1500;
    setStatus("wait");
    ble.send("req", force ? { force: true } : undefined);
}

// ============================================================================
// §4. UI 构建
// ============================================================================

function statusDot() {
    var c = COL_DIM;
    if (state.status === 'ok')      c = COL_OK;
    else if (state.status === 'wait')   c = COL_WARN;
    else if (state.status === 'err')    c = COL_ERR;
    else if (state.status === 'offline')c = COL_DIM;
    return h('panel', {
        id: 'dot', size: [10, 10], radius: 5, bg: c,
        align: ['rm', -64, 0]
    });
}

function header() {
    return h('panel', { id: 'hdr', size: [-100, 40], bg: COL_HEADER,
                        align: ['tm', 0, 0] }, [
        h('label', { id: 'hdrT', text: "天气面板",
                     fg: COL_TEXT, font: 'title', align: ['lm', 12, 0] }),
        statusDot(),
        h('button', {
            id: 'refresh', size: [44, 28], radius: 14,
            bg: COL_CARD, align: ['rm', -8, 0],
            onClick: function () { requestData(false); },
            onLongPress: function () { requestData(true); }
        }, [
            h('label', { id: 'refreshL', text: sys.symbols.RIGHT,
                         fg: COL_ACCENT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function statusBar() {
    var txt = "";
    var col = COL_DIM;
    if (state.status === 'init')    txt = "正在请求…";
    else if (state.status === 'wait')    { txt = "拉取中…"; col = COL_WARN; }
    else if (state.status === 'offline') { txt = "PC 未连接"; col = COL_ERR; }
    else if (state.status === 'err')     { txt = "错误：" + state.err; col = COL_ERR; }
    else if (state.status === 'ok' && state.last) {
        txt = state.last.city + " · " + state.last.desc;
        col = COL_DIM;
    }
    return h('label', {
        id: 'statusBar', text: txt,
        fg: col, font: 'text', align: ['tm', 0, 44]
    });
}

function bigTempCard() {
    var d = state.last;
    var t = d ? d.temp_c : null;
    var tStr = (t === null || t === undefined) ? "--" : ("" + t);
    return h('panel', {
        id: 'mainCard', size: [-100, 130], bg: COL_CARD, radius: 14,
        align: ['tm', 0, 70], pad: [8, 8, 8, 8]
    }, [
        h('image', {
            id: 'mainIcon',
            src: d ? iconOf(d.code) : iconOf("unknown"),
            align: ['lm', 16, 0]
        }),
        h('label', {
            id: 'tempBig', text: tStr,
            fg: tempColor(t), font: 'huge', align: ['rm', -56, -10]
        }),
        h('label', {
            id: 'tempUnit', text: "°C",
            fg: COL_DIM, font: 'title', align: ['rm', -16, -10]
        }),
        h('label', {
            id: 'tempCity', text: d ? d.city : "--",
            fg: COL_TEXT, font: 'title', align: ['bm', 0, -8]
        })
    ]);
}

function sideCards() {
    var d = state.last;
    var tmin = d ? d.temp_min : null;
    var tmax = d ? d.temp_max : null;
    var hum  = d ? d.humidity : null;

    return [
        h('panel', {
            id: 'cMin', size: [110, 56], bg: COL_CARD, radius: 10,
            align: ['tl', 8, 210], pad: [6, 6, 6, 6]
        }, [
            h('label', { id: 'cMinL', text: "最低 / 最高",
                         fg: COL_DIM, font: 'text', align: ['tm', 0, 2] }),
            h('label', { id: 'cMinV',
                         text: ((tmin === null) ? "--" : tmin) + " / " +
                               ((tmax === null) ? "--" : tmax) + "°",
                         fg: COL_TEXT, font: 'title', align: ['bm', 0, -2] })
        ]),
        h('panel', {
            id: 'cHum', size: [110, 56], bg: COL_CARD, radius: 10,
            align: ['tr', -8, 210], pad: [6, 6, 6, 6]
        }, [
            h('label', { id: 'cHumL', text: "湿度",
                         fg: COL_DIM, font: 'text', align: ['tm', 0, 2] }),
            h('label', { id: 'cHumV',
                         text: (hum === null) ? "--" : (hum + " %"),
                         fg: COL_ACCENT, font: 'title', align: ['bm', 0, -2] })
        ])
    ];
}

function forecastStrip() {
    // 显示最近 5 次采样的图标（最右是最新）
    var hist = state.history;
    var pad = (5 - hist.length);
    var kids = [
        h('label', { id: 'fcL', text: "最近采样",
                     fg: COL_DIM, font: 'text', align: ['tl', 6, 4] })
    ];
    for (var i = 0; i < 5; i++) {
        var idx = i - pad;
        var rec = (idx >= 0) ? hist[idx] : null;
        var x = 8 + i * 44;
        if (rec) {
            kids.push(h('image', {
                id: 'fc_' + i, src: iconOf(rec.code),
                align: ['bl', x, -6]
            }));
        } else {
            kids.push(h('panel', {
                id: 'fc_' + i, size: [32, 32], radius: 6, bg: COL_CARD_HI,
                align: ['bl', x, -6]
            }));
        }
    }
    return h('panel', {
        id: 'forecast', size: [-100, 56], bg: COL_CARD, radius: 10,
        align: ['bm', 0, -8], pad: [4, 4, 4, 4]
    }, kids);
}

function build() {
    var kids = [ header(), statusBar(), bigTempCard() ];
    var s = sideCards();
    for (var i = 0; i < s.length; i++) kids.push(s[i]);
    kids.push(forecastStrip());
    return h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                        scrollable: false }, kids);
}

// ============================================================================
// §5. 局部更新（status 变化只 patch 相关节点，不重建整树）
// ============================================================================

var rootMounted = false;

function refreshDom() {
    if (!rootMounted) return;
    // dot
    var c = COL_DIM;
    if (state.status === 'ok')      c = COL_OK;
    else if (state.status === 'wait')   c = COL_WARN;
    else if (state.status === 'err')    c = COL_ERR;
    VDOM.set('dot', { bg: c });

    // statusBar
    var txt = "", col = COL_DIM;
    if (state.status === 'init')         txt = "正在请求…";
    else if (state.status === 'wait')    { txt = "拉取中…"; col = COL_WARN; }
    else if (state.status === 'offline') { txt = "PC 未连接"; col = COL_ERR; }
    else if (state.status === 'err')     { txt = "错误：" + state.err; col = COL_ERR; }
    else if (state.status === 'ok' && state.last) {
        txt = state.last.city + " · " + state.last.desc;
    }
    VDOM.set('statusBar', { text: txt, fg: col });

    // 主卡片
    var d = state.last;
    if (d) {
        VDOM.set('mainIcon', { src: iconOf(d.code) });
        VDOM.set('tempBig',  { text: "" + d.temp_c, fg: tempColor(d.temp_c) });
        VDOM.set('tempCity', { text: d.city });
        VDOM.set('cMinV',    { text: d.temp_min + " / " + d.temp_max + "°" });
        VDOM.set('cHumV',    { text: d.humidity + " %" });
    }

    // 历史 strip（每次都全量重画 5 格）
    var hist = state.history;
    var pad = (5 - hist.length);
    for (var i = 0; i < 5; i++) {
        var idx = i - pad;
        var rec = (idx >= 0) ? hist[idx] : null;
        // VDOM.set 只能改属性；image 节点已是 image 节点，setImageSrc 即可
        if (rec) {
            VDOM.set('fc_' + i, { src: iconOf(rec.code) });
        }
        // 注：占位 panel 那几个不能直接换成 image；初次构建若为 panel 就维持。
        //     启动时只要 last/history 已 load，build() 时就会建 image 节点。
    }
}

// ============================================================================
// §6. 启动
// ============================================================================

sys.log("dashboard: boot");
loadHist();
VDOM.mount(build(), null);
rootMounted = true;
sys.ui.attachRootListener('appRoot');
sys.log("dashboard: ui ready, history=" + state.history.length);

// 启动后稍等再请求一次（让 UI 先挂上）
var bootTid = setInterval(function () {
    clearInterval(bootTid);
    requestData(false);
}, 400);
