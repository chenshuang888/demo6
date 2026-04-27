# 动态 App 开发指南

> 这份文档假设你**已经能编译 / 烧录固件**，且 **PC 端能跑 Python 3.10+**。
> 想了解协议细节看 [`动态app双端通信协议.md`](./动态app双端通信协议.md)；
> 想查 JS 能调什么看 [`动态app_JS_API速查.md`](./动态app_JS_API速查.md)。

---

## 0. 你能做什么

写一个**可以热替换**的小 app，跑在 ESP32-S3 屏幕上：

- 完整的图形 UI（panel / label / button + flex 布局 + 触摸手势）
- 自己的 NVS 持久化（断电不丢）
- 通过 BLE 跟 PC 端做任意 JSON 双向通信
- setInterval 周期回调
- 多 app 串行切换，互不干扰

**不能做**：访问硬件外设（GPIO / I2C / SPI）、联网（无网络栈）、读真实墙钟（除非自己 BLE 拉）、CPU 密集运算（比 C 慢 10×）。

---

## 1. 关键认知：你只写业务

固件在每次启动 app 之前，会自动 eval 一份**标准库 prelude**，给你提供：

| 全局符号 | 干啥 |
|---|---|
| `VDOM` | 声明式 UI 框架（h / mount / render / set / destroy） |
| `h` | `VDOM.h` 短名 |
| `makeBle(appName)` | BLE 路由 helper 工厂 |

**所以你的 .js 文件不用拷任何框架代码**。直接：

```js
var ble = makeBle("myapp");
VDOM.mount(h('panel', {...}, [...]), null);
ble.on('data', function (msg) { ... });
```

就完了。

---

## 2. 双端架构图

```
            ┌────────────────────────┐
            │ ESP32-S3 屏幕          │
            │  ┌──────────────────┐  │
            │  │ prelude.js       │  │  ← 框架自动注入
            │  │  (VDOM/makeBle)  │  │
            │  └──────────────────┘  │
            │  ┌──────────────────┐  │
            │  │ 你的 myapp.js    │  │
            │  │  var ble=makeBle │ ─┼─────────────────────┐
            │  │  VDOM.mount(...) │  │                     │
            │  └──────────────────┘  │                     ▼
            │  dynamic_app runtime   │            ┌────────────────┐
            │  + dynapp_bridge BLE   │            │ JSON over BLE  │
            └────────────────────────┘            └────────┬───────┘
                                                           │
            ┌────────────────────────┐                     │
            │ PC                     │                     │
            │  ┌──────────────────┐  │ ◀───────────────────┘
            │  │ DynappClient     │  │
            │  │  (dynapp_sdk)    │  │     @client.on("myapp", "type")
            │  └──────────────────┘  │     async def handler(msg): ...
            │  ┌──────────────────┐  │
            │  │ myapp_provider.py│  │     await client.send("myapp", "type", body=...)
            │  └──────────────────┘  │
            └────────────────────────┘
```

---

## 3. 文件清单

写一个 app 涉及这些文件：

| 端 | 文件 | 改动类型 |
|---|---|---|
| 固件 | `dynamic_app/scripts/myapp.js` | 新建（**只写业务**，不用拷框架） |
| 固件 | `dynamic_app/CMakeLists.txt` | 加 1 行 EMBED |
| 固件 | `dynamic_app/dynamic_app_registry.c` | 加 1 个 extern + 1 行表项 |
| 固件 | `app/pages/page_menu.c` | 加菜单项 + click 回调 |
| PC | `tools/providers/myapp_provider.py` | 新建（如果需要 PC 配合） |
| PC | `tools/dynapp_companion.py` | 加 1 行 `register_myapp(client)` |

---

## 4. 第一个 app：30 分钟手把手

### Step 1：写脚本（业务全部）

新建 `dynamic_app/scripts/myapp.js`：

```js
// 顶部一行声明 BLE 通道
var ble = makeBle("myapp");

// UI：声明式描述
VDOM.mount(h('panel', { id: 'root', size: [-100, -100], bg: 0x14101F }, [
    h('label', { id: 'status', text: 'loading...',
                 fg: 0xFFFFFF, font: 'title',
                 align: ['c', 0, -20] }),
    h('button', { id: 'btn', size: [120, 40],
                   bg: 0x06B6D4, radius: 20,
                   align: ['c', 0, 30],
                   onClick: function () {
                       ble.send('hello');
                       VDOM.set('status', { text: 'sent hello' });
                   }}, [
        h('label', { id: 'btnL', text: 'Hi PC',
                     fg: 0x000814, align: ['c', 0, 0] })
    ])
]), null);
sys.ui.attachRootListener('root');   // ⚠️ 必须！否则按钮不响应

// 收 PC 推过来的消息
ble.on('data', function (msg) {
    VDOM.set('status', { text: JSON.stringify(msg.body) });
});

// 启动时主动请求一次
if (ble.isConnected()) ble.send('hello');
sys.log('myapp: ready');
```

