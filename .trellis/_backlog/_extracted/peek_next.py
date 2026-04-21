import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List


TRACKER_PATH = Path("spec/iot/_extracted/project_tracker.json")


def load_tracker() -> Dict[str, Any]:
    return json.loads(TRACKER_PATH.read_text(encoding="utf-8"))


def load_lines(file_path: str) -> List[str]:
    return Path(file_path).read_text(encoding="utf-8").splitlines()


def clean_snippet(lines: List[str], start: int, end: int, max_lines: int) -> List[str]:
    chunk = lines[start - 1 : end]
    out: List[str] = []
    for l in chunk:
        if l.strip() == "":
            continue
        out.append(l)
        if len(out) >= max_lines:
            break
    return out


def main() -> None:
    sys.stdout.reconfigure(encoding="gbk", errors="replace")

    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True, help="Conversation md filename in repo root.")
    ap.add_argument("-n", type=int, default=10, help="How many pending blocks to show.")
    ap.add_argument("--snippet-lines", type=int, default=8, help="Non-empty lines to show per block.")
    args = ap.parse_args()

    tracker = load_tracker()
    f = tracker["files"].get(args.file)
    if not f:
        raise SystemExit(f"Unknown file: {args.file}")

    blocks = [b for b in f["blocks"] if b.get("status") == "pending"]
    blocks.sort(key=lambda b: (int(b["start"]), int(b["end"])))

    lines = load_lines(args.file)

    for b in blocks[: args.n]:
        start, end = int(b["start"]), int(b["end"])
        length = end - start + 1
        print(f"{args.file}:{start}-{end} len={length} score={b.get('score')} title={b.get('title')}")
        snippet = clean_snippet(lines, start, end, args.snippet_lines)
        for s in snippet:
            print(f"  {s}")
        print()


if __name__ == "__main__":
    main()

