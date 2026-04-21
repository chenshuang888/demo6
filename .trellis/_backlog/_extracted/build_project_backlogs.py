import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


TARGET_FILES = [
    "C--Users-ChenShuang-Desktop-esp32-demo5.md",
    "C--Users-ChenShuang-Desktop-OLED-----.md",
    "C--Users-ChenShuang-Desktop-esp32-demo6.md",
]


@dataclass(frozen=True)
class BlockKey:
    file: str
    start_line: int
    end_line: int
    title: str

    def id(self) -> str:
        # stable-ish id
        safe_title = self.title.replace("|", " ")
        return f"{self.file}:{self.start_line}-{self.end_line}:{safe_title}"


def load_index() -> Dict:
    return json.loads(Path("spec/iot/_extracted/source_index.json").read_text(encoding="utf-8"))


def classify_domain(counts: Dict[str, int]) -> str:
    if counts.get("协议", 0) or counts.get("状态机", 0) or counts.get("串口", 0):
        return "protocol"
    if counts.get("OTA", 0) or counts.get("分区", 0) or counts.get("Flash", 0):
        return "ota"
    if counts.get("NVS", 0) or counts.get("Flash", 0):
        return "firmware"
    if counts.get("LVGL", 0) or counts.get("UI", 0) or counts.get("字体", 0):
        return "device-ui"
    return "guides"


def build_backlog_for_file(file_entry: Dict, top_only: bool = False) -> List[Dict]:
    blocks = file_entry["top_blocks"] if top_only else file_entry["blocks"]
    rows: List[Dict] = []
    for b in blocks:
        counts = b.get("keyword_counts", {})
        rows.append(
            {
                "score": int(b.get("score", 0)),
                "level": int(b.get("level", 0)),
                "start": int(b["start_line"]),
                "end": int(b["end_line"]),
                "title": b["title"],
                "domain": classify_domain(counts),
                "code_fences": int(b.get("code_fences", 0)),
                "signals": {k: counts.get(k, 0) for k in ["结论", "根因", "修复", "验收", "风险", "回退", "协议", "状态机", "串口", "OTA", "NVS", "LVGL", "重构"]},
            }
        )
    rows.sort(key=lambda r: (r["score"], r["code_fences"]), reverse=True)
    return rows


def write_backlog_md(file_entry: Dict, rows: List[Dict], *, order_name: str) -> Path:
    out_dir = Path("spec/iot/_extracted")
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"backlog_{order_name}__{file_entry['file']}.md"

    meta = file_entry.get("meta", {})
    lines: List[str] = []
    lines.append(f"# 蒸馏待办（全量块）— `{file_entry['file']}`\n\n")
    if order_name == "score":
        lines.append("> 说明：这是“从头到尾切块”的全量清单（按 score 排序）。用于优先抓最值钱的块。\n\n")
    else:
        lines.append("> 说明：这是“从头到尾切块”的全量清单（按文件顺序）。用于按顺序逐块过一遍，避免漏。\n\n")
    lines.append(f"- 行数：{file_entry.get('lines')}\n")
    lines.append(f"- 文件大小：{file_entry.get('size_bytes')} bytes\n")
    if meta.get("start") and meta.get("end"):
        lines.append(f"- 时间范围：{meta['start']} → {meta['end']}\n")
    if "main_user" in meta and "main_asst" in meta:
        lines.append(f"- main(U/A)：{meta['main_user']}/{meta['main_asst']}\n")
    if "sub_user" in meta and "sub_asst" in meta:
        lines.append(f"- sub(U/A)：{meta['sub_user']}/{meta['sub_asst']}\n")
    lines.append("\n## 块清单（按 score 降序）\n")
    lines.append("| score | 域 | lvl | 起止行 | 代码块 | 标题 | 结构信号 |\n")
    lines.append("| ----: | --- | --: | ---- | ----: | ---- | ---- |\n")
    for r in rows:
        sig = " ".join([f"{k}×{v}" for k, v in r["signals"].items() if v])
        title = r["title"].replace("|", "\\|")
        lines.append(
            f"| {r['score']} | `{r['domain']}` | {r['level']} | {r['start']}-{r['end']} | {r['code_fences']} | {title} | {sig} |\n"
        )

    out.write_text("".join(lines), encoding="utf-8")
    return out


def write_tracker(indices: List[Dict]) -> Path:
    out = Path("spec/iot/_extracted/project_tracker.json")
    tracker: Dict[str, Dict] = {"version": 1, "files": {}}
    for f in indices:
        file_name = f["file"]
        blocks = []
        for b in f["blocks"]:
            key = BlockKey(file=file_name, start_line=int(b["start_line"]), end_line=int(b["end_line"]), title=b["title"])
            blocks.append(
                {
                    "id": key.id(),
                    "start": key.start_line,
                    "end": key.end_line,
                    "title": key.title,
                    "score": int(b.get("score", 0)),
                    "status": "pending",
                    "outputs": [],
                }
            )
        tracker["files"][file_name] = {
            "meta": f.get("meta", {}),
            "lines": f.get("lines"),
            "size_bytes": f.get("size_bytes"),
            "blocks": blocks,
        }
    out.write_text(json.dumps(tracker, ensure_ascii=False, indent=2), encoding="utf-8")
    return out


def main() -> None:
    data = load_index()
    files = [f for f in data["files"] if f["file"] in TARGET_FILES]
    files_by_name = {f["file"]: f for f in files}

    missing = [f for f in TARGET_FILES if f not in files_by_name]
    if missing:
        raise SystemExit(f"Missing in index: {missing}")

    for f in TARGET_FILES:
        entry = files_by_name[f]
        rows_score = build_backlog_for_file(entry, top_only=False)
        out1 = write_backlog_md(entry, rows_score, order_name="score")
        print("Wrote backlog:", out1)

        rows_seq = sorted(rows_score, key=lambda r: (r["start"], r["end"], -r["score"]))
        out2 = write_backlog_md(entry, rows_seq, order_name="seq")
        print("Wrote backlog:", out2)

    tracker_path = write_tracker(files)
    print("Wrote tracker:", tracker_path)


if __name__ == "__main__":
    main()
