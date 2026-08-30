#!/usr/bin/env python3
"""Run the harness once per PrevisFixSites spec and tabulate the result.

Each run rewrites PrevisFixSites in the deployed INI, launches the game,
captures previs-ON (with those sites neutralised) against previs-OFF, and
records how much of the defect remains.  `strongPixels` -- pixels differing by
>= 20 on any channel -- is the signal to watch: the glass tint produces tens of
thousands, temporal noise a few hundred.

  python tools/bisect_previs.py none 0-18 19-37
  python tools/bisect_previs.py 20 --repeat 2

Results are archived per spec under reference/bisect/<spec>/ so captures are
not lost to the next run.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4")
INI = GAME / "Data" / "F4SE" / "Plugins" / "fo4test.ini"
OUT = Path.home() / "Documents" / "My Games" / "Fallout4" / "F4SE" / "fo4test"
ARCHIVE = REPO / "reference" / "bisect"
RUNNER = REPO / "tools" / "run_harness.ps1"


def set_spec(spec: str) -> None:
    text = INI.read_text(encoding="utf-8", errors="replace")
    new, n = re.subn(r"(?im)^PrevisFixSites\s*=.*$", f"PrevisFixSites = {spec}", text)
    if n == 0:
        new = text.rstrip() + f"\nPrevisFixSites = {spec}\n"
    INI.write_text(new, encoding="utf-8")


def run_once(spec: str, timeout_min: int) -> dict | None:
    set_spec(spec)
    proc = subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(RUNNER),
         "-NoDeploy", "-TimeoutMinutes", str(timeout_min)],
        capture_output=True, text=True)

    comparison = OUT / "comparison.json"
    if not comparison.is_file():
        print(f"  !! no comparison.json for {spec!r}")
        print("  " + "\n  ".join(proc.stdout.strip().splitlines()[-6:]))
        return None

    stats = json.loads(comparison.read_text(encoding="utf-8"))

    dest = ARCHIVE / spec.replace(",", "_").replace("-", "to")
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("01_previs_on.bmp", "02_previs_off.bmp", "03_diff.bmp",
                 "comparison.json", "result.json"):
        src = OUT / name
        if src.is_file():
            shutil.copy2(src, dest / name)
    return stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("specs", nargs="+")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--timeout-min", type=int, default=15)
    args = ap.parse_args()

    rows = []
    for spec in args.specs:
        for attempt in range(args.repeat):
            label = spec if args.repeat == 1 else f"{spec}#{attempt + 1}"
            print(f"\n=== {label} ===", flush=True)
            stats = run_once(spec, args.timeout_min)
            if stats is None:
                rows.append((label, None))
                continue
            print(f"  strong>=20 {stats['strongPixels']:>7,}   "
                  f"max {stats['maxChannelDelta']:>3}   "
                  f"mean {stats['meanDeltaOverChanged']:.2f}", flush=True)
            rows.append((label, stats))

    print("\n" + "=" * 66)
    print(f"{'spec':<16} {'strong>=20':>11} {'max':>5} {'mean':>7} {'changed%':>9}")
    print("-" * 66)
    for label, s in rows:
        if s is None:
            print(f"{label:<16} {'FAILED':>11}")
            continue
        print(f"{label:<16} {s['strongPixels']:>11,} {s['maxChannelDelta']:>5} "
              f"{s['meanDeltaOverChanged']:>7.2f} {s['changedFraction'] * 100:>8.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
