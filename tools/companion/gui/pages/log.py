"""Log 页：滚动日志 + provider 过滤。"""

from __future__ import annotations

import time as _time

import customtkinter as ctk

from ..theme import (
    COLOR_ACCENT, COLOR_ERR, COLOR_MUTED, COLOR_OK, COLOR_PANEL_HI,
    COLOR_TEXT, COLOR_WARN,
)
from ..widgets import Card

PROVIDERS = ["all", "core", "time", "weather", "notify",
             "system", "media", "bridge", "upload"]


class LogPage(ctk.CTkFrame):
    MAX_LINES = 1000

    def __init__(self, master, app) -> None:
        super().__init__(master, fg_color="transparent")
        self._app = app
        self._filter = "all"
        self._build()
        app.bus.on("log", lambda p: self.after(0, self._on_log, p))

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        bar = Card(self)
        bar.grid(row=0, column=0, sticky="ew", padx=20, pady=(20, 8))
        bar.grid_columnconfigure(2, weight=1)
        ctk.CTkLabel(bar, text="过滤", text_color=COLOR_MUTED) \
            .grid(row=0, column=0, padx=(14, 4), pady=10)
        self._om = ctk.CTkOptionMenu(bar, values=PROVIDERS,
                                       fg_color=COLOR_PANEL_HI,
                                       button_color=COLOR_PANEL_HI,
                                       button_hover_color=COLOR_ACCENT,
                                       command=self._set_filter)
        self._om.set("all")
        self._om.grid(row=0, column=1, padx=4, pady=10)
        ctk.CTkButton(bar, text="清空", width=80,
                       fg_color=COLOR_PANEL_HI,
                       command=self._clear).grid(row=0, column=3, padx=14, pady=10)

        body = Card(self)
        body.grid(row=1, column=0, sticky="nsew", padx=20, pady=(8, 20))
        body.grid_columnconfigure(0, weight=1)
        body.grid_rowconfigure(0, weight=1)
        self._tb = ctk.CTkTextbox(body, fg_color="transparent",
                                    text_color=COLOR_TEXT, border_width=0)
        self._tb.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)

    def _set_filter(self, v: str) -> None:
        self._filter = v

    def _clear(self) -> None:
        self._tb.delete("1.0", "end")

    def _on_log(self, payload: object) -> None:
        try:
            level, name, msg = payload  # type: ignore[misc]
        except Exception:
            return
        if self._filter != "all" and self._filter != name:
            return
        ts = _time.strftime("%H:%M:%S")
        line = f"{ts}  [{level:4}] {name:7} {msg}\n"
        self._tb.insert("end", line)
        # 截断
        cnt = int(self._tb.index("end-1c").split(".")[0])
        if cnt > self.MAX_LINES:
            self._tb.delete("1.0", f"{cnt - self.MAX_LINES}.0")
        self._tb.see("end")
