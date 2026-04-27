// ============================================================================
// prelude.js —— 动态 app 标准库（runtime 自动在业务脚本之前 eval 一次）
//
// 暴露给业务脚本的全局：
//   VDOM     —— 声明式 UI 框架，含 h / mount / find / set / destroy /
//               dispatch / render（带 diff）
//   h        —— VDOM.h 的短名
//   makeBle  —— BLE 路由 helper 工厂；业务调 makeBle("myapp") 拿到
//               { send, on, onAny, onError, isConnected, appName }
//
// 内部副作用：
//   - 注册 sys.__setDispatcher(VDOM.dispatch)，业务无需关心事件分发
//
// 约束：esp-mquickjs 仅支持 ES5；同一函数作用域内多个 catch 不能用同名标识符。
// ============================================================================

// ---------------------------- VDOM ------------------------------------------

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
        if (props.shadow !== undefined) {
            var sh = props.shadow;
            sys.ui.setStyle(id, sys.style.SHADOW,
                sh[0] | 0, sh[1] | 0, sh[2] | 0, 0);
        }
        if (props.gap !== undefined) {
            var g = props.gap;
            sys.ui.setStyle(id, sys.style.GAP,
                g[0] | 0, g[1] | 0, 0, 0);
        }
        if (props.scrollable !== undefined) {
            sys.ui.setStyle(id, sys.style.SCROLLABLE,
                props.scrollable ? 1 : 0, 0, 0, 0);
        }
    }

    var HOOK_NAME = {
        1: 'onClick', 2: 'onPress', 3: 'onDrag',
        4: 'onRelease', 5: 'onLongPress'
    };

    function dispatch(startId, type, dx, dy) {
        var node = nodes[startId];
        if (!node) return;
        var hook = HOOK_NAME[type];
        if (!hook) return;
        var stopped = false;
        var ev = {
            target: startId, currentTarget: null,
            dx: dx | 0, dy: dy | 0,
            stopPropagation: function () { stopped = true; }
        };
        var cur = node;
        while (cur) {
            if (typeof cur.props[hook] === 'function') {
                ev.currentTarget = cur.props.id;
                var ret = cur.props[hook](ev);
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

        for (var i = 0; i < node.children.length; i++) {
            node.children[i]._parent = node;
            mount(node.children[i], id);
        }
        return node;
    }

    function find(id) { return nodes[id] || null; }

    function set(id, patch) {
        var node = nodes[id];
        if (!node) { sys.log("VDOM.set: id not found: " + id); return; }
        for (var k in patch) {
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
        for (var i = 0; i < kids.length; i++) {
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

    var STYLE_KEYS = ['bg', 'fg', 'radius', 'size', 'align',
                      'pad', 'borderBottom', 'flex', 'font',
                      'shadow', 'gap', 'scrollable'];

    function shallowEq(a, b) {
        if (a === b) return true;
        if (a == null || b == null) return false;
        if (typeof a !== 'object' || typeof b !== 'object') return false;
        if (a.length === undefined || b.length === undefined) return false;
        if (a.length !== b.length) return false;
        for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
    }

    function diffProps(oldNode, newNode) {
        var id = oldNode.props.id;
        var oldP = oldNode.props;
        var newP = newNode.props;
        var patch = {};
        var hasPatch = false;

        if (newP.text !== undefined && newP.text !== oldP.text) {
            sys.ui.setText(id, "" + newP.text);
        }
        for (var i = 0; i < STYLE_KEYS.length; i++) {
            var k = STYLE_KEYS[i];
            if (newP[k] !== undefined && !shallowEq(oldP[k], newP[k])) {
                patch[k] = newP[k];
                hasPatch = true;
            }
        }
        if (hasPatch) applyStyle(id, patch);

        newP.id = id;
        oldNode.props = newP;
    }

    function diffNode(oldNode, newNode, parentId) {
        var id = oldNode.props.id;
        if (oldNode.type !== newNode.type) {
            var parent = oldNode._parent;
            destroy(id);
            newNode._parent = parent;
            mount(newNode, parentId);
            return newNode;
        }
        diffProps(oldNode, newNode);
        diffChildren(oldNode, newNode, id);
        return oldNode;
    }

    function diffChildren(oldNode, newNode, parentId) {
        var oldKids = oldNode.children;
        var newKids = newNode.children || [];

        var oldById = {};
        for (var i = 0; i < oldKids.length; i++) {
            var oid = oldKids[i].props.id;
            if (oid) oldById[oid] = oldKids[i];
        }

        var resultKids = [];
        var seen = {};
        for (i = 0; i < newKids.length; i++) {
            var nk = newKids[i];
            var nid = nk.props.id;
            if (nid && oldById[nid]) {
                seen[nid] = true;
                resultKids.push(diffNode(oldById[nid], nk, parentId));
            } else {
                nk._parent = oldNode;
                mount(nk, parentId);
                resultKids.push(nk);
            }
        }

        var toRemove = [];
        for (i = 0; i < oldKids.length; i++) {
            var rid = oldKids[i].props.id;
            if (rid && !seen[rid]) toRemove.push(rid);
        }
        for (i = 0; i < toRemove.length; i++) destroy(toRemove[i]);

        oldNode.children = resultKids;
    }

    function render(rootDesc, parentId) {
        if (!rootDesc || !rootDesc.props || !rootDesc.props.id) {
            sys.log("VDOM.render: root must have id");
            return null;
        }
        var existing = nodes[rootDesc.props.id];
        if (!existing) return mount(rootDesc, parentId || null);
        diffNode(existing, rootDesc, parentId || null);
        return existing;
    }

    return {
        h: h, mount: mount, find: find, set: set, destroy: destroy,
        dispatch: dispatch, render: render
    };
})();

var h = VDOM.h;

sys.__setDispatcher(function (id, type, dx, dy) {
    VDOM.dispatch(id, type, dx, dy);
});

// ---------------------------- BLE helper ------------------------------------
//
// 用法:
//   var ble = makeBle("myapp");
//   ble.on("data", function (msg) { sys.log(JSON.stringify(msg.body)); });
//   ble.send("req", { force: true });
//
// 返回对象:
//   send(type, body?)   -> bool   发 { from: appName, type, body? } 给 PC
//   on(type, fn)                  注册 type 回调；fn 收到已解析的 msg
//   onAny(fn)                     收到任何"给我的"消息都触发（不含 ping）
//   onError(fn)                   JSON 解析失败回调；fn 收 raw 字符串
//   isConnected()        -> bool
//   appName              字符串
//
// 行为约定:
//   - 每个 app 调用一次 makeBle 即可；新调用会覆盖旧的底层 onRecv
//   - "to" 字段不匹配 appName 且不为 "*" 的消息会被静默丢弃
//   - type === "ping" 自动回 pong，业务侧 on("ping") 不会被触发
// ----------------------------------------------------------------------------

function makeBle(appName) {
    var typeRoutes = {};
    var anyHandler = null;
    var errorHandler = null;

    sys.ble.onRecv(function (raw) {
        var msg;
        try { msg = JSON.parse(raw); }
        catch (eParse) {
            if (errorHandler) {
                try { errorHandler(raw); } catch (_) {}
            }
            return;
        }
        if (!msg || (msg.to !== appName && msg.to !== "*")) return;

        if (msg.type === "ping") {
            var reply = { from: appName, type: "pong" };
            if (msg.body !== undefined) reply.body = msg.body;
            sys.ble.send(JSON.stringify(reply));
            return;
        }

        if (anyHandler) {
            try { anyHandler(msg); }
            catch (eAny) { sys.log("ble.onAny throw: " + eAny); }
        }
        if (msg.type && typeRoutes[msg.type]) {
            try { typeRoutes[msg.type](msg); }
            catch (eType) { sys.log("ble.on(" + msg.type + ") throw: " + eType); }
        }
    });

    return {
        appName: appName,
        send: function (type, body) {
            var msg = { from: appName, type: type };
            if (body !== undefined) msg.body = body;
            return sys.ble.send(JSON.stringify(msg));
        },
        on:        function (t, fn) { typeRoutes[t] = fn; },
        onAny:     function (fn)    { anyHandler = fn; },
        onError:   function (fn)    { errorHandler = fn; },
        isConnected: sys.ble.isConnected
    };
}
