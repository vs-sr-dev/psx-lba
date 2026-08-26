# psx-lba — handoff, end of session 5 (2026-08-26)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md), [M4](docs/M4-NOTES.md), [M5](docs/M5-NOTES.md),
[M6](docs/M6-NOTES.md). This file is only "where to pick it up".

---

## Verdict in three lines

**Little Big Adventure is playable.** The pad drives it: Twinsen walks, turns,
acts, changes behaviour, opens the inventory, talks to the nurse and rides the
hoverpad, at 30 fps on a real scene off a real disc. One cube, 18.8 s to load
it, and the shadow leaves a trail — but it is a game now, not a demo.

## What changed this session

1. **M6: the pad.** Full mapping, tank controls, analog stick folded into the
   d-pad bits, disconnect handled. [M6 §1](docs/M6-NOTES.md).
2. **`-DPSX_M6_AUTOPILOT=ON`** — a scripted controller and a per-step report of
   what the hero did about it, so the input path is verified by grepping the
   TTY like everything else in this port. [M6 §2](docs/M6-NOTES.md).
3. **Input costs nothing.** 34 ms and 29 fps right through the walk; the M5
   frame is unchanged.
4. **A found bug, and it is a memory bug wearing a rendering costume.**
   `Screen` and `Log` have been the same allocation since M3, so `ClsBoxes`
   restores a box onto itself and nothing the software rasteriser draws is ever
   erased. Three symptoms: the shadow trail, the behaviour panel burning in,
   the inventory burning in. [M6 §5](docs/M6-NOTES.md).
5. **A modal is 3–6 seconds.** Nobody had timed one.

## Measured numbers (do not re-measure)

Cube 0, DuckStation, retail BIOS, interpreter, software renderer. M5's table
still stands; these are new.

| | |
|---|---|
| forward, 150 ticks (`J_UP`) | **+1547** world units, `GEN_ANIM_MARCHE` |
| backward, 100 ticks | **−1518** — the round trip lands **29 units** from the start |
| turn left, 60 ticks | beta **+204** of 1024 |
| turn right, 120 ticks | beta **+512** — half a circle |
| a frame while walking | **34 ms, 29 fps**, 1 rect, ~3000 px |
| behaviour panel, worst frame | **5851 ms** |
| inventory, worst frame | **3276 ms** |
| minimum L1 hold that changes behaviour | **> 1.4 s** (3.0 s works) |
| ticks burned inside one `ChangeCube` | close to **1000** — the tick does not pause |

## The three rules that come from experience, not preference

**Run under DuckStation with a retail BIOS, and under PCSX-Redux
`-interpreter`.** Neither is sufficient. PCSX-Redux hid a COP0 load delay for
three milestones; PCSX-Redux's CD model is four times too fast.

**Kill the emulator before building, not after.** mkpsxiso cannot overwrite the
image while DuckStation holds it open and reports that where nobody is looking.

**A harness that can measure the scenery will.** M6's `J_DOWN` step reported
zero movement twice before anyone noticed Twinsen was reversing into a wall he
had just walked up to. Reorder the test so the ground under it is known.

---

# M7 — the buffer, the archive, and the memory that pays for both

M6 turned the port into a game and the game immediately asked for the 300 KB
M3 took away. That is the same 300 KB the HQM reclaim frees, so M7's two items
are one item.

### Worth doing first, in this order

1. **Reclaim HQM, then give `Screen` its own allocation.** HQM is 400000 bytes
   and the worst scene measured uses 144280; `tools/scene_census.py` bounds
   every cube offline and the mask term is the only unknown. That takes the
   margin from 39 KB to near 290, against a 300 KB buffer — the exact figure
   has to be worked, and it is the whole of the fix for the shadow trail and
   both burned-in modals. [M6 §5](docs/M6-NOTES.md).
