import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List


TRACKER_PATH = Path("spec/iot/_extracted/project_tracker.json")
INDEX_PATH = Path("spec/iot/_extracted/source_index.json")


@dataclass(frozen=True)
class BlockKey:
    file: str
    start_line: int
    end_line: int
    title: str

    def id(self) -> str:
        safe_title = self.title.replace("|", " ")
        return f"{self.file}:{self.start_line}-{self.end_line}:{safe_title}"


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, obj: Dict[str, Any]) -> None:
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="+", required=True, help="File names (basename) to add into tracker.")
    ap.add_argument("--force", action="store_true", help="Overwrite existing file entries (DANGEROUS).")
    args = ap.parse_args()

    tracker = load_json(TRACKER_PATH)
    index = load_json(INDEX_PATH)
    files_by_name = {f["file"]: f for f in index.get("files", [])}

    changed = False
    for fn in args.files:
        entry = files_by_name.get(fn)
        if not entry:
            raise SystemExit(f"Missing in source index: {fn}")

        exists = fn in tracker.get("files", {})
        if exists and not args.force:
            print(f"Skip (exists): {fn}")
            continue

        blocks: List[Dict[str, Any]] = []
        for b in entry.get("blocks", []):
            key = BlockKey(
                file=fn,
                start_line=int(b["start_line"]),
                end_line=int(b["end_line"]),
                title=b.get("title", ""),
            )
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

        tracker.setdefault("files", {})[fn] = {
            "meta": entry.get("meta", {}),
            "lines": entry.get("lines"),
            "size_bytes": entry.get("size_bytes"),
            "blocks": blocks,
        }
        changed = True
        print(f"Added: {fn} blocks={len(blocks)}")

    if changed:
        save_json(TRACKER_PATH, tracker)
        print("OK: wrote tracker", TRACKER_PATH)
    else:
        print("OK: no changes")


if __name__ == "__main__":
    main()

