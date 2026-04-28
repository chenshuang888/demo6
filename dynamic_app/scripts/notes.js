// ============================================================================
// Dynamic App —— Notes（便签：演示 sys.fs.* + sys.app.* + 视图切换）
//
// 不需要 PC provider；纯本地、纯 LittleFS 沙箱。
//
// 文件布局（落到 /littlefs/apps/notes/data/）：
//   nN.txt    —— 单条便签纯文本，N = 自增序号（1..）
// 状态（落到 NVS sys.app.* blob）：
//   { nextSeq: <int> }   —— 下一条便签的序号，避免重启冲突
//
// 关键约束（来自 sys.fs.* 实现）：
//   - 单次 fs.write 上限 FS_CHUNK_MAX_BYTES = 196 B（一次性覆盖写）
//   - 文件总大小 ≤ 64KB（这里我们用单帧，所以远低于）
//   - 路径长度 ≤ 31；fs.list() 最多 16 个（多了截断）
//
// 视图：
//   listView    —— 顶部计数 + 滚动列表（首行预览 + 字节数），底部"新建/刷新"
//   detailView  —— 全文 + 字节统计；按钮：戳记 / 删除 / 返回
//   confirmView —— 删除二次确认遮罩
//
// 约束：esp-mquickjs 仅 ES5。
// ============================================================================

var APP_NAME = "notes";

// ---- 配色（沿用项目深紫青绿） ----
var COL_BG     = 0x1E1B2E;
var COL_HEADER = 0x161426;
var COL_CARD   = 0x2D2640;
var COL_CARD_H = 0x3A3252;
var COL_TEXT   = 0xF1ECFF;
var COL_DIM    = 0x9B94B5;
var COL_OFF    = 0x6B6485;
var COL_ACCENT = 0x06B6D4;   // 青绿
var COL_OK     = 0x10B981;
var COL_WARN   = 0xF59E0B;
var COL_DANGER = 0xEF4444;

// ---- 容量限制（与 native 端对齐） ----
var MAX_BYTES_PER_NOTE = 180;   // 留点余量给 \n / utf-8 续字节
var MAX_NOTES          = 16;    // fs.list() 上限
var STAMP_TAIL         = "\n[+%ds]"; // 戳记格式：当前 uptime 秒

// ============================================================================
// §1. 状态
// ============================================================================

var state = {
    view:    "list",     // "list" | "detail" | "confirm"
    nextSeq: 1,          // 持久化字段
    notes:   [],         // [{ name: "n3.txt", seq: 3, body: "...", bytes: 12 }]
    open:    null,       // detail 时的 note 引用
    confirm: null,       // confirm 时被删的 note 引用
    msg:     "",         // 顶部状态提示
    msgUntil:0
};

// ============================================================================
// §2. 持久化（仅存 nextSeq；便签正文走 fs）
// ============================================================================

function loadState() {
    var raw = sys.app.loadState();
    if (!raw) return;
    var parsed = null;
    try { parsed = JSON.parse(raw); } catch (e) { parsed = null; }
    if (parsed && typeof parsed.nextSeq === "number" && parsed.nextSeq > 0) {
        state.nextSeq = parsed.nextSeq | 0;
    }
}

function saveState() {
    sys.app.saveState(JSON.stringify({ nextSeq: state.nextSeq }));
}

// ============================================================================
// §3. 文件操作 helper
// ============================================================================

// "n3.txt" -> 3；不匹配返回 -1
function parseSeq(name) {
    if (!name || name.charAt(0) !== "n") return -1;
    var dot = name.lastIndexOf(".txt");
    if (dot < 2) return -1;
    var s = name.substring(1, dot);
    var n = parseInt(s, 10);
    if (isNaN(n) || n <= 0) return -1;
    return n;
}

function reloadNotes() {
    var names = sys.fs.list();
    var arr = [];
    var i;
    for (i = 0; i < names.length; i++) {
        var nm  = names[i];
        var seq = parseSeq(nm);
        if (seq < 0) continue;
        var body = sys.fs.read(nm);
        if (body === null) continue;
        arr.push({ name: nm, seq: seq, body: body, bytes: body.length });
    }
    // 倒序：新（seq 大的）在上
    arr.sort(function (a, b) { return b.seq - a.seq; });
    state.notes = arr;
}

function uptimeSec() { return (sys.time.uptimeMs() / 1000) | 0; }

function flash(msg) {
    state.msg = msg;
    state.msgUntil = sys.time.uptimeMs() + 2500;
}

function tickFlash() {
    if (state.msg && sys.time.uptimeMs() > state.msgUntil) {
        state.msg = "";
        return true;
    }
    return false;
}

// ============================================================================
// §4. 业务动作
// ============================================================================

