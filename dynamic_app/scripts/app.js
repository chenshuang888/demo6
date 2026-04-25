// Dynamic App —— 闹钟页面（华为风格复刻，240×320 适配）
//
// 本文件分两段：
//   §1. VDOM 框架（通用，可复用）
//   §2. 闹钟业务
//
// 设计原则：
//   - VDOM 节点 = { type, props, children, _parent }，靠 props.id 寻址
//   - 在最外层 root container 调一次 sys.ui.attachRootListener，之后
//     所有子按钮的点击都由 LVGL 冒泡到 root，C 侧的 on_lv_root_click
//     拿到被点对象的 id 字符串，通过事件队列回到 JS 侧由 dispatcher
//     沿 _parent 链上爬触发 onClick。
//   - 整页只挂 1 次 native cb；子节点都不调 sys.ui.* 的事件 API。
//   - 状态变化 → 显式调 vdom.set(id, partialProps) 或 vdom.destroy/mount
//
// 约束：esp-mquickjs 仅支持 ES5。

// ============================================================================
// §1. VDOM 框架
// ============================================================================

var VDOM = (function () {

    var nodes = {};
    var autoSeq = 0;

    var FONT_MAP  = { text: sys.font.TEXT, title: sys.font.TITLE, huge: sys.font.HUGE };
    var ALIGN_MAP = {
        tl: sys.align.TOP_LEFT,    tm: sys.align.TOP_MID,    tr: sys.align.TOP_RIGHT,
        lm: sys.align.LEFT_MID,    c:  sys.align.CENTER,     rm: sys.align.RIGHT_MID,
        bl: sys.align.BOTTOM_LEFT, bm: sys.align.BOTTOM_MID, br: sys.align.BOTTOM_RIGHT
    };

    function h(type, props, children) {
        return {
            type: type,
            props: props || {},
            children: children || [],
            _parent: null,
            _mounted: false
        };
    }

    function autoId(type) {
        autoSeq += 1;
        return "_" + type + "_" + autoSeq;
    }

    function applyStyle(id, props) {
        if (props.bg !== undefined)
            sys.ui.setStyle(id, sys.style.BG_COLOR, props.bg, 0, 0, 0);
        if (props.fg !== undefined)
            sys.ui.setStyle(id, sys.style.TEXT_COLOR, props.fg, 0, 0, 0);
        if (props.radius !== undefined)
            sys.ui.setStyle(id, sys.style.RADIUS, props.radius, 0, 0, 0);
        if (props.size !== undefined)
            sys.ui.setStyle(id, sys.style.SIZE,
                props.size[0] | 0, props.size[1] | 0, 0, 0);
        if (props.align !== undefined) {
            var a = props.align;
            var atype = ALIGN_MAP[a[0]];
            if (atype === undefined) atype = sys.align.CENTER;
            sys.ui.setStyle(id, sys.style.ALIGN,
                atype, a[1] | 0, a[2] | 0, 0);
        }
        if (props.pad !== undefined) {
            var p = props.pad;
            sys.ui.setStyle(id, sys.style.PAD,
                p[0] | 0, p[1] | 0, p[2] | 0, p[3] | 0);
        }
        if (props.borderBottom !== undefined)
            sys.ui.setStyle(id, sys.style.BORDER_BOTTOM, props.borderBottom, 0, 0, 0);
        if (props.flex !== undefined)
            sys.ui.setStyle(id, sys.style.FLEX,
                props.flex === 'row' ? 1 : 0, 0, 0, 0);
        if (props.font !== undefined) {
            var f = FONT_MAP[props.font];
            if (f === undefined) f = sys.font.TEXT;
            sys.ui.setStyle(id, sys.style.FONT, f, 0, 0, 0);
        }
    }

    // 事件冒泡 dispatcher
    function dispatch(startId) {
        var node = nodes[startId];
        if (!node) return;
        var stopped = false;
        var ev = {
            target: startId,
            currentTarget: null,
            stopPropagation: function () { stopped = true; }
        };
        var cur = node;
        while (cur) {
            if (typeof cur.props.onClick === 'function') {
                ev.currentTarget = cur.props.id;
                var ret = cur.props.onClick(ev);
                if (ret === false || stopped) return;
            }
            cur = cur._parent;
        }
    }

    function mount(node, parentId) {
        if (node._mounted) return node;

        var id = node.props.id;
        if (!id) { id = autoId(node.type); node.props.id = id; }

        if (node.type === 'panel')       sys.ui.createPanel(id, parentId || null);
        else if (node.type === 'button') sys.ui.createButton(id, parentId || null);
        else if (node.type === 'label')  sys.ui.createLabel(id, parentId || null);
        else { sys.log("VDOM: unknown type " + node.type); return node; }

        if (node.props.text !== undefined) sys.ui.setText(id, "" + node.props.text);
        applyStyle(id, node.props);

        nodes[id] = node;
        node._mounted = true;

        var i;
        for (i = 0; i < node.children.length; i++) {
            node.children[i]._parent = node;
            mount(node.children[i], id);
        }
        return node;
    }

    function find(id) { return nodes[id] || null; }

    function set(id, patch) {
        var node = nodes[id];
        if (!node) { sys.log("VDOM.set: id not found: " + id); return; }
        var k;
        for (k in patch) {
            if (!patch.hasOwnProperty(k)) continue;
            node.props[k] = patch[k];
        }
        if (patch.text !== undefined) sys.ui.setText(id, "" + patch.text);
        applyStyle(id, patch);
    }

    function destroy(id) {
        var node = nodes[id];
        if (!node) return;
        var kids = node.children.slice();
        var i;
        for (i = 0; i < kids.length; i++) {
            if (kids[i].props && kids[i].props.id) destroy(kids[i].props.id);
        }
        var parent = node._parent;
        if (parent && parent.children) {
            var idx = parent.children.indexOf(node);
            if (idx >= 0) parent.children.splice(idx, 1);
        }
        sys.ui.destroy(id);
        delete nodes[id];
    }

    return { h: h, mount: mount, find: find, set: set, destroy: destroy, dispatch: dispatch };
})();

