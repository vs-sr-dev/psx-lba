# psx-lba — handoff, end of session 1 (2026-08-24)

The full study is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md); the milestone
notes are [M0](docs/M0-NOTES.md), [M1](docs/M1-NOTES.md), [M2](docs/M2-NOTES.md).
This file is only "where to pick it up".

---

## Verdict in three lines

Feasible. M0/M1/M2 are done and the engine runs on a PlayStation: it boots off
a CD image, reads its config, brings up its drivers, initialises 640x480
interlaced, and runs out of memory by 337 KB before the first scene. The three
moves the architecture already required free 751 KB, so it fits — with 72 KB to
spare and no room to be casual afterwards.

## Corrections from this session (they were my own wrong theses)

1. **The MIPS binary is not 450-550 KB, it is 335 KB.** The estimate came from
   scaling the DS's 414 KB of Thumb. Section garbage collection plus not
   linking libnds and calico more than pay for MIPS's fixed 32-bit encoding.
   That is 150 KB the RAM budget did not know it had.
2. **The R3000A is 1.82x the ARM9, not 2.5-3x.** Measured, same rasteriser,
   same synthetic work, both from main RAM. The conclusion is unchanged — a
   software actor still costs ~15 ms of a 20 ms frame — but the margin was
   overstated by a third.
3. **A resident 640x480 8bpp background does not fit VRAM.** The feasibility
   study said ~124 KB would be left; the real figure is 40 KB, because texture
   pages are 256x256 *texels* on a 64-halfword grid, so 640 texels wide needs
   three pages (384 halfwords), not 320. Replaced by the staging-page design:
   background in main RAM, one 64 KB page, 256 KB of VRAM genuinely free.
4. **The polygon fill modes were the biggest risk and are now the smallest.**
   98.03% of BODY.HQR is flat or gouraud and maps to the GPU directly; the
   awkward set is 1.78%. Two of the four "awkward" modes turned out not to be:
   Marbre is a gradient along the span (gouraud in index space, 8 polygons in
   the whole game) and Tele is 4-colour noise a 32x32 texture reproduces.

## Measured numbers (do not re-measure)

| | |
|---|---|
| BODY.HQR | 132 bodies, **19826 polygons**, 1015 lines, 272 spheres |
| polygon mix | 79.7% triangles, 19.2% quads; **98.03% flat/gouraud** |
| awkward fills (Tele/Copper/Bopper) | **353 polygons, 1.78%** |
| rasteriser, 150 gouraud tris | PSX **16826 us** / DS 9224 us = **1.82x** |
| same, large polys (r=48) | 2.21x — the memory system, not the ALU |
| memset 640x480 | PSX 7556 us / DS 5158 us = **1.46x** |
| binary | text 209K + data 3K + bss 122K = **335 KB** |
| heap at boot | **1579 KB** |
| engine wants, to first scene | 1525K allocated + 391K HQM + ~342K pools ≈ **2258 KB** |
| freed by the required moves | Log 300K + BufSpeak 256K + SampleMem 195K = **751 KB** |

`AffGrille` full rebuild: DS measures 207 ms; scaled by the 1.46x bandwidth
ratio the PlayStation should land near **300 ms**, not the 550 ms first guessed.
Unverified until M3 runs it.

## Environment (installed, do not redo)

- **Docker image `psx-lba-psn00b`** — PSn00bSDK 0.24, `mipsel-none-elf-gcc`
  12.3.0, ninja, mkpsxiso, dumpsxiso. Built from
  `tools/docker/Dockerfile.psn00b`; the Linux release archive bundles the whole
  toolchain, so it is the only download.
- **PCSX-Redux** via winget, at
  `C:\Users\utente\AppData\Local\Microsoft\WinGet\Packages\GrumpyCoders.PCSX-Redux_Microsoft.Winget.Source_8wekyb3d8bbwe\pcsx-redux.exe`
- **melonDS** at `D:\Emulatori\DS\melonDS` — needed only for the DS side of the
  M0 calibration benchmark.
- **devkitpro/devkitarm:latest** — same.
- **DuckStation is NOT installed.** Install it: PCSX-Redux is the right debugger
  and the wrong stopwatch (see gotchas).

## Gotchas found the hard way

- **`MSYS_NO_PATHCONV=1` in front of every `docker run`.** Git Bash rewrites
  `/work` into `C:/Program Files/Git/work` otherwise.
- **PCSX-Redux does not model GPU timing.** It reported a 640x480 clear in 54 us
  and 1000 gouraud triangles in 258 us. CPU-side measurements are usable, fill
  rates are not. Anything about GPU throughput needs DuckStation or hardware.
- **mkpsxiso takes `name="..."` literally.** Never pad the attribute for
  alignment: the spaces go onto the disc and the engine cannot open the file.
- **`CdOpenDir` returns `CdlDIR *`, and `CdlDIR` is `void *`.** Assigning one to
  the other compiles and silently does the wrong thing.
