// hello_pkg —— 动态 app 最小可运行示例。
// 看这个文件 + 同目录 manifest.json，再翻一份 docs/动态app开发者指南.md
// 就能开始造自己的 app。约 15 行业务代码。

var T = UI.T;          // 颜色 / 间距 / 圆角等设计 token
var clicks = 0;

var page = h('panel', {
    bg: T.C_BG, flex: 'col', gap: [16, 0], pad: [24, 16, 24, 16],
    flexAlign: ['center', 'center', 'center']
}, [
    h('label', { text: 'Hello!', fg: T.C_TEXT, font: 'huge' }),
    h('label', { id: 'tip', text: '点下面按钮', fg: T.C_TEXT_MUTED, font: 'text' }),
    UI.pillBtn({
        text: '点我',
        textId: 'btnLbl',         // 给内部 label 一个 id，方便 VDOM.set 改文字
        onClick: function () {
            clicks++;
            VDOM.set('tip',    { text: '已点 ' + clicks + ' 次' });
            VDOM.set('btnLbl', { text: '再点一次' });
            UI.toast('Hi 👋', 600);
        }
    })
]);

VDOM.mount(page, null);                       // 挂到屏幕根
sys.ui.attachRootListener(page.props.id);     // 让按钮事件能冒泡到 onClick
