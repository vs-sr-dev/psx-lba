# LBA1 on the PlayStation — feasibility

Session 1, 2026-08-24. Written before any code existed; revised the same day
with the M0 measurements, which are in [M0-NOTES.md](M0-NOTES.md). Where a
figure here was later measured, the measured value is the one quoted and the
original guess is noted, because the gap between the two is the interesting
part.

House rule of this family of ports: **derive, do not recreate.**

The official 1996 console port exists and is deliberately ignored. The
reference for this project is the **DOS CD version at 640x480**.

---

## 1. What is inherited

The Nintendo DS port is not a target that happens to resemble this one. It is
the direct parent.

| | DOS (origin) | DS (parent) | **PSX (target)** |
|---|---|---|---|
| Endianness | little | little | **little** |
| Data model | ILP32 flat | ILP32 | **ILP32** |
| Default `char` | signed | unsigned (`-fsigned-char`) | **signed** |
| Unaligned access | legal | legal and **silently wrong** (rotates) | **Address Error exception** |
| RAM | 4 MB+ | 4 MB | **2 MB** |

Consequences:

- **`engine/` (31k lines of C) and `translate/` (5.4k lines) are reusable
  almost entirely.** The sixteen original x86 assembly modules — the polygon
  filler, the 3D object renderer, the trigonometry tables, the texture mapper —
  are already translated to C.
- **Every fix in the DS port's DEVLOG applies here**: the unaligned reads in
  `GERELIFE`/`GERETRAK`/`FICHE`/`GRILLE`/`GRILLE_A`/`P_ANIM`/`DISKFUNC`, the
  punned store over `T_OBJET.Info`, the twelve-byte `T_REAL_VALUE` overlay, the
  `Malloc(-1)` stub that collapsed every memory pool. These are latent defects
  of the 1994 code, not ARM problems.
- **MIPS is stricter than ARM, and that is an advantage.** Where the ARM
  silently rotated an unaligned word, the R3000A raises an exception. Whatever
  is still hiding in the code will announce itself during M1 and M2.
- The DS port's `platform/sdl/` target (MinGW i686, ILP32 on purpose) stays the
  debugging bench with a real debugger. Anything touching `engine/` gets
  checked there first.
- **`LIB_CD` already exists in the original source.** The DOS CD release played
  its music from CD-DA tracks. The target is a CD. This is not a lucky
  coincidence: it is the machine the game was already written for, minus the
  RAM.

HAL surface to write, by analogy with the DS port's `platform/nds/`: about
eight files — `psx_video.c`, `psx_audio.c`, `psx_music.c`, `psx_sys.c`,
`psx_cd.c`, `psx_prof.c`, `psx_fastmem.c`, `stubs.c`.

Assets live in `Speedrun/Windows/` of a GOG install: 17 DOS files (~9.3 MB of
`.HQR` plus `SETUP.LST` and `LBA.CFG`) and `VOX/` (99 MB, 36 banks, five
languages). **Not** `Common/`, which is the 2023 remaster under the same file
names. Music: 33 tracks, available as MP3 in `Common/Music` (38 MB) — the same
source the DS port uses.

Licence: the source is **GPL v2**, so the toolchain has to be open. Psy-Q is
out on both legal and licence grounds.

---

## 2. The machine

