"""dynapp_push_gui —— ESP32 动态 app 上传 GUI。

启动：
    python dynapp_push_gui.py

依赖：customtkinter, bleak。已在 requirements.txt。

注意：BLE 一次只允许一个 central 连接。运行本工具前请先停掉
desktop_companion.py / dynapp_companion.py。

线程模型：
  - 主线程：tkinter mainloop
  - 后台线程：asyncio event loop（跑 bleak 协程）
  - 协程提交：loop.call_soon_threadsafe / asyncio.run_coroutine_threadsafe
  - UI 更新：root.after(0, ...)
"""

from __future__ import annotations

import asyncio
import logging
import os
import sys
import threading
from concurrent.futures import Future as ConcFuture
from pathlib import Path
from typing import Awaitable, Callable, Optional

import customtkinter as ctk
from tkinter import filedialog

# 让 tools/ 加进 sys.path，方便直接 `python dynapp_push_gui.py` 启动
sys.path.insert(0, str(Path(__file__).parent))

from dynapp_uploader import (
    DEFAULT_DEVICE_NAME_HINT,
    NAME_LEN,
    UploadError,
    UploaderClient,
)


# =============================================================================
# 配色（与项目里其它 GUI 一致：深紫青绿）
# =============================================================================

CTK_APPEARANCE = "dark"
CTK_THEME = "dark-blue"

COLOR_BG       = "#1E1B2E"
COLOR_PANEL    = "#2D2640"
COLOR_PANEL_HI = "#3A3354"
COLOR_ACCENT   = "#06B6D4"
COLOR_TEXT     = "#F1ECFF"
COLOR_MUTED    = "#9B94B5"
COLOR_OK       = "#10B981"
COLOR_ERR      = "#EF4444"
COLOR_WARN     = "#F59E0B"


# =============================================================================
# asyncio 桥
# =============================================================================

