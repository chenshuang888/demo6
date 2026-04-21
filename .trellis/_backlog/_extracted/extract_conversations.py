import argparse
import json
import re
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


RE_HEADING = re.compile(r"^(#{1,6})\s+(.+?)\s*$")
RE_CODE_FENCE = re.compile(r"^\s*```")
# 典型形态：### [2026-04-08T09:36:25] [a16ce503] USER
RE_TIMESTAMP_TITLE = re.compile(
    r"^\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\]\s+\[[0-9a-f]{8}\]\s+(?:USER|ASST)(?:-SUB)?\s*$"
)


KEYWORDS: Dict[str, str] = {
    # 高信号结构词
    "结论": "结论",
    "最终": "最终",
    "根因": "根因",
    "原因": "原因",
    "修复": "修复",
    "验收": "验收",
    "风险": "风险",
    "回退": "回退",
    "建议": "建议",
    "方案": "方案",
    "TODO": "TODO",
    "踩坑": "踩坑",
    "复盘": "复盘",
    # IoT/嵌入式主题词（用于分域）
    "协议": "protocol",
    "状态机": "state_machine",
    "串口": "uart",
    "OTA": "ota",
    "分区": "partition",
    "Flash": "flash",
    "NVS": "nvs",
    "BLE": "ble",
    "WiFi": "wifi",
    "MQTT": "mqtt",
    "HTTP": "http",
    "LVGL": "lvgl",
    "I2C": "i2c",
    "SPI": "spi",
    "UI": "ui",
    "Web": "web",
    "前端": "frontend",
    "后端": "backend",
    "重构": "refactor",
}


def count_keywords(text: str) -> Dict[str, int]:
    counts: Dict[str, int] = {}
    for k in KEYWORDS.keys():
        counts[k] = text.count(k)
    return counts


def score_block(counts: Dict[str, int]) -> int:
    # 评分策略：结构词权重大于主题词；可调。
    weight: Dict[str, int] = {
        "结论": 6,
        "最终": 5,
        "根因": 6,
        "原因": 3,
        "修复": 5,
        "验收": 5,
        "风险": 4,
        "回退": 4,
        "建议": 3,
        "方案": 2,
        "TODO": 2,
        "踩坑": 5,
        "复盘": 5,
        # 主题词轻权重
        "协议": 2,
        "状态机": 2,
        "串口": 2,
        "OTA": 3,
        "分区": 2,
        "Flash": 2,
        "NVS": 3,
        "BLE": 2,
        "WiFi": 2,
        "MQTT": 2,
        "HTTP": 2,
        "LVGL": 2,
        "I2C": 2,
        "SPI": 2,
        "UI": 1,
        "Web": 1,
        "前端": 1,
        "后端": 1,
        "重构": 2,
    }
    total = 0
    for k, v in counts.items():
        total += weight.get(k, 0) * v
    return total


def extract_meta(head_lines: List[str]) -> Dict[str, Any]:
    meta: Dict[str, Any] = {}
    for line in head_lines:
        if "Time range" in line:
            ts = re.findall(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}", line)
            if len(ts) >= 2:
                meta["start"] = ts[0]
                meta["end"] = ts[1]
        if "USER / ASST (main)" in line:
            m = re.search(r"(\d+)\s*/\s*(\d+)", line)
            if m:
                meta["main_user"] = int(m.group(1))
                meta["main_asst"] = int(m.group(2))
        if "USER-SUB / ASST-SUB (subagents)" in line:
            m = re.search(r"(\d+)\s*/\s*(\d+)", line)
            if m:
                meta["sub_user"] = int(m.group(1))
                meta["sub_asst"] = int(m.group(2))
        if "Clean text bytes" in line:
            m = re.search(r"(\d[\d,]*)", line)
            if m:
                meta["clean_text_bytes"] = m.group(1)
    return meta


@dataclass
class Block:
    kind: str  # heading | preface
    level: int
    title: str
    start_line: int  # 1-based
    end_line: int  # 1-based, inclusive
    code_fences: int
    keyword_counts: Dict[str, int]
    score: int


@dataclass
class FileIndex:
    file: str
    size_bytes: int
    lines: int
    encoding: str
    meta: Dict[str, Any]
    blocks: List[Block]
    top_blocks: List[Block]


def split_blocks(
    lines: List[str],
    *,
    max_heading_level: int = 1,
    ignore_timestamp_headings: bool = True,
) -> List[Tuple[int, int, int, str]]:
    """
    Return list of (start_idx, end_idx_exclusive, level, title) blocks by heading.
    Includes a preface block before first heading if needed.
    """
    headings: List[Tuple[int, int, str]] = []
    for i, line in enumerate(lines):
        m = RE_HEADING.match(line)
        if m:
            level = len(m.group(1))
            title = m.group(2)
            if level > max_heading_level:
                continue
            if ignore_timestamp_headings and RE_TIMESTAMP_TITLE.match(title):
                continue
            headings.append((i, level, title))

    blocks: List[Tuple[int, int, int, str]] = []
    if not headings:
        blocks.append((0, len(lines), 0, "（无标题）"))
        return blocks

    first_idx = headings[0][0]
    if first_idx > 0:
        blocks.append((0, first_idx, 0, "（前言/无标题区）"))

    for h_i, (start, level, title) in enumerate(headings):
        end = headings[h_i + 1][0] if h_i + 1 < len(headings) else len(lines)
        blocks.append((start, end, level, title))
    return blocks


def count_code_fences(block_lines: List[str]) -> int:
    return sum(1 for l in block_lines if RE_CODE_FENCE.match(l))


