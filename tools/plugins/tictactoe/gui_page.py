"""TicTacToePage —— 井字棋只读监控面板。

业务规则：
  - PC 不能操作（操作在设备上）。本页是被动旁观者。
  - 订阅 bus("tictactoe:state") 事件，每次 plugin 状态变化重渲。
  - 进入页面时主动调一次 plugin.snapshot() 把当前状态拉过来。

视觉：
  ┌───────────────┐
  │ 标题 + 状态点  │
  │               │
  │   3×3 棋盘    │
  │               │
  │ AI 上手坐标    │
  │ ─────         │
  │ 历史日志（滚） │
  └───────────────┘
"""

from __future__ import annotations

import tkinter as tk
from typing import Any

import customtkinter as ctk

from companion.plugin_sdk.gui import theme, widgets

C = theme

CELL_PX  = 64
GAP_PX   = 6
BD_W     = CELL_PX * 3 + GAP_PX * 2 + 24


class TicTacToePage(ctk.CTkFrame):
    def __init__(self, master, app, plugin: Any) -> None:
        super().__init__(master, fg_color="transparent")
        self._app    = app
        self._plugin = plugin
        self._unsubs: list = []

        self._cells: list[ctk.CTkLabel] = []
        self._win_line: list[int] | None = None

        self._build()
        self._bind_bus()
        # 进/出本页：tk 的 <Map>/<Unmap>
        self.bind("<Map>",   lambda _e: self._refresh(self._plugin.snapshot()))

    # ------------------------------------------------------------------
    # build
    # ------------------------------------------------------------------

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        # 让 row 3（日志卡）吃剩余空间，前 3 行按内容收紧
        self.grid_rowconfigure(0, weight=0)
        self.grid_rowconfigure(1, weight=0)
        self.grid_rowconfigure(2, weight=0)
        self.grid_rowconfigure(3, weight=1)

        # 顶部：标题 + 在线状态
        head = ctk.CTkFrame(self, fg_color="transparent")
        head.grid(row=0, column=0, sticky="ew", padx=20, pady=(20, 8))
        head.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(head, text="井字棋（设备 vs AI）",
                      text_color=C.COLOR_TEXT,
                      font=ctk.CTkFont(size=18, weight="bold"),
                      anchor="w") \
            .grid(row=0, column=0, sticky="w")
        self._status_lbl = ctk.CTkLabel(
            head, text="设备未连接",
            text_color=C.COLOR_MUTED,
            font=ctk.CTkFont(size=12), anchor="e")
        self._status_lbl.grid(row=0, column=1, sticky="e")

        # 棋盘卡
        board_card = widgets.Card(self)
        board_card.grid(row=1, column=0, padx=20, pady=8)
        for r in range(3):
            for c in range(3):
                idx = r * 3 + c
                cell = ctk.CTkLabel(
                    board_card, text=" ",
                    width=CELL_PX, height=CELL_PX,
                    fg_color=C.COLOR_PANEL_HI,
                    text_color=C.COLOR_TEXT,
                    corner_radius=10,
                    font=ctk.CTkFont(size=36, weight="bold"))
                cell.grid(row=r, column=c, padx=4, pady=4)
                self._cells.append(cell)

        # AI 上一手
        self._ai_lbl = ctk.CTkLabel(
            self, text="AI 还未落子",
            text_color=C.COLOR_MUTED,
            font=ctk.CTkFont(size=13), anchor="w")
        self._ai_lbl.grid(row=2, column=0, sticky="w", padx=24, pady=(6, 0))

        # 历史日志（滚动）
        log_card = widgets.Card(self)
        log_card.grid(row=3, column=0, sticky="nsew", padx=20, pady=(8, 20))
        log_card.grid_rowconfigure(0, weight=1)
        log_card.grid_columnconfigure(0, weight=1)
        self._log_box = ctk.CTkTextbox(
            log_card, fg_color="transparent",
            text_color=C.COLOR_TEXT,
            font=ctk.CTkFont(size=12),
            wrap="none", activate_scrollbars=True)
        self._log_box.grid(row=0, column=0, sticky="nsew", padx=8, pady=8)
        self._log_box.configure(state="disabled")

    # ------------------------------------------------------------------
    # bus
    # ------------------------------------------------------------------

    def _bind_bus(self) -> None:
        def _on_state(payload: object) -> None:
            if not isinstance(payload, dict):
                return
            # bus 可能跨线程过来，强制丢回 tk 主线程
            self.after(0, lambda p=payload: self._refresh(p))
        self._unsubs.append(self._app.bus.on("tictactoe:state", _on_state))

    def destroy(self) -> None:
        for u in self._unsubs:
            try: u()
            except Exception: pass
        self._unsubs.clear()
        super().destroy()

    # ------------------------------------------------------------------
    # render
    # ------------------------------------------------------------------

    def _refresh(self, snap: dict) -> None:
        board:    list[str] = snap.get("board") or [""] * 9
        winner:   str       = snap.get("winner") or ""
        win_line                  = snap.get("win_line") or None
        last_ai:  int       = int(snap.get("last_ai") or -1)
        online:   bool      = bool(snap.get("device_online"))
        history:  list[str] = snap.get("history") or []

        # 状态点
        if winner == "X":
            self._status_lbl.configure(
                text="玩家胜利", text_color=C.COLOR_OK)
        elif winner == "O":
            self._status_lbl.configure(
                text="AI 胜利", text_color=C.COLOR_ERR)
        elif winner == "draw":
            self._status_lbl.configure(
                text="平局", text_color=C.COLOR_WARN)
        elif online:
            self._status_lbl.configure(
                text="对局进行中", text_color=C.COLOR_ACCENT)
        else:
            self._status_lbl.configure(
                text="设备未连接", text_color=C.COLOR_MUTED)

        # 棋盘
        win_set = set(win_line) if win_line else set()
        for i in range(9):
            v = board[i] or ""
            cell = self._cells[i]
            text = v
            if v == "X":
                tc = C.COLOR_OK if i in win_set else C.COLOR_ACCENT
            elif v == "O":
                tc = C.COLOR_ERR if i in win_set else C.COLOR_TEXT
            else:
                text = " "
                tc = C.COLOR_TEXT

            # 高亮 AI 最后落子（若不在胜利线里）
            bg = C.COLOR_PANEL_HI
            if i == last_ai and i not in win_set and not winner:
                bg = C.COLOR_PANEL
            if i in win_set:
                bg = C.COLOR_PANEL

            cell.configure(text=text, text_color=tc, fg_color=bg)

        # AI 最后一手
        if last_ai >= 0:
            r, c = last_ai // 3, last_ai % 3
            self._ai_lbl.configure(text=f"AI 上手：({r}, {c})")
        else:
            self._ai_lbl.configure(text="AI 还未落子")

        # 历史日志
        self._log_box.configure(state="normal")
        self._log_box.delete("1.0", "end")
        if history:
            self._log_box.insert("1.0", "\n".join(history))
            self._log_box.see("end")
        self._log_box.configure(state="disabled")
