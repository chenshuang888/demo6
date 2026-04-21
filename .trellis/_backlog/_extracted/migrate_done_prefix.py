import argparse
import json
from pathlib import Path
from typing import Any, Dict, List, Tuple


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, obj: Dict[str, Any]) -> None:
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def calc_done_prefix_end(blocks: List[Dict[str, Any]]) -> int:
    done = sorted([(int(b["start"]), int(b["end"])) for b in blocks if b.get("status") == "done"])
    cur_end = 0
    for start, end in done:
        if start <= cur_end + 1:
            cur_end = max(cur_end, end)
        else:
            break
    return cur_end


def union_outputs(old_blocks: List[Dict[str, Any]], start: int, end: int) -> List[str]:
    outs: List[str] = []
    seen = set()
    for b in old_blocks:
        bs, be = int(b["start"]), int(b["end"])
        if bs >= start and be <= end and b.get("status") == "done":
            for o in b.get("outputs", []) or []:
                if o not in seen:
                    outs.append(o)
                    seen.add(o)
    return outs


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--old",
        type=Path,
        default=Path("spec/iot/_extracted_v1_20260420/project_tracker.json"),
        help="Old tracker path (default: spec/iot/_extracted_v1_20260420/project_tracker.json).",
    )
    ap.add_argument(
        "--new",
        type=Path,
        default=Path("spec/iot/_extracted/project_tracker.json"),
        help="New tracker path (default: spec/iot/_extracted/project_tracker.json).",
    )
    args = ap.parse_args()

    old = load_json(args.old)
    new = load_json(args.new)

    migrated = 0
    for file_name, new_file in new.get("files", {}).items():
        old_file = old.get("files", {}).get(file_name)
        if not old_file:
            continue

        prefix_end = calc_done_prefix_end(old_file.get("blocks", []))
        old_blocks = old_file.get("blocks", [])
        for nb in new_file.get("blocks", []):
            if int(nb["end"]) <= prefix_end:
                nb["status"] = "done"
                nb["outputs"] = union_outputs(old_blocks, int(nb["start"]), int(nb["end"]))
                nb["note"] = f"migrated_from_v1_prefix_end={prefix_end}"
                migrated += 1

    save_json(args.new, new)
    print(f"migrated_blocks={migrated}")


if __name__ == "__main__":
    main()

