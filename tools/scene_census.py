"""What every LBA scene costs in RAM, for all 120 of them.

The PlayStation port fits its first scene with about 39 KB to spare, and that
number was measured on cube 0 — Twinsen's house, one of the smallest scenes in
the game. This tool answers the obvious next question without needing a
console: how big does a scene get, and is cube 0 representative?

It reproduces exactly what `GRILLE.C:InitGrille` and `LoadUsedBrick` do:

    BufMap          = LBA_GRI.HQR[cube]         -> HQM
    TabBlock        = LBA_BLL.HQR[cube]         -> HQM
    the brick set   = every brick referenced by every USED block, from
                      LBA_BRK.HQR, decompressed                -> BufferBrick
    BufferMaskBrick = a mask built over that brick set         -> HQM

`BufferBrick` is a fixed 361472-byte allocation (`MAX_SIZE_BRICK_CUBE` in
GRILLE.H) and the 1994 code does not check against it, so a scene whose brick
set overflows would corrupt the heap silently. That check is worth having on
its own.

The mask is the one term this cannot compute exactly: `CreateMaskGph` walks the
decompressed bricks and emits a run-length mask that `HQM_Shrink_Last` then
trims. It is bounded above by the brick total, which is what gets reported.

No game data ships with this repository. Point it at your own DOS install — in
a GOG copy that is `Speedrun/Windows/`, NOT `Common/`.

    python tools/scene_census.py "<your DOS install>/Speedrun/Windows"
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from hqr import Hqr

# GRILLE.H
MAX_SIZE_BRICK_CUBE = 361472
OFFSET_GRM_HQR = 120

# PERSO.C
HQM_MEMORY = 400000


def brick_offsets(path: Path) -> list[int]:
    """LBA_BRK's offset table, read the way LoadUsedBrick reads it: the first
    u32 is the table size in bytes, and the table includes that word."""
    raw = path.read_bytes()
    table_bytes = struct.unpack_from("<I", raw, 0)[0]
    count = table_bytes // 4
    return list(struct.unpack_from(f"<{count}I", raw, 0)), raw


def brick_size(raw: bytes, offset: int) -> int:
    """The decompressed size of one brick — T_HEADER.SizeFile."""
    if offset == 0 or offset + 10 > len(raw):
        return 0
    return struct.unpack_from("<I", raw, offset)[0]


def used_bricks(gri: bytes, bll: bytes) -> set[int]:
    """Every brick index referenced by a used block.

    LoadUsedBrick's first pass. The used-block bitmap is the last 32 bytes of
    the .gri; bit i (MSB first within each byte) says block i is in play. A
    block's record starts at TabBlock + *(u32 *)(TabBlock + (i-1)*4), carries
    dx, dy, dz as three bytes, and its brick entries are 4-byte pairs starting
    five bytes in — of which only the first halfword is the brick number.
    """
    used: set[int] = set()

    if len(gri) < 32 or len(bll) < 4:
        return used

    flags = gri[len(gri) - 32:]

    for i in range(1, 256):
        if not (flags[i >> 3] & (1 << (7 - (i & 7)))):
            continue

        ptr_at = (i - 1) * 4
        if ptr_at + 4 > len(bll):
            continue
        base = struct.unpack_from("<I", bll, ptr_at)[0]
        if base + 5 > len(bll):
            continue

        dx, dy, dz = bll[base], bll[base + 1], bll[base + 2]
        entries = dx * dy * dz
        at = base + 5

        for _ in range(entries):
            if at + 2 > len(bll):
                break
            brick = bll[at] | (bll[at + 1] << 8)
            if brick:
                used.add(brick - 1)
            at += 4

    return used


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", help="the DOS install (GOG: Speedrun/Windows)")
    ap.add_argument("--top", type=int, default=10, help="how many to list")
    args = ap.parse_args()

    root = Path(args.assets)
    for name in ("LBA_GRI.HQR", "LBA_BLL.HQR", "LBA_BRK.HQR"):
        if not (root / name).exists():
            print(f"error: {name} is not in {root}", file=sys.stderr)
            print("   Is this the DOS install? A GOG copy keeps it in "
                  "Speedrun/Windows, not Common.", file=sys.stderr)
            return 1

    gri_hqr = Hqr(root / "LBA_GRI.HQR")
    bll_hqr = Hqr(root / "LBA_BLL.HQR")
    brk_offsets, brk_raw = brick_offsets(root / "LBA_BRK.HQR")

    ncubes = min(len(gri_hqr), len(bll_hqr), OFFSET_GRM_HQR)
    rows = []

    for cube in range(ncubes):
        try:
            gri = gri_hqr[cube]
            bll = bll_hqr[cube]
        except Exception as exc:                    # a hole in the table
            print(f"  cube {cube}: {exc}", file=sys.stderr)
            continue
        if not gri or not bll:
            continue

        bricks = used_bricks(gri, bll)
        total = sum(brick_size(brk_raw, brk_offsets[b])
                    for b in bricks if b < len(brk_offsets))
        total += (len(bricks) + 1) * 4           # the offset table it writes

        rows.append({
            "cube": cube,
            "gri": len(gri),
            "bll": len(bll),
            "bricks": len(bricks),
            "brick_bytes": total,
        })

    if not rows:
        print("no scenes read; is this the right archive set?", file=sys.stderr)
        return 1

    # HQM holds gri + bll + the mask; the mask is bounded by the brick total.
    for r in rows:
        r["hqm_max"] = r["gri"] + r["bll"] + r["brick_bytes"]

    print(f"{len(rows)} scenes read from {root}\n")

    print(f"  {'cube':>4}  {'gri':>7}  {'bll':>7}  {'bricks':>7}  "
          f"{'brick bytes':>12}  {'HQM upper bound':>16}")
    print("  " + "-" * 62)
    for r in sorted(rows, key=lambda r: -r["hqm_max"])[:args.top]:
        print(f"  {r['cube']:>4}  {r['gri']:>7}  {r['bll']:>7}  "
              f"{r['bricks']:>7}  {r['brick_bytes']:>12}  {r['hqm_max']:>16}")

    cube0 = next((r for r in rows if r["cube"] == 0), None)
    worst = max(rows, key=lambda r: r["hqm_max"])
    fattest_bricks = max(rows, key=lambda r: r["brick_bytes"])

    print()
    if cube0:
        print(f"  cube 0 (the one the port measured):  "
              f"gri {cube0['gri']}, bll {cube0['bll']}, "
              f"bricks {cube0['brick_bytes']} bytes in {cube0['bricks']} bricks")
        print(f"  HQM upper bound for cube 0:          {cube0['hqm_max']}")
    print(f"  worst scene is cube {worst['cube']}:              "
          f"{worst['hqm_max']} (HQM is fixed at {HQM_MEMORY})")
    if cube0 and cube0["hqm_max"]:
        print(f"  ratio, worst over cube 0:            "
              f"{worst['hqm_max'] / cube0['hqm_max']:.2f}x")

    print()
    print(f"  BufferBrick is {MAX_SIZE_BRICK_CUBE} bytes (MAX_SIZE_BRICK_CUBE).")
    print(f"  Fattest brick set is cube {fattest_bricks['cube']}: "
          f"{fattest_bricks['brick_bytes']} bytes.")
    if fattest_bricks["brick_bytes"] > MAX_SIZE_BRICK_CUBE:
        over = fattest_bricks["brick_bytes"] - MAX_SIZE_BRICK_CUBE
        print(f"  *** OVERFLOWS by {over} bytes. LoadUsedBrick does not "
              f"check. ***")
    else:
        head = MAX_SIZE_BRICK_CUBE - fattest_bricks["brick_bytes"]
        print(f"  Fits, with {head} bytes ({head // 1024} KB) to spare.")

    over_hqm = [r for r in rows if r["hqm_max"] > HQM_MEMORY]
    print()
    if over_hqm:
        print(f"  {len(over_hqm)} scenes exceed the {HQM_MEMORY}-byte HQM pool "
              f"at the upper bound.")
        print("  The bound counts the mask as the full brick total; the real "
              "mask is smaller,")
        print("  so this is a list of scenes to measure on hardware, not a "
              "list of failures.")
    else:
        print(f"  Every scene fits the {HQM_MEMORY}-byte HQM pool even at the "
              f"upper bound,")
        print("  which counts the brick mask at the full size of the brick "
              "data it covers.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
