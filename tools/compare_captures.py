#!/usr/bin/env python3
"""Compare the harness's two captures and report how much previs changed.

Reads the 32-bit BGRA .bmp files written by Capture::ToBMP, so there is no
imaging dependency -- pure stdlib, works on whatever Python the machine has.

Usage:
  python tools/compare_captures.py [output-dir]

`output-dir` defaults to Documents/My Games/Fallout4/F4SE/fo4test.
Exit codes: 0 = the captures differ (the toggle did something),
            1 = identical (nothing changed -- suspicious),
            2 = the run itself failed / files missing.
"""
from __future__ import annotations

import json
import os
import struct
import sys
from pathlib import Path

# A capture pair that differs by less than this fraction of pixels is treated
# as noise (post-process dither, a clock in the HUD if `tm` did not take).
NOISE_FLOOR = 0.0005  # 0.05% of pixels

# A per-channel delta at or above this counts as a real difference rather
# than temporal noise.  Chosen from the measured baseline, where the glass
# tint peaks at 108 while the rest of the frame sits at 1-3.
STRONG_DELTA = 20


def default_output_dir() -> Path:
    return (Path(os.path.expanduser("~")) / "Documents" / "My Games" / "Fallout4"
            / "F4SE" / "fo4test")


def read_bmp(path: Path) -> tuple[int, int, bytearray]:
    """Return (width, height, BGRA rows top-down) for a 32bpp BI_RGB bitmap."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path.name}: not a bitmap")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size, width, height, planes, bpp, compression = struct.unpack_from(
        "<IiiHHI", data, 14)
    if header_size < 40:
        raise ValueError(f"{path.name}: unsupported DIB header size {header_size}")
    if bpp != 32 or compression != 0:
        raise ValueError(f"{path.name}: expected 32bpp BI_RGB, got {bpp}bpp "
                         f"compression={compression}")

    top_down = height < 0
    height = abs(height)
    stride = width * 4
    needed = pixel_offset + stride * height
    if len(data) < needed:
        raise ValueError(f"{path.name}: truncated ({len(data)} bytes, need {needed})")

    pixels = bytearray(data[pixel_offset:pixel_offset + stride * height])
    if not top_down:
        rows = [pixels[y * stride:(y + 1) * stride] for y in range(height)]
        rows.reverse()
        pixels = bytearray(b"".join(rows))

    return width, height, pixels


def write_bmp(path: Path, width: int, height: int, bgra: bytes) -> None:
    stride = width * 4
    size = stride * height
    header = b"BM" + struct.pack("<IHHI", 54 + size, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, size,
                          2835, 2835, 0, 0)
    path.write_bytes(header + bgra)


def compare(before: Path, after: Path, diff_out: Path) -> dict:
    w1, h1, a = read_bmp(before)
    w2, h2, b = read_bmp(after)
    if (w1, h1) != (w2, h2):
        raise ValueError(f"resolution mismatch: {w1}x{h1} vs {w2}x{h2}")

    total = w1 * h1
    changed = 0
    max_delta = 0
    sum_delta = 0
    strong = 0
    sum_db = sum_dg = sum_dr = 0
    diff = bytearray(len(a))

    for i in range(0, len(a), 4):
        db = abs(a[i] - b[i])
        dg = abs(a[i + 1] - b[i + 1])
        dr = abs(a[i + 2] - b[i + 2])
        delta = db if db > dg else dg
        if dr > delta:
            delta = dr
        if delta:
            changed += 1
            sum_delta += delta
            sum_db += db
            sum_dg += dg
            sum_dr += dr
            if delta > max_delta:
                max_delta = delta
            # The glass tint is a large, systematic shift; TAA/dither noise is
            # a scattering of 1-3 level differences.  Counting only sizeable
            # deltas separates the defect from the background chatter, which
            # matters when bisecting -- overall means move very little.
            if delta >= STRONG_DELTA:
                strong += 1
        amped = 255 if delta * 8 > 255 else delta * 8
        diff[i] = amped
        diff[i + 1] = amped
        diff[i + 2] = amped
        diff[i + 3] = 255

    write_bmp(diff_out, w1, h1, bytes(diff))

    return {
        "width": w1,
        "height": h1,
        "pixels": total,
        "changedPixels": changed,
        "changedFraction": changed / total if total else 0.0,
        "maxChannelDelta": max_delta,
        "meanDeltaOverChanged": (sum_delta / changed) if changed else 0.0,
        "strongPixels": strong,
        "strongFraction": strong / total if total else 0.0,
        "meanChannelDelta": {
            "b": (sum_db / changed) if changed else 0.0,
            "g": (sum_dg / changed) if changed else 0.0,
            "r": (sum_dr / changed) if changed else 0.0,
        },
    }


def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else default_output_dir()

    result_path = out_dir / "result.json"
    if not result_path.is_file():
        print(f"FAIL: {result_path} not found -- the harness did not finish")
        return 2

    result = json.loads(result_path.read_text(encoding="utf-8"))
    if not result.get("ok"):
        print(f"FAIL: harness reported failure: {result.get('failure')!r}")
        return 2

    before = out_dir / result["before"]
    after = out_dir / result["after"]
    for path in (before, after):
        if not path.is_file():
            print(f"FAIL: missing capture {path}")
            return 2

    stats = compare(before, after, out_dir / "03_diff.bmp")
    stats.update({
        "cell": result.get("cell"),
        "cellFormID": f"{result.get('cellFormID', 0):08X}",
        "toggleCommand": result.get("toggleCommand"),
        "player": [result.get("playerX"), result.get("playerY"), result.get("playerZ")],
    })

    (out_dir / "comparison.json").write_text(
        json.dumps(stats, indent=2), encoding="utf-8")

    pct = stats["changedFraction"] * 100.0
    print(f"cell            : {stats['cell']} ({stats['cellFormID']})")
    print(f"player          : {stats['player']}")
    print(f"toggle          : {stats['toggleCommand']}")
    print(f"resolution      : {stats['width']}x{stats['height']}")
    print(f"changed pixels  : {stats['changedPixels']:,} / {stats['pixels']:,} ({pct:.3f}%)")
    print(f"strong (>={STRONG_DELTA:2d})   : {stats['strongPixels']:,} "
          f"({stats['strongFraction'] * 100.0:.3f}%)   <-- the defect signal")
    print(f"max channel diff: {stats['maxChannelDelta']}")
    print(f"mean diff       : {stats['meanDeltaOverChanged']:.2f} (over changed pixels)")
    mc = stats["meanChannelDelta"]
    print(f"mean per channel: B {mc['b']:.2f}  G {mc['g']:.2f}  R {mc['r']:.2f}")
    print(f"diff image      : {out_dir / '03_diff.bmp'}")

    if stats["changedFraction"] <= NOISE_FLOOR:
        print("\nRESULT: captures are effectively identical -- the toggle changed nothing.")
        return 1

    print("\nRESULT: the toggle changed the frame.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
