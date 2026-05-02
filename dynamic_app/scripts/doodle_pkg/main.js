// ============================================================================
// doodle —— 涂鸦板（验证 sys.canvas + sys.fs 大文件读写 + assets）
//
// 协议：纯本地，无 BLE
//
// 数据：
//   每张作品 = 240×216 RGB565 二进制 = 103680 字节，存为 data/d_<ts>.bin
//   sys.fs.list() 列出所有 d_*.bin
//
// UI：
//   [画布 240×216]                       y=0..216
//   [工具栏 240×52  6 按钮]              y=216..268
//   黑笔 / 红 / 蓝 / 擦 / 保存 / 菜单
//
//   菜单 → 模态：画廊 / 清空 / 取消
//   画廊 → 全屏 panel 列出所有 d_*.bin，点击恢复编辑、长按删除
// ============================================================================

var APP_NAME = "doodle_pkg";
var T = UI.T;
var I = UI.I;

// 画布尺寸
var CW = 240, CH = 216;

// 颜色调色板
var COLORS = [0x000000, 0xE74C3C, 0x2563EB];   // 黑 / 红 / 蓝
var ERASER_COLOR = 0xFFFFFF;
var DEFAULT_THICKNESS = 3;
var ERASER_THICKNESS  = 14;

// --- 状态 ----------------------------------------------------------------
var state = {
    color: COLORS[0],
    thickness: DEFAULT_THICKNESS,
    isErase: false,
    cur: { x: -1, y: -1 }
};

// --- 工具栏按钮渲染 -------------------------------------------------------
function colorBtn(id, color, idx) {
    return h('button', {
        id: id,
        size: [40, 40],
        bg: T.C_PANEL,
        scrollable: false,
        onClick: function () {
            state.color = color;
            state.isErase = false;
            state.thickness = DEFAULT_THICKNESS;
            highlightTool(idx);
        }
    }, [
        // 36x36 实色块居中
        h('panel', {
            id: id + '_d',
            size: [28, 28], bg: color, radius: 14,
            scrollable: false,
            align: ['c', 0, 0]
        })
    ]);
}

function iconBtn(id, label, color, idx, fn) {
    return h('button', {
        id: id, size: [40, 40], bg: T.C_PANEL,
        scrollable: false, onClick: fn
    }, [
        h('label', { id: id + '_l', text: label, fg: color,
                     font: 'text', align: ['c', 0, 0] })
    ]);
}

/* 图标按钮：assets/<src> 渲染成 32×32 内嵌图。 */
function imgBtn(id, src, idx, fn) {
    return h('button', {
        id: id, size: [40, 40], bg: T.C_PANEL,
        scrollable: false, onClick: fn
    }, [
        h('image', { id: id + '_i', src: src, align: ['c', 0, 0] })
    ]);
}

function highlightTool(idx) {
    // 6 个按钮 id：tb0..tb5
    var allIds = ['tb0', 'tb1', 'tb2', 'tb3', 'tb4', 'tb5'];
    for (var i = 0; i < allIds.length; i++) {
        VDOM.set(allIds[i], { bg: (i === idx) ? T.C_PANEL_HI : T.C_PANEL });
    }
}

// --- 主 UI ---------------------------------------------------------------
function buildUI() {
    var canvas = h('canvas', {
        id: 'cv',
        w: CW, h: CH,
        align: ['tl', 0, 0],
        onPress: function (ev) {
            // 起笔点；不画线只记位置
            state.cur.x = ev.dx;
            state.cur.y = ev.dy;
        },
        onDrag: function (ev) {
            var nx = ev.dx, ny = ev.dy;
            if (state.cur.x < 0) {
                state.cur.x = nx;
                state.cur.y = ny;
                return;
            }
            var c = state.isErase ? ERASER_COLOR : state.color;
            sys.canvas.line('cv',
                state.cur.x, state.cur.y, nx, ny,
                c, state.thickness);
            state.cur.x = nx;
            state.cur.y = ny;
        },
        onRelease: function () {
            state.cur.x = -1;
            state.cur.y = -1;
        }
    });

    var toolbar = h('panel', {
        id: 'tb',
        size: [-100, 52], bg: T.C_BG,
        align: ['tl', 0, CH],
        scrollable: false,
        border: { color: T.C_BORDER, width: 1, side: 'top', opa: 128 },
        flex: 'row',
        flexAlign: ['evenly', 'center', 'center'],
        gap: [0, 0]
    }, [
        colorBtn('tb0', COLORS[0], 0),
        colorBtn('tb1', COLORS[1], 1),
        colorBtn('tb2', COLORS[2], 2),
        imgBtn('tb3', 'ic_erase.bin', 3, function () {
            // 橡皮：白色粗笔
            state.isErase = true;
            state.thickness = ERASER_THICKNESS;
            highlightTool(3);
        }),
        imgBtn('tb4', 'ic_save.bin', 4, function () {
            // 保存
            var ts = sys.time.now();
            var name = 'd_' + ts + '.bin';
            sys.canvas.saveTo('cv', name);
            UI.toast('已保存', 800);
        }),
        imgBtn('tb5', 'ic_more.bin', 5, function () {
            UI.modal({
                title: '更多',
                body:  '查看保存过的画作，或清空当前画布',
                action0: '画廊',
                action1: '清空',
                onResult: function (r) {
                    if (r === 0) showGallery();
                    else if (r === 1) {
                        sys.canvas.fill('cv', 0xFFFFFF);
                        UI.toast('已清空', 600);
                    }
                }
            });
        })
    ]);

    var page = h('panel', {
        id: 'root',
        size: [-100, -100],
        bg: T.C_BG,
        scrollable: false,
        pad: [0, 0, 0, 0]
    }, [canvas, toolbar]);

    VDOM.mount(page, null);
    sys.canvas.fill('cv', 0xFFFFFF);
    highlightTool(0);
}

