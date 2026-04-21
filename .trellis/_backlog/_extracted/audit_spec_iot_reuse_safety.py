#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spec/iot 复用安全审查脚本（轻量、可重复执行）

做什么：
- 统计 playbook/contract/pitfall 条目数量
- 检查是否都包含 “## 上下文签名（Context Signature）” Guard
- 可选：粗略检查 Markdown 内引用的本地 .md 是否存在（链接 + 反引号路径）

用法：
  python spec/iot/_extracted/audit_spec_iot_reuse_safety.py
  python spec/iot/_extracted/audit_spec_iot_reuse_safety.py --strict
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path("spec/iot")
EXCLUDE_DIRS = {"_extracted", "_extracted_v1_20260420", "_templates"}

# 允许不同条目在标题后附加“必填”等说明，因此只匹配前缀即可
GUARD_HEADING_PREFIX = "## 上下文签名（Context Signature"


def is_excluded(p: Path) -> bool:
    return any(part in EXCLUDE_DIRS or part.startswith("_extracted") for part in p.parts)


def iter_files(glob: str) -> list[Path]:
    files: list[Path] = []
    for p in ROOT.rglob(glob):
        if is_excluded(p):
            continue
        if not p.is_file():
            continue
        files.append(p)
    return sorted(files)


def has_guard(text: str) -> bool:
    return GUARD_HEADING_PREFIX in text


@dataclass(frozen=True)
class BrokenRef:
    file: Path
    raw: str
    resolved: Path


LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
BACKTICK_MD_RE = re.compile(r"`([^`]+?\.md)`")


def iter_local_md_refs(md_text: str) -> list[str]:
    refs: list[str] = []
    for m in LINK_RE.finditer(md_text):
        target = m.group(1).strip()
        if target.startswith(("http://", "https://")):
            continue
        if ".md" not in target:
            continue
        if any(ch in target for ch in ("*", "?")):
            continue
        refs.append(target)
    for m in BACKTICK_MD_RE.finditer(md_text):
        target = m.group(1).strip()
        if ".md" not in target:
            continue
        if any(ch in target for ch in ("*", "?")):
            continue
        # 只检查“明显是路径”的引用，避免把文件名示例（如 PROJECT_README.md）误报为断链
        if not (
            target.startswith("./")
            or target.startswith("../")
            or target.startswith("spec/")
            or target.startswith("spec\\")
            or ("/" in target)
            or ("\\" in target)
        ):
            continue
        refs.append(target)
    return refs


def resolve_ref(from_file: Path, raw: str) -> Path | None:
    raw = raw.split("#", 1)[0].strip()
    raw = raw.split("?", 1)[0].strip()
    if not raw:
        return None
    # 对话蒸馏来源文件名（provenance），不是仓库内路径，跳过
    if raw.startswith("C--Users-"):
        return None
    # 仅检查相对路径的 .md 引用，避免误把“示例路径”当成仓库路径
    if re.match(r"^[A-Za-z]:\\\\", raw):
        return None
    if raw.startswith(("/", "\\")):
        return None
    # 支持以仓库根为基准的写法：spec/...（很多条目会这么写）
    if raw.startswith("spec/") or raw.startswith("spec\\"):
        candidate = (Path(raw)).resolve()
    else:
        candidate = (from_file.parent / raw).resolve()
    return candidate


def check_broken_local_refs(files: list[Path]) -> list[BrokenRef]:
    broken: list[BrokenRef] = []
    seen: set[Path] = set()
    for p in files:
        if p in seen:
            continue
        seen.add(p)
        text = p.read_text(encoding="utf-8")
        for raw in iter_local_md_refs(text):
            resolved = resolve_ref(p, raw)
            if resolved is None:
                continue
            # 限定在仓库的 spec/iot 树内（避免误报）
            try:
                resolved.relative_to(ROOT.resolve())
            except ValueError:
                continue
            if not resolved.exists():
                broken.append(BrokenRef(file=p, raw=raw, resolved=resolved))
    return broken


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true", help="exit non-zero if any check fails")
    args = ap.parse_args()

    playbooks = iter_files("*-playbook.md")
    contracts = iter_files("*-contract.md")
    pitfalls = [p for p in iter_files("*.md") if "pitfall" in p.name]

    missing_playbook = [p for p in playbooks if not has_guard(p.read_text(encoding="utf-8"))]
    missing_contract = [p for p in contracts if not has_guard(p.read_text(encoding="utf-8"))]
    missing_pitfall = [p for p in pitfalls if not has_guard(p.read_text(encoding="utf-8"))]

    print(f"playbooks_total={len(playbooks)} missing_guard={len(missing_playbook)}")
    print(f"contracts_total={len(contracts)} missing_guard={len(missing_contract)}")
    print(f"pitfalls_total={len(pitfalls)} missing_guard={len(missing_pitfall)}")

    if missing_playbook or missing_contract or missing_pitfall:
        print("\nmissing_guard_files:")
        for p in (missing_playbook + missing_contract + missing_pitfall):
            print(f"- {p.as_posix()}")

    broken = check_broken_local_refs(playbooks + contracts + pitfalls + iter_files("index.md") + iter_files("guides/*.md"))
    print(f"\nbroken_local_md_refs={len(broken)}")
    for b in broken[:30]:
        print(f"- {b.file.as_posix()} -> {b.raw} (missing: {b.resolved.as_posix()})")
    if len(broken) > 30:
        print(f"... ({len(broken) - 30} more)")

    ok = (not missing_playbook) and (not missing_contract) and (not missing_pitfall) and (not broken)
    if args.strict and not ok:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
