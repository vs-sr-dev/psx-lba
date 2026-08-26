# psx-lba — handoff, end of session 3 (2026-08-26)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md), [M4](docs/M4-NOTES.md), [M5](docs/M5-NOTES.md). This
file is only "where to pick it up".

---

## Verdict in three lines

**Little Big Adventure runs.** The engine's own `MainLoop`, on a real scene off
a real disc, at **30 fps locked to the field**, with Twinsen animating, the
background composed by the 1994 software path and the actor rasterised by the
GPU. 1535 KB of a 1574 KB heap. It is not playable — no input handling has been
looked at, one scene, 18.8 s to load it — but it is running.

## What changed this session

1. **DuckStation, and the emulator's TTY is a file now.** Portable install at
   `F:\DuckStation`, retail SCPH-1001 BIOS, interpreter, Fast Boot,
   `LogToFile`. Every measurement below was grepped out of
   `F:\DuckStation\duckstation.log` instead of read off a terminal. See
   [M5 §1](docs/M5-NOTES.md).
2. **The port did not boot on a retail BIOS at all**, and the reason was ours:
   `mfc0` has a load delay slot on the R3000A and `psx_exc.S` used Cause in the
   next instruction, so the exception triage tested a dead register. Fixed.
   **Neither PCSX-Redux mode models that delay** — not the recompiler and not
   the `-interpreter` M3 switched to. Two emulators now, because they hide
   different things.
3. **The exception handler is verified.** M3's open item. Self-test on retail
   BIOS: AdEL, the right odd address, the right instruction at epc, after 9148
   exceptions chained through the vector.
4. **M5: the frame loop**, with the dirty-rectangle discipline driving VRAM.
5. **`ChangeCube` is 17-19 s, not 3.9.** PCSX-Redux's drive model is optimistic.
6. **The actor budget was never the problem it looked like.** M4's 94 ms was
   the harness transforming all 23 objects; the engine's preclip means the game
   transforms one.

## Measured numbers (do not re-measure)

Cube 0, DuckStation, retail BIOS, interpreter, software renderer.

| | |
|---|---|
| full build (menu) | text 221792 + data 3304 + bss 128214, heap **1574 KB** |
| at the first scene | **1535 KB**, 19 blocks, largest free block 114 KB |
| cube 59, the game's heaviest | heap **1535 KB, identical** |
| HQM used | 141936 (cube 0), 144280 (cube 59), of 400000 |
| worst brick set in the game | 351894 of BufferBrick's 361472 — **2.6% spare** |
| `ChangeCube` | **18808 ms** (PCSX-Redux said 3894) |
| `AffGrille` (640x480 compose) | 461 ms (cube 0), 152 ms (cube 59) |
| full present | 307200 px, **120 ms** |
| **a frame** | **34 ms, 29-30 fps, locked to two fields** |
| of which game logic | 4 ms |
| of which `AffScene` to present | 28 ms, ~9 of it the vsync wait |
| of which the actor replay | **0 ms** (one `DrawOTag`) |
| dirty rectangles per frame | **1**, about **2700 pixels**, **1 ms** |
| `AffObjetIso`, 136 entities | 18 ms: xform 2, build 5, sort 1, **emit 10** |
| objects transformed per frame | **1** (the engine preclips) |

## The two rules that come from experience, not preference

**Run under DuckStation with a retail BIOS, and under PCSX-Redux
`-interpreter`.** Neither is sufficient. PCSX-Redux hid a COP0 load delay for
three milestones; DuckStation found it in one run. PCSX-Redux's CD model is
four times too fast.

**Kill the emulator before building, not after.** mkpsxiso cannot overwrite the
image while DuckStation holds it open and reports that where nobody is looking.
One wasted run.

---

# M6 — the game, not one scene

M5 proved the loop. What it has not touched is everything that makes the loop a
game: input, scene changes from inside the loop, the scripts driving anything
other than Twinsen's idle.

### Worth doing first, in this order

1. **Input.** `MyJoy`/`MyFire`/`MyKey` are read every frame from `Joy`, `Fire`,
   `Key`, which `PORT_ScanInput` fills from the pad. Nothing has ever driven
   the game with it. This is the difference between a demo and a build someone
   can walk around in, and it is probably small.
