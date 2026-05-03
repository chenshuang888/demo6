"""tictactoe plugin —— 井字棋 PC 端 AI 对手 + GUI 监控页。

bind_app="tictactoe_pkg"：设备端动态 app 跟本插件做人机对战。
设备永远是 X 先手，本插件是 O。

协议（与 dynamic_app/scripts/tictactoe_pkg/main.js 对偶）：
  ESP → PC  { from:"tictactoe_pkg", type:"hello" }                握手
  ESP → PC  { from:"tictactoe_pkg", type:"move",  body:{r,c} }    玩家落子（X）
  ESP → PC  { from:"tictactoe_pkg", type:"reset" }                请求新局

  PC → ESP  { to:"tictactoe_pkg",   type:"ready" }                AI 已就绪
  PC → ESP  { to:"tictactoe_pkg",   type:"move",  body:{r,c} }    AI 落子（O）
  PC → ESP  { to:"tictactoe_pkg",   type:"reset_ack" }            AI 同意重开

GUI 解耦：
  本 plugin 不直接 import GUI，每次状态变化 emit("tictactoe:state", {...})。
  GuiPage 在自己的 mainloop 里订阅这个 bus 事件被动渲染——只读监控面板。

AI 实现：启发式 1-step lookahead
  1) 自己能一步赢 → 走那一步
  2) 对手能一步赢 → 堵
  3) 中心 → 角 → 边
"""

from __future__ import annotations

import time

from companion.plugin_sdk import Plugin


WIN_LINES = [
    (0, 1, 2), (3, 4, 5), (6, 7, 8),
    (0, 3, 6), (1, 4, 7), (2, 5, 8),
    (0, 4, 8), (2, 4, 6),
]


def _check_win(board: list[str]) -> tuple[str, tuple[int, int, int] | None]:
    """返回 ('X'|'O'|'draw'|'', winning_line_or_None)"""
    for line in WIN_LINES:
        a, b, c = line
        if board[a] and board[a] == board[b] == board[c]:
            return board[a], line
    if all(board):
        return "draw", None
    return "", None


def _win_step(board: list[str], who: str) -> int:
    for i in range(9):
        if board[i]:
            continue
        board[i] = who
        win = _check_win(board)[0] == who
        board[i] = ""
        if win:
            return i
    return -1


def _ai_pick(board: list[str]) -> int:
    s = _win_step(board, "O")
    if s >= 0:
        return s
    s = _win_step(board, "X")
    if s >= 0:
        return s
    for idx in (4, 0, 2, 6, 8, 1, 3, 5, 7):
        if not board[idx]:
            return idx
    return -1


class TicTacToePlugin(Plugin):
    plugin_id = "tictactoe"
    title     = "井字棋"        # 有侧边栏入口
    bind_app  = "tictactoe_pkg"

    def __init__(self) -> None:
        super().__init__()
        self._board: list[str] = [""] * 9
        self._history: list[str] = []        # 文本日志，给 GUI 显示
        self._last_ai_idx: int = -1
        self._winner: str = ""               # '' / 'X' / 'O' / 'draw'
        self._win_line: tuple[int, int, int] | None = None
        self._device_online: bool = False    # hello 收到过则 True

    # ------------------------------------------------------------------
    # 给 GUI 用：当前快照
    # ------------------------------------------------------------------

    def snapshot(self) -> dict:
        return {
            "board":         list(self._board),
            "history":       list(self._history),
            "last_ai":       self._last_ai_idx,
            "winner":        self._winner,
            "win_line":      list(self._win_line) if self._win_line else None,
            "device_online": self._device_online,
        }

    def _emit_state(self) -> None:
        self.bus.emit("tictactoe:state", self.snapshot())

    def _log(self, text: str) -> None:
        ts = time.strftime("%H:%M:%S")
        self._history.append(f"{ts}  {text}")
        # 保留最近 60 条
        if len(self._history) > 60:
            self._history = self._history[-60:]

    # ------------------------------------------------------------------
    # 生命周期
    # ------------------------------------------------------------------

    def on_load(self) -> None:
        self.log.info("tictactoe AI loaded")

    async def on_disconnect(self) -> None:
        self._device_online = False
        self._emit_state()

    async def on_message(self, msg: dict) -> None:
        mtype = msg.get("type")

        if mtype == "hello":
            self._device_online = True
            self._board = [""] * 9
            self._winner = ""
            self._win_line = None
            self._last_ai_idx = -1
            self._log("设备上线，AI 就绪")
            self.tx("ready")
            self._emit_state()
            return

        if mtype == "reset":
            self._board = [""] * 9
            self._winner = ""
            self._win_line = None
            self._last_ai_idx = -1
            self._log("新一局")
            self.tx("reset_ack")
            self._emit_state()
            return

        if mtype == "move":
            body = msg.get("body") if isinstance(msg.get("body"), dict) else {}
            try:
                r = int(body.get("r", -1))
                c = int(body.get("c", -1))
            except (TypeError, ValueError):
                r, c = -1, -1
            if not (0 <= r < 3 and 0 <= c < 3):
                self.log.warning("bad coords r=%s c=%s", r, c)
                return
            idx = r * 3 + c
            if self._board[idx]:
                self.log.warning("square %d already taken", idx)
                return

            self._board[idx] = "X"
            self._log(f"玩家 → ({r},{c})")

            res, line = _check_win(self._board)
            if res:
                self._winner = res
                self._win_line = line
                self._log_end(res)
                self._emit_state()
                return

            ai_idx = _ai_pick(self._board)
            if ai_idx < 0:
                self._emit_state()
                return
            self._board[ai_idx] = "O"
            self._last_ai_idx = ai_idx
            ar, ac = ai_idx // 3, ai_idx % 3
            self.tx("move", body={"r": ar, "c": ac})
            self._log(f"AI → ({ar},{ac})")

            res, line = _check_win(self._board)
            if res:
                self._winner = res
                self._win_line = line
                self._log_end(res)
            self._emit_state()
            return

    def _log_end(self, res: str) -> None:
        if res == "X":
            self._log("🏆 玩家胜")
        elif res == "O":
            self._log("💀 AI 胜")
        else:
            self._log("🤝 平局")

    # ------------------------------------------------------------------
    # GUI
    # ------------------------------------------------------------------

    def make_gui_page(self, master, app):
        from gui_page import TicTacToePage
        return TicTacToePage(master, app, plugin=self)
