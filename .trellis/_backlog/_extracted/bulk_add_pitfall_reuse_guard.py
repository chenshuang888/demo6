#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量给 spec/iot 下的 *pitfall*.md 注入“复用刹车片（reuse guard）”。

Pitfall 的危险点：
- 症状相似 ≠ 根因相同；最容易被“看到像就照搬修复”误导

因此统一在文档顶部补齐：上下文签名 / 证据最小集 / 停手规则（无证据先别给最终修复）。

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

> 这是“坑点复盘（Pitfall）”。症状相似不代表根因相同。
> 如果无法对齐本节上下文与证据：**禁止**直接给“最终修复实现”，只能给排查路径与最小验证闭环。

- 目标平台：ESP32/ESP32-S3/STM32/…
- SDK/版本：ESP-IDF x.y / LVGL x.y / HAL 版本 / …
- 关键外设：LCD/Touch/I2C/SPI/UART/Wi‑Fi/BLE/…
- 资源约束：Flash/RAM/是否有 PSRAM / heap 策略
- 并发模型：谁是 single-writer？哪些回调/中断上下文？

---

## 证据最小集（必须补齐，否则只给排查清单）

- 复现步骤：最短 3~5 步
- 关键日志：至少 10 行（含时间戳/线程/错误码）
- 关键配置：`sdkconfig`/分区表/LVGL 配置/驱动配置（只列与问题相关的）
- 边界条件：是否“只在某分辨率/某字体/某 MTU/某波特率/某温度/某电源”下发生？

---

## 停手规则（Stop Rules）

- 无复现、无日志、无法确认平台/版本：不要输出最终修复，只输出“要补齐的信息 + 排查清单”。
- 修复涉及写 flash/修改分区/改并发 owner：先给最小冒烟闭环与回滚方案，再进入实现细节。
- 多个根因都解释得通：先加观测点（日志/计数器/抓包）缩小假设空间，再改代码。

---

"""


def iter_pitfalls() -> list[Path]:
    files: list[Path] = []
    for p in ROOT.rglob("*.md"):
        if any(d in p.parts for d in EXCLUDE_DIRS):
            continue
        if "pitfall" not in p.name:
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

    pitfalls = iter_pitfalls()
    changed: list[Path] = []

    for p in pitfalls:
        text = p.read_text(encoding="utf-8")
        if has_guard(text):
            continue
        new_text = insert_after_first_hr_or_title(text, GUARD_BLOCK)
        if new_text != text:
            changed.append(p)
            if args.apply:
                p.write_text(new_text, encoding="utf-8", newline="\n")

    print(f"pitfalls_total={len(pitfalls)} changed={len(changed)} apply={bool(args.apply)}")
    for p in changed:
        print(f"- {p.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

