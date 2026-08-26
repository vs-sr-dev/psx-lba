# psx-lba — handoff, end of session 2 (2026-08-24)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md),
[M3](docs/M3-NOTES.md), [M4](docs/M4-NOTES.md). This file is only "where to
pick it up".

---

## Verdict in three lines

**A Little Big Adventure scene is on screen at 640x480 with Twinsen standing in
it** — background composed by the 1994 software path, actor rasterised by the
GPU — off a CD image made from a real DOS install. The engine reaches its first
scene with 1535 KB allocated against 1574 KB of heap, and the GPU actor path
costs zero further bytes, which is what merging `Log` with `Screen` was betting
on. Scene load 3.9 s, background compose 588 ms. There is no frame loop yet.

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
5. **The engine's sort IS the ordering table.** M4 was expected to need work
   reconciling `SergeSort`'s key with a PlayStation OT index. There was none:
   DOS had no depth buffer and neither does this machine, so both were already
   committed to a painter's algorithm and primitives submitted in the engine's
   own order land correctly. Replacing the rasteriser cost three `#ifdef`s.
6. **`TimerRef` was never being incremented**, and `Key` latched on the first
   START press. Together those are why the port had needed a pad press to get
   past the first bumper screen since M1. Both fixed; it boots unattended now.
   `MainLoop`'s frame regulator waits on `TimerRef`, so this would have stopped
   the game loop dead the first time it ran.
7. **The 39 KB margin is not a small-scene artefact.** Cube 59, the heaviest
   scene in the game, uses exactly the same 1535 KB — the big allocations are
   fixed-size buffers sized for the worst case up front. And **`AffGrille` is
   scene-dependent by 4x in the direction nobody would guess**: cube 59 has 50%
   more brick data than cube 0 and composes in 152 ms against 589. The cost
   tracks what is on camera, not what exists, so neither figure is a worst
   case.

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
| cube 0 objects | 23, of which 11 have bodies and **1 is on screen** |
| Twinsen, as GPU primitives | 105 polygons, 19 lines, 5 spheres |
| main RAM cost of the actor path | **zero** |
| cube 59 (the game's heaviest scene) | heap **1535 KB, identical to cube 0** |
| HQM on cube 59 | **144280** of 400000 |
| `ChangeCube` / `AffGrille` on cube 59 | 5775 ms / **152 ms** |
| worst brick set in the game (cube 59) | 351894 of BufferBrick's 361472 — **2.6% spare** |

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
| `-DPSX_M4=ON` (default) | draw the scene's actors on the GPU after it |
| `-DPSX_M3_CUBE=59` | which scene the harness opens (0 default; 59 is the heaviest) |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to test the crash handler |
| `-DPSX_EXC_TRACE=ON` | one BIOS `putchar` at the top of the fatal path, then stop |

---

# M5 — a frame loop

Everything so far runs once and stops. M5 is the first build that keeps
running: the engine's `MainLoop`, at 50 Hz, with the dirty-rectangle discipline
it already has driving what reaches VRAM.

That discipline is the difference between plausible and playable. Right now a
present is `PORT_PresentAll` — 300 KB through the staging page, 122 ms. The
engine has known since 1994 exactly which boxes changed (`FLIPBOX.C`:
`AddPhysBox`, `OptListBox`, `FlipBoxes`, `ClsBoxes`) and `psx_video.c` already
takes a rectangle. They have simply never been connected.

### The order to do it in

1. **Turn on `MainLoop`** (`-DPSX_M3=OFF` builds the menu; a middle setting that
   runs the loop on a fixed cube without the menu is probably worth having).
   `TimerRef` now ticks, so the frame regulator will work — that was M4's
   accidental prerequisite.
2. **Wire `FlipBoxes` to `PORT_PresentRect`.** `CopyBlockPhys` already is
   `PresentRect`; the question is whether the box list is tight enough to be
   worth it, which is measurable the moment the loop runs.
3. **Watch the interaction with the actors.** The background is `Log`; the
   actors are GPU primitives over it and exist nowhere else. So a box the
   engine re-presents *erases* the actors in it, which is correct — but it
   means present order matters: background boxes first, then every actor,
   every frame. `ClsBoxes` restoring from `Screen` into `Log` is a self-copy
   now (M3) and does nothing, which is also correct and worth re-checking with
   something moving.
4. **Then animation.** `SetInterAnimObjet2` is being called with a frame that
   never advances.

### Watch out for

- `InitGraphMcga` reallocates `Log` and would break the `Screen` alias.
- Anything that draws into `Log` expecting a later restore will silently do
  nothing. That is by design (M3) but it is a trap for 2D UI work.
- The GPU actor path holds no state between frames, so nothing needs freeing —
  but it also means every frame re-clips and re-emits all 129 primitives.

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
- **Copper and Bopper are decided: flat.** 48 polygons out of 19826 justify
  neither a software path nor a per-scanline GPU trick. Tele (~300 polygons) is
  flat for now and wants a 32x32 noise texture. See docs/M4-NOTES.md §3.
- **Twinsen wears the wrong costume** — body 0, the scene-file default, instead
  of the prisoner shirt. `InitBody`/`SearchBody` are present and running, so
  why it lands there is not yet known. Game state, not rendering.
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
