import argparse
import json
from pathlib import Path
from typing import Any, Dict, List, Optional


TRACKER_PATH = Path("spec/iot/_extracted/project_tracker.json")


def load_tracker() -> Dict[str, Any]:
    return json.loads(TRACKER_PATH.read_text(encoding="utf-8"))


def save_tracker(tracker: Dict[str, Any]) -> None:
    TRACKER_PATH.write_text(json.dumps(tracker, ensure_ascii=False, indent=2), encoding="utf-8")


def find_block(tracker: Dict[str, Any], file: str, start: int, end: int) -> Optional[Dict[str, Any]]:
    f = tracker["files"].get(file)
    if not f:
        return None
    for b in f["blocks"]:
        if int(b["start"]) == start and int(b["end"]) == end:
            return b
    return None


def cmd_next(args: argparse.Namespace) -> None:
    tracker = load_tracker()
    f = tracker["files"].get(args.file)
    if not f:
        raise SystemExit(f"Unknown file: {args.file}")
    blocks = f["blocks"]
    pending = [b for b in blocks if b["status"] == "pending"]
    pending.sort(key=lambda b: (b["start"], b["end"]))
    for b in pending[: args.n]:
        print(f"{args.file}:{b['start']}-{b['end']} score={b['score']} title={b['title']}")


def cmd_mark(args: argparse.Namespace) -> None:
    tracker = load_tracker()
    b = find_block(tracker, args.file, args.start, args.end)
    if not b:
        raise SystemExit("Block not found. Check file/start/end.")
    if args.output:
        for o in args.output:
            if o not in b["outputs"]:
                b["outputs"].append(o)
    if args.note:
        b["note"] = args.note
    b["status"] = args.status
    save_tracker(tracker)
    print("OK")


def cmd_summary(args: argparse.Namespace) -> None:
    tracker = load_tracker()
    files = args.files or list(tracker["files"].keys())
    for fn in files:
        f = tracker["files"].get(fn)
        if not f:
            continue
        blocks = f["blocks"]
        total = len(blocks)
        done = sum(1 for b in blocks if b["status"] == "done")
        skip = sum(1 for b in blocks if b["status"] == "skipped")
        pending = total - done - skip
        print(f"{fn}: total={total} done={done} skipped={skip} pending={pending}")


def main() -> None:
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)

    p_next = sub.add_parser("next")
    p_next.add_argument("--file", required=True)
    p_next.add_argument("-n", type=int, default=10)
    p_next.set_defaults(func=cmd_next)

    p_mark = sub.add_parser("mark")
    p_mark.add_argument("--file", required=True)
    p_mark.add_argument("--start", type=int, required=True)
    p_mark.add_argument("--end", type=int, required=True)
    p_mark.add_argument("--status", choices=["pending", "done", "skipped"], required=True)
    p_mark.add_argument("--output", action="append", default=[])
    p_mark.add_argument("--note")
    p_mark.set_defaults(func=cmd_mark)

    p_sum = sub.add_parser("summary")
    p_sum.add_argument("--files", nargs="*")
    p_sum.set_defaults(func=cmd_summary)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

