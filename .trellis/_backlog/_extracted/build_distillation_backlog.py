import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


@dataclass
class Proposal:
    score: int
    source_file: str
    source_lines: str
    source_title: str
    suggested_type: str
    suggested_domain: str
    suggested_path: str
    reason: str


def domain_from_counts(counts: Dict[str, int]) -> str:
    def has(k: str) -> bool:
        return counts.get(k, 0) > 0

    if has("协议") or has("状态机") or has("串口"):
        return "protocol"
    if has("OTA") or has("分区") or has("Flash"):
        return "ota"
    if has("NVS"):
        return "firmware"
    if has("LVGL") or has("UI") or has("I2C") or has("SPI"):
        return "device-ui"
    if has("Web") or has("前端") or has("后端"):
        return "host-tools"
    return "guides"


def type_from_title(title: str, counts: Dict[str, int]) -> str:
    t = title
    if "协议" in t or counts.get("协议", 0) > 0:
        return "Contract"
    if "状态机" in t:
        return "Contract"
    if "坑" in t or "踩坑" in t or counts.get("踩坑", 0) > 0:
        return "Pitfall"
    if "结论" in t or counts.get("结论", 0) > 0:
        # 结论既可能是契约，也可能是 pitfall；先保守归为指南/契约候选
        return "Decision/Pitfall"
    if "实现顺序" in t or "步骤" in t:
        return "Playbook"
    if "方案" in t:
        return "Decision"
    if "验收" in t:
        return "Checklist"
    return "Guide"


def slugify(title: str) -> str:
    # 非严格：只做简化，避免 Windows 不合法字符
    bad = '<>:"/\\\\|?*'
    s = "".join("_" if c in bad else c for c in title.strip())
    s = s.replace(" ", "-")
    s = s.replace("—", "-").replace("–", "-")
    # 控制长度
    if len(s) > 60:
        s = s[:60]
    # 太空时兜底
    return s or "untitled"


def suggest_path(domain: str, typ: str, title: str) -> str:
    base = f"spec/iot/{domain}"
    if typ.startswith("Contract"):
        return f"{base}/{slugify(title)}-contract.md"
    if typ.startswith("Pitfall"):
        return f"{base}/{slugify(title)}-pitfall.md"
    if typ.startswith("Playbook"):
        return f"{base}/{slugify(title)}-playbook.md"
    if typ.startswith("Checklist"):
        return f"{base}/{slugify(title)}-checklist.md"
    if typ.startswith("Decision"):
        return f"{base}/{slugify(title)}-decision.md"
    return f"{base}/{slugify(title)}.md"


def build() -> List[Proposal]:
    idx_path = Path("spec/iot/_extracted/source_index.json")
    data = json.loads(idx_path.read_text(encoding="utf-8"))
    proposals: List[Proposal] = []
    for f in data["files"]:
        source_file = f["file"]
        for b in f["top_blocks"]:
            score = int(b["score"])
            if score < 60:
                continue
            title = b["title"]
            counts = b["keyword_counts"]
            domain = domain_from_counts(counts)
            typ = type_from_title(title, counts)
            path = suggest_path(domain, typ, title)

            reasons: List[str] = []
            for k in ["结论", "根因", "修复", "验收", "风险", "回退", "协议", "状态机", "串口", "OTA", "NVS", "LVGL", "重构"]:
                if counts.get(k, 0) > 0:
                    reasons.append(f"{k}×{counts[k]}")
            reason = "，".join(reasons[:10]) if reasons else "高得分块"
            proposals.append(
                Proposal(
                    score=score,
                    source_file=source_file,
                    source_lines=f"{b['start_line']}-{b['end_line']}",
                    source_title=title,
                    suggested_type=typ,
                    suggested_domain=domain,
                    suggested_path=path,
                    reason=reason,
                )
            )
    proposals.sort(key=lambda p: p.score, reverse=True)
    return proposals


def main() -> None:
    out = Path("spec/iot/_extracted/distillation_backlog.md")
    props = build()
    lines: List[str] = []
    lines.append("# 蒸馏待办队列（自动生成）\n\n")
    lines.append("> 说明：基于 `source_index.json` 的 Top blocks 生成，用于保证不漏蒸馏点。实际落地时会人工合并/改名/去重。\n\n")
    lines.append("| score | 建议类型 | 建议域 | 建议文件 | 来源 | 标题 | 线索 |\n")
    lines.append("| ----: | ---- | ---- | ---- | ---- | ---- | ---- |\n")
    for p in props[:240]:
        safe_title = p.source_title.replace("|", "\\|")
        lines.append(
            f"| {p.score} | {p.suggested_type} | `{p.suggested_domain}` | `{p.suggested_path}` | `{p.source_file}`:{p.source_lines} | {safe_title} | {p.reason} |\n"
        )
    out.write_text("".join(lines), encoding="utf-8")
    print("OK")
    print("Wrote:", out)


if __name__ == "__main__":
    main()
