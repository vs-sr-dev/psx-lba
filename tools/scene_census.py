"""What every LBA scene costs in the HQM pool, for all 120 of them.

The PlayStation port reserves 400000 bytes for HQM because that is what the
1994 DOS build reserved, and cube 59 -- the heaviest scene in the game -- was
measured using 144280 of it. This tool answers the question that gap raises
without needing a console: what does the pool actually have to hold, at its
*peak*, on the worst scene, and how much of the 400000 is dead weight?

It reproduces what a `ChangeCube` does, in order (OBJECT.C:484 onwards):

    HQM_Free_All()
    LoadFicPerso()      5 x FILE3D.HQR                          -> HQM
    LoadScene(cube)     SCENE.HQR[cube]                         -> HQM
                        FILE3D.HQR[n] per non-sprite actor      -> HQM
    InitGrille(cube)    BufMap   = LBA_GRI.HQR[cube]            -> HQM
                        TabBlock = LBA_BLL.HQR[cube]            -> HQM
                        the brick set: every brick referenced by every USED
                        block, from LBA_BRK.HQR, decompressed   -> BufferBrick
                        BufferMaskBrick, the run-length mask over that brick
                        set                                     -> HQM

The mask is computed exactly, by running `CalcGraphMsk` (translate/graphmsk.c,
itself a translation of LIB_SVGA/GRAPHMSK.ASM) over the reconstructed brick
bank. That matters for more than accuracy: `GRILLE.C` allocates the mask buffer
at the size of the *brick data* and only then shrinks it to the mask it
actually built, so for the length of one function call HQM holds a buffer five
times larger than the one it keeps. That transient is the reason the pool
cannot be made smaller, and it is invisible to the engine's own `CHECK_MEMORY`
accounting, which only samples after the shrink.

One term is not static and is not reported per scene: BODY.HQR. `SearchBody`
pulls a body into HQM the first time an actor needs one, so what a scene
accumulates depends on what happens in it. Measured on the console it is about
48 KB on cube 0 and on cube 59 alike; `--body` sets the allowance the verdict
uses.

`BufferBrick` is a fixed 361472-byte allocation (`MAX_SIZE_BRICK_CUBE` in
GRILLE.H) outside the pool, and the 1994 code did not check against it. It is
checked here, and since M7 in the engine too.

No game data ships with this repository. Point it at your own DOS install -- in
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

# LIB_SYS/HQ_MEM.C -- every HQM_Alloc rounds up to 4 and prepends a header
HQM_HEADER = 12

# DEFINES.H -- the five fiches LoadFicPerso pulls in before the scene
FILE_3D_PERSO = (1, 2, 3, 4, 0)

# COMMON.H
SPRITE_3D = 1024

# what one console session actually accumulated out of BODY.HQR
BODY_MEASURED = 48 * 1024


def hqm(size: int) -> int:
    """One HQM_Alloc of `size`: rounded up to 4, plus the block header."""
    return ((size + 3) & ~3) + HQM_HEADER


def brick_offsets(path: Path) -> tuple[list[int], bytes]:
    """LBA_BRK's offset table, read the way LoadUsedBrick reads it: the first
    u32 is the table size in bytes, and the table includes that word."""
    raw = path.read_bytes()
    table_bytes = struct.unpack_from("<I", raw, 0)[0]
    count = table_bytes // 4
    return list(struct.unpack_from(f"<{count}I", raw, 0)), raw


def used_bricks(gri: bytes, bll: bytes) -> set[int]:
    """Every brick index referenced by a used block.

    LoadUsedBrick's first pass. The used-block bitmap is the last 32 bytes of
    the .gri; bit i (MSB first within each byte) says block i is in play. A
    block's record starts at TabBlock + *(u32 *)(TabBlock + (i-1)*4), carries
    dx, dy, dz as three bytes, and its brick entries are 4-byte pairs starting
    five bytes in -- of which only the first halfword is the brick number.
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


def brick_bank(bricks: list[bytes]) -> bytes:
    """What LoadUsedBrick leaves in BufferBrick: an offset table of nbbrick+1
    u32s, then every decompressed brick end to end."""
    first = (len(bricks) + 1) * 4
    offsets, at = [first], first
    for b in bricks:
        at += len(b)
        offsets.append(at)
    return struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(bricks)


