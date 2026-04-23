// Dynamic App MVP (MicroQuickJS)
//
// 这是一段“演示脚本”，目的是让你快速看到整条链路是否跑通：
// - JS 运行在 Script Task（脚本任务）里；
// - JS 通过 sys.ui.setText() 把“更新 UI 的请求”发给 C 代码（入队）；
// - UI Task 在自己的循环里 drain 队列，再真正调用 LVGL 更新 label。
//
// 你在这里会用到的内置 API（由 C 侧提供）：
// - sys.log(msg)：输出日志
// - sys.time.uptimeStr()：返回运行时长字符串（HH:MM:SS）
// - sys.ui.setText(id, text)：把某个 label 的文本更新请求发给 UI 线程（异步）

sys.log("dynamic app started");

// 页面刚进入时：脚本侧自己创建/注册 label（仅在 PAGE_DYNAMIC_APP 生效）
if (sys && sys.ui && sys.ui.createLabel) {
    sys.ui.createLabel("time");
}

// 初始化显示
sys.ui.setText("time", sys.time.uptimeStr());

setInterval(function () {
    // 1Hz 更新，屏幕上应该能看到每秒变化（并且串口日志会不断输出 tick）。
    var s = sys.time.uptimeStr();
    sys.ui.setText("time", s);
    sys.log("tick " + s);
}, 1000);
