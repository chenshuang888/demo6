"""Home 页：连接状态卡 + 7 provider 健康灯网格。"""

from __future__ import annotations

import customtkinter as ctk

from ..theme import (
    COLOR_ACCENT, COLOR_ERR, COLOR_MUTED, COLOR_OFF, COLOR_OK, COLOR_TEXT,
)
from ..widgets import Card, StatusDot


PROVIDERS = ["time", "weather", "notify", "system", "media", "bridge", "upload"]


class HomePage(ctk.CTkFrame):
    def __init__(self, master, app) -> None:
        super().__init__(master, fg_color="transparent")
        self._app = app
        self._dots: dict[str, StatusDot] = {}
        self._build()
        # 监听 bus 事件
        app.bus.on("connect",    lambda addr: self.after(0, self._on_connect, addr))
        app.bus.on("disconnect", lambda _:    self.after(0, self._on_disconnect))
        app.bus.on("log",        lambda payload: self.after(0, self._on_log, payload))

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)

        # 大连接卡
        self._conn_card = Card(self)
        self._conn_card.grid(row=0, column=0, sticky="ew", padx=20, pady=(20, 10))
        self._conn_card.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self._conn_card, text="设备连接",
                      text_color=COLOR_MUTED, anchor="w") \
            .grid(row=0, column=0, sticky="w", padx=16, pady=(12, 4))
        self._conn_status = ctk.CTkLabel(self._conn_card,
                                          text="●  未连接",
                                          text_color=COLOR_OFF,
                                          font=ctk.CTkFont(size=18, weight="bold"),
                                          anchor="w")
        self._conn_status.grid(row=1, column=0, sticky="w", padx=16, pady=(0, 4))
        self._conn_addr = ctk.CTkLabel(self._conn_card,
                                        text="--",
                                        text_color=COLOR_MUTED,
                                        anchor="w")
        self._conn_addr.grid(row=2, column=0, sticky="w", padx=16, pady=(0, 12))

        # Provider 健康网格
        grid = Card(self)
        grid.grid(row=1, column=0, sticky="nsew", padx=20, pady=10)
        for c in range(2):
            grid.grid_columnconfigure(c, weight=1)

        ctk.CTkLabel(grid, text="服务状态",
                      text_color=COLOR_MUTED, anchor="w") \
            .grid(row=0, column=0, columnspan=2, sticky="w",
                   padx=16, pady=(12, 4))

        for i, name in enumerate(PROVIDERS):
            r = 1 + (i // 2)
            c = i % 2
            cell = ctk.CTkFrame(grid, fg_color="transparent")
            cell.grid(row=r, column=c, sticky="ew", padx=12, pady=4)
            cell.grid_columnconfigure(0, weight=1)
            dot = StatusDot(cell, text=name, color=COLOR_OFF)
            dot.grid(row=0, column=0, sticky="w")
            sub = ctk.CTkLabel(cell, text="--",
                                text_color=COLOR_MUTED, anchor="w")
            sub.grid(row=1, column=0, sticky="w", padx=18, pady=(0, 6))
            self._dots[name] = dot
            setattr(self, f"_sub_{name}", sub)

    # bus handlers

    def _on_connect(self, addr: object) -> None:
        self._conn_status.configure(text="●  已连接", text_color=COLOR_OK)
        self._conn_addr.configure(text=str(addr))
        for name, dot in self._dots.items():
            dot.set(name, COLOR_ACCENT)

    def _on_disconnect(self) -> None:
        self._conn_status.configure(text="●  未连接", text_color=COLOR_OFF)
        self._conn_addr.configure(text="--")
        for name, dot in self._dots.items():
            dot.set(name, COLOR_OFF)
            getattr(self, f"_sub_{name}").configure(text="--")

    def _on_log(self, payload: object) -> None:
        try:
            level, name, msg = payload  # type: ignore[misc]
        except Exception:
            return
        sub = getattr(self, f"_sub_{name}", None)
        if sub is None:
            return
        color = COLOR_MUTED
        dot = self._dots.get(name)
        if level == "info":
            if dot: dot.set(name, COLOR_OK)
        elif level == "warn":
            if dot: dot.set(name, COLOR_ERR)
            color = COLOR_ERR
        elif level == "err":
            if dot: dot.set(name, COLOR_ERR)
            color = COLOR_ERR
        sub.configure(text=str(msg)[:80], text_color=color)