def build_index_for_file(
    path: Path,
    *,
    top_n: int = 60,
    max_heading_level: int = 1,
    ignore_timestamp_headings: bool = True,
) -> FileIndex:
    raw = path.read_bytes()
    encoding = "utf-8"
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        # Windows 对话转储中经常混有 GBK/GB18030
        encoding = "gb18030"
        try:
            text = raw.decode("gb18030")
        except UnicodeDecodeError:
            encoding = "utf-8-replace"
            text = raw.decode("utf-8", errors="replace")
    lines = text.splitlines()
    meta = extract_meta(lines[:80])

    raw_blocks = split_blocks(
        lines,
        max_heading_level=max_heading_level,
        ignore_timestamp_headings=ignore_timestamp_headings,
    )
    blocks: List[Block] = []
    for start, end, level, title in raw_blocks:
        block_text = "\n".join(lines[start:end])
        kw_counts = count_keywords(block_text)
        fences = count_code_fences(lines[start:end])
        sc = score_block(kw_counts) + fences  # fence 轻微加分，通常代表“可操作内容”
        blocks.append(
            Block(
                kind="heading" if level > 0 else "preface",
                level=level,
                title=title,
                start_line=start + 1,
                end_line=max(start + 1, end),
                code_fences=fences,
                keyword_counts=kw_counts,
                score=sc,
            )
        )

    top_blocks = sorted(blocks, key=lambda b: b.score, reverse=True)[:top_n]
    return FileIndex(
        file=path.name,
        size_bytes=path.stat().st_size,
        lines=len(lines),
        encoding=encoding,
        meta=meta,
        blocks=blocks,
        top_blocks=top_blocks,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--max-heading-level",
        type=int,
        default=1,
        help="Split blocks by headings up to this level (default: 1).",
    )
    ap.add_argument(
        "--keep-timestamp-headings",
        action="store_true",
        help="Do not ignore timestamp headings like '### [2026-..] [hash] USER'.",
    )
    ap.add_argument(
        "--top-n",
        type=int,
        default=60,
        help="How many top-scored blocks to include in the report (default: 60).",
    )
    args = ap.parse_args()

    root = Path(".")
    out_dir = Path("spec/iot/_extracted")
    out_dir.mkdir(parents=True, exist_ok=True)

    # 只索引“对话记录”，避免把 spec 文档也当语料
    md_files = sorted([p for p in root.glob("*.md") if p.is_file()], key=lambda p: p.stat().st_size, reverse=True)

    indices: List[FileIndex] = []
    for p in md_files:
        indices.append(
            build_index_for_file(
                p,
                top_n=args.top_n,
                max_heading_level=args.max_heading_level,
                ignore_timestamp_headings=not args.keep_timestamp_headings,
            )
        )

    # JSON 索引（可机器处理）
    json_obj = {
        "version": 1,
        "source_root": str(root.resolve()),
        "files": [
            {
                **{k: v for k, v in asdict(idx).items() if k not in ["blocks", "top_blocks"]},
                "blocks": [asdict(b) for b in idx.blocks],
                "top_blocks": [asdict(b) for b in idx.top_blocks],
            }
            for idx in indices
        ],
    }
    (out_dir / "source_index.json").write_text(json.dumps(json_obj, ensure_ascii=False, indent=2), encoding="utf-8")

    # Markdown 覆盖报告（人可读）
    lines_out: List[str] = []
    lines_out.append("# 对话记录通读覆盖报告\n")
    lines_out.append("> 说明：本报告由脚本对每个 `.md` 对话文件从头到尾读取并切块生成，用于后续蒸馏不漏内容。\n")
    lines_out.append("\n## 文件概览\n")
    lines_out.append("| 文件 | 行数 | 大小(bytes) | 时间范围 | main(U/A) | sub(U/A) | top块数 |\n")
    lines_out.append("| ---- | ----: | ----: | ---- | ---- | ---- | ----: |\n")
    for idx in indices:
        tr = ""
        if idx.meta.get("start") and idx.meta.get("end"):
            tr = f"{idx.meta['start']} → {idx.meta['end']}"
        main_ua = ""
        if "main_user" in idx.meta and "main_asst" in idx.meta:
            main_ua = f"{idx.meta['main_user']}/{idx.meta['main_asst']}"
        sub_ua = ""
        if "sub_user" in idx.meta and "sub_asst" in idx.meta:
            sub_ua = f"{idx.meta['sub_user']}/{idx.meta['sub_asst']}"
        lines_out.append(
            f"| `{idx.file}` | {idx.lines} | {idx.size_bytes} | {tr} | {main_ua} | {sub_ua} | {len(idx.top_blocks)} |\n"
        )

    lines_out.append("\n## 每个文件的高信号块（Top 20 标题）\n")
    for idx in indices:
        lines_out.append(f"\n### `{idx.file}`\n")
        lines_out.append("| score | lvl | 起止行 | 代码块 | 标题 |\n")
        lines_out.append("| ----: | --: | ---- | ----: | ---- |\n")
        for b in idx.top_blocks[:20]:
            safe_title = b.title.replace("|", "\\|")
            lines_out.append(
                f"| {b.score} | {b.level} | {b.start_line}-{b.end_line} | {b.code_fences} | {safe_title} |\n"
            )

    (out_dir / "source_coverage_report.md").write_text("".join(lines_out), encoding="utf-8")

    print("OK")
    print("Indexed files:", len(indices))
    print("Wrote:", out_dir / "source_index.json")
    print("Wrote:", out_dir / "source_coverage_report.md")


if __name__ == "__main__":
    main()
