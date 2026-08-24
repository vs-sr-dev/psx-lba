# psx-lba — handoff, end of session 2 (2026-08-24)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md). This file is only "where to pick it up".

---

## Verdict in three lines

**A Little Big Adventure scene is on screen at 640x480, off a CD image made
from a real DOS install.** The engine reaches its first scene with 1535 KB
allocated against 1574 KB of heap — a fit with 39 KB spare, arrived at by three
moves worth 554 KB. Scene load costs 3.9 s and composing the background costs
588 ms, both measured, both worse than predicted.

## Corrections from this session

1. **2258 KB was never the requirement.** M2 estimated high in two directions
   at once. The real total at the first scene is **1535 KB**. `SampleMem`'s
   195 KB was never allocated at all (`Wave_Driver_Enable` is false, so
   `HQR_Samples` is skipped), so M2 scored a saving on a cost that had not been
   paid. The three things that actually made it fit are `Log` merged with
   `Screen` (300 KB), `BufSpeak` cut to 64 KB (192 KB) and `Phys` allocated on
   demand (62 KB).
2. **An honest `Malloc(-1)` changes nothing.** It is implemented — `psx_sys.c`
   bisects on what the allocator will actually grant — and the pools still land
   on their clamped floors, because by the time the question is asked there are
   only ~230 KB free. On the DS this bought resident sprite and anim pools; here
   the floors are the size of the machine. Sprites and animations *will* evict,
   and the `HQR_Del_Bloc`-compacts-under-live-pointers hazard the DS documented
   is inherited whole.
3. **`AffGrille` is 588 ms, not the ~300 ms predicted.** The prediction scaled
   the DS's 207 ms by M0's 1.46x `memset` ratio. Wrong ratio: a brick blit is
   strided reads against strided writes with a per-pixel transparency test, so
   the 2.21x large-polygon figure is the right one (207 x 2.21 = 458). Real
   answer is ~2.8x the DS. **Never scale a blit estimate off `memset` again.**
4. **PCSX-Redux's recompiler does not check MIPS alignment.** This is the big
   one — see below.

## Measured numbers (do not re-measure)

| | |
|---|---|
| full build (menu) | text 221792 + data 3304 + bss 128214, heap **1574 KB** |
| M3 harness build | text 141212 + data 3000 + bss 127346, heap 1656 KB |
| at the first scene | **use 1535 KB**, 19 blocks, largest free block 111 KB |
| `ChangeCube` (scene + dial + grid + bricks) | **3894 ms** |
| `AffGrille` (640x480 compose) | **588 ms** |
| `Flip` (300 KB to VRAM via the staging page) | **122 ms** |
| HQM used by cube 0 | 141936 of 400000 |
| cube 0 camera start cell | 53, 4, 54 |

The two heaps differ by 80 KB because the M3 harness replaces `MainGameMenu`
and the linker garbage-collects most of `GAMEMENU.C` with it. **Always check a
memory claim against the 1574 KB figure, not the harness's.**

---

## The one thing that changes how you work

**Run PCSX-Redux with `-interpreter`. Always.**

Its recompiler does not raise MIPS address errors: an unaligned `lw` returns
the correctly assembled value and does not fault. Since unaligned access off a
byte stream is the single most likely bug class in a 1994 DOS engine moved to
MIPS, every clean run from M0 to M3 under the recompiler proved less than it
appeared to.

Switching to `-interpreter` found a fault immediately, and it was the port's
own: `PSX_malloc` wrote its canary directly past a block of arbitrary size, so
every odd-sized allocation stored a word to an odd address. Fixed. With that
fixed, the whole scene-load chain — `FICHE`, `GERETRAK`, `GERELIFE`,
`InitDial`, `InitGrille`, `LoadUsedBrick`, `AffGrille` — walks through clean,
which is the DS port's inheritance rather than the 1994 code being careful.

Related: PCSX-Redux keeps `emulator/Debug/FirstChanceException` in
`%APPDATA%/pcsx-redux/pcsx.json`. It shipped at `7408`; **this session set it to
0** so exceptions reach the program instead of breaking into the debugger. Put
7408 back if you want the debugger to stop on them.

---

## Environment (installed, do not redo)

Unchanged from session 1 except as noted.

- **Docker image `psx-lba-psn00b`** — PSn00bSDK 0.24, `mipsel-none-elf-gcc`
  12.3.0, ninja, mkpsxiso, dumpsxiso. Built from
  `tools/docker/Dockerfile.psn00b`.
- **PCSX-Redux** via winget, at
  `%LOCALAPPDATA%/Microsoft/WinGet/Packages/GrumpyCoders.PCSX-Redux_*/pcsx-redux.exe`
- **melonDS** and **devkitpro/devkitarm:latest** — only for the DS side of the
  M0 calibration benchmark.
- **DuckStation is still NOT installed** and is now wanted for two separate
  reasons: GPU fill rate, and settling the exception handler (below).

## Gotchas found the hard way

Session 1's all still hold (`MSYS_NO_PATHCONV=1` before every `docker run`;
mkpsxiso takes `name="..."` literally; `CdOpenDir` returns `CdlDIR *` and
`CdlDIR` is `void *`; address 0 is kernel RAM; engine `.C` files need
`LANGUAGE C`). New ones:

- **`make_cd.py` copies the PS-EXE onto the staged disc.** It has to run
  *after* the build and *before* `mkpsxiso`, or you will spend twenty minutes
  debugging the previous binary. This happened.
