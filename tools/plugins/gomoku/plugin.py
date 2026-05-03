"""gomoku plugin —— BLE 联机五子棋（设备 ↔ PC GUI 页）。

bind_app="gomoku_pkg"：把所有 from=gomoku_pkg 的消息透到 bus("gomoku:rx")，
GUI 页订阅这个事件刷新棋盘。GUI 页落子时 emit("gomoku:tx", (mtype, body))，
本插件在 on_load 监听 gomoku:tx 并转 self.tx() 发给设备。

GUI 页：./gui_page.py 里的 GomokuPage（CTk 棋盘 + 心跳 + 棋谱日志）。
"""

from __future__ import annotations

from typing import Any

from companion.plugin_sdk import Plugin


class GomokuPlugin(Plugin):
    plugin_id = "gomoku"
    title     = "五子棋"
    bind_app  = "gomoku_pkg"

    def __init__(self) -> None:
        super().__init__()
        self._unsubs: list = []

    def on_load(self) -> None:
        # GUI 页发的 ('move'/'reset'/...) → BLE
        def _on_tx(payload: object) -> None:
            if not isinstance(payload, tuple) or len(payload) != 2:
                return
            mtype, body = payload
            self.tx(str(mtype), body)
        self._unsubs.append(self.bus.on("gomoku:tx", _on_tx))

    def on_unload(self) -> None:
        for u in self._unsubs:
            try: u()
            except Exception: pass
        self._unsubs.clear()

    async def on_message(self, msg: dict) -> None:
        # 设备 → GUI（present / move / sync / reset / resign / leave）
        body = msg.get("body") if isinstance(msg.get("body"), dict) else {}
        self.bus.emit("gomoku:rx", (msg.get("type"), body))

    def make_gui_page(self, master, app):
        # 相对 import：plugin 目录已加入 sys.path
        from gui_page import GomokuPage
        return GomokuPage(master, app)
