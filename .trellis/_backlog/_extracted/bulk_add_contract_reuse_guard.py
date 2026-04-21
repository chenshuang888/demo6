#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量给 spec/iot 下的 *-contract.md 注入“复用刹车片（reuse guard）”。

目标：
- 避免把“某项目契约细节”当成“必须如此”的硬规则直接照搬到新项目
- 统一在契约文档顶部补齐：上下文签名 / 不变式 / 参数清单 / 停手规则

说明：
- 仅当文件内不存在 `## 上下文签名（Context Signature）` 时才会注入（幂等）
- 默认只打印将改动的文件；加 `--apply` 才会写回
"""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path("spec/iot")
EXCLUDE_DIRS = {"_extracted", "_extracted_v1_20260420", "_templates"}


GUARD_BLOCK = """## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---

"""


def iter_contracts() -> list[Path]:
    files: list[Path] = []
    for p in ROOT.rglob("*-contract.md"):
        if any(d in p.parts for d in EXCLUDE_DIRS):
            continue
        files.append(p)
    return sorted(files)


def has_guard(text: str) -> bool:
    return "## 上下文签名（Context Signature）" in text


def insert_after_first_hr_or_title(text: str, block: str) -> str:
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if line.strip() == "---":
            insert_at = i + 1
            if insert_at < len(lines) and lines[insert_at].strip() != "":
                lines.insert(insert_at, "\n")
                insert_at += 1
            lines.insert(insert_at, block)
            return "".join(lines)
    if lines:
        return lines[0] + "\n" + block + "".join(lines[1:])
    return block


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="write changes to files")
    args = ap.parse_args()

    contracts = iter_contracts()
    changed: list[Path] = []

    for p in contracts:
        text = p.read_text(encoding="utf-8")
        if has_guard(text):
            continue
        new_text = insert_after_first_hr_or_title(text, GUARD_BLOCK)
        if new_text != text:
            changed.append(p)
            if args.apply:
                p.write_text(new_text, encoding="utf-8", newline="\n")

    print(f"contracts_total={len(contracts)} changed={len(changed)} apply={bool(args.apply)}")
    for p in changed:
        print(f"- {p.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