function actNew() {
    if (state.notes.length >= MAX_NOTES) {
        flash("已达上限 " + MAX_NOTES + " 条");
        rerender();
        return;
    }
    var seq = state.nextSeq;
    var name = "n" + seq + ".txt";
    var body = "便签 #" + seq + "\n创建于 +" + uptimeSec() + "s";
    if (body.length > MAX_BYTES_PER_NOTE) body = body.substring(0, MAX_BYTES_PER_NOTE);

    var ok = sys.fs.write(name, body);
    if (!ok) { flash("写入失败"); rerender(); return; }

    state.nextSeq = seq + 1;
    saveState();
    flash("已创建 " + name);
    // worker 是异步落盘；这里 list 立刻可能查不到，加入本地缓存先
    state.notes.unshift({ name: name, seq: seq, body: body, bytes: body.length });
    rerender();
}

function actRefresh() {
    reloadNotes();
    flash("已刷新（" + state.notes.length + " 条）");
    rerender();
}

function actOpen(note) {
    state.open = note;
    state.view = "detail";
    rerender();
}

function actBack() {
    state.open    = null;
    state.confirm = null;
    state.view    = "list";
    rerender();
}

function actStamp() {
    if (!state.open) return;
    var tail = STAMP_TAIL.replace("%d", "" + uptimeSec());
    var nb   = state.open.body + tail;
    if (nb.length > MAX_BYTES_PER_NOTE) {
        flash("追加会超 " + MAX_BYTES_PER_NOTE + " B");
        rerender();
        return;
    }
    var ok = sys.fs.write(state.open.name, nb);
    if (!ok) { flash("写入失败"); rerender(); return; }
    state.open.body  = nb;
    state.open.bytes = nb.length;
    flash("已戳记");
    rerender();
}

function actAskDelete(note) {
    state.confirm = note;
    state.view    = "confirm";
    rerender();
}

function actConfirmDelete() {
    var n = state.confirm;
    if (!n) { actBack(); return; }
    var ok = sys.fs.remove(n.name);
    if (!ok) { flash("删除失败"); state.view = "list"; rerender(); return; }
    // 立刻从内存里摘掉（worker 异步落盘）
    var i;
    for (i = 0; i < state.notes.length; i++) {
        if (state.notes[i].name === n.name) { state.notes.splice(i, 1); break; }
    }
    flash("已删除 " + n.name);
    state.confirm = null;
    state.open    = null;
    state.view    = "list";
    rerender();
}

function actCancelDelete() {
    state.confirm = null;
    // 从 list 长按进入的回 list；从 detail 进入的回 detail
    state.view = state.open ? "detail" : "list";
    rerender();
}

// ============================================================================
// §5. 视图（VDOM）
// ============================================================================

function previewLine(body) {
    if (!body) return "(空)";
    var nl = body.indexOf("\n");
    var line = nl < 0 ? body : body.substring(0, nl);
    if (line.length > 24) line = line.substring(0, 24) + "...";
    return line;
}

// 列表的一行
function viewNoteRow(n) {
    return h('button', {
        id: "row_" + n.seq,
        size: [-100, 44],
        bg:   COL_CARD,
        radius: 10,
        onClick:     function () { actOpen(n); },
        onLongPress: function () { actAskDelete(n); }
    }, [
        h('label', {
            id: "rowName_" + n.seq, text: n.name,
            fg: COL_DIM, font: 'text', align: ['lm', 12, -10]
        }),
        h('label', {
            id: "rowPrev_" + n.seq, text: previewLine(n.body),
            fg: COL_TEXT, font: 'text', align: ['lm', 12, 10]
        }),
        h('label', {
            id: "rowSize_" + n.seq, text: n.bytes + " B",
            fg: COL_OFF, font: 'text', align: ['rm', -12, 0]
        })
    ]);
}

