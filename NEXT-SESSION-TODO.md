# psx-lba — handoff, end of session 7 (2026-09-08)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md), [M4](docs/M4-NOTES.md), [M5](docs/M5-NOTES.md),
[M6](docs/M6-NOTES.md), [M7](docs/M7-NOTES.md), [M8](docs/M8-NOTES.md). This
file is only "where to pick it up".

---

## Verdict in four lines

**Nothing visible is outstanding.** The trail and the burn-in were never the
background: the port built its CLUT without bit 15, so a colour that quantised
to black became `0x0000`, and a textured primitive does not draw `0x0000` —
the present has been skipping every black pixel since M1. One line. **The
modals draw their bodies now** (they were emitting GPU primitives into a
buffer only `AffScene` ever flushed), **the first frame of an animation costs
107 ms instead of 401**, and **actors are occluded by the scenery**, shadow
included — the bricks became a textured primitive drawn after the actor,
through a second palette whose index 0 is a hole. All four confirmed on the
screen. What is left is time and memory.

## What changed this session

1. **The self-test proved the transfers and cleared them.** Five phases at
   boot: full-screen round trip **0 of 307200 wrong**, tiled fetch and store of
   a deliberately unaligned rectangle 0 wrong inside and 0 outside. Every
   transfer-level explanation died in one run. [M8 §1](docs/M8-NOTES.md).
2. **`clut[i] = c ? c : 0x8000`.** Black was the GPU's "do not draw", so the
   framebuffer kept whatever was underneath. Shadow trail, behaviour panel and
   inventory: one cause, and not the one M7 was looking for.
   [M8 §2](docs/M8-NOTES.md).
3. **The modals bracket their own primitives.** `PORT_ActorBegin` /
   `PORT_ActorEnd` had two callers, both in `AffScene`. The panel's four
   characters were not static — they were never drawn.
   [M8 §3](docs/M8-NOTES.md).
4. **`DrawOneInventory` clips to its cell.** `Draw3dObject` sets no clip, and
   on this machine an overflowing body goes straight to the framebuffer where
   nothing will present over it again. [M8 §3](docs/M8-NOTES.md).
5. **`Size_HQR` deleted from the miss path** — it walked the archive for a
   size the next twenty lines discarded. **200 ms of every 401.**
   [M8 §4](docs/M8-NOTES.md).
6. **The CD sector cache is eight windows of two sectors**, 32 KB, LRU. It held
   one sector, so an HQR miss re-read the archive's index every time.
   [M8 §4](docs/M8-NOTES.md).
7. **`TILE_W` was 256 and the stage only has room for 128.** Every full-screen
   present had been writing 64 halfwords into the background's top-left
   corner. The self-test's fifth phase prices it: `4096 of 307200 wrong`
   before, 0 after. [M8 §5b](docs/M8-NOTES.md).
8. **Actors are occluded by scenery** — `platform/psx/psx_depth.c`, a second
   CLUT with index 0 transparent, and `CopyMask` generalised at both ends. Two
   overlays a frame, 1 ms, frame time unchanged. The source is the VRAM
   background and not `Log`, because a shadow is already drawn into `Log` by
   the time it asks. [M8 §6](docs/M8-NOTES.md).

## Measured numbers (do not re-measure)

Cube 0, DuckStation, retail BIOS, interpreter, software renderer.

| | |
|---|---|
| VRAM background, full-screen round trip | **0 of 307200 bytes wrong** |
| an HQR miss, before | 401 ms (probe 200, load 200) |
| an HQR miss, after | **107 ms worst, 1.4 ms for 13 of 21** |
| of a 201 ms miss: open / index / payload | 0.9 ms / **200 ms** / 0.06 ms |
| a sector that has to be sought to | about **100 ms** |
| a four-sector read against a one-sector read | about **+20 ms** |
| heap after the cache | 1540 KB, `use=1465K` before the loop |
| a frame while walking | 34 ms, 28 fps — unchanged by any of this |
| the depth pass, VRAM reads included | **1 ms**, 2 overlays a frame |
| the background after a full present | 4096 of 307200 wrong before `TILE_W`, **0** after |
| heap after the depth buffers | 1521 KB, `use=1465K` — **56 KB free** |
| heap in use at the first scene (M7) | 1400 KB of 1574 — **sampled elsewhere, see below** |
| HQM peak, cube 0 / cube 59 | 92940 / 96628 of 262144 |
| ClsBoxes out of the VRAM background | 1 ms |
| a fade, since it re-presents | 52 full presents, about 270 ms |
| cube 59, best frame | about 100 ms, 192 entities, zoom on |

