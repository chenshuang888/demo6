#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
对话块快速提纲工具（用于蒸馏提速）

目标：
- 不把整段内容全量打印出来（太慢/太长）
- 通过“标题/列表/关键字命中/代码块边界”等线索，快速判断该块价值与应落地 spec 的方向

用法示例：
  python spec/iot/_extracted/block_outline.py --file "C--Users-...-OLED-----.md" --start 9269 --end 11492
  python spec/iot/_extracted/block_outline.py --file "..." --start 9269 --end 11492 --max 200
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Tuple


KEYWORDS = [
    "结论",
    "总结",
    "风险",
    "坑",
    "根因",
    "原因",
    "修复",
    "方案",
    "设计",
    "接口",
    "协议",
    "兼容",
    "回滚",
    "验收",
    "TODO",
    "FIXME",
]


@dataclass(frozen=True)
class Hit:
    lineno: int
    kind: str
    text: str


def _iter_range_lines(path: Path, start: int, end: int) -> Iterable[Tuple[int, str]]:
    # 1-based inclusive
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for idx, line in enumerate(f, start=1):
            if idx < start:
                continue
            if idx > end:
                break
            yield idx, line.rstrip("\n")


def _collect_hits(lines: Iterable[Tuple[int, str]]) -> List[Hit]:
    hits: List[Hit] = []
    in_code = False

    md_heading = re.compile(r"^\s{0,3}#{1,6}\s+")
    md_list = re.compile(r"^\s{0,6}([-*]|\d+\.)\s+")
    md_code_fence = re.compile(r"^\s*```")
    md_inline_pathish = re.compile(r"(`[^`]+`|[A-Za-z0-9_./\\\\-]+\.(c|h|cpp|js|ts|py|md|json|yml|yaml))")

    for lineno, raw in lines:
        line = raw.rstrip()
        if not line:
            continue

        if md_code_fence.match(line):
            in_code = not in_code
            hits.append(Hit(lineno, "code_fence", line))
            continue

        if md_heading.match(line):
            hits.append(Hit(lineno, "heading", line.strip()))
            continue

        # 列表项优先保留（尤其是风险/步骤/结构）
        if md_list.match(line) and not in_code:
            hits.append(Hit(lineno, "list", line.strip()))
            continue

        # 关键字命中（允许在 code 内也命中，但会更杂；因此仅在非 code 内命中）
        if not in_code and any(k in line for k in KEYWORDS):
            hits.append(Hit(lineno, "kw", line.strip()))
            continue

        # 文件名/路径/标识符引用（弱信号）
        if not in_code and md_inline_pathish.search(line):
            hits.append(Hit(lineno, "ref", line.strip()))
            continue

    return hits


def _format_hits(hits: List[Hit], max_lines: int) -> str:
    if max_lines <= 0:
        return ""

    # 先按 kind 分组排序：heading > list > kw > ref > code_fence
    priority = {"heading": 0, "list": 1, "kw": 2, "ref": 3, "code_fence": 4}
    hits_sorted = sorted(hits, key=lambda h: (priority.get(h.kind, 9), h.lineno))

    shown = hits_sorted[:max_lines]
    out: List[str] = []
    out.append(f"hits={len(hits)} shown={len(shown)} max={max_lines}")
    for h in shown:
        out.append(f"{h.lineno:>6} [{h.kind}] {h.text}")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, help="源对话文件名（位于当前目录）")
    ap.add_argument("--start", type=int, required=True, help="起始行号（1-based, inclusive）")
    ap.add_argument("--end", type=int, required=True, help="结束行号（1-based, inclusive）")
    ap.add_argument("--max", type=int, default=180, help="最多输出多少行命中（默认 180）")
    args = ap.parse_args()

    if args.start <= 0 or args.end <= 0 or args.end < args.start:
        raise SystemExit("invalid range: start/end")

    path = Path(args.file)
    if not path.exists():
        raise SystemExit(f"file not found: {path}")

    hits = _collect_hits(_iter_range_lines(path, args.start, args.end))
    print(f"{path.name}:{args.start}-{args.end}")
    print(_format_hits(hits, args.max))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

