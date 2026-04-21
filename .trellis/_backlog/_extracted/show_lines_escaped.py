#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
以“可读但不依赖终端 Unicode 渲染”的方式查看指定行区间。

在某些终端/采集环境里，UTF-8 中文输出会出现乱码（例如被按 latin-1 解码）。
本脚本把非 ASCII 字符转成 \\uXXXX/\\UXXXXXXXX 转义，保证在任何环境里都可稳定阅读。

用法：
  python spec/iot/_extracted/show_lines_escaped.py --file "xxx.md" --start 2173 --end 2245
"""

from __future__ import annotations

import argparse
from pathlib import Path


def _escape_line(s: str) -> str:
    # unicode_escape 会把中文等非 ASCII 字符转成 \uXXXX，便于稳定查看。
    # 同时保留 \n 以外的原始字符（空格、标点、反引号等）。
    return s.encode("unicode_escape").decode("ascii", errors="strict")


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

    with path.open("r", encoding="utf-8", errors="strict") as f:
        for idx, line in enumerate(f, start=1):
            if idx < args.start:
                continue
            if idx > args.end:
                break
            print(f"{idx:>6} {_escape_line(line.rstrip())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