## The rules that come from experience, not preference

Sessions 1–6's all still hold. New:

**When the self-test comes back clean, believe it.** Four phases said the
transfers were right, against two milestones of notes saying the full-screen
path was the suspect. The value of the clean result was that it made every
remaining explanation a non-transfer one, and there turned out to be exactly
one.

**The player's description was the diagnosis.** *Only the shadow, and only its
black pixels* is not a bug report, it is the mechanism — and the session's
other finding started from someone noticing a hitch that no measurement had
ever been pointed at. Ask what the screen looks like, in those words.

**Measure the halves before optimising the whole.** Splitting a 401 ms miss in
two said one half was free to delete; splitting the rest into open, index and
payload put the cost in none of the places the code looks expensive.

**A path exercised hundreds of times can still have an unexercised shape.** The
present ran for seven milestones and was never once asked to write a black
pixel over a different one — and it had been writing into the background's
corner the whole time, above the play area where nobody looks.

**A comment that states a constraint is worth checking against the constant
under it.** Twice in M8 the prose was right and the code was not.

---

# M9 — the heap, the archive layout, and the things with no workspace

### Worth doing first, in this order

1. **The heap, before anything else.** M8 spent 50 KB of headroom on two
   features — 32 KB of CD cache and 19 KB of depth buffers — and the build now
   reports 1521 KB with `use=1465K`, so **56 KB free**. M7's `1400 KB in use`
   was sampled at a different moment, so the first job is one build that
   measures both at the same point. Nothing else on this list should be
   planned until that number is real, and the holomap's workspace is on the
   other side of it. [M8 §5, §6](docs/M8-NOTES.md).
2. **`ChangeCube`, 18.8 s**, and the 107 ms that is left on an animation, are
   the same problem: **the archive layout**. `LoadUsedBrick` does one seek per
   brick over a 3.9 MB archive and a seek is 100 ms. The bricks a cube uses
   want to be contiguous, and so do the animations a body uses. Untouched, and
   now by far the largest number in the port.
3. **The modals, 3–6 seconds.** They no longer burn in and their bodies now
   animate, so what is left is the price: `DrawMenuComportement` draws four
   animated bodies and `AffScene(TRUE)` recomposes 640x480 behind it.
   [M6 §4](docs/M6-NOTES.md).
4. **The fade is 52 full presents**, about 270 ms. Presenting every second or
   fourth step would look the same for a quarter of the cost.
   [M7 §5](docs/M7-NOTES.md).
5. **Cube 59 at 100 ms.** The zoom re-presents 64000 pixels every frame through
   the tile path, and the scene carries 192 entities. Two different problems
   sharing one number; separate them before optimising either.
6. **The emit, 10 ms of 18.** `PORT_ActorPoly` clips every polygon with a
   Sutherland-Hodgman that runs even when the polygon is entirely inside, and
   resolves a palette entry per vertex. A trivial-accept test and a cached ramp
   lookup are both obvious and neither has been tried.

## Carried forward

- **The holomap has no workspace.** It used to lay 200 KB out inside `Screen`;
  `Screen` is 64 KB now and the holomap refuses instead. See point 1 before
  assuming there is room. [M7 §3](docs/M7-NOTES.md).
- **The worst frame went from 51 ms to 66.** The average did not move and
  nothing points at the depth pass, which costs 1 ms. Unexplained, and it is
  the same shape as the 51 ms that was already unexplained.
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
- **The FLA movies are not on the disc.** Still undecided whether the 2023
  remaster's `Common/Fla` is format-identical.
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
  `[Logging] LogToFile = true`. The pad is mapped in
  Settings → Controllers, by hand.