2. **A second actor, and a busier scene.** Every number in this file is one
   object on camera. Cube 59 (`-DPSX_M3_CUBE=59`) is the stress case and the
   primitive buffer's 290-triangle limit has never been approached.
3. **The emit, 10 ms of 18.** `PORT_ActorPoly` clips every polygon with a
   Sutherland-Hodgman that runs even when the polygon is entirely inside, and
   resolves a palette entry per vertex. A trivial-accept test against the clip
   rectangle and a cached ramp lookup are both obvious and neither has been
   tried.
4. **Walk into the next room.** `ChangeCube` from inside the loop, with the
   fade, is the first thing that will exercise scene teardown — and it is 18.8
   seconds, which is the M7 problem arriving whether or not anyone is ready.

## Carried forward

- **`ChangeCube` is 18.8 seconds.** `LoadUsedBrick` does one seek per brick
  over a 3.9 MB archive. The fix is the archive layout, not the streaming
  policy: the bricks a cube uses want to be contiguous. Measurable today,
  M7 officially.
- **HQM is 250 KB of dead weight.** 400000 bytes allocated, 144280 used by the
  worst scene measured. `tools/scene_census.py` bounds every cube offline; the
  mask term is the only unknown and it is 35-49% of the brick bytes on the two
  cubes measured. Reclaiming it takes the margin from 39 KB to near 290.
- **The 51 ms frames.** 24 ms of work against a 33.3 ms budget; something
  occasionally eats the 9 ms of margin. Unexplained.
- **Nothing has been run under PCSX-Redux since the `mfc0` fix.**
- **Twinsen wears the wrong costume** — body 0, the scene-file default, instead
  of the prisoner shirt. Game state, not rendering.
- **The FLA movies are not on the disc**, and `-DPSX_SKIP_INTRO=ON` now skips
  looking for them. M8 still has to decide whether the 2023 remaster's
  `Common/Fla` is format-identical.
- **`InitGraphMcga` reallocates `Log`** and would break the `Screen` alias. Not
  reached yet; will be in M8.
- **`MemoLog` is unused** in the PSX build.
- **Copper and Bopper are flat, deliberately.** 48 polygons of 19826. Tele
  (~300) wants a 32x32 noise texture. docs/M4-NOTES.md §3.
- **CD drive contention** between streamed music and loading is untouched. M7.

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

Sessions 1 and 2's all still hold (`MSYS_NO_PATHCONV=1` before every
`docker run`; mkpsxiso takes `name="..."` literally; `CdOpenDir` returns
`CdlDIR *` and `CdlDIR` is `void *`; address 0 is kernel RAM; engine `.C` files
need `LANGUAGE C`; `make_cd.py` runs after the build and before mkpsxiso;
PCSX-Redux's TTY cannot be captured by shell redirection; `0xA0000` is not a VGA
aperture here; `Log` and `Screen` are one allocation). New:

- **Kill the emulator before rebuilding the ISO.** mkpsxiso fails silently
  while the image is open.
- **A path exercised with one shape is not a working path.** The present hung
  the machine the first time it was asked for a rectangle that was not the
  whole screen, because a VRAM upload has to be a whole number of 16-word DMA
  blocks and every previous present had been one by accident. Third time for
  this class: M2's directory listing worked only at the root, M3's `Malloc`
  canary only on even sizes.
- **`FlagVsync` shipped as 0.** On a machine that cannot double-buffer, that is
  the difference between a picture and a flicker. It is 1 here now.

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

Build knobs, all in `platform/psx/CMakeLists.txt`:

| | |
|---|---|
| `-DPSX_M5=ON` | the engine's game loop on a fixed cube. Turns M3 off |
| `-DPSX_M3=ON` (default) | one static scene instead of the main menu |
| `-DPSX_M4=ON` (default) | the scene's actors, on the GPU |
| `-DPSX_M3_CUBE=59` | which scene (0 default; 59 is the heaviest) |
| `-DPSX_SKIP_INTRO=ON` | no bumpers, no FLA hunt — ninety seconds of boot |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_ENTRYLOG=ON` | what the hardware said at the vector, and the install |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path |