class AsyncioBridge:
    """把 asyncio event loop 跑在后台线程，给主线程 (Tk) 用。"""

    def __init__(self) -> None:
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def _run(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def submit(self, coro: Awaitable) -> ConcFuture:
        """从主线程把协程提交到后台 loop，返回 concurrent.futures.Future。"""
        return asyncio.run_coroutine_threadsafe(coro, self._loop)

    def stop(self) -> None:
        self._loop.call_soon_threadsafe(self._loop.stop)


# =============================================================================
# 状态共享：连接状态 + 当前 client
# =============================================================================

class AppState:
    """跨 view 共享：当前 UploaderClient + 连接信息。"""

    def __init__(self) -> None:
        self.client: Optional[UploaderClient] = None
        self.address: Optional[str] = None
        self.device_hint: str = DEFAULT_DEVICE_NAME_HINT


# =============================================================================
# 顶部连接条
# =============================================================================

class ConnectionBar(ctk.CTkFrame):
    """顶部连接条：扫描/连接/断开 + 状态指示。"""

    def __init__(self, master, *, bridge: AsyncioBridge, state: AppState,
                 on_connect_change: Callable[[bool], None]) -> None:
        super().__init__(master, fg_color=COLOR_PANEL, corner_radius=10)
        self._bridge = bridge
        self._state = state
        self._on_change = on_connect_change

        self._build()

    def _build(self) -> None:
        self.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(self, text="Device", text_color=COLOR_MUTED,
                     anchor="w", width=60).grid(row=0, column=0, padx=(14, 4), pady=10)

        self._name_entry = ctk.CTkEntry(self, fg_color=COLOR_PANEL_HI,
                                        text_color=COLOR_TEXT,
                                        border_width=0)
        self._name_entry.insert(0, self._state.device_hint)
        self._name_entry.grid(row=0, column=1, sticky="ew", padx=4, pady=10)

        self._btn = ctk.CTkButton(self, text="Connect", width=110,
                                  fg_color=COLOR_ACCENT,
                                  hover_color="#0EA5C0",
                                  command=self._on_btn)
        self._btn.grid(row=0, column=2, padx=4, pady=10)

        self._status = ctk.CTkLabel(self, text="● Disconnected",
                                    text_color=COLOR_MUTED,
                                    anchor="w", width=240)
        self._status.grid(row=0, column=3, padx=(8, 14), pady=10)

    # ----- 行为 -----

    def _on_btn(self) -> None:
        if self._state.client is None:
            self._do_connect()
        else:
            self._do_disconnect()

    def _do_connect(self) -> None:
        hint = self._name_entry.get().strip() or DEFAULT_DEVICE_NAME_HINT
        self._state.device_hint = hint
        self._set_status("Connecting...", COLOR_WARN)
        self._btn.configure(state="disabled")

        async def coro() -> str:
            client = UploaderClient(device_name=hint)
            addr = await client.connect()
            self._state.client = client
            self._state.address = addr
            return addr

        fut = self._bridge.submit(coro())
        fut.add_done_callback(lambda f: self.after(0, self._on_connect_done, f))

    def _on_connect_done(self, fut: ConcFuture) -> None:
        try:
            addr = fut.result()
        except Exception as e:
            logging.exception("connect failed")
            self._set_status(f"Failed: {e}", COLOR_ERR)
            self._state.client = None
            self._state.address = None
            self._btn.configure(state="normal", text="Connect")
            self._on_change(False)
            return
        self._set_status(f"● Connected ({addr})", COLOR_OK)
        self._btn.configure(state="normal", text="Disconnect")
        self._on_change(True)

    def _do_disconnect(self) -> None:
        client = self._state.client
        if client is None:
            return
        self._btn.configure(state="disabled")

        async def coro() -> None:
            await client.disconnect()

        fut = self._bridge.submit(coro())
        fut.add_done_callback(lambda f: self.after(0, self._on_disconnect_done))

    def _on_disconnect_done(self) -> None:
        self._state.client = None
        self._state.address = None
        self._set_status("● Disconnected", COLOR_MUTED)
        self._btn.configure(state="normal", text="Connect")
        self._on_change(False)

    def _set_status(self, text: str, color: str) -> None:
        self._status.configure(text=text, text_color=color)


# =============================================================================
# Upload 视图
# =============================================================================

class UploadView(ctk.CTkFrame):
    def __init__(self, master, *, bridge: AsyncioBridge, state: AppState,
                 log: Callable[[str, str], None]) -> None:
        super().__init__(master, fg_color="transparent")
        self._bridge = bridge
        self._state = state
        self._log = log

        self._file_path: Optional[str] = None
        self._folder_path: Optional[str] = None  # 选目录模式（含 main.js + assets/）
        self._build()
        self.set_enabled(False)

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)

        # —— 文件选择行 ——
        row1 = ctk.CTkFrame(self, fg_color=COLOR_PANEL, corner_radius=10)
        row1.grid(row=0, column=0, sticky="ew", padx=14, pady=(14, 8))
        row1.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(row1, text="File", text_color=COLOR_MUTED,
                     width=60, anchor="w").grid(row=0, column=0, padx=(14, 4), pady=10)
        self._file_lbl = ctk.CTkLabel(row1, text="(none)", text_color=COLOR_TEXT,
                                      anchor="w")
        self._file_lbl.grid(row=0, column=1, sticky="ew", padx=4, pady=10)
        self._browse_btn = ctk.CTkButton(row1, text="Browse...", width=110,
                                         fg_color=COLOR_PANEL_HI,
                                         hover_color=COLOR_ACCENT,
                                         command=self._on_browse)
        self._browse_btn.grid(row=0, column=2, padx=(4, 4), pady=10)
        self._folder_btn = ctk.CTkButton(row1, text="Folder...", width=110,
                                         fg_color=COLOR_PANEL_HI,
                                         hover_color=COLOR_ACCENT,
                                         command=self._on_browse_folder)
        self._folder_btn.grid(row=0, column=3, padx=(0, 14), pady=10)

        # —— 名称 + 元信息 ——
        row2 = ctk.CTkFrame(self, fg_color=COLOR_PANEL, corner_radius=10)
        row2.grid(row=1, column=0, sticky="ew", padx=14, pady=8)
        row2.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(row2, text="App ID", text_color=COLOR_MUTED,
                     width=70, anchor="w").grid(row=0, column=0, padx=(14, 4), pady=(10, 4))
        self._name_entry = ctk.CTkEntry(row2, fg_color=COLOR_PANEL_HI,
                                        text_color=COLOR_TEXT, border_width=0,
                                        placeholder_text="e.g. alarm  (a-z 0-9 _ -)")
        self._name_entry.grid(row=0, column=1, columnspan=2, sticky="ew",
                              padx=4, pady=(10, 4))
        ctk.CTkLabel(row2, text="Display", text_color=COLOR_MUTED,
                     width=70, anchor="w").grid(row=1, column=0, padx=(14, 4), pady=(0, 4))
        self._display_entry = ctk.CTkEntry(row2, fg_color=COLOR_PANEL_HI,
                                            text_color=COLOR_TEXT, border_width=0,
                                            placeholder_text="manifest.name (中文 OK)")
        self._display_entry.grid(row=1, column=1, columnspan=2, sticky="ew",
                                  padx=4, pady=(0, 4))
        ctk.CTkLabel(row2, text="Size", text_color=COLOR_MUTED,
                     width=70, anchor="w").grid(row=2, column=0, padx=(14, 4), pady=(0, 10))
        self._meta_lbl = ctk.CTkLabel(row2, text="—", text_color=COLOR_TEXT,
                                      anchor="w")
        self._meta_lbl.grid(row=2, column=1, columnspan=2, sticky="ew",
                            padx=4, pady=(0, 10))

        # —— 进度条 ——
        row3 = ctk.CTkFrame(self, fg_color=COLOR_PANEL, corner_radius=10)
        row3.grid(row=2, column=0, sticky="ew", padx=14, pady=8)
        row3.grid_columnconfigure(0, weight=1)
        self._progress = ctk.CTkProgressBar(row3, fg_color=COLOR_PANEL_HI,
                                            progress_color=COLOR_ACCENT,
                                            height=18)
        self._progress.set(0)
        self._progress.grid(row=0, column=0, sticky="ew", padx=14, pady=(14, 6))
        self._progress_lbl = ctk.CTkLabel(row3, text="ready",
                                          text_color=COLOR_MUTED,
                                          anchor="w")
        self._progress_lbl.grid(row=1, column=0, sticky="w", padx=14, pady=(0, 14))

        # —— 按钮 ——
        self._upload_btn = ctk.CTkButton(self, text="Upload", height=40,
                                         fg_color=COLOR_ACCENT,
                                         hover_color="#0EA5C0",
                                         command=self._on_upload)
        self._upload_btn.grid(row=3, column=0, sticky="ew", padx=14, pady=(8, 14))

    # ----- 行为 -----

    def set_enabled(self, ok: bool) -> None:
        """连接状态变化时调；连接断开就禁用所有操作按钮。"""
        state = "normal" if ok else "disabled"
        self._upload_btn.configure(state=state)
        # browse 永远可用，方便没连接也能挑文件

    def _on_browse(self) -> None:
        path = filedialog.askopenfilename(
            title="Select dynamic app .js (main.js)",
            filetypes=[("JavaScript", "*.js"), ("All files", "*.*")])
        if not path:
            return
        self._file_path = path
        self._folder_path = None   # 切回单文件模式
        self._file_lbl.configure(text=os.path.basename(path))

        # 自动从文件名推断 app_id（去掉 .js 后缀）
        guess = os.path.splitext(os.path.basename(path))[0]
        guess = guess[:NAME_LEN]
        self._name_entry.delete(0, "end")
        self._name_entry.insert(0, guess)
        # display 字段默认 = app_id；用户可改成中文名
        self._display_entry.delete(0, "end")
        self._display_entry.insert(0, guess)

        try:
            sz = os.path.getsize(path)
            self._meta_lbl.configure(text=f"{sz:,} bytes")
        except OSError as e:
            self._meta_lbl.configure(text=f"size error: {e}",
                                     text_color=COLOR_ERR)

    def _on_browse_folder(self) -> None:
        """选一个 app pack 目录：必须包含 main.js，可选 assets/*.bin / manifest.json。
        相比单文件 Browse：app_id 来自目录名，size 字段汇总所有文件。"""
        path = filedialog.askdirectory(title="Select dynamic app pack directory")
        if not path:
            return
        main_js = os.path.join(path, "main.js")
        if not os.path.isfile(main_js):
            self._set_progress_text("dir missing main.js", COLOR_ERR)
            return

        self._folder_path = path
        self._file_path = None    # 切回目录模式
        guess = os.path.basename(os.path.normpath(path))
        # 容忍习惯性后缀：imgdemo_pkg → imgdemo（也可由用户在输入框里改）
        for suf in ("_pkg", "-pkg", ".pkg"):
            if guess.endswith(suf):
                guess = guess[:-len(suf)]
                break
        guess = guess[:NAME_LEN]

        # 汇总：main.js 大小 + 可选 icon.bin + assets/*.bin 数量与字节
        total = os.path.getsize(main_js)
        icon_local = os.path.join(path, "icon.bin")
        has_icon = os.path.isfile(icon_local)
        if has_icon:
            total += os.path.getsize(icon_local)
        asset_count = 0
        assets_dir = os.path.join(path, "assets")
        if os.path.isdir(assets_dir):
            for nm in os.listdir(assets_dir):
                fp = os.path.join(assets_dir, nm)
                if os.path.isfile(fp):
                    total += os.path.getsize(fp)
                    asset_count += 1

        parts = ["main.js"]
        if has_icon: parts.append("icon")
        if asset_count: parts.append(f"{asset_count} asset(s)")
        self._file_lbl.configure(
            text=f"{guess}/  ({' + '.join(parts)})")
        self._name_entry.delete(0, "end")
        self._name_entry.insert(0, guess)
        self._display_entry.delete(0, "end")
        self._display_entry.insert(0, guess)
        self._meta_lbl.configure(text=f"{total:,} bytes total",
                                 text_color=COLOR_TEXT)

    def _on_upload(self) -> None:
        if self._state.client is None:
            self._set_progress_text("not connected", COLOR_ERR)
            return
        if not self._file_path and not self._folder_path:
            self._set_progress_text("no file/folder selected", COLOR_ERR)
            return
        app_id = self._name_entry.get().strip()
        if not app_id:
            self._set_progress_text("app_id empty", COLOR_ERR)
            return
        display = self._display_entry.get().strip() or app_id

        client = self._state.client
        self._upload_btn.configure(state="disabled")
        self._progress.set(0)

        def on_progress(sent: int, total: int) -> None:
            self.after(0, self._update_progress, sent, total)

        if self._folder_path:
            pack = self._folder_path
            self._set_progress_text("uploading pack...", COLOR_WARN)
            self._log("upload", f"start pack {app_id} ({pack})")

            def on_step(name: str, idx: int, total: int) -> None:
                self.after(0, self._set_progress_text,
                           f"[{idx}/{total}] {name}", COLOR_MUTED)
                # 步级进度（按文件数粗粒度）
                self.after(0, self._progress.set, (idx - 1) / total)

            async def coro_pack() -> None:
                await client.upload_app_pack(app_id, pack,
                                              display_name=display,
                                              on_step=on_step,
                                              on_progress=on_progress)

            fut = self._bridge.submit(coro_pack())
        else:
            path = self._file_path
            self._set_progress_text("uploading...", COLOR_WARN)
            self._log("upload", f"start {app_id} ({path})")

            async def coro_single() -> None:
                await client.upload_app(app_id, path,
                                        display_name=display,
                                        on_progress=on_progress)

            fut = self._bridge.submit(coro_single())

        fut.add_done_callback(lambda f: self.after(0, self._on_upload_done, f, app_id))

    def _update_progress(self, sent: int, total: int) -> None:
        self._progress.set(sent / total if total else 0)
        self._set_progress_text(f"{sent:,} / {total:,} B  ({100*sent//total}%)",
                                COLOR_MUTED)

    def _on_upload_done(self, fut: ConcFuture, name: str) -> None:
        self._upload_btn.configure(state="normal")
        try:
            fut.result()
        except UploadError as e:
            self._set_progress_text(f"failed: {e}", COLOR_ERR)
            self._log("upload", f"{name} FAILED: {e}", level="error")
            return
        except Exception as e:
            self._set_progress_text(f"error: {e}", COLOR_ERR)
            self._log("upload", f"{name} ERROR: {e}", level="error")
            return
        self._progress.set(1)
        self._set_progress_text(f"done ({name})", COLOR_OK)
        self._log("upload", f"{name} OK")

    def _set_progress_text(self, text: str, color: str) -> None:
        self._progress_lbl.configure(text=text, text_color=color)


