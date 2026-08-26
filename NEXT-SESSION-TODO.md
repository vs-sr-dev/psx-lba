# psx-lba — handoff, end of session 6 (2026-08-26)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md), [M4](docs/M4-NOTES.md), [M5](docs/M5-NOTES.md),
[M6](docs/M6-NOTES.md), [M7](docs/M7-NOTES.md). This file is only "where to
pick it up".

---

## Verdict in four lines

**The port has never drawn a background, and now it does.** Since M1 it has
presented the composed scene while the palette was black and only ever shown
what was redrawn afterwards; M6's autopilot opened a modal, which forced a
redraw, and that is why nobody saw it. **The clean background is also a real
buffer again** — in VRAM, because main RAM never had the 300 KB and now
provably never will. The shadow trail is gone walking forward and turning, and
still there walking backwards; both modals still burn in. 34 ms and 29 fps,
unchanged. Cube 59, the heaviest scene in the game, runs for the first time.

## What changed this session

1. **HQM was 400 KB because of one function call.** `InitGrille` sized the
   brick mask at the size of the brick bank and shrank it afterwards, so the
   pool peaked at **384696 of 400000** on cube 59 while its own accounting,
   which samples after the shrink, reported 144280. Measured before it is
   allocated, HQM fits in **256 KB**: 135 KB back to the heap.
   [M7 §1](docs/M7-NOTES.md).
2. **`tools/scene_census.py` computes the mask exactly** — `CalcGraphMsk`
   ported to Python — and models a whole `ChangeCube`. It agrees with the
   console to the byte on both cubes it has been checked against.
3. **300 KB for `Screen` are not in main RAM**, with the arithmetic.
   [M7 §2](docs/M7-NOTES.md).
4. **So the clean background lives in VRAM**, at x 704, and `CopyScreen` /
   `CopyBlock` become DMA. Restoring the frame's dirty boxes costs **1 ms**,
   and the per-box path works. The full-screen path the modals use does not
   yet. [M7 §3, §6](docs/M7-NOTES.md).
4b. **The screen was black, and had been since M1.** A fade is a palette write
   here and the framebuffer is already RGB, so the background was presented
   black and never re-presented. `ApplyPalRange` now re-presents.
   [M7 §5](docs/M7-NOTES.md).
5. **PSn00bSDK 0.24's `StoreImage` hangs on every VRAM read** — it waits on
   GPUSTAT bit 28, which is the upload's handshake. `psx_video.c:VramRead`
   does it by hand. [M7 §3](docs/M7-NOTES.md).
6. **Cube 59 runs**, after an unaligned load in `CopyBlockMCGA` and a
   misunderstanding of what MCGA is (a zoom, not a screen mode).
   [M7 §4](docs/M7-NOTES.md).

## Measured numbers (do not re-measure)

Cube 0, DuckStation, retail BIOS, interpreter, software renderer.

| | |
|---|---|
| heap in use at the first scene | **1400 KB** of 1574 (was 1535) |
| HQM peak, cube 0 / cube 59 | **92940 / 96628** of 262144 |
| HQM peak before the fix, cube 59 | **384696** of 400000 |
| brick mask, cube 0 / cube 59 | 56736 / 63827 (bank 236142 / 351894) |
| BODY.HQR over a session | about 48 KB, both cubes |
| worst brick bank, all 120 scenes | 351894 of `MAX_SIZE_BRICK_CUBE` 361472 |
| a frame while walking | **34 ms, 29 fps** — unchanged by any of this |
| ClsBoxes out of the VRAM background | **1 ms** |
| a fade, since it re-presents | 52 full presents, about **270 ms** |
| cube 59, best frame | about **100 ms**, 192 entities, zoom on |
| VRAM background round trip, at boot | **0 of 256 bytes wrong** |

## The rules that come from experience, not preference

Sessions 1–5's all still hold. New:

**When a transfer hangs, isolate it before you guess at it.** Four runs went
into bisecting the VRAM read inside a live frame — moving the staging tile,
checking buffer alignment — and produced nothing but a black screen. One run
of `-DPSX_BG_SELFTEST=ON`, which does the round trip at boot with nothing else
happening, answered it. If the thing under test can be run on its own, run it
on its own first.

**Read the library's disassembly before believing its documentation.**
`StoreImage` is the documented way to read VRAM and it cannot work; the reason
is six instructions long and visible in `objdump`.

**A bound that cannot see the peak is not a bound.** The census's 373878 and
the console's 144280 disagreed by 2.6x for four milestones and the difference
was written off as pessimism. They were measuring different moments, and the
one nobody was measuring was the one that mattered.