2. **`ChangeCube`, 18.8 s.** `LoadUsedBrick` does one seek per brick over a
   3.9 MB archive. The fix is the archive layout, not the streaming policy: the
   bricks a cube uses want to be contiguous. Walking through a door is the
   first thing that makes this a player's problem rather than a boot cost.
3. **The modals, 3–6 seconds.** `DrawMenuComportement` draws four animated
   bodies and `AffScene(TRUE)` recomposes 640x480 behind it. A behaviour change
   is a verb an LBA player uses every thirty seconds.
4. **The emit, 10 ms of 18.** `PORT_ActorPoly` clips every polygon with a
   Sutherland-Hodgman that runs even when the polygon is entirely inside, and
   resolves a palette entry per vertex. A trivial-accept test against the clip
   rectangle and a cached ramp lookup are both obvious and neither has been
   tried.
5. **A busier scene.** Every frame number in this project is one object on
   camera. Cube 59 (`-DPSX_M3_CUBE=59`) is the stress case and the primitive
   buffer's 290-triangle limit has never been approached.

## Carried forward

- **`Screen` must become a real buffer.** See above; it is item 1 because three
  visible artefacts depend on it.
- **CD drive contention** between streamed music and loading is untouched.
- **The 51 ms frames.** 24 ms of work against a 33.3 ms budget; something
  occasionally eats the 9 ms of margin. Unexplained.
- **Nothing has been run under PCSX-Redux since the `mfc0` fix.**
- **Twinsen wears the wrong costume** — body 0, the scene-file default, instead
  of the prisoner shirt. Game state, not rendering.
- **`GetAscii` returns nothing**, so the save-name entry has no characters. It
  wants the memory card (M8) first.
- **The FLA movies are not on the disc**, and `-DPSX_SKIP_INTRO=ON` now skips
  looking for them. M8 still has to decide whether the 2023 remaster's
  `Common/Fla` is format-identical.
- **`InitGraphMcga` reallocates `Log`** and would break the `Screen` alias —
  which item 1 above may make moot, one way or the other.
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

Sessions 1–4's all still hold (`MSYS_NO_PATHCONV=1` before every `docker run`;
mkpsxiso takes `name="..."` literally; `CdOpenDir` returns `CdlDIR *` and
`CdlDIR` is `void *`; address 0 is kernel RAM; engine `.C` files need
`LANGUAGE C`; `make_cd.py` runs after the build and before mkpsxiso;
PCSX-Redux's TTY cannot be captured by shell redirection; `0xA0000` is not a
VGA aperture here; kill the emulator before rebuilding the ISO; a path
exercised with one shape is not a working path; `FlagVsync` shipped as 0).
New:

- **`Log` and `Screen` being one allocation is not free, it is deferred.** It
  was recorded as a saving with a justification, the justification covered the
  actors and only the actors, and the bill arrived four milestones later as
  three unrelated-looking visual bugs. When a port drops a buffer the engine
  expects, write down what still reads it.
- **The 50 Hz tick runs during an 18.8-second scene load.** Anything scheduled
  on the tick — an autopilot, a timeout, a fade — gets a thousand ticks that
  the game never sees, because MainLoop is not iterating. Arm on the loop, not
  on the timer.
- **`MenuComportement` reads the live `Fire`, not the sampled `MyFire`,** and
  only after it has drawn four animated bodies. Any modal that spins on a
  hardware global has a minimum hold, and on this machine it is seconds.

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
| `-DPSX_M6_AUTOPILOT=ON` | a scripted pad, and a report per step. Needs M5 |
| `-DPSX_M3=ON` (default) | one static scene instead of the main menu |
| `-DPSX_M4=ON` (default) | the scene's actors, on the GPU |
| `-DPSX_M3_CUBE=59` | which scene (0 default; 59 is the heaviest) |
| `-DPSX_SKIP_INTRO=ON` | no bumpers, no FLA hunt — ninety seconds of boot |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_ENTRYLOG=ON` | what the hardware said at the vector, and the install |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path |