- **PCSX-Redux** via winget, at
  `%LOCALAPPDATA%/Microsoft/WinGet/Packages/GrumpyCoders.PCSX-Redux_*/`.
  `emulator/Debug/FirstChanceException` in `%APPDATA%/pcsx-redux/pcsx.json` was
  set to 0 in session 2; put 7408 back to break into the debugger.
- **melonDS** and **devkitpro/devkitarm:latest** — the DS side of the M0
  calibration benchmark only.

## Gotchas found the hard way

Sessions 1–6's all still hold (`MSYS_NO_PATHCONV=1` before every `docker run`;
mkpsxiso takes `name="..."` literally; `CdOpenDir` returns `CdlDIR *` and
`CdlDIR` is `void *`; address 0 is kernel RAM; engine `.C` files need
`LANGUAGE C`; `make_cd.py` runs after the build and before mkpsxiso;
PCSX-Redux's TTY cannot be captured by shell redirection; `0xA0000` is not a
VGA aperture here; kill the emulator before rebuilding the ISO; a path
exercised with one shape is not a working path; `FlagVsync` shipped as 0; the
50 Hz tick runs during an 18.8-second scene load; `MenuComportement` reads the
live `Fire`; `StoreImage` cannot read VRAM in PSn00bSDK 0.24; a palette is not
a palette here; DMA blocks are 16 words in both directions; MCGA is a zoom, not
a screen mode; `Screen` is two things wearing one name). New:

- **`0x0000` is not black, it is "do not draw".** A textured primitive skips
  any texel whose CLUT entry is exactly zero. Anything the port blits through
  the CLUT has to keep bit 15 on black, and anything that wants a transparent
  index needs its own CLUT.
- **`PORT_ActorBegin` / `PORT_ActorEnd` are not optional and not automatic.**
  Any code outside `AffScene` that calls `AffObjetIso` and then presents its
  own rectangle has to bracket itself, or its primitives go into a buffer
  nothing flushes.
- **The engine's "draw wide into `Log`, blit narrow" idiom leaks here.** On DOS
  the overspill was invisible because it was never blitted; a GPU primitive
  goes straight to the framebuffer. Every 3D draw wants a clip matching the
  rectangle its caller presents.
- **A CD seek is 100 ms.** Not 10, not 30. Any per-item seek in a loop is the
  whole cost of that loop, and the answer is the archive layout.
- **The engine reads more than one file at a time.** The one-sector cache's own
  comment predicted the symptom would be slow loading. It was.
- **A comment that states a constraint is worth checking against the constant
  under it.** Twice in M8 the prose was right and the code was not: the CD
  cache, and `TILE_W` against the width `STAGE_X`'s comment claims.
- **`Log` is the clean background inside an actor's box, and is not inside a
  shadow's.** `ClsBoxes` restores it at the top of the frame, but TYPE_SHADOW
  draws into `Log` before calling `DrawOverBrick`. Anything that needs the
  clean pixels mid-frame has to read VRAM.

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
| `-DPSX_BG_SELFTEST=ON` | four round trips through it at boot, before anything else |
| `-DPSX_HQR_TRACE=ON` | what every resource cache miss cost, split three ways |
| `-DPSX_MODAL_TRACE=ON` | each presented rectangle, and the clip the primitives after it were emitted under |
| `-DPSX_DEPTH=ON` (default) | occlude actors with the scenery in front of them. 19 KB |
| `-DPSX_HQM_MEMORY=<bytes>` | the scene pool, 262144 by default |
| `-DPSX_M3=ON` (default) | one static scene instead of the main menu |
| `-DPSX_M4=ON` (default) | the scene's actors, on the GPU |
| `-DPSX_M3_CUBE=59` | which scene (0 default; 59 is the heaviest, and zooms) |
| `-DPSX_SKIP_INTRO=ON` | no bumpers, no FLA hunt — ninety seconds of boot |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_ENTRYLOG=ON` | what the hardware said at the vector, and the install |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path |