- **Address 0 is kernel RAM.** Keep a PCSX-Redux watchpoint over
  `0x00000000-0x00010000` for the whole bring-up. A null write here corrupts
  quietly where the DS raised a data abort.
- The engine's `.C` files read as C++ to CMake; the build sets `LANGUAGE C`
  explicitly. Anything added under `engine/` needs the same.

## Rebuild and run, from the repository root

```sh
# engine + platform
MSYS_NO_PATHCONV=1 docker run --rm -v "D:/Homebrew6/PSX-LBA:/work" \
    -w /work/platform/psx psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'

# stage the disc from your own install, then author it
python tools/make_cd.py "I:/GOG/Little Big Adventure/Speedrun/Windows"
MSYS_NO_PATHCONV=1 docker run --rm -v "D:/Homebrew6/PSX-LBA:/work" -w /work \
    psx-lba-psn00b mkpsxiso -y -o build/lba1psx.bin -c build/lba1psx.cue \
    build/iso.xml

# run
pcsx-redux -run -stdout -no-gui-log -iso D:/Homebrew6/PSX-LBA/build/lba1psx.cue
```

---

# M3 — the first thing this port draws off the disc

The goal: a static LBA scene on screen at 640x480. `AffGrille` composes the
brick grid into the 8bpp background in software (inherited, unchanged), and the
GPU puts it on the framebuffer through the staging page and the CLUT.

The M0 probe already proved that path works with synthetic data. M3 is the same
path with the real thing behind it — and the real thing has to survive being
parsed on MIPS first.

### 1. Get past the memory wall (blocking)

The engine currently dies before reaching a scene. In order of what it buys:

- **Free `Log`.** In M3 the actors are not drawn yet, so `Log` is still the
  compose target for the background and cannot go. Instead: allocate it once
  and confirm `Screen` and `Log` are not both needed for a static scene. If
  they are, M3 runs with pools clamped and accepts it.
- **Move `BufSpeak` (256 KB) out.** Nothing plays voices yet, so for M3 it can
  simply be a much smaller stub allocation. Cheap, unblocking, honest as long
  as it is written down.
- **Give `Malloc(-1)` a real answer.** It is `return 0;` in the community
  source, so every pool falls to its floor. Returning the largest allocatable
  block is correct, will make the pools *bigger*, and will therefore make the
  fit worse before it makes the port better. Do it, measure it, then decide
  what the PlayStation pool sizes should be — the DS does exactly this under
  `#ifdef PORT_NDS` in PERSO.C, and the PSX numbers will be smaller.

Order matters: unblock with the stub, then measure with `Malloc(-1)` honest.

### 2. Expect the alignment gauntlet

This is the part M1 could not reach and M2 only brushed. HQR archives are LZSS
compressed and `LIB_SYS/EXPAND.C` pulls its 16-bit control tokens from whatever
offset the stream happens to be at:

```c
ax = (UWORD)(esi[0] | (esi[1] << 8)); /* PORT: esi is often odd */
```

That one is already fixed — the DS port rewrote it as two byte loads. The ones
to expect are the callers the DS DEVLOG names: `GERELIFE`, `GERETRAK`, `FICHE`,
`GRILLE`, `GRILLE_A`, `P_ANIM`, `DISKFUNC`. Every one is a place the engine
walks a byte stream and pulls a word or a long out of it.

**MIPS will raise an Address Error rather than rotating silently.** That is the
good case. Install a real exception handler that prints `cause`, `epc` and the
faulting address before M3 gets far — it turns each of these from an afternoon
into a line number.

### 3. The steps

1. Exception handler with pc/cause/registers, and the watchpoint on page zero.
2. Unblock the heap (above), reach `InitDial` / the first scene load.
3. Watch `RESS.HQR`, `SCENE.HQR`, `LBA_BRK.HQR` decompress. Fix what faults.
4. `[MEM]` report at the first scene — this is the number the whole budget
   argument has been waiting for, and it replaces every estimate in
   FEASIBILITY.md §3.
5. Let `AffGrille` compose the background. Time it: the prediction is ~300 ms.
6. Present it through `PresentRect` — the path is already written in
   `psx_video.c` and already works with synthetic data.
7. Screenshot. That is M3.

---

## Carried forward

- **GPU fill rate is still unmeasured.** Needed before the actor budget can be
  stated as a number. Install DuckStation.
- **Copper and Bopper** (48 polygons in the entire game) need a decision:
  approximate with gouraud, or keep a software path for them.
- **The FLA movies are not on the disc.** They are not in the DOS install
  either — only `Common/Fla`, which belongs to the 2023 remaster. Establish
  whether those files are format-identical before relying on them. M8.
- **CD drive contention** between streamed music and loading is untouched and
  remains the one open risk with no precedent in the family of ports. M7.
- **The dirty-rectangle discipline is not exploited yet.** `psx_video.c`
  presents whole rectangles on request but the engine's own dirty-box tracking
  is not driving it. That is what makes 50 fps possible rather than merely
  plausible. M5.
- **`MemoLog` is unused** in the PSX build; check whether the engine's MCGA
  path expects it before M8.
