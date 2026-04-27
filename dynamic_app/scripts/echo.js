// Dynamic App —— BLE Echo
//
// 协议（PC ↔ ESP）:
//   PC → ESP   { "to": "echo",  "type": "msg", "body": "..." }
//   ESP → PC   { "from": "echo", "type": "msg", "body": "..." }
//
// 框架：VDOM / h / makeBle 由 prelude.js 提供，业务直接用。
// 配套 PC 工具：tools/dynapp_bridge_test.py --to echo
// 约束 ES5。

var ble = makeBle("echo");

// ---- 配色 ----
var COL_BG       = 0x14101F;
var COL_CARD     = 0x231C3A;
var COL_TEXT     = 0xF1ECFF;
var COL_MUTED    = 0x8B8AA8;
var COL_OK       = 0x10B981;
var COL_WAIT     = 0xF59E0B;
var COL_ERR      = 0xEF4444;
var COL_INFO_BG  = 0x0A0816;

// ---- UI ----
function buildUI() {
    var tree = h('panel', { id: 'appRoot', size: [-100, -100], bg: COL_BG,
                             scrollable: false }, [
        h('label', { id: 'title', text: 'BLE Echo (' + ble.appName + ')',
                     fg: COL_TEXT, font: 'title', align: ['tm', 0, 8] }),

        h('panel', { id: 'stBox', size: [-100, 32], align: ['tm', 0, 44],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'stLabel', text: 'STATUS',
                         fg: COL_MUTED, font: 'text', align: ['lm', 12, 0] }),
            h('label', { id: 'stVal',   text: 'waiting...',
                         fg: COL_WAIT,  font: 'text', align: ['rm', -12, 0] })
        ]),

        h('panel', { id: 'cntBox', size: [-100, 28], align: ['tm', 0, 80],
                     bg: COL_CARD, scrollable: false }, [
            h('label', { id: 'cntLbl', text: 'recv / sent',
                         fg: COL_MUTED, font: 'text', align: ['lm', 12, 0] }),
            h('label', { id: 'cntVal', text: '0 / 0',
                         fg: COL_TEXT,  font: 'text', align: ['rm', -12, 0] })
        ]),

        h('panel', { id: 'lastBox', size: [-100, 80], align: ['tm', 0, 116],
                     bg: COL_INFO_BG, scrollable: false }, [
            h('label', { id: 'lastTitle', text: 'last from PC',
                         fg: COL_MUTED, font: 'text', align: ['tl', 10, 6] }),
            h('label', { id: 'lastVal', text: '(none)',
                         fg: COL_TEXT,  font: 'text', align: ['tl', 10, 28] })
        ]),

        h('label', { id: 'hint',
                     text: 'protocol: {to,type,body} JSON',
                     fg: COL_MUTED, font: 'text', align: ['bm', 0, -10] })
    ]);
    VDOM.mount(tree, null);
}

// ---- 业务 ----
var counts = { recv: 0, sent: 0 };

function refreshCounts() { VDOM.set('cntVal', { text: counts.recv + ' / ' + counts.sent }); }

function setStatus(text, color) {
    VDOM.set('stVal', { text: text, fg: color });
}

function checkConn() {
    if (ble.isConnected()) setStatus('connected', COL_OK);
    else                   setStatus('no PC', COL_MUTED);
}

ble.on('msg', function (msg) {
    counts.recv += 1;
    var body = msg.body == null ? '' : ('' + msg.body);
    var disp = body.length > 64 ? body.substring(0, 64) + '...' : body;
    VDOM.set('lastVal', { text: disp || '(empty body)' });

    var ok = ble.send('msg', body + ' (echo)');
    if (ok) {
        counts.sent += 1;
        setStatus('echoed', COL_OK);
    } else {
        setStatus('send failed', COL_ERR);
    }
    refreshCounts();
});

ble.onError(function (raw) {
    setStatus('bad json', COL_ERR);
    sys.log('echo: bad json (' + raw.length + ' bytes)');
});

// ---- 启动 ----
sys.log('echo: build start');
buildUI();
sys.ui.attachRootListener('appRoot');

checkConn();
setInterval(checkConn, 1000);

sys.log('echo: build done, app=' + ble.appName);