def mask_size(bank: bytes, num: int) -> int:
    """CalcGraphMsk (translate/graphmsk.c) with the stores taken out.

    A brick is DX, DY, HotX, HotY then DY lines of (NbBlock, blocks); a block
    is 00xxxxxx skip x+1, 01xxxxxx copy x+1 pixels, 10xxxxxx x+1 pixels of one
    colour. The mask keeps the four header bytes and rewrites each line as
    alternating skip/draw counts, always starting on a skip.
    """
    src = struct.unpack_from("<I", bank, num * 4)[0]
    out = 4
    nbline = bank[src + 1]
    p = src + 4

    for _ in range(nbline if nbline else 256):     # a DY of 0 runs 256 times
        nbdata = 0
        out += 1                                   # the NbBlock slot
        nbblock = bank[p]
        p += 1
        if bank[p] & 0xC0:                         # a line must start on a skip
            out += 1
        for _ in range(nbblock if nbblock else 256):
            op = bank[p]
            p += 1
            cl = (op & 0x3F) + 1
            if op & 0x80:                          # one colour, one data byte
                nbdata = (nbdata + cl) & 0xFF
                p += 1
            elif op & 0x40:                        # cl pixels, cl data bytes
                nbdata = (nbdata + cl) & 0xFF
                p += cl
            else:                                  # cl zeros
                if nbdata:
                    out += 1
                    nbdata = 0
                out += 1
        if nbdata:
            out += 1

    return out


def total_mask(bank: bytes, count: int) -> int:
    """CreateMaskGph's return: the offset table it rewrites, then every mask."""
    return (count + 1) * 4 + sum(mask_size(bank, i) for i in range(count))


class Reader:
    def __init__(self, data: bytes):
        self.b, self.p = data, 0

    def u16(self) -> int:
        v = self.b[self.p] | (self.b[self.p + 1] << 8)
        self.p += 2
        return v

    def skip(self, n: int) -> None:
        self.p += n


def scene_fiches(sce: bytes) -> list[int]:
    """The FILE3D.HQR index of every non-sprite actor, read the way LoadScene
    reads it (DISKFUNC.C). A sprite actor takes no FILE3D entry."""
    r = Reader(sce)
    r.skip(2)                       # Island, GameOverCube
    r.skip(4)                       # two unused words
    r.skip(4)                       # AlphaLight, BetaLight
    r.skip(24)                      # 4 x (ambiance, repeat, rnd)
    r.skip(4)                       # SecondMin, SecondEcart
    r.skip(1)                       # CubeJingle
    r.skip(6)                       # CubeStart X Y Z
    r.skip(r.u16())                 # the hero's track
    r.skip(r.u16())                 # the hero's life
    nbobjets = r.u16()

    out = []
    for _ in range(1, nbobjets):
        flags = r.u16()
        index = r.u16()
        if not (flags & SPRITE_3D):
            out.append(index)
        r.skip(2 + 2 + 6 + 1 + 2 + 6 + 8 + 4)   # the rest of the actor record
        r.skip(r.u16())             # track
        r.skip(r.u16())             # life
    return out


