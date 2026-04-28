// ============================================================================
// Dynamic App —— ImgDemo（图片资源能力演示）
//
// 用途：
//   - 验证 sys.ui.createImage / setImageSrc 两个新 native
//   - 验证 LVGL FS driver 正常解析 "A:/littlefs/apps/imgdemo/assets/*.bin"
//   - 验证 dynapp_push_gui 的"Folder..."上传链路
//
// 用法：
//   1. 在 dynamic_app/scripts/imgdemo_pkg/ 下放好：
//        main.js              （即本文件）
//        assets/a.bin         （任意 LVGL .bin 图片，如 32x32 RGB565）
//        assets/b.bin
//        assets/c.bin
//      转换：python managed_components/lvgl__lvgl/scripts/LVGLImage.py \
//                       --ofmt BIN --cf RGB565 -o out src.png
//   2. dynapp_push_gui.py → Folder... → 选 imgdemo_pkg → Upload
//   3. 设备菜单出现 imgdemo → 进入即可
//
// 行为：
//   - 顶部 3 个图片节点（a/b/c），底部 3 个按钮"切到 a/b/c"
//   - 点按钮：把第 4 个"主图"换源（演示 setImageSrc 热切）
// ============================================================================

var COL_BG     = 0x111827;
var COL_HEADER = 0x1F2937;
var COL_TEXT   = 0xF9FAFB;
var COL_DIM    = 0x9CA3AF;
var COL_ACCENT = 0x06B6D4;
var COL_CARD   = 0x374151;

var current = "a.bin";

function viewBtn(id, label, src) {
    return h('button', {
        id: id, size: [80, 38], radius: 19,
        bg: (current === src) ? COL_ACCENT : COL_CARD,
        onClick: function () {
            current = src;
            VDOM.set('hero', { src: src });
            // 同步刷新底部按钮高亮
            VDOM.set('btnA', { bg: (current === "a.bin") ? COL_ACCENT : COL_CARD });
            VDOM.set('btnB', { bg: (current === "b.bin") ? COL_ACCENT : COL_CARD });
            VDOM.set('btnC', { bg: (current === "c.bin") ? COL_ACCENT : COL_CARD });
            VDOM.set('label', { text: "当前: " + src });
        }
    }, [
        h('label', { id: id + 'L', text: label,
                     fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
    ]);
}

function build() {
    var tree = h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                             scrollable: false }, [

        h('panel', { id: 'hdr', size: [-100, 36], bg: COL_HEADER,
                     align: ['tm', 0, 0] }, [
            h('label', { id: 'hdrT', text: "图片演示",
                         fg: COL_TEXT, font: 'title', align: ['lm', 12, 0] })
        ]),

        // 横排三个缩略图
        h('image', { id: 'thumbA', src: "a.bin", align: ['tl', 24,  56] }),
        h('image', { id: 'thumbB', src: "b.bin", align: ['tm', 0,   56] }),
        h('image', { id: 'thumbC', src: "c.bin", align: ['tr', -24, 56] }),

        // 主图（受按钮控制）
        h('image', { id: 'hero', src: current, align: ['c', 0, -10] }),

        h('label', { id: 'label', text: "当前: " + current,
                     fg: COL_DIM, font: 'text', align: ['c', 0, 60] }),

        // 底部三个切换按钮
        h('panel', { id: 'btns', size: [-100, 50], bg: COL_BG,
                     align: ['bm', 0, -8],
                     flex: 'row', pad: [4, 4, 4, 4], gap: [12, 0] }, [
            viewBtn('btnA', 'A', 'a.bin'),
            viewBtn('btnB', 'B', 'b.bin'),
            viewBtn('btnC', 'C', 'c.bin')
        ])
    ]);
    VDOM.mount(tree, null);
}

sys.log("imgdemo: build start");
build();
sys.ui.attachRootListener('appRoot');
sys.log("imgdemo: build done");