function viewList() {
    var rows = [];
    var i;
    for (i = 0; i < state.notes.length; i++) {
        rows.push(viewNoteRow(state.notes[i]));
    }
    if (rows.length === 0) {
        rows.push(h('label', {
            id: "empty", text: "（暂无便签，点下方 + 新建）",
            fg: COL_OFF, font: 'text', align: ['c', 0, 0]
        }));
    }

    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [

        h('panel', { id: "header", size: [-100, 36], bg: COL_HEADER,
                     align: ['tm', 0, 0] }, [
            h('label', { id: "hdrTitle", text: "便签",
                         fg: COL_TEXT, font: 'title', align: ['lm', 14, 0] }),
            h('label', { id: "hdrCount",
                         text: state.notes.length + " / " + MAX_NOTES,
                         fg: COL_DIM, font: 'text', align: ['rm', -14, 0] })
        ]),

        h('label', { id: "msg",
                     text: state.msg || "长按一行删除 · 点击进入查看",
                     fg: state.msg ? COL_OK : COL_DIM,
                     font: 'text', align: ['tm', 0, 42] }),

        h('panel', { id: "list", size: [-100, 200], bg: COL_BG,
                     align: ['tm', 0, 64],
                     pad: [10, 6, 10, 6],
                     gap: [6, 0],
                     flex: 'col',
                     scrollable: true }, rows),

        h('button', { id: "btnNew", size: [110, 38], radius: 19,
                      bg: COL_ACCENT, align: ['bl', 18, -10],
                      onClick: actNew }, [
            h('label', { id: "btnNewL", text: "+ 新建",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]),
        h('button', { id: "btnRef", size: [110, 38], radius: 19,
                      bg: COL_CARD_H, align: ['br', -18, -10],
                      onClick: actRefresh }, [
            h('label', { id: "btnRefL", text: "刷新",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function viewDetail() {
    var n = state.open;
    var bodyText = n ? n.body : "";
    var info     = n ? (n.name + "  ·  " + n.bytes + " / "
                        + MAX_BYTES_PER_NOTE + " B") : "";
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [

        h('panel', { id: "dHdr", size: [-100, 36], bg: COL_HEADER,
                     align: ['tm', 0, 0] }, [
            h('button', { id: "dBack", size: [60, 36], bg: COL_HEADER,
                          align: ['lm', 0, 0], onClick: actBack }, [
                h('label', { id: "dBackL", text: sys.symbols.LEFT,
                             fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
            ]),
            h('label', { id: "dTitle", text: "查看便签",
                         fg: COL_TEXT, font: 'title', align: ['c', 0, 0] }),
            h('label', { id: "dInfo", text: info,
                         fg: COL_DIM, font: 'text', align: ['rm', -10, 0] })
        ]),

        h('panel', { id: "dBody", size: [-100, 180], bg: COL_CARD,
                     radius: 10, align: ['tm', 0, 46],
                     pad: [10, 10, 10, 10], scrollable: true }, [
            h('label', { id: "dBodyL", text: bodyText,
                         fg: COL_TEXT, font: 'text', align: ['tl', 0, 0] })
        ]),

        h('label', { id: "dMsg",
                     text: state.msg ||
                           "戳记会在末尾追加一行 [+秒数s]",
                     fg: state.msg ? COL_OK : COL_OFF,
                     font: 'text', align: ['tm', 0, 232] }),

        h('button', { id: "dStamp", size: [100, 38], radius: 19,
                      bg: COL_ACCENT, align: ['bl', 12, -10],
                      onClick: actStamp }, [
            h('label', { id: "dStampL", text: "+ 戳记",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]),
        h('button', { id: "dDel", size: [100, 38], radius: 19,
                      bg: COL_DANGER, align: ['bm', 0, -10],
                      onClick: function () { actAskDelete(state.open); } }, [
            h('label', { id: "dDelL", text: "删除",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]),
        h('button', { id: "dRet", size: [100, 38], radius: 19,
                      bg: COL_CARD_H, align: ['br', -12, -10],
                      onClick: actBack }, [
            h('label', { id: "dRetL", text: "返回",
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]);
}

function viewConfirm() {
    var n = state.confirm;
    var who = n ? n.name : "";
    return h('panel', { id: "appRoot", size: [-100, -100], bg: COL_BG,
                         scrollable: false }, [
        h('panel', { id: "cBox", size: [220, 150], bg: COL_CARD,
                     radius: 14, align: ['c', 0, 0],
                     pad: [16, 16, 16, 16] }, [
            h('label', { id: "cTitle", text: "确认删除？",
                         fg: COL_WARN, font: 'title', align: ['tm', 0, 4] }),
            h('label', { id: "cName",  text: who,
                         fg: COL_TEXT, font: 'text', align: ['c', 0, -6] }),
            h('label', { id: "cTip",   text: "此操作不可撤销",
                         fg: COL_DIM,  font: 'text', align: ['c', 0, 14] }),
            h('button', { id: "cNo", size: [80, 32], radius: 16,
                          bg: COL_CARD_H, align: ['bl', 0, 0],
                          onClick: actCancelDelete }, [
                h('label', { id: "cNoL", text: "取消",
                             fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
            ]),
            h('button', { id: "cYes", size: [80, 32], radius: 16,
                          bg: COL_DANGER, align: ['br', 0, 0],
                          onClick: actConfirmDelete }, [
                h('label', { id: "cYesL", text: "删除",
                             fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
            ])
        ])
    ]);
}

function view() {
    if (state.view === "detail")  return viewDetail();
    if (state.view === "confirm") return viewConfirm();
    return viewList();
}

function rerender() { VDOM.render(view(), null); }

// ============================================================================
// §6. tick：让顶部 flash 提示自动消失
// ============================================================================

function tick() {
    if (tickFlash()) rerender();
}

// ============================================================================
// §7. 启动
// ============================================================================

sys.log("notes: build start");
loadState();
reloadNotes();
rerender();
sys.ui.attachRootListener("appRoot");
setInterval(tick, 1000);
sys.log("notes: build done, " + state.notes.length + " note(s), nextSeq=" + state.nextSeq);