完整业务就这些，**没有一行框架代码**。

### Step 2：注册到固件

`dynamic_app/CMakeLists.txt`：
```cmake
EMBED_TXTFILES
    "scripts/prelude.js"
    "scripts/alarm.js"
    ...
    "scripts/myapp.js"      # ← 加这行
```

`dynamic_app/dynamic_app_registry.c`：
```c
extern const uint8_t myapp_js_start[] asm("_binary_myapp_js_start");
extern const uint8_t myapp_js_end[]   asm("_binary_myapp_js_end");

static const app_entry_t g_apps[] = {
    ...
    { "myapp", myapp_js_start, myapp_js_end },
};
```

`app/pages/page_menu.c`：
- 在 `s_ui` struct 里加 `lv_obj_t *myapp_item;`
- `create_menu_list` 末尾加：
  ```c
  s_ui.myapp_item = create_list_item(card, LV_SYMBOL_OK, "My App",
                                      NULL, NULL, 0, false);
  ```
- 加 click 回调：
  ```c
  static void on_myapp_clicked(lv_event_t *e) {
      (void)e;
      page_dynamic_app_prepare_and_switch("myapp");
  }
  ```
- `bind_events` 加：
  ```c
  lv_obj_add_event_cb(s_ui.myapp_item, on_myapp_clicked,
                       LV_EVENT_CLICKED, NULL);
  ```
- `page_menu_destroy` 把 `myapp_item = NULL` 加进清单

### Step 3：写 PC provider（如果需要 PC 配合）

`tools/providers/myapp_provider.py`：
```python
import logging
from dynapp_sdk import DynappClient

logger = logging.getLogger(__name__)

def register_myapp(client: DynappClient) -> None:

    @client.on("myapp", "hello")
    async def _on_hello(msg):
        logger.info("myapp said hello")
        await client.send("myapp", "data", body={"answer": 42})
```

`tools/dynapp_companion.py`：
```python
from providers.myapp_provider import register_myapp

# 在 amain 里
register_myapp(client)
```

### Step 4：跑

```bash
# 一端
idf.py build flash monitor

# 另一端
cd tools
pip install -r requirements.txt
python dynapp_companion.py
```

ESP 进 menu → 选 My App → 屏幕显示 `loading...` → 按"Hi PC" → 收到 `{"answer": 42}` → status 变成 JSON 字符串。完成。

---

## 5. 调试技巧

### 5.1 单独调试协议（不写 PC provider）

```bash
python tools/dynapp_bridge_test.py --to myapp
> {"hello": true}
```

直接发任意 JSON / 收 ESP 推的所有消息。开发协议时最快。

### 5.2 看 ESP 端日志

`idf.py monitor` 串口里搜 `dynamic_app_natives`、`dynapp_bridge`、你脚本里 `sys.log` 的内容。

prelude 注入失败会立刻看到 `eval prelude failed` —— 这种情况是 prelude 自己的语法错（动了它本身才会发生）。

### 5.3 看 PC 端日志

```bash
python dynapp_companion.py --log-level DEBUG
```

DEBUG 级会打印每条收到 / 发出的消息内容。

### 5.4 协议字节超长？

PC 端 `client.send` 会抛 `ValueError`，立刻看到。ESP 端 `ble.send` 返 `false`，记得检查返回值。

### 5.5 BLE 没连上？

- 确认手机蓝牙别抢同一个 ESP（NimBLE 一对一）
- `--device-name "你的实际名字"` 改对（可在 monitor 看 `BLE Device Name set to ...`）
- Windows 11 有时候 bleak 扫描很慢，超时调到 30s：编辑 `dynapp_companion.py` 加 `scan_timeout=30`

---

## 6. 设计规范建议

### 6.1 业务字段名用蛇形

```json
{ "temp_c": 23.4, "is_playing": true, "weather_code": "rain" }
```

不要 `tempC` / `isPlaying`。