**A scene exercised by one harness is not an exercised scene.** Cube 59 loaded
fine under M3 for three sessions. Under a game loop it runs life scripts, and
the first one it runs faulted in ten seconds.

**The test harness was hiding the bug it was built to find.** Every screenshot
since M5 was taken with the autopilot running, and the autopilot opens the
inventory — which forces a full recompose and present. That is the only reason
a background ever appeared on screen. Look at the plain build too, and look at
it before anything has happened in it.

---

# M8 — the load, the modals, and the things with no workspace

### Worth doing first, in this order

0. **Finish the background.** Two things are left and both are narrow. The
   shadow still trails **walking backwards only** — the per-box fetch reads on
   64-pixel columns and writes only the columns asked for, and that asymmetry
   is the first suspect. And **both modals still burn in**: they use the
   full-screen `CopyScreen` pair, which is `PORT_BgStoreAll` /
   `PORT_BgFetchAll`, a different path from the tiled one and the only one
   never proved. Extending `-DPSX_BG_SELFTEST=ON` to a full-screen round trip
   of a known pattern is one run and answers it outright. The behaviour
   panel's backdrop reading *transparent* rather than *darker* says the
   restore is putting something back, just not the right something.
   [M7 §6](docs/M7-NOTES.md).
1. **The fade is 52 full presents**, about 270 ms. Presenting every second or
   fourth step would look the same for a quarter of the cost.
   [M7 §5](docs/M7-NOTES.md).
2. **`ChangeCube`, 18.8 s.** `LoadUsedBrick` does one seek per brick over a
   3.9 MB archive. The fix is the archive layout, not the streaming policy: the
   bricks a cube uses want to be contiguous. Untouched by M7 and now the
   largest single number in the port.
3. **The modals, 3–6 seconds.** `DrawMenuComportement` draws four animated
   bodies and `AffScene(TRUE)` recomposes 640x480 behind it. They no longer
   burn in; they are still unusable at that price. [M6 §4](docs/M6-NOTES.md).
4. **Cube 59 at 100 ms.** The zoom re-presents 64000 pixels every frame through
   the tile path, and the scene carries 192 entities. Two different problems
   sharing one number; separate them before optimising either.
5. **The emit, 10 ms of 18.** `PORT_ActorPoly` clips every polygon with a
   Sutherland-Hodgman that runs even when the polygon is entirely inside, and
   resolves a palette entry per vertex. A trivial-accept test and a cached ramp
   lookup are both obvious and neither has been tried.
6. **Actors are not occluded by scenery** — the missing depth on Twinsen. `DrawOverBrick` cannot work here —
   the actor is a GPU primitive drawn after the present, not pixels in `Log`.
   It needs the foreground bricks replayed as primitives with their mask as a
   texture. `GRILLE.C:PORT_CopyMaskBg` is the hole left for it.

## Carried forward

- **The holomap has no workspace.** It used to lay 200 KB out inside `Screen`;
  `Screen` is 64 KB now and the holomap refuses instead. It wants its own
  allocation — there are 174 KB free — and a port. [M7 §3](docs/M7-NOTES.md).
- **`GetAscii` returns nothing**, so the save-name entry has no characters. It
  wants the memory card first.
- **The memory card**, for saves and for `DisableAutoSave` to go away.
- **Voices.** VOX is not on the disc and the packed path is refused; they are
  an SPU job.
- **CD drive contention** between streamed music and loading is untouched.
- **The 51 ms frames.** 24 ms of work against a 33.3 ms budget; something
  occasionally eats the 9 ms of margin. Unexplained.
- **Nothing has been run under PCSX-Redux since the `mfc0` fix.**
- **Twinsen wears the wrong costume** — body 0, the scene-file default, instead
  of the prisoner shirt. Game state, not rendering.
- **The FLA movies are not on the disc.** M8 still has to decide whether the
  2023 remaster's `Common/Fla` is format-identical.
- **`InitGraphMcga` reallocates `Log`.** Only PLAYFLA calls it and the FLA path
  is dead, but it would strand the VRAM background's assumptions.
- **`MemoLog` is unused** in the PSX build.
- **Copper and Bopper are flat, deliberately.** 48 polygons of 19826. Tele
  (~300) wants a 32x32 noise texture. docs/M4-NOTES.md §3.

---

## Environment (installed, do not redo)

- **Docker image `psx-lba-psn00b`** — PSn00bSDK 0.24, `mipsel-none-elf-gcc`
  12.3.0, ninja, mkpsxiso, dumpsxiso. From `tools/docker/Dockerfile.psn00b`.
