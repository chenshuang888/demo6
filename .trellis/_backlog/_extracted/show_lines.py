#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
按行号打印指定范围（用于精读某一小段）。

示例：
  python spec/iot/_extracted/show_lines.py --file "C--Users-...-OLED-----.md" --start 9650 --end 9720
"""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True)
    ap.add_argument("--start", type=int, required=True)
    ap.add_argument("--end", type=int, required=True)
    args = ap.parse_args()

    if args.start <= 0 or args.end < args.start:
        raise SystemExit("invalid range")

    path = Path(args.file)
    if not path.exists():
        raise SystemExit(f"file not found: {path}")

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for idx, line in enumerate(f, start=1):
            if idx < args.start:
                continue
            if idx > args.end:
                break
            print(f"{idx:>6} {line.rstrip()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

