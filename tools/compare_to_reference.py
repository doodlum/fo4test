#!/usr/bin/env python3
"""Compare each archived run's previs-ON capture against a fixed reference.

Comparing 01 against 02 *within* a run is degenerate once the predicate is
patched: both captures then take the previs-OFF path, so they match because
previs was effectively disabled, not because the lighting was fixed.

The honest test is candidate-vs-reference, where the reference is the
unpatched previs-OFF frame (reference/bisect/none/02_previs_off.bmp) -- the
way the glass is supposed to look.  A real fix renders previs-ON like that.

  python tools/compare_to_reference.py [run ...]      # default: all archived
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_captures import STRONG_DELTA, compare  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
ARCHIVE = REPO / "reference" / "bisect"
REFERENCE = ARCHIVE / "none" / "02_previs_off.bmp"


def main() -> int:
    if not REFERENCE.is_file():
        print(f"missing reference {REFERENCE}")
        print("run: python tools/bisect_previs.py none")
        return 2

    runs = sys.argv[1:]
    if not runs:
        runs = sorted(p.name for p in ARCHIVE.iterdir() if p.is_dir())

    print(f"reference: {REFERENCE.relative_to(REPO)}  (previs OFF, unpatched)\n")
    print(f"{'run':<14} {'strong>=' + str(STRONG_DELTA):>12} {'max':>5} {'mean':>7} "
          f"{'changed%':>9}   vs reference")
    print("-" * 74)

    rows = []
    for name in runs:
        cand = ARCHIVE / name / "01_previs_on.bmp"
        if not cand.is_file():
            print(f"{name:<14} {'(missing)':>12}")
            continue
        stats = compare(cand, REFERENCE, ARCHIVE / name / "04_vs_reference.bmp")
        rows.append((name, stats))
        print(f"{name:<14} {stats['strongPixels']:>12,} {stats['maxChannelDelta']:>5} "
              f"{stats['meanDeltaOverChanged']:>7.2f} "
              f"{stats['changedFraction'] * 100:>8.2f}%")

    if rows:
        best = min(rows, key=lambda r: r[1]["strongPixels"])
        print(f"\nclosest to reference: {best[0]} "
              f"({best[1]['strongPixels']:,} strong pixels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