sys.__setDispatcher(function (id) { VDOM.dispatch(id); });

var h = VDOM.h;

// ============================================================================
// §2. 闹钟业务
// ============================================================================
//
// 屏幕 240×320。布局（自上而下）：
//   - header        高 36：标题"闹钟" + 右上两点菜单（装饰，不响应）
//   - clockArea     高 80：当前时间大字（HH:MM:SS）+ 上方时段标签
//   - statusLine    高 20：'已开启 N 个闹钟' / '所有闹钟已关闭'
//   - listArea      剩下空间 -36-80-20-44 = 320-180 = 140：闹钟卡片列表
//   - fabArea       高 44：底部居中圆形 +
//
// 卡片：宽 -100%（即铺满），高 56，圆角 14
//   左侧：时段（小字）+ 时间（大字）+ 副标题"闹钟，每天"
//   右侧：手搓 switch（按钮 36×22，圆角 11，里面一个 16×16 圆 label 当滑块）

var COLOR_BG          = 0x1A1530;   // 深紫主背景
var COLOR_HEADER      = 0x2D2640;
var COLOR_CARD        = 0x2D2640;   // 卡片底
var COLOR_TEXT        = 0xF1ECFF;   // 主文字
var COLOR_TEXT_DIM    = 0x9088A8;   // 副文字
var COLOR_TEXT_OFF    = 0x6B6480;   // 关闭态时间字色
var COLOR_ACCENT      = 0x007DFF;   // 蓝色强调（开关 on / FAB）
var COLOR_SWITCH_OFF  = 0x4A4360;
var COLOR_KNOB        = 0xFFFFFF;

// ---- 闹钟数据模型 ----
var alarms = [
    { tag: "清晨", time: "6:55", sub: "闹钟，每天", on: false },
    { tag: "早上", time: "8:20", sub: "早上好，每天", on: true  },
    { tag: "早上", time: "8:30", sub: "早上好，每天", on: false }
];
var nextAlarmSeq = 100;   // 新建闹钟的 id 起点，避免和初始 0/1/2 冲突

function alarmCardId(seq)    { return "alarm_" + seq; }
function alarmSwitchId(seq)  { return "sw_"     + seq; }
function alarmKnobId(seq)    { return "knob_"   + seq; }
function alarmTimeId(seq)    { return "tm_"     + seq; }

