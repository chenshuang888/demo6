// Dynamic App —— Hello (FS 上传链路测试)
//
// 用途：验证 BLE 上传 → LittleFS 落盘 → menu 显示 → 点击运行 全链路。
// 名字 "hello" 不在内嵌表里，必须从 /littlefs/apps/hello.js 读出来才能跑。
//
// 功能：
//   - 标题 + 一行 "from FS"
//   - 运行计时 (uptimeStr，每秒刷)
//   - 一个 "Tap me" 按钮，点一次 +1
//   - 底部 BLE 连接状态
//
// 约束：ES5（esp-mquickjs 不支持箭头函数 / let / 模板字符串）。

var ble = makeBle("hello");

// ---- 配色（沿用项目深紫青绿）----
var COL_BG    = 0x1E1B2E;
var COL_CARD  = 0x2D2640;
var COL_TEXT  = 0xF1ECFF;
var COL_MUTED = 0x9B94B5;
var COL_OK    = 0x10B981;
var COL_WARN  = 0xF59E0B;
var COL_ACCENT= 0x06B6D4;

var taps = 0;

// ---- UI ----
function buildUI() {
    var tree = h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                             scrollable: false }, [

        h('label', { id: 'title', text: 'Hello from FS',
                     fg: COL_TEXT, font: 'title', align: ['tm', 0, 10] }),

        h('label', { id: 'subtitle', text: 'uploaded via BLE',
                     fg: COL_MUTED, font: 'text', align: ['tm', 0, 40] }),

        h('panel', { id: 'upBox', size: [-100, 36], align: ['tm', 0, 70],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'upLbl', text: 'uptime',
                         fg: COL_MUTED, font: 'text', align: ['lm', 12, 0] }),
            h('label', { id: 'upVal', text: '00:00:00',
                         fg: COL_TEXT,  font: 'text', align: ['rm', -12, 0] })
        ]),

        h('panel', { id: 'tapBox', size: [-100, 36], align: ['tm', 0, 112],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'tapLbl', text: 'taps',
                         fg: COL_MUTED, font: 'text', align: ['lm', 12, 0] }),
            h('label', { id: 'tapVal', text: '0',
                         fg: COL_ACCENT, font: 'title', align: ['rm', -12, 0] })
        ]),

        h('button', { id: 'btn', size: [140, 44], align: ['tm', 0, 158],
                      bg: COL_ACCENT, radius: 8,
                      onClick: function () {
                          taps += 1;
                          VDOM.set('tapVal', { text: '' + taps });
                          sys.log('hello: tap ' + taps);
                      } }, [
            h('label', { id: 'btnLbl', text: 'Tap me',
                         fg: COL_TEXT, font: 'text', align: ['c', 0, 0] })
        ]),

        h('label', { id: 'bleVal', text: 'BLE: ?',
                     fg: COL_MUTED, font: 'text', align: ['bm', 0, -10] })
    ]);
    VDOM.mount(tree, null);
}

// ---- 周期任务 ----
function tick() {
    VDOM.set('upVal', { text: sys.time.uptimeStr() });
    if (ble.isConnected()) {
        VDOM.set('bleVal', { text: 'BLE: connected', fg: COL_OK });
    } else {
        VDOM.set('bleVal', { text: 'BLE: idle', fg: COL_WARN });
    }
}

// ---- 启动 ----
sys.log('hello: build start');
buildUI();
sys.ui.attachRootListener('appRoot');
tick();
setInterval(tick, 1000);
sys.log('hello: build done');
