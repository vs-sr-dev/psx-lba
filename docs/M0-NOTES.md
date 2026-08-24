# M0 — the experiments that decide the architecture

Session 1, 2026-08-24. M0 exists to replace three guesses in
[FEASIBILITY.md](FEASIBILITY.md) with measurements. All three moved, and two of
them moved enough to change the design.

Everything below is reproducible from this repository; the game data is not
included and never is.

---

## 1. Toolchain

`tools/docker/Dockerfile.psn00b` — Debian + PSn00bSDK 0.24. The Linux release
archive bundles the `mipsel-none-elf` GCC 12.3.0 cross compiler, ninja,
mkpsxiso and dumpsxiso, so it is the only download; building GCC from source
would add most of an hour to every image rebuild for nothing.

Licences line up: the engine is GPL v2, PSn00bSDK is MPL-2.0, GCC is GPL,
mkpsxiso is MIT. Psy-Q would be usable neither legally nor compatibly.

Debugging is PCSX-Redux, which prints the BIOS TTY — the equivalent of the DS
port's bottom-screen console, and the channel every diagnostic in this project
uses. It runs a PS-EXE straight from the command line:

```sh
pcsx-redux -run -stdout -loadexe m0-hello/build/m0hello.exe
```

---

## 2. Which polygon fill modes does the game actually use?

`tools/poly_census.py`, over `BODY.HQR` from a GOG DOS install. 132 bodies
parsed, 0 rejected, and the walk lands 2 bytes from the end of every single
entry — the body layout derived from `translate/p_ob_iso.c` is exactly right.

19826 polygons, 1015 lines, 272 spheres, 17522 points.

| fill type | polygons | share | PSX GPU |
|---|---|---|---|
| Gouraud | 7835 | 39.52% | direct |
| Dith (gouraud + dither) | 7625 | 38.46% | direct |
| Triste (flat) | 3976 | 20.05% | direct |
| Tele (4-colour noise) | 305 | 1.54% | small tiled texture |
| Copper (gradient) | 43 | 0.22% | needs a call |
| Trans (transparency) | 15 | 0.08% | direct, hardware semi-transparency |
| Trame (screen door) | 14 | 0.07% | direct, dither |
| Marbre (span gradient) | 8 | 0.04% | direct, gouraud along the span |
| Bopper (gradient) | 5 | 0.03% | needs a call |

**98.03% of the game's polygons are flat or gouraud.** The feasibility study
worried that four of the ten fill modes are procedural effects on the palette
index with no GPU equivalent, and that the cost of losing them was unknown.
Measured, the awkward set (Tele + Copper + Bopper) is **353 polygons, 1.78%**,
and two of the four "awkward" modes turned out not to be awkward at all once
their source was read:

- `SVGAPolyMarbre` interpolates the palette index from one colour to another
  across each span. That is gouraud, in index space. 8 polygons in the whole
  game use it.
- `SVGAPolyTele` picks pseudo-randomly among four adjacent palette entries. A
  32x32 noise texture reproduces it closely enough at 305 polygons' worth of
  scrutiny.

Vertex counts: **79.7% triangles, 19.2% quads**, the rest (216 polygons, 1.1%)
between 5 and 15 vertices. The PSX GPU has native triangle *and* quad
primitives, so 98.9% of the game's polygons are a single primitive each and
only the long tail needs fan decomposition.

Lighting: 78% per-vertex normals, 20% face normal, 2% unlit.

> **Verdict: the GPU actor path costs almost nothing in fidelity.** This was
> the largest open risk in the feasibility study and it is now the smallest.

---

## 3. What does 640x480 do to VRAM?

`m0-hello/` sets the mode and prints the map. The interesting part is not that
it works — it is what the texture-page geometry does to the plan.

VRAM is 1024x512 halfwords. A 640x480 16bpp framebuffer takes 600 KB of it.
The feasibility study then proposed keeping a full 640x480 8bpp copy of the
composed background in the remaining space, and estimated ~124 KB left over.

That estimate was wrong, because texture pages are 256x256 **texels** and their
origin snaps to a 64-halfword grid. At 8bpp a page is 128 halfwords wide, so a
640-texel-wide image needs **three** pages across (384 halfwords, not 320) and
two down. The real cost of a resident background is 384x512 halfwords = 393 KB,
which is every column to the right of the framebuffer, leaving only the 640x32
strip underneath it — **40 KB**, not 124 KB. That is not enough for CLUTs plus
masks plus sprites plus fonts.

So the background does not live in VRAM. The layout that does work:

| region | origin | size | purpose |
|---|---|---|---|
| framebuffer | (0, 0) | 640x480 | 600 KB, single buffered |
| staging page | (640, 0) | 128x256 | 64 KB, one 8bpp texture page |
| spare | (768, 0) | 256x512 | 256 KB, masks / sprites / fonts |
| CLUTs | (0, 480) | 640x32 | 40 KB |

The background stays in main RAM, where the engine's `Screen` buffer already
is, and a dirty rectangle is DMAed into the staging page on its way to the
framebuffer. One page instead of six, 64 KB instead of 393 KB, and 256 KB of
VRAM that is genuinely free.