# =============================================================================
# Apps 视图（list + delete）
# =============================================================================

class AppsView(ctk.CTkFrame):
    def __init__(self, master, *, bridge: AsyncioBridge, state: AppState,
                 log: Callable[[str, str], None]) -> None:
        super().__init__(master, fg_color="transparent")
        self._bridge = bridge
        self._state = state
        self._log = log

        self._selected: Optional[str] = None
        self._build()
        self.set_enabled(False)

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # 顶部按钮条
        bar = ctk.CTkFrame(self, fg_color="transparent")
        bar.grid(row=0, column=0, sticky="ew", padx=14, pady=(14, 8))
        self._refresh_btn = ctk.CTkButton(bar, text="Refresh", width=110,
                                          fg_color=COLOR_PANEL_HI,
                                          hover_color=COLOR_ACCENT,
                                          command=self._on_refresh)
        self._refresh_btn.pack(side="left")
        self._delete_btn = ctk.CTkButton(bar, text="Delete selected", width=160,
                                         fg_color=COLOR_ERR,
                                         hover_color="#B91C1C",
                                         command=self._on_delete)
        self._delete_btn.pack(side="right")

        # 列表（用 scrollable frame 装）
        self._list_frame = ctk.CTkScrollableFrame(self, fg_color=COLOR_PANEL,
                                                  corner_radius=10)
        self._list_frame.grid(row=1, column=0, sticky="nsew", padx=14, pady=(0, 14))

        self._empty_lbl = ctk.CTkLabel(self._list_frame,
                                       text="(refresh to load)",
                                       text_color=COLOR_MUTED)
        self._empty_lbl.pack(pady=20)

    def set_enabled(self, ok: bool) -> None:
        state = "normal" if ok else "disabled"
        self._refresh_btn.configure(state=state)
        self._delete_btn.configure(state=state)

    # ----- 行为 -----

    def _on_refresh(self) -> None:
        client = self._state.client
        if client is None:
            return
        self._refresh_btn.configure(state="disabled")
        self._log("apps", "list...")

        async def coro() -> list[str]:
            return await client.list_apps()

        fut = self._bridge.submit(coro())
        fut.add_done_callback(lambda f: self.after(0, self._on_refresh_done, f))

    def _on_refresh_done(self, fut: ConcFuture) -> None:
        self._refresh_btn.configure(state="normal")
        try:
            names = fut.result()
        except Exception as e:
            self._log("apps", f"list FAILED: {e}", level="error")
            return
        self._render_list(names)
        self._log("apps", f"list OK ({len(names)} on FS)")

    def _render_list(self, fs_names: list[str]) -> None:
        # 清空
        for w in self._list_frame.winfo_children():
            w.destroy()

        # 单源化：只渲染 FS 上存在的 app
        if not fs_names:
            ctk.CTkLabel(self._list_frame, text="(empty)",
                         text_color=COLOR_MUTED).pack(pady=20)
            return

        self._selected = None
        self._row_frames: dict[str, ctk.CTkFrame] = {}
        for name in sorted(fs_names):
            self._add_row(name)

    def _add_row(self, name: str) -> None:
        row = ctk.CTkFrame(self._list_frame, fg_color=COLOR_PANEL_HI,
                           corner_radius=6, height=44)
        row.pack(fill="x", padx=8, pady=4)
        row.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(row, text=name, text_color=COLOR_TEXT,
                     anchor="w").grid(row=0, column=0, sticky="w",
                                       padx=14, pady=10)
        ctk.CTkLabel(row, text="FS", text_color=COLOR_OK,
                     anchor="e").grid(row=0, column=1, sticky="e",
                                       padx=14, pady=10)

        row.bind("<Button-1>", lambda _e, n=name: self._select(n))
        for child in row.winfo_children():
            child.bind("<Button-1>", lambda _e, n=name: self._select(n))

        self._row_frames[name] = row

    def _select(self, name: str) -> None:
        self._selected = name
        for n, w in self._row_frames.items():
            color = COLOR_ACCENT if n == name else COLOR_PANEL_HI
            w.configure(fg_color=color)

    def _on_delete(self) -> None:
        client = self._state.client
        if client is None or not self._selected:
            return
        name = self._selected
        self._delete_btn.configure(state="disabled")
        self._log("apps", f"delete {name}...")

        async def coro() -> None:
            await client.delete_app(name)

        fut = self._bridge.submit(coro())
        fut.add_done_callback(lambda f: self.after(0, self._on_delete_done, f, name))

    def _on_delete_done(self, fut: ConcFuture, name: str) -> None:
        self._delete_btn.configure(state="normal")
        try:
            fut.result()
        except Exception as e:
            self._log("apps", f"delete {name} FAILED: {e}", level="error")
            return
        self._log("apps", f"delete {name} OK")
        self._on_refresh()