- **PCSX-Redux's TTY cannot be captured by shell redirection.** It writes to the
  console handle directly, so `> file` yields an empty file. Read it off the
  terminal.
- **`0xA0000` is not a VGA aperture here.** `GAMEMENU.C`'s zoom path fell
  through to the DOS branch and wrote 64000 bytes to it — ordinary heap, 640 KB
  up. Fixed, but the class is the point: on this machine every address is legal,
  which is a different hazard from the DS's and needs looking for by hand.
- **`Log` and `Screen` are now the same allocation.** Anything that wants to
  draw over the background has to be a GPU primitive. Writing into `Log` and
  expecting a restore will silently do nothing, and taking the 300 KB back is
  not an option.
- **`InitGraphMcga` reallocates `Log`** and would break the `Screen` alias. It
  is not reached yet; it will be in M8.

## Rebuild and run, from the repository root

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" \
    -w /work/platform/psx psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'

python tools/make_cd.py "<your DOS install>/Speedrun/Windows"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" -w /work \
    psx-lba-psn00b mkpsxiso -y -o build/lba1psx.bin -c build/lba1psx.cue \
    build/iso.xml

pcsx-redux -run -stdout -interpreter -iso build/lba1psx.cue
```

Build knobs, all in `platform/psx/CMakeLists.txt`:

| | |
|---|---|
| `-DPSX_M3=ON` (default) | stop at one static scene instead of the main menu |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path, then stop |

---

# M4 — the actors

`AffGrille` proved the background half of the table in the README. M4 is the
other half: 3D actor transforms on the GTE, rasterisation as GPU flat and
gouraud primitives, over a background the GPU textures out of main RAM.

This is the move the whole architecture was built around, and M3 has already
committed to it: `Log` and `Screen` share one buffer *because* the CPU is not
going to draw actors. There is no software fallback left to retreat to.

### What M0 already settled

- 19826 polygons in `BODY.HQR` across 132 bodies. **98.03% are flat or gouraud**
  and map to GPU primitives directly.
- The awkward set is 353 polygons, 1.78%. Tele is 4-colour noise a 32x32 texture
  reproduces; Marbre is a gradient along the span (gouraud in index space, 8
  polygons in the entire game). **Copper and Bopper — 48 polygons total — still
  need a decision:** approximate with gouraud, or keep a software path.
- The R3000A is 1.82x the ARM9 on the same rasteriser, so a software actor costs
  ~15 ms of a 20 ms frame. That is the number the GPU path exists to avoid.

### The order to do it in

1. **Palette and colour.** The engine works in 8bpp index space and the GPU
   works in 15bpp RGB. Flat and gouraud actor colours are palette indices into
   the *scene* palette, so the GPU has to be handed RGB resolved through
   `PORT_PalRGB` at draw time — and a palette fade then has to reach the actors
   too, not just the CLUT. Get this wrong and everything renders in the wrong
   colours in a way that looks like a rasteriser bug.
2. **One body, no animation.** `AffObjetIso` on a single 3D object over the M3
   scene. `GAMEMENU.C:3069`-ish already does exactly this for the menu's
   spinning object, which makes it a ready-made test harness.
3. **The GTE.** The engine has its own fixed-point transform in `LIB_3D`
   (`P_FUNC.C`, `P_SINTAB.C`). Decide deliberately whether to route it through
   the GTE or leave it on the CPU and only hand the GPU the projected vertices
   — the second is much less work and M0 never measured that the transform was
   the bottleneck.
4. **Depth.** The engine sorts actors itself (`BUBSORT.C`, `T_SORT`) because
   DOS had no z-buffer. The PlayStation has an ordering table and also no
   z-buffer, so the two disciplines line up — but the engine's sort key and the
   OT index have to be reconciled, and that is real design work, not plumbing.
5. **Then animation**, and only then the frame budget.

### Measure before believing

GPU fill rate is **still unmeasured**, and PCSX-Redux cannot measure it (M0:
it reported a 640x480 clear in 54 us). Install DuckStation before claiming any
actor budget as a number.

---

## Carried forward

- **Size `HQM` from a census of all 120 cubes.** It is 391 KB of a 1574 KB heap,
  fixed at the inherited constant 400000, and cube 0 uses 139 KB. The margin
  against the *largest* scene is currently unknown — which means the 39 KB
  spare is a claim about one small scene, not about the game.
- **The 3894 ms scene load.** `LoadUsedBrick` does one seek per brick over a
  3.9 MB archive. This is the M7 drive-contention problem arriving early, and
  unlike everything else about M7 it is measurable today.
- **The exception handler's fatal path is unverified.** The vector at
  `0x80000080` is provably ours and the chaining path provably works (every
  interrupt and syscall in a full run goes through it), but PCSX-Redux with
  OpenBIOS answers a real address error from its own handler and halts without
  ever reaching ours. Settle it on DuckStation, a retail BIOS image, or
  hardware. Until then OpenBIOS's dump carries the same information.
- **Copper and Bopper** (48 polygons in the entire game) still need a decision.
- **The FLA movies are not on the disc.** Not in the DOS install either — only
  `Common/Fla`, which belongs to the 2023 remaster. Establish whether those
  files are format-identical before relying on them. M8.
- **CD drive contention** between streamed music and loading is untouched. M7.
- **The dirty-rectangle discipline is not exploited yet.** `psx_video.c`
  presents whole rectangles on request but the engine's own dirty-box tracking
  is not driving it. That is what makes 50 fps possible rather than merely
  plausible. M5.
- **`MemoLog` is unused** in the PSX build; check whether the engine's MCGA path
  expects it before M8.