### 6.2 时间戳一律 unix 秒

```json
{ "ts": 1714214400 }
```

JS 端用 `sys.time.uptimeMs()` 算时间差时，把 PC 给的 `ts` 加上自己的 `uptimeMs - (uptimeMs at recv)` 插值（参考 music.js 的进度条插值）。

### 6.3 错误约定

业务级错误一律走 `type: "error"`：

```json
{ "from": "myapp", "type": "error", "body": { "code": "no_data", "msg": "city not set" } }
```

PC 端：
```python
@client.on("myapp", "error")
async def _on_err(msg):
    body = msg.get("body", {})
    logger.error("myapp error: %s - %s", body.get("code"), body.get("msg"))
```

### 6.4 别让 JS 干 CPU 密集活

mquickjs 是解释器，循环 1 万次就明显卡。重活让 PC 算完推结果。

### 6.5 持久化 = sys.app

每个 app 一个 NVS blob，最多 4KB，随便存。重启自动恢复。

### 6.6 高频更新用 `VDOM.set`，低频用 `VDOM.render`

- 30Hz 进度条 / 拖动跟手 → `VDOM.set('id', { size: [w, h] })` 单点
- 状态机切换、按钮按下后改一组字段 → `VDOM.render(view(state), null)` 整树 diff

每帧整树 render 会重跑 view() + diff，文本节点多时会卡。

---

## 7. 进阶范式

### 7.1 Pull 模式（weather）

ESP 进 app → 主动 `ble.send("req")` → PC 拉数据 → 推 `data`。
详见 `weather.js` + `weather_provider.py`。

### 7.2 Push 模式（music）

PC 监听本机状态变化，**变化时**主动推 ESP。ESP 端不主动 req，被动收。
详见 `music.js` + `media_provider.py`。

### 7.3 Command-Ack（按钮上行）

ESP 端用户按按钮 → `ble.send("btn", {id: "play"})` → PC 执行后回推一次新 state（不需要专门 ack）。music.js 就是这么做的。

### 7.4 状态插值（避免高频 push）

PC 推一次 base 值 + ts，JS 端用 `sys.time.uptimeMs()` 自己往前推算（如音乐进度条）。

### 7.5 乐观更新

按按钮时**本地立刻翻转 UI**，再发 BLE 命令。PC 回推 state 时如果跟本地一致就无视，不一致再纠正。music.js 的播放/暂停就这么做。

---

## 8. 性能参考

| 操作 | 大约耗时 | 备注 |
|---|---|---|
| `VDOM.mount` 60 节点 | ~80ms | 大 build 走 prepare 缓冲，用户看不到中间态 |
| `VDOM.render` 整树 diff | ~10ms（30 节点）| 没变化的字段不下发 |
| `VDOM.set` 单节点 | < 2ms | 30Hz 完全无感 |
| `ble.send` 200B | < 5ms（已连接）| NimBLE mbuf 限速，>50Hz 可能堆积 |
| `JSON.parse` 100B | ~0.5ms | 不要在 setInterval 里反复 parse 大对象 |
| `sys.app.saveState` 1KB | ~30ms | 同步 NVS，操作类按下时调即可，不要每帧调 |

---

## 9. 常见问题

**Q：我能在多个 app 之间共享数据吗？**
A：不能直接共享，但可以让 PC 端 provider 间接做。或两个 app 都读 `sys.app.loadState`（**不推荐**，违背隔离）。

**Q：Python provider 能开线程吗？**
A：能，但建议用 `asyncio.to_thread()` 包装阻塞调用。完全用线程会和 bleak 的事件循环打架。

**Q：能否同时连两个 ESP？**
A：当前 SDK 假设一对一。要做多设备，自己起多个 `DynappClient` 实例（`address` 参数指定 MAC）。

**Q：消息会丢吗？**
A：BLE notify 不重传。PC → ESP 用 write-without-response，理论上不丢；ESP → PC notify 在订阅前发的会丢。关键消息建议加 `id`，业务自己做 ack。

**Q：能改 prelude.js 加我自己的 helper 吗？**
A：可以，但要慎重 —— 它对所有 app 都生效。建议私有工具放自己的 .js 顶部，prelude 只动通用的能力（如新加一种 widget）。

**Q：能开源给别人玩吗？**
A：固件 + SDK 完全独立。开发者只需要：固件烧好、`tools/dynapp_sdk/`、`tools/dynapp_companion.py`、`tools/requirements.txt`。
