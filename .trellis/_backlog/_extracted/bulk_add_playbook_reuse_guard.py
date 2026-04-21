#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量给 spec/iot 下的 *-playbook.md 补“复用刹车片”段落。

目标：
- 降低“检索命中后不思考直接照搬实现细节”的风险
- 不追求补齐项目专属细节，只补“上下文签名/不变式/参数清单/可替换点/停手规则”的框架

安全策略：
- 仅在文件中不存在“## 上下文签名（Context Signature”时插入
- 插入位置：文档头部 intro 后的第一条分隔线（---）之后
"""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path("spec/iot")
EXCLUDE_DIRS = {"_extracted", "_extracted_v1_20260420", "_templates"}


GUARD_BLOCK = """## 上下文签名（Context Signature，必填）

> 目的：避免“场景相似但背景不同”时照搬实现细节。

- 目标平台：ESP32/ESP32‑S3/STM32/…（按项目填写）
- SDK/版本：ESP-IDF x.y / LVGL 9.x / STM32 HAL / …（按项目填写）
- 外设/链路：UART/BLE/Wi‑Fi/TCP/UDP/屏幕/触摸/外置 Flash/…（按项目填写）
- 资源约束：Flash/RAM/PSRAM，是否允许双缓冲/大缓存（按项目填写）
- 并发模型：回调在哪个线程/任务触发；谁是 owner（UI/协议/存储）（按项目填写）

---

## 不变式（可直接复用）

- 先跑通最小闭环（冒烟）再叠功能/优化，避免不可定位
- 只直接复用“原则与边界”，实现细节必须参数化
- 必须可观测：日志/计数/错误码/抓包等至少一种证据链

---

## 参数清单（必须由当前项目提供/确认）

> 关键参数缺失时必须停手，先做 Fit Check：`spec/iot/guides/spec-reuse-safety-playbook.md`

- 常量/边界：magic、分辨率、slot 大小、最大 payload、buffer 大小等（按项目填写）
- 时序策略：超时/重试/节奏/窗口/幂等（按项目填写）
- 存储语义：写入位置、校验策略、激活/回滚策略（如适用）（按项目填写）

---

## 可替换点（示例映射，不得照搬）

- 本文若出现文件名/目录名/参数示例值：一律视为“示例”，必须先做角色映射再落地
- 角色映射建议：transport/codec/protocol/ui/persist 的 owner 边界先明确

---

## 停手规则（Stop Rules）

- 上下文签名关键项不匹配（平台/版本/外设/资源/并发模型）时，禁止照搬实施步骤
- 关键参数缺失（端序/magic/分辨率/slot/payload_size/超时重试等）时，禁止给最终实现
- 缺少可执行的最小冒烟闭环（无法验收）时，禁止继续叠功能

---

"""


def iter_playbooks() -> list[Path]:
    files: list[Path] = []
    for p in ROOT.rglob("*-playbook.md"):
        if any(d in p.parts for d in EXCLUDE_DIRS):
            continue
        files.append(p)
    return sorted(files)


def has_guard(text: str) -> bool:
    return "## 上下文签名（Context Signature" in text


def insert_after_first_hr(text: str, block: str) -> str:
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if line.strip() == "---":
            insert_at = i + 1
            # 保证分隔线后至少有一个空行
            if insert_at < len(lines) and lines[insert_at].strip() != "":
                lines.insert(insert_at, "\n")
                insert_at += 1
            lines.insert(insert_at, block)
            return "".join(lines)
    # 没有找到分隔线：退化为在标题行后插入
    if lines:
        return lines[0] + "\n" + block + "".join(lines[1:])
    return block


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="write changes to files")
    args = ap.parse_args()

    playbooks = iter_playbooks()
    changed: list[Path] = []

    for p in playbooks:
        text = p.read_text(encoding="utf-8")
        if has_guard(text):
            continue
        new_text = insert_after_first_hr(text, GUARD_BLOCK)
        if new_text != text:
            changed.append(p)
            if args.apply:
                p.write_text(new_text, encoding="utf-8", newline="\n")

    print(f"playbooks_total={len(playbooks)} changed={len(changed)} apply={bool(args.apply)}")
    for p in changed:
        print(f"- {p.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