// ---- 状态行更新 ----
function refreshStatus() {
    var n = 0;
    var i;
    for (i = 0; i < alarms.length; i++) if (alarms[i].on) n++;
    var msg = (n === 0) ? "所有闹钟已关闭" : ("已开启 " + n + " 个闹钟");
    VDOM.set("statusLine", { text: msg });
}

// ---- 单张卡片视觉刷新（开/关状态切换） ----
function refreshCard(seq) {
    var idx = findIndexBySeq(seq);
    if (idx < 0) return;
    var on = alarms[idx].on;
    // 时间字色：开 = 主白色，关 = 灰
    VDOM.set(alarmTimeId(seq), { fg: on ? COLOR_TEXT : COLOR_TEXT_OFF });
    // 滑块：on 时背景蓝、knob 推到右；off 时背景灰、knob 在左
    VDOM.set(alarmSwitchId(seq), { bg: on ? COLOR_ACCENT : COLOR_SWITCH_OFF });
    VDOM.set(alarmKnobId(seq),   { align: ['lm', on ? 18 : 2, 0] });
}

// 因为可以 destroy / 重建，alarms[] 索引和卡片 seq 不一一对应，做个反查
function findIndexBySeq(seq) {
    var i;
    for (i = 0; i < alarms.length; i++) {
        if (alarms[i].seq === seq) return i;
    }
    return -1;
}

// ---- 事件回调 ----
function onSwitchClick(e) {
    // e.target 是被点中的叶子（可能是滑块 knob 或 switch 本身）
    // currentTarget 是当前正在执行 handler 的节点 —— 我们把 onClick 挂在 sw_ 上
    var seq = parseInt(e.currentTarget.substring(3), 10);
    var idx = findIndexBySeq(seq);
    if (idx < 0) return;
    alarms[idx].on = !alarms[idx].on;
    refreshCard(seq);
    refreshStatus();
}

function onCardClick(e) {
    // 长按删除：当前没 LONG_PRESS 事件，先用普通 click 演示 destroy
    // 实际产品里这里应该是"打开编辑页"。删除从菜单出
    // 这里我们让"点卡片本身（非滑块区域）" → 删除
    // 注意冒泡顺序：滑块 onClick 先 stop，所以这里只接收非滑块区域点击
    var seq = parseInt(e.currentTarget.substring(6), 10);  // alarm_<seq>
    var idx = findIndexBySeq(seq);
    if (idx < 0) return;
    alarms.splice(idx, 1);
    VDOM.destroy(alarmCardId(seq));
    refreshStatus();
    sys.log("alarm removed: seq=" + seq);
}

function onSwitchClickStop(e) {
    onSwitchClick(e);
    return false;   // 阻止冒泡到卡片，不会触发 onCardClick
}

function onAddClick() {
    var newAlarm = {
        tag: "上午", time: "9:00", sub: "闹钟，仅一次", on: true,
        seq: nextAlarmSeq++
    };
    alarms.push(newAlarm);
    mountAlarmCard(newAlarm);
    refreshCard(newAlarm.seq);
    refreshStatus();
    sys.log("alarm added: seq=" + newAlarm.seq);
}

// ---- 卡片工厂 ----
function makeAlarmCard(a) {
    var seq = a.seq;
    return h('button', {
        id: alarmCardId(seq),
        size: [-100, 56],
        bg: COLOR_CARD,
        radius: 14,
        onClick: onCardClick
    }, [
        // 左侧：时段小字 + 时间大字（同一行 baseline 对齐用 align 微调）
        h('label', { id: "tag_" + seq,  text: a.tag, fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['lm', 14, -10] }),
        h('label', { id: alarmTimeId(seq), text: a.time, fg: COLOR_TEXT,
                     font: 'title', align: ['lm', 42, -10] }),
        h('label', { id: "sub_" + seq, text: a.sub, fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['lm', 14, 14] }),
        // 右侧 switch：button 当轨道，里面 knob label 当圆点
        h('button', {
            id: alarmSwitchId(seq),
            size: [36, 22],
            radius: 11,
            bg: COLOR_SWITCH_OFF,
            align: ['rm', -12, 0],
            onClick: onSwitchClickStop
        }, [
            h('label', { id: alarmKnobId(seq), text: " ", bg: COLOR_KNOB,
                         size: [16, 16], radius: 8,
                         align: ['lm', 2, 0] })
        ])
    ]);
}