def census(root: Path) -> list[dict]:
    gri_hqr = Hqr(root / "LBA_GRI.HQR")
    bll_hqr = Hqr(root / "LBA_BLL.HQR")
    sce_hqr = Hqr(root / "SCENE.HQR")
    f3d_hqr = Hqr(root / "FILE3D.HQR")
    brk_hqr = Hqr(root / "LBA_BRK.HQR")
    brk_offsets, _ = brick_offsets(root / "LBA_BRK.HQR")

    f3d = [len(f3d_hqr[i]) for i in range(len(f3d_hqr))]
    perso = sum(hqm(f3d[i]) for i in FILE_3D_PERSO)

    cache: dict[int, bytes] = {}

    def brick(i: int) -> bytes:
        if i not in cache:
            cache[i] = brk_hqr[i]
        return cache[i]

    rows = []
    ncubes = min(len(gri_hqr), len(bll_hqr), OFFSET_GRM_HQR)

    for cube in range(ncubes):
        try:
            gri, bll = gri_hqr[cube], bll_hqr[cube]
        except Exception as exc:                    # a hole in the table
            print(f"  cube {cube}: {exc}", file=sys.stderr)
            continue
        if not gri or not bll:
            continue

        used = sorted(b for b in used_bricks(gri, bll) if b < len(brk_offsets))
        bank = brick_bank([brick(b) for b in used])
        mask = total_mask(bank, len(used))

        sce = sce_hqr[cube]
        actors = sum(hqm(f3d[i]) for i in scene_fiches(sce) if i < len(f3d))

        before = perso + hqm(len(sce)) + actors
        grid = hqm(len(gri)) + hqm(len(bll))

        rows.append({
            "cube": cube,
            "gri": len(gri),
            "bll": len(bll),
            "bricks": len(used),
            "bank": len(bank),
            "mask": mask,
            "static": before,
            # the pool's high-water mark is the moment the mask buffer is
            # allocated inside InitGrille, not the state it settles into
            "peak_now": before + grid + hqm(len(bank)),
            "peak_fixed": before + grid + hqm(mask),
        })

    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", help="the DOS install (GOG: Speedrun/Windows)")
    ap.add_argument("--top", type=int, default=10, help="how many to list")
    ap.add_argument("--body", type=int, default=BODY_MEASURED,
                    help="BODY.HQR allowance in bytes (measured: 48 KB)")
    args = ap.parse_args()

    root = Path(args.assets)
    for name in ("LBA_GRI.HQR", "LBA_BLL.HQR", "LBA_BRK.HQR",
                 "SCENE.HQR", "FILE3D.HQR"):
        if not (root / name).exists():
            print(f"error: {name} is not in {root}", file=sys.stderr)
            print("   Is this the DOS install? A GOG copy keeps it in "
                  "Speedrun/Windows, not Common.", file=sys.stderr)
            return 1

    rows = census(root)
    if not rows:
        print("no scenes read; is this the right archive set?", file=sys.stderr)
        return 1

    print(f"{len(rows)} scenes read from {root}\n")

    print(f"  {'cube':>4}  {'gri':>6}  {'bll':>6}  {'brk':>4}  {'bank':>7}  "
          f"{'mask':>6}  {'scene+3d':>9}  {'HQM peak':>9}  {'if fixed':>9}")
    print("  " + "-" * 78)
    for r in sorted(rows, key=lambda r: -r["peak_now"])[:args.top]:
        print(f"  {r['cube']:>4}  {r['gri']:>6}  {r['bll']:>6}  "
              f"{r['bricks']:>4}  {r['bank']:>7}  {r['mask']:>6}  "
              f"{r['static']:>9}  {r['peak_now']:>9}  {r['peak_fixed']:>9}")

    worst_now = max(rows, key=lambda r: r["peak_now"])
    worst_fix = max(rows, key=lambda r: r["peak_fixed"])
    worst_bank = max(rows, key=lambda r: r["bank"])
    cube0 = next((r for r in rows if r["cube"] == 0), None)

    print()
    if cube0:
        print(f"  cube 0, the scene the port runs:           peak "
              f"{cube0['peak_now']}, {cube0['peak_fixed']} if fixed")
    print(f"  worst peak as the engine allocates today:  cube {worst_now['cube']}, "
          f"{worst_now['peak_now']} of {HQM_MEMORY} "
          f"({HQM_MEMORY - worst_now['peak_now']} spare)")
    print(f"  worst peak with an exactly-sized mask:     cube {worst_fix['cube']}, "
          f"{worst_fix['peak_fixed']}")

    print()
    print("  The gap between those two is one allocation: GRILLE.C asks HQM for a")
    print(f"  mask buffer the size of the whole brick bank ({worst_now['bank']}")
    print("  bytes on that scene), builds a mask about a fifth of that into it,")
    print("  and shrinks. Nothing else in the pool is transient.")

    need = worst_fix["peak_fixed"] + args.body
    print()
    print(f"  With a {args.body // 1024} KB BODY.HQR allowance on top, the pool has to hold")
    print(f"  {need} bytes. HQM is {HQM_MEMORY}: {HQM_MEMORY - need} bytes "
          f"({(HQM_MEMORY - need) // 1024} KB) of it is dead.")

    print()
    print(f"  BufferBrick is {MAX_SIZE_BRICK_CUBE} bytes (MAX_SIZE_BRICK_CUBE).")
    print(f"  Fattest brick bank is cube {worst_bank['cube']}: {worst_bank['bank']} bytes.")
    if worst_bank["bank"] > MAX_SIZE_BRICK_CUBE:
        print(f"  *** OVERFLOWS by {worst_bank['bank'] - MAX_SIZE_BRICK_CUBE} bytes. ***")
    else:
        head = MAX_SIZE_BRICK_CUBE - worst_bank["bank"]
        print(f"  Fits, with {head} bytes ({head // 1024} KB) to spare.")

    over = [r for r in rows if r["peak_now"] > HQM_MEMORY]
    if over:
        print()
        print(f"  {len(over)} scenes exceed the {HQM_MEMORY}-byte pool as it is "
              f"allocated today: {', '.join(str(r['cube']) for r in over)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