# =============================================================================
# Log 视图
# =============================================================================

class LogView(ctk.CTkFrame):
    def __init__(self, master) -> None:
        super().__init__(master, fg_color="transparent")
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self._text = ctk.CTkTextbox(self, fg_color=COLOR_PANEL,
                                    text_color=COLOR_TEXT,
                                    corner_radius=10,
                                    font=ctk.CTkFont(family="Consolas", size=12))
        self._text.grid(row=0, column=0, sticky="nsew", padx=14, pady=14)
        self._text.configure(state="disabled")

    def append(self, source: str, message: str, level: str = "info") -> None:
        prefix = {"info": "  ", "warn": "! ", "error": "X "}[level]
        line = f"{prefix}[{source}] {message}\n"
        self._text.configure(state="normal")
        self._text.insert("end", line)
        self._text.see("end")
        self._text.configure(state="disabled")


# =============================================================================
# 主窗口
# =============================================================================

class App(ctk.CTk):
    def __init__(self, bridge: AsyncioBridge) -> None:
        super().__init__()
        self.title("ESP32 Dynamic App Uploader")
        self.geometry("760x540")
        self.minsize(720, 520)
        self.configure(fg_color=COLOR_BG)

        self._bridge = bridge
        self._state = AppState()

        self._build()

    def _build(self) -> None:
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # —— 侧边栏 ——
        sidebar = ctk.CTkFrame(self, fg_color=COLOR_PANEL, width=160,
                               corner_radius=0)
        sidebar.grid(row=0, column=0, rowspan=2, sticky="nsw")
        sidebar.grid_propagate(False)

        ctk.CTkLabel(sidebar, text="Uploader",
                     text_color=COLOR_ACCENT,
                     font=ctk.CTkFont(size=18, weight="bold")
                     ).pack(pady=(20, 30), padx=20, anchor="w")

        self._tab_buttons: dict[str, ctk.CTkButton] = {}
        for tab in ("Upload", "Apps", "Log"):
            btn = ctk.CTkButton(sidebar, text=tab, height=40,
                                fg_color="transparent",
                                hover_color=COLOR_PANEL_HI,
                                text_color=COLOR_TEXT,
                                anchor="w",
                                command=lambda t=tab: self._switch_tab(t))
            btn.pack(fill="x", padx=10, pady=2)
            self._tab_buttons[tab] = btn

        # —— 顶部连接条 ——
        self._conn_bar = ConnectionBar(self,
                                       bridge=self._bridge,
                                       state=self._state,
                                       on_connect_change=self._on_conn_change)
        self._conn_bar.grid(row=0, column=1, sticky="ew", padx=14, pady=14)

        # —— 内容区（三个 view 叠在同一格，按 tab 切 raise）——
        self._content = ctk.CTkFrame(self, fg_color="transparent")
        self._content.grid(row=1, column=1, sticky="nsew", padx=0, pady=0)
        self._content.grid_rowconfigure(0, weight=1)
        self._content.grid_columnconfigure(0, weight=1)

        self._log_view = LogView(self._content)
        self._upload_view = UploadView(self._content,
                                       bridge=self._bridge,
                                       state=self._state,
                                       log=self._log_view.append)
        self._apps_view = AppsView(self._content,
                                   bridge=self._bridge,
                                   state=self._state,
                                   log=self._log_view.append)

        for v in (self._upload_view, self._apps_view, self._log_view):
            v.grid(row=0, column=0, sticky="nsew")

        self._switch_tab("Upload")

    def _switch_tab(self, tab: str) -> None:
        view = {"Upload": self._upload_view,
                "Apps":   self._apps_view,
                "Log":    self._log_view}[tab]
        view.tkraise()
        # 高亮当前 tab 按钮
        for name, btn in self._tab_buttons.items():
            color = COLOR_PANEL_HI if name == tab else "transparent"
            btn.configure(fg_color=color)

    def _on_conn_change(self, ok: bool) -> None:
        self._upload_view.set_enabled(ok)
        self._apps_view.set_enabled(ok)


# =============================================================================
# main
# =============================================================================

def main() -> None:
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    ctk.set_appearance_mode(CTK_APPEARANCE)
    ctk.set_default_color_theme(CTK_THEME)

    bridge = AsyncioBridge()
    bridge.start()

    app = App(bridge)
    try:
        app.mainloop()
    finally:
        # 尝试干净断开（窗口关闭时）
        if app._state.client is not None:
            try:
                bridge.submit(app._state.client.disconnect()).result(timeout=3)
            except Exception:
                pass
        bridge.stop()


if __name__ == "__main__":
    main()