// --- 画廊（模态全屏）-----------------------------------------------------
var galleryOpen = false;

function rmDraft(name) {
    sys.fs.remove(name);
    UI.toast('已删除', 500);
    closeGallery();
    // 用 setInterval 凑一次性回调延迟刷新（让 destroy cmd 先 drain）
    var t = setInterval(function () {
        clearInterval(t);
        showGallery();
    }, 150);
}

function loadDraft(name) {
    closeGallery();
    sys.canvas.loadFrom('cv', name);
    UI.toast('已加载', 500);
}

function closeGallery() {
    if (!galleryOpen) return;
    var node = VDOM.find('gal');
    if (node) {
        var kids = node.children.slice();
        for (var i = 0; i < kids.length; i++) {
            VDOM.destroy(kids[i].props.id);
        }
        VDOM.destroy('gal');
    }
    galleryOpen = false;
}

function showGallery() {
    if (galleryOpen) return;
    galleryOpen = true;

    var files = sys.fs.list();
    // 只保留 d_*.bin，按时间倒序
    var drafts = [];
    for (var i = 0; i < files.length; i++) {
        var f = files[i];
        if (f.length >= 6 && f.substring(0, 2) === 'd_' &&
            f.substring(f.length - 4) === '.bin') {
            drafts.push(f);
        }
    }
    drafts.sort(function (a, b) { return a < b ? 1 : -1; });

    var rows = [];
    rows.push(h('panel', {
        id: 'g_hdr', size: [-100, 36],
        bg: T.C_PANEL,
        scrollable: false,
        flex: 'row',
        flexAlign: ['between', 'center', 'center'],
        pad: [12, 6, 12, 6]
    }, [
        h('label', { id: 'g_t', text: '画廊', fg: T.C_TEXT, font: 'title' }),
        h('button', {
            id: 'g_close', size: [56, 28], bg: T.C_PANEL_HI, radius: 14,
            onClick: closeGallery,
            scrollable: false
        }, [
            h('label', { id: 'g_close_l', text: '关闭',
                          fg: T.C_TEXT, font: 'text', align: ['c', 0, 0] })
        ])
    ]));

    if (drafts.length === 0) {
        rows.push(h('label', {
            id: 'g_empty',
            text: '暂无作品',
            fg: T.C_TEXT_MUTED, font: 'text',
            align: ['c', 0, 0],
            size: [-100, 80]
        }));
    } else {
        for (var j = 0; j < drafts.length && j < 12; j++) {
            (function (name, idx) {
                rows.push(h('button', {
                    id: 'g_r' + idx,
                    size: [-100, 40],
                    bg: T.C_PANEL,
                    radius: T.R_MD,
                    pad: [12, 8, 12, 8],
                    pressedBg: { color: T.C_PANEL_HI, opa: 255 },
                    scrollable: false,
                    onClick: function () { loadDraft(name); },
                    onLongPress: function () {
                        UI.modal({
                            title: '删除？',
                            body: name,
                            action0: '取消',
                            action1: '删除',
                            onResult: function (rr) {
                                if (rr === 1) rmDraft(name);
                            }
                        });
                    }
                }, [
                    h('label', { id: 'g_r' + idx + '_n',
                                  text: name, fg: T.C_TEXT,
                                  font: 'text', align: ['lm', 0, 0] })
                ]));
            })(drafts[j], j);
        }
    }

    var gal = h('panel', {
        id: 'gal',
        size: [-100, -100],
        bg: T.C_BG,
        scrollable: true,
        pad: [0, 0, 0, 0],
        flex: 'column',
        flexAlign: ['start', 'center', 'start'],
        gap: [8, 0]
    }, rows);

    VDOM.mount(gal, 'root');
}

// --- 入口 ----------------------------------------------------------------
sys.log('doodle: build start');
buildUI();
sys.ui.attachRootListener('root');
sys.log('doodle: build done');