There is still no room for a second 640x480 buffer (1.2 MB against 1 MB), so
the port is single-buffered with dirty rectangles — which is what the DOS build
did when it blitted dirty rectangles to the VGA. The tearing is parity, not a
regression.

`m0-hello` also draws the composite — a CLUT-blitted background with gouraud
triangles over it — which is the shape of a real frame.

**Timings from `m0-hello` are not usable.** PCSX-Redux does not model GPU
timing: it reports a 640x480 clear in 54 us and 1000 gouraud triangles in
258 us, both far beyond what the hardware does. What the probe proves is that
the code paths work and the layout is valid. Fill-rate numbers need DuckStation
or real hardware.

---

## 4. How much slower is the R3000A than the ARM9?

This was the weakest number in the feasibility study: an unmeasured "2.5-3x"
that the entire software-versus-GPU argument rested on.

`m0-bench/` settles it. `bench_core.c` drives the **real** rasteriser —
`translate/s_poly.c` and `translate/s_fillv.c`, the C translations of Adeline's
`S_POLY.ASM` and `S_FILLV.ASM`, unmodified — over synthetic polygons sized from
the census (~100 px, the BODY.HQR mean) into a 640x480 8bpp buffer. The same
file compiles for the PlayStation (`main_psx.c`) and for the Nintendo DS
(`nds/main_nds.c`).

The DS build deliberately omits `-DPORT_NDS`, so `PORT_FASTCODE` collapses to
nothing and the rasteriser runs from main RAM exactly as it does on the
PlayStation. The shipping DS port puts that code in ITCM and gains ~23%; that
is a DS advantage the PlayStation cannot copy, since its 1 KB scratchpad will
not hold the fillers.

Both sides: GCC -O2, 32-bit instruction encoding (`-marm` on the DS, MIPS has
no other), `-fsigned-char`.

| workload | DS ARM9 67 MHz | PSX R3000A 33.87 MHz | ratio |
|---|---|---|---|
| one actor, 150 gouraud triangles | 9224 us | 16826 us | **1.82x** |
| one actor, 150 flat triangles | 6834 us | 12406 us | **1.82x** |
| one actor, 150 gouraud quads | 11919 us | 20428 us | 1.71x |
| 1000 gouraud triangles, r=12 | 61477 us | 112178 us | 1.82x |
| 1000 gouraud triangles, r=24 | 88890 us | 184251 us | 2.07x |
| 1000 gouraud triangles, r=48 | 150341 us | 331766 us | 2.21x |
| memset of the 640x480 buffer | 5158 us | 7556 us | **1.46x** |

Three things fall out of this:

1. **The R3000A is 1.8x the ARM9's time on actor-sized rasterisation**, not
   2.5-3x. The guess was pessimistic by a third.
2. **The ratio grows with polygon size** — 1.82x at r=12, 2.21x at r=48 — which
   is the memory system talking. Small polygons stay in cache on both machines;
   large ones do not, and the PlayStation has 1 KB of scratchpad where the DS
   has 16 KB of DTCM and a real data cache. Pure `memset` bandwidth is only
   1.46x apart, so this is latency on scattered writes, not raw throughput.
3. **It changes nothing about the conclusion.** Scaling the DS port's measured
   6300-6600 us per real actor (which includes transform and sort, and has ITCM)
   through these ratios puts a PlayStation software actor at **~15 ms**. The
   frame is 20 ms. One actor would eat three quarters of it, and LBA scenes have
   four or five.

> **Verdict: the software rasteriser is confirmed non-viable at 640x480, by
> measurement rather than by extrapolation.** Together with the RAM budget — the
> 300 KB `Log` buffer only disappears if the GPU draws the actors — the GTE/GPU
> path is settled.

The `memset` figure is worth keeping for a different reason: the background
composition (`AffGrille`) stays in software, and 1.46x is the honest scaling
factor for it. The DS measures a full background rebuild at 207 ms; the
PlayStation should land near 300 ms, not the 550 ms the feasibility study
guessed. Still bad enough that incremental rebuild on scroll goes on the list.

---

## 5. What M0 changed

| feasibility study said | M0 measured |
|---|---|
| 4 of 10 fill modes unmappable, cost unknown | 98.0% direct, awkward set is 1.78% |
| ~124 KB of VRAM spare with a resident background | 40 KB — the layout does not work; use a staging page instead |
| R3000A 2.5-3x slower than ARM9 | 1.82x on actor work, 1.46x on bandwidth |
| ~17-20 ms per software actor | ~15 ms |
| full background rebuild ~550 ms | ~300 ms |

The architecture is unchanged: GTE and GPU for actors, software for the brick
grid, staging page for the background restore. It is now chosen on evidence.

---

## 6. Open, and next

- **GPU fill rate is unmeasured.** PCSX-Redux cannot answer it. Needs
  DuckStation or hardware before the actor budget can be stated as a number.
- **Copper and Bopper** (48 polygons total) still need a decision — approximate
  with gouraud, or keep a software path for them.
- **CD drive contention** between streamed music and loads is untouched and
  remains the one risk area with no precedent in the family of ports.
- **M1** is the engine itself: compile `engine/` and the rest of `translate/`
  for mipsel with a stubbed HAL, and find out how much of the 1994 code objects
  to a machine that raises an exception on an unaligned load instead of quietly
  rotating the result.