function mountAlarmCard(a) {
    VDOM.mount(makeAlarmCard(a), "list");
}

// ---- 时钟 tick：把 uptimeMs 当作"演示时间" ----
//   实际产品应读 RTC，但本 demo 不接入。直接用 uptime 秒数 mod 一天显示。
function pad2(n) { n = n | 0; return n < 10 ? "0" + n : "" + n; }
function refreshClock() {
    var totalSec = (sys.time.uptimeMs() / 1000) | 0;
    var s = totalSec % 60;
    var m = (totalSec / 60 | 0) % 60;
    var hh = (totalSec / 3600 | 0) % 24;
    var period = (hh < 6) ? "凌晨"
               : (hh < 12) ? "上午"
               : (hh < 13) ? "中午"
               : (hh < 18) ? "下午"
               : "晚上";
    VDOM.set("clockPeriod", { text: period });
    VDOM.set("clockMain",   { text: pad2(hh) + ":" + pad2(m) + ":" + pad2(s) });
}

// ============================================================================
// §3. 构造 UI 树
// ============================================================================

sys.log("alarm: build start");

// 根容器
VDOM.mount(
    h('panel', { id: "appRoot", size: [-100, -100], bg: COLOR_BG }),
    null
);

// Header
VDOM.mount(
    h('panel', { id: "header", size: [-100, 36], bg: COLOR_HEADER,
                 align: ['tm', 0, 0] }, [
        h('label', { id: "headerTitle", text: "闹钟", fg: COLOR_TEXT,
                     font: 'title', align: ['lm', 14, 0] }),
        // 右上"⋮"用 BARS 图标顶替（symbols 里没有 dots）
        h('label', { id: "headerMore", text: sys.symbols.BARS, fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['rm', -14, 0] })
    ]),
    "appRoot"
);

// 时钟区
VDOM.mount(
    h('panel', { id: "clockArea", size: [-100, 80], bg: COLOR_BG,
                 align: ['tm', 0, 36] }, [
        h('label', { id: "clockPeriod", text: "上午", fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['c', -82, -2] }),
        h('label', { id: "clockMain", text: "00:00:00", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 14, 0] })
    ]),
    "appRoot"
);

// 状态行
VDOM.mount(
    h('panel', { id: "statusBar", size: [-100, 20], bg: COLOR_BG,
                 align: ['tm', 0, 116] }, [
        h('label', { id: "statusLine", text: "...", fg: COLOR_TEXT_DIM,
                     font: 'text', align: ['c', 0, 0] })
    ]),
    "appRoot"
);

// 列表区（panel 默认 SCROLLABLE 开着，超过高度可滑动）
VDOM.mount(
    h('panel', { id: "list", size: [-100, 140], bg: COLOR_BG, flex: 'col',
                 align: ['tm', 0, 136],
                 pad: [8, 4, 8, 4] }),
    "appRoot"
);

// 初始 3 张卡片（给它们补上 seq）
(function () {
    var i;
    for (i = 0; i < alarms.length; i++) {
        alarms[i].seq = i;   // 0/1/2
        mountAlarmCard(alarms[i]);
    }
})();

// FAB +
VDOM.mount(
    h('button', { id: "fab", size: [44, 44], radius: 22, bg: COLOR_ACCENT,
                  align: ['bm', 0, -8], onClick: onAddClick }, [
        h('label', { id: "fabL", text: "+", fg: COLOR_TEXT,
                     font: 'huge', align: ['c', 0, -4] })
    ]),
    "appRoot"
);

// 一次性挂载 root listener
sys.ui.attachRootListener("appRoot");

// 初始刷新
refreshClock();
refreshStatus();
(function () {
    var i;
    for (i = 0; i < alarms.length; i++) refreshCard(alarms[i].seq);
})();

// 时钟每秒走一下
setInterval(refreshClock, 1000);

sys.log("alarm: build done");
