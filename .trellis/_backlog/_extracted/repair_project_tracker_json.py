#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复 project_tracker.json 被意外追加“多余尾巴”导致 JSONDecodeError: Extra data 的问题。

做法：使用 json.JSONDecoder().raw_decode 只解析第一个 JSON 对象并截断后续内容，
然后重新 pretty-print 回写为规范 JSON。

用法：
  python spec/iot/_extracted/repair_project_tracker_json.py
"""

from __future__ import annotations

import json
from pathlib import Path


TRACKER_PATH = Path("spec/iot/_extracted/project_tracker.json")


def main() -> int:
    raw = TRACKER_PATH.read_text(encoding="utf-8", errors="strict")

    decoder = json.JSONDecoder()
    obj, end = decoder.raw_decode(raw)

    trailer = raw[end:].strip()
    before_len = len(raw)

    TRACKER_PATH.write_text(
        json.dumps(obj, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    after_len = TRACKER_PATH.stat().st_size
    if trailer:
        print(f"Repaired: truncated extra trailer (len={len(trailer)})")
    else:
        print("OK: no extra trailer")
    print(f"Size: {before_len} -> {after_len}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