- **DuckStation** at `F:\DuckStation`, **portable** (`portable.txt` next to the
  exe, because this machine's `Documents` is redirected into OneDrive and a
  non-portable install lands its settings where it cannot find them).
  `F:\DuckStation\bios\scph1001.bin`. Settings that matter:
  `[CPU] ExecutionMode = Interpreter`, `[BIOS] PatchFastBoot = true` (our disc
  has no licence string), `[BIOS] TTYLogging = true`,
  `[Logging] LogToFile = true`.
- **PCSX-Redux** via winget, at
  `%LOCALAPPDATA%/Microsoft/WinGet/Packages/GrumpyCoders.PCSX-Redux_*/`.
  `emulator/Debug/FirstChanceException` in `%APPDATA%/pcsx-redux/pcsx.json` was
  set to 0 in session 2; put 7408 back to break into the debugger.
- **melonDS** and **devkitpro/devkitarm:latest** — the DS side of the M0
  calibration benchmark only.

## Gotchas found the hard way

Sessions 1–5's all still hold (`MSYS_NO_PATHCONV=1` before every `docker run`;
mkpsxiso takes `name="..."` literally; `CdOpenDir` returns `CdlDIR *` and
`CdlDIR` is `void *`; address 0 is kernel RAM; engine `.C` files need
`LANGUAGE C`; `make_cd.py` runs after the build and before mkpsxiso;
PCSX-Redux's TTY cannot be captured by shell redirection; `0xA0000` is not a
VGA aperture here; kill the emulator before rebuilding the ISO; a path
exercised with one shape is not a working path; `FlagVsync` shipped as 0; the
50 Hz tick runs during an 18.8-second scene load; `MenuComportement` reads the
live `Fire`). New:

- **`StoreImage` cannot read VRAM in PSn00bSDK 0.24.** It waits on GPUSTAT
  bit 28. Use `psx_video.c:VramRead`.
- **A palette is not a palette here.** The CLUT is applied when a rectangle is
  blitted, not on the way to the monitor, so the framebuffer holds RGB and a
  fade changes nothing already on screen. Anything that changes the palette
  has to re-present what it wants to keep.
- **DMA blocks are 16 words in both directions.** `PresentTile` has said so
  since M5 for uploads; the background transfers pad to 64-pixel columns for
  the same reason.
- **MCGA in this engine is a zoom, not a screen mode.** The surface stays
  640x480; only the 320x200 crop that gets presented changes. Three separate
  pieces of 1994 code say so and the port had assumed otherwise.
- **`Screen` is two things wearing one name** — the clean background and a
  300 KB scratch area. Anything that splits them has to answer for both.

## Rebuild and run, from the repository root

Kill the emulator first — `taskkill //F //IM duckstation-qt-x64-ReleaseLTCG.exe`
— then:

```sh
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" \
    -w /work/platform/psx psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DPSX_M5=ON -DPSX_SKIP_INTRO=ON \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'

python tools/make_cd.py "<your DOS install>/Speedrun/Windows"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" -w /work \
    psx-lba-psn00b mkpsxiso -y -o build/lba1psx.bin -c build/lba1psx.cue \
    build/iso.xml

"F:/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe" \
    -batch -fastboot build/lba1psx.cue
grep TTY "F:/DuckStation/duckstation.log"
```

Only the executable changes between builds, so `tools/make_cd.py` can be
skipped when the staged disc in `build/cd` is current: copy
`platform/psx/build/lba1psx.exe` over `build/cd/LBA1PSX.EXE` and run mkpsxiso.

The census needs the same DOS install and no console:

```sh
python tools/scene_census.py "<your DOS install>/Speedrun/Windows"
```

Build knobs, all in `platform/psx/CMakeLists.txt`:

| | |
|---|---|
| `-DPSX_M5=ON` | the engine's game loop on a fixed cube. Turns M3 off |
| `-DPSX_M6_AUTOPILOT=ON` | a scripted pad, and a report per step. Needs M5 |
| `-DPSX_BG_VRAM=ON` (default) | the clean background in VRAM. OFF is M6's rendering |
| `-DPSX_BG_SELFTEST=ON` | write a pattern into it at boot and read it back |
| `-DPSX_HQM_MEMORY=<bytes>` | the scene pool, 262144 by default |
| `-DPSX_M3=ON` (default) | one static scene instead of the main menu |
| `-DPSX_M4=ON` (default) | the scene's actors, on the GPU |
| `-DPSX_M3_CUBE=59` | which scene (0 default; 59 is the heaviest, and zooms) |
| `-DPSX_SKIP_INTRO=ON` | no bumpers, no FLA hunt — ninety seconds of boot |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_ENTRYLOG=ON` | what the hardware said at the vector, and the install |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path |