| | |
|---|---|
| CPU | R3000A (LSI CW33300) at **33.8688 MHz**, ~30 MIPS peak |
| Cache | **4 KB instruction**; the data cache is **1 KB of manual scratchpad** at `0x1F800000` |
| FPU | none. **GTE (COP2)**: fixed-point matrices and vectors |
| RAM | **2 MB**, no MMU — address 0 is kernel RAM, not an unmapped page |
| GPU | **1 MB** VRAM (1024x512x16), no z-buffer, ordering by OT |
| Video | 256/320/368/512/**640** across, 240 or **480 interlaced**, 15 bpp |
| SPU | **512 KB**, 24 ADPCM voices, 44.1 kHz, hardware reverb |
| CD | 2x, **300 KB/s**, XA-ADPCM interleavable with data, CD-DA |

Three asymmetries matter more than everything else:

1. **RAM is half the DS's.** This is what decides the architecture.
2. **The CPU cannot write to VRAM.** Every pixel software produces goes through
   a GPU DMA, so a software rasteriser is *obliged* to keep its framebuffer in
   main RAM.
3. **The GPU and the GTE are free** against the CPU budget — if the 1994
   renderer consents to being mapped onto them.

---

## 3. The number that decides everything: RAM

From the DS port, allocations measured at steady state:

| block | KB | fate on PSX |
|---|---|---|
| Log 640x480 (software render target) | 300 | **removable only via the GPU path** |
| Screen 640x480 (clean background for restores) | 300 | stays in main RAM, streamed to VRAM in pieces |
| BufSpeak (voice) | 256 | **moves to SPU RAM** |
| BufferBrick | 353 | stays |
| BufCube 64x25x64x2 | 200 | stays |
| HQM (scene / grid / mask pool) | 391 | stays |
| Phys (MCGA 320x200 backbuffer, FLA movies) | 62 | stays |
| Anim + sprite + invobj pools (shipping build) | ~774 | compressible with CD paging |
| Binary (text+data+bss, ARM thumb -O2) | 414 | ~450-550 on MIPS |

DS total at steady state: **~3.07 MB used, 380 KB free of 4 MB.**

That does not fit in 2 MB. The honest arithmetic:

- Incompressible working set (BufferBrick + BufCube + HQM + Phys) = **~1.0 MB**
- Estimated MIPS binary = **~0.5 MB**
- Leaving **~0.5 MB** for the anim/sprite pools, text, stack and working heap.

So all of these are mandatory, not optional: `BufSpeak` and the samples move to
SPU RAM, and `Log` disappears.

> **Structural conclusion: on the PlayStation the RAM budget and the rendering
> architecture are the same decision.** They are not two problems. They are one.

---

## 4. The two possible architectures

### (A) Software rasteriser — pixel parity

Port `translate/` as it stands: `Log` 640x480 8bpp in main RAM, dirty
rectangles restored from a background copy, presented as an 8bpp CLUT texture
that the GPU expands.

RAM: ~2.2 MB, falling to ~1.75 MB once voice and samples move to the SPU. It
fits, barely.

Speed is the problem, and M0 measured it instead of guessing. Running the real
`s_poly.c` + `s_fillv.c` over identical synthetic work on both machines:
**the R3000A takes 1.82x the ARM9's time** on actor-sized polygons (the
feasibility guess was 2.5-3x, pessimistic by a third), and 2.21x on large ones.
Scaling the DS port's measured 6300-6600 us per real actor through that puts a
PlayStation software actor at **~15 ms** against a 20 ms frame.

One actor would consume three quarters of the frame. LBA scenes have four or
five. **Non-viable at 640x480**, and now non-viable by measurement.

### (B) GTE + GPU for actors, software for the background — recommended

A division of labour that follows the structure of the 1994 engine instead of
fighting it:

| element | who does it | why |
|---|---|---|
| Brick grid to 640x480 8bpp background | **software**, inherited `AffGrille` | once per scene, not per frame |
| Dirty-rectangle restore | GPU, 8bpp CLUT texture via a staging page | palette fade = rewriting 256 halfwords |
| 3D actor transforms | **GTE** (`RotList`/`TransRotList`/`RotMat`) | replaces `p_trigo` |
| Actor rasterisation | **GPU**, flat and gouraud primitives | 98.0% of BODY.HQR maps directly |
| Ordering | the GPU's OT | the engine **already** painter-sorts (`BUBSORT.C`) |
| Overbrick masks (`CPYMASK`) | GPU, textured quads with colour 0 transparent | needs a per-scene bake |
| 2D sprites, fonts, menus | GPU | |
| FLA movies, holomap | **software** (MCGA 320x200 path, `texture.c`) | static, cost irrelevant |

The RAM win matters as much as the CPU win: with no CPU rasteriser, `Log`
disappears — 300 KB, 15% of the machine.

**The fidelity cost turned out to be small.** The engine has ten polygon fill
modes, all operating on *palette indices* rather than colours. The census of
`BODY.HQR` (`tools/poly_census.py`, 19826 polygons) says:

- Gouraud 39.5%, gouraud+dither 38.5%, flat 20.1% — **98.03% map directly.**
- `Trans` and `Trame` map to hardware semi-transparency and dithering.
- `Marbre` is a gradient along the span: gouraud, in index space. 8 polygons in
  the entire game.
- Only `Tele` (305 polygons, a 4-colour noise that a small tiled texture
  reproduces), `Copper` (43) and `Bopper` (5) are genuinely awkward:
  **1.78% of the game**.
- 79.7% triangles and 19.2% quads, and the GPU has native quads, so 98.9% of
  polygons are one primitive each.

The remaining parity cost is the palette. With actors in RGB on the GPU,
palette fades — which the software path applied to everything for free — have
to be applied to vertex colours. Straightforward, but it is extra code and a
source of colour differences to check by eye.

---

## 5. 640x480: it works, and it is the natural choice

Not a stretch goal: it is the mode the game runs in. `Proj_ISO` has the scale
**hard-wired** (`Xp=((x-z)*24)>>9`), so the resolution is not a quality setting
but a statement about *how much world is visible*. The Falcon030 port learned
this the hard way: rendering at 320x240 natively gives a zoomed, unplayable
view. The DS renders internally at 640x480 and decimates for its screen.

The PlayStation has 640x480 interlaced in hardware, so the project's ideal
target and the path of least resistance are the same thing.

VRAM arithmetic, corrected by M0. Texture pages are 256x256 **texels** on a
64-halfword origin grid, so a resident 640x480 8bpp background needs three
pages across and two down — 384x512 halfwords, 393 KB, every column beside the
framebuffer, leaving 40 KB. Not enough. The layout that works:

| region | origin | size | purpose |
|---|---|---|---|
| framebuffer | (0, 0) | 640x480 | 600 KB, single buffered |
| staging page | (640, 0) | 128x256 halfwords | 64 KB, one 8bpp texture page |
| spare | (768, 0) | 256x512 halfwords | 256 KB, masks / sprites / fonts |
| CLUTs | (0, 480) | 640x32 halfwords | 40 KB |

The background stays in main RAM and dirty rectangles are DMAed through the
staging page. No layout allows a second 640x480 buffer (1.2 MB against 1 MB),
so the port is single-buffered with dirty rectangles — exactly what the DOS
build did against the VGA. The tearing is parity, not a regression.

Risks specific to the mode:

- **Interlace flicker.** LBA's backgrounds are detailed and a 480i CRT will
  shimmer on them. Mitigation: a light vertical filter when baking the bricks
  (free at runtime), or accept it — on emulators, upscalers and modern outputs
  it does not arise.
- **Overbrick masks** need a per-scene bake into a VRAM atlas. The staging-page
  layout leaves 256 KB for it, which the resident-background layout did not.

---

## 6. Audio and CD

The DOS CD model: CD-DA for tracks 1-9, XMI/MIDI for the rest (`MIDI_MI.HQR`
for MT-32, `MIDI_SB.HQR` for SoundBlaster), voices from the `.VOX` banks loaded
as samples, effects from `SAMPLES.HQR`.

The PlayStation plan:

- **Music**: pre-render all 33 tracks to **XA-ADPCM** interleaved into the CD
  image. Same choice as the DS port (which pre-renders to WAV and streams from
  SD) and it **removes the XMI synthesiser problem** entirely. It also sounds
  better than the 1994 OPL3 rendition.
- **Effects**: `SAMPLES.HQR` (2.4 MB) to VAG ADPCM, LRU pool in **SPU RAM**
  (512 KB). The DS port already runs a 320 KB LRU pool on the same logic.
- **Voices**: 99 MB, 36 banks, five languages. Loaded on demand from CD into
  SPU RAM — a line is a few seconds, roughly 40 KB in ADPCM.

**The real risk, and the only genuinely open one:** there is one drive head.
Streaming music competes with loading a voice or a scene. This is the classic
PlayStation problem; the ways out exist (XA interleave plus prefetching a
scene's lines at scene entry, which is close to what the engine already does
with its VOX banks) but they have to be designed, not improvised. M7, not
before.

Disc capacity: 9.3 MB of HQR plus ~20 MB of VOX for **one** language plus XA
music (stereo, ~66 minutes, ~300 MB) fits a CD comfortably. All five languages
on one disc is possible at mono or half-rate XA. Since the build is
bring-your-own-assets, that choice belongs to whoever runs the script.

---

## 7. Toolchain

Chosen, and built in M0:

| role | tool | licence |
|---|---|---|
| SDK + library | **PSn00bSDK 0.24** | MPL-2.0 |
| Compiler | `mipsel-none-elf-gcc` 12.3.0 | GPL |
| CD image | **mkpsxiso** | MIT |
| XA / VAG audio | **psxavenc** | |
| Build | CMake + Ninja in `tools/docker/Dockerfile.psn00b` | |
| Primary debugger | **PCSX-Redux** — GDB, symbols, memory breakpoints, BIOS TTY | |
| Timing check | **DuckStation** | |

Operational notes:

- The debug console is the BIOS TTY, which PCSX-Redux prints. It is the
  equivalent of `consoleDemoInit` on the DS's bottom screen — indispensable,
  and it went in during M0 rather than later.
- **Address 0 on the PlayStation is kernel RAM**, not an unmapped page. Null
  pointers that produced a guru meditation on the DS will corrupt silently
  here. Countermeasure: a watchpoint over `0x00000000-0x00010000` in PCSX-Redux
  for the whole bring-up.
- An exception handler that prints pc and registers is needed, like the DS
  port's `defaultExceptionHandler`.
- The instrumented heap with canaries from `nds_sys.c` gets ported wholesale.
  On 2 MB it matters more than it did on 4.
- **PCSX-Redux does not model GPU timing.** It is the right debugger and the
  wrong stopwatch for anything the GPU does; CPU-side measurements are usable,
  fill rates are not.

---

## 8. Verdict

**Feasible.** With two non-negotiable conditions and one open risk.

1. **The GPU/GTE path is not an optimisation, it is the entry ticket.** The
   software rasteriser fits 2 MB by a hair but runs one actor in ~15 ms of a
   20 ms frame. The GPU path solves the RAM budget and the frame rate *at the
   same time*, because it is what makes the 300 KB `Log` buffer disappear.
2. **It is paid for in parity**, but far less than feared: 1.78% of the game's
   polygons use a fill mode with no direct GPU equivalent, plus palette fades
   that have to be reapplied to vertex colours.
3. **Open risk: CD drive contention** between streamed music and loading. No
   known show-stopper, but it is the one area with no precedent in the family
   of ports.

Realistic expectation with architecture (B): **50 fps locked in normal
gameplay** — the game tick is already 50 Hz and decoupled from the frame rate —
with the drops concentrated on scene changes, where `AffGrille` stays in
software. Worst case is a full background rebuild: the DS measures 207 ms and
the memory-bandwidth ratio measured in M0 (1.46x) puts the PlayStation near
300 ms. Bad enough that the incremental rebuild already identified as a TODO on
the DS ("in the scroll case 90% of the bricks stay the same") goes on this
project's list too.

---

## 9. Milestones

| | goal | question it answers |
|---|---|---|
| **M0** | Docker + PSn00bSDK, 640x480i, BIOS console, polygon census, rasteriser calibration against the DS | How slow is software really? How much of the game uses unmappable fills? |
| **M1** | `engine/` + `translate/` compile for mipsel with a stubbed HAL. Boot to the main loop, log only | How much of the code objects to MIPS? |
| **M2** | HQR from CD (ISO9660), instrumented heap, `[MEM]` at the first scene | **The real RAM verdict.** |
| **M3** | `AffGrille` in software to an 8bpp background, staged to VRAM, on screen. Static scene visible | Does the 640x480 background hold up? |
| **M4** | Actors: GTE + GPU, with the SDL build's software path as the visual reference | Does parity hold? |
| **M5** | Overbrick masks, sprites, dirty-rectangle discipline, OT ordering | Does occlusion work? |
| **M6** | Pad input, exact 50 Hz tick, menus, text, five languages | Is it playable? |
| **M7** | Audio: VAG in the SPU, XA music, voices from CD | Does the drive contention resolve? |
| **M8** | FLA movies, holomap, Memory Card saves | Is it complete? |

M0, M1 and M2 are done; see [M0-NOTES.md](M0-NOTES.md),
[M1-NOTES.md](M1-NOTES.md) and [M2-NOTES.md](M2-NOTES.md). The RAM verdict this
document estimated has now been measured, and it lands where the estimate said
it would: the engine needs about 2258 KB, the heap is 1579 KB, and the three
moves this architecture already requires free 751 KB.
