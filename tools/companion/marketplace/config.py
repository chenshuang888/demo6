"""marketplace 模块配置：base URL + 缓存目录 + 持久化。"""
from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


def _config_dir() -> Path:
    """跨平台 config 目录。"""
    if os.name == "nt":
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    p = Path(base) / "esp32-companion"
    p.mkdir(parents=True, exist_ok=True)
    return p


CONFIG_FILE = _config_dir() / "marketplace.json"
CACHE_DIR = _config_dir() / "cache"
CACHE_DIR.mkdir(parents=True, exist_ok=True)

DEFAULT_BASE_URL = "https://marketplace.chenshuang.fun"
DEFAULT_TIMEOUT = 8.0


def load() -> dict[str, Any]:
    if not CONFIG_FILE.exists():
        return {"base_url": DEFAULT_BASE_URL}
    try:
        return json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {"base_url": DEFAULT_BASE_URL}


def save(cfg: dict[str, Any]) -> None:
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")


def get_base_url() -> str:
    return load().get("base_url", DEFAULT_BASE_URL).rstrip("/")


def set_base_url(url: str) -> None:
    cfg = load()
    cfg["base_url"] = url.rstrip("/")
    save(cfg)
