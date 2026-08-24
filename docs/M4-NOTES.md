# M4 — the actors, on the GPU

Session 2, 2026-08-24, after [M3](M3-NOTES.md).

> **Twinsen is on screen, rasterised by the PlayStation's GPU**, standing in
> the scene M3 composed, at his own position, transformed by the engine's own
> 1993 fixed-point pipeline. 129 primitives, and not one byte of main RAM.

This is the move the whole port was designed around, and it turned out to be
the smallest change in the project so far: three `#ifdef`s in
`translate/p_ob_iso.c` and one new file. The reason is in section 1.

---

## 1. Why replacing the rasteriser was nearly free

`AffObjetIso` does five things: transform the vertex cloud, compute normals,
build a display list of entities, **sort it back to front**, and scan-convert
each entity into `Log`.

Only the fifth moved. And the fourth is why that is enough:

**The DOS engine had no depth buffer, and neither does the PlayStation.** Both
machines are already committed to a painter's algorithm, so the engine's own
`SergeSort` — a descending-Z hybrid quicksort, transliterated 1:1 from the ASM
because tie order is visible — *is* the ordering table. Primitives submitted in
its order land in the right order. No OT, no z, no reconciliation between two
notions of depth. The handoff for this milestone listed "reconcile the engine's
sort key with the OT index" as real design work; there was none to do.

What did need care was three things the software filler got for free.

**Colour.** The engine works in 8bpp index space. A flat polygon carries a
palette index; a gouraud polygon carries one per vertex and the filler
interpolates the *index*, writing it straight out as a pixel. That only looks
like shading because LBA's palette is laid out as 16-entry ramps. The GPU
interpolates RGB, so every index is resolved through `PORT_PalRGB` at emit
time. Inside a ramp the two are the same picture. Across a ramp boundary the
GPU is arguably the more correct of the two — and either way a palette fade now
has to reach the actors as well as the CLUT, which it does, because both read
the same `PORT_PalRGB`.

The layout, for whoever needs it next: `P_OB_ISO` hands over `nbp` (colour, x,
y) `WORD` triplets. For flat fills the polygon colour is the low byte of the
entity's `coul` word; for gouraud the per-vertex index is the low byte of each
triplet's colour word, and the high byte is the 8.8 fraction the software DDA
used and the GPU does not need.

**Clipping.** GPU vertices are 11-bit signed and the GPU silently drops any
primitive wider than 1023 or taller than 511. The engine cheerfully produces
screen coordinates in the tens of thousands, so handing them over unfiltered
loses whole polygons at exactly the moment they fill the screen.
`ComputePoly_A` used to clip and is no longer on this path, so `psx_poly.c`
does its own Sutherland-Hodgman against the engine's `ClipXmin/Ymin/Xmax/Ymax`
— which keeps `SetClip` working for the water line and the zoom box, and
interpolates the vertex index at each crossing the same way the scan converter
would have.

A whole-polygon box reject runs first. That is not an edge-of-screen
optimisation: an LBA scene holds twenty-odd actors and the camera frames one of
them, so most of what arrives is an object that is somewhere else entirely.

**Winding.** The engine's vertices go round the perimeter; a PlayStation
`POLY_F4` is a strip, not a loop. Everything is fanned from vertex 0, which is
always right for a convex face and is what clipping produces anyway — it
routinely turns a quad into a 5- or 6-gon.

---

## 2. What is measured

Cube 0, Twinsen's house, the M3 scene with the actors drawn over it.

| | |
|---|---|
| objects in the scene | 23 |
| with a body to draw | 11 |
| **reaching the screen** | **1** |
| Twinsen, primitives | **105 polygons, 19 lines, 5 spheres** |
| his screen box | 281,170 .. 315,253 |
| polygons rejected by the clip | 549 |
| main RAM used, before and after | **1535 KB, unchanged** |

**The last row is the point of the milestone.** The GPU actor path costs zero
bytes of main RAM — no vertex buffers, no primitive lists, no render target —
which is precisely the assumption M3 spent its 300 KB on when it merged `Log`
with `Screen`. That assumption is now tested rather than believed.

One actor on screen out of 23 objects is correct, not a bug: LBA frames a small
window of a 64x64 cube and the rest of the cast is off-camera. It took an
honest counter to see that, though — `AffObjetIso` reports success when it
built at least one *entity*, a decision it makes before any of them meet the
clip rectangle, so an object entirely off screen still comes back "drawn". The
harness now counts primitives actually emitted on either side of each object.
The first version of this report claimed 11 objects drawn and was wrong.

**The timing is not reported, on purpose.** The figure that comes back is
~95 ms for 129 primitives, and it means nothing: PCSX-Redux's interpreter is
slow by construction, its recompiler cannot be used for correctness (M3 §5),
and M0 already established that neither models GPU timing at all — it clocked
a 640x480 clear at 54 us. The actor budget stays unmeasured until DuckStation
or hardware, which is the same sentence M0 and M3 both ended on and is now
blocking a third milestone.

---

## 3. Decisions taken, with their reasons

**The GTE is not used.** The engine's transform is its own 16-bit fixed-point
code in `translate/p_trigo.c` and `engine/LIB_3D`, with a group hierarchy,
per-group matrices and a normal pipeline feeding index-space lighting. Routing
that through the GTE means reproducing its exact rounding or accepting visible
differences in every actor's silhouette, and M0 never measured the transform as
the bottleneck — the rasteriser was. So the CPU keeps the maths and the GPU
gets the pixels. This is a deliberate stopping point, not an oversight: if the
frame budget in M5 says otherwise, the GTE is still there.

**The awkward fill modes.** M0's census of `BODY.HQR`: 98.03% of 19826 polygons
are flat or gouraud and map exactly. The remaining 1.78%:

| mode | polygons | what happens here |
|---|---|---|
| Dith | — | gouraud. Dith existed to hide banding on a 256-colour DAC; the GPU has no such problem |
| Marbre | 8 | gouraud. It is a gradient along the span, which is gouraud in index space |
| Trans, Trame | — | **GPU semi-transparency**, 0.5 back + 0.5 front. Trame was a 50% dither *because* a paletted framebuffer cannot blend. This one can |
| Tele | ~300 | flat, for now. M0 established a 32x32 noise texture reproduces it; that is a texture upload, and M4 does not upload textures |
| Copper, Bopper | 48 | flat. These cycle the colour per scanline inside a 16-colour ramp, and there are 48 polygons of them in the entire game |

Copper and Bopper have been an open decision since M0. Taking it: **flat**. 48
polygons out of 19826 do not justify either a software path on a machine with
no spare cycles or a per-scanline GPU trick, and if it ever shows in a
screenshot the entry above says exactly what was given up.

**Semi-transparency needs the blend mode set explicitly.** An untextured
primitive takes its blend equation from the GPU's current texture page, not
from the primitive, so `PORT_ActorBegin` pushes a `DR_TPAGE`. It happened to be
correct without that, because `psx_video.c` leaves mode 0 behind after a
background blit — a coupling that would not have been funny to find later.

**2D polygon drawing stays in software.** Only `AffObjetIso` moved. The one
other caller of the software filler, `MESSAGE.C`'s dialogue-window shading,
still renders into `Log`, and that is right: the dialogue box belongs in the
background image, not in a layer of primitives over it. The distinction is
worth stating because it is the port's actual architecture — `Log` is what the
GPU textures from, and anything that should survive a dirty-rectangle restore
has to be in it.

---

## 4. Two bugs the milestone flushed out, neither of them about actors

**`TimerRef` was never incremented.** The engine has two clocks and DOS drove
both from the same interrupt: `TimerSystem` free-running (`AMBIANCE.C` times CD
tracks against it) and `TimerRef` the *game* clock, the one
`SaveTimer`/`RestoreTimer` freeze by snapshot-and-rollback so a pause does not
advance the world. The port only ticked the first.

The symptom had been there since M1 and had been mistaken for normal: the boot
stopped on the first bumper screen and needed a pad press to continue.
`TimerPause()` spins on `TimerRef` with `if (Key OR Fire OR Joy) return;` for
company, so with `TimerRef` frozen at zero the only way out was a button. And
`MainLoop`'s own frame regulator waits on `TimerRef` too — the game loop would
have stopped dead the first time it ran.

**`Key` latched.** `PORT_ScanInput` set `Key = 1` on START and never cleared
it, so one press unstuck not just the screen it was on but every `TimerPause`
for the rest of the run. That is why a single press appeared to fix the whole
boot sequence, and why the real bug stayed hidden behind it. The engine reads
`Key` as "the scancode down right now"; it now goes back to zero on release.

The port boots unattended for the first time.

---

## 5. Known wrong, and not yet chased

**Twinsen is wearing the wrong costume.** The body drawn is index 0, whatever
`FICHE` assigned when it loaded the scene; at the start of the game he should
be in the prisoner shirt. `StartInitObj` does call `InitBody(GenBody, numobj)`
and `SearchBody` resolves a generic body to a real one, so the mechanism is
present and running — why it lands on the scene default is not yet known. It is
game state rather than rendering, and the harness deliberately never runs a
`MainLoop` iteration, so this is expected to be somewhere in that gap. Named
here rather than guessed at.

**Nothing animates.** `SetInterAnimObjet2` is called with the object's current
frame and nothing advances it. That is M5's problem, along with the frame loop
that would give it something to advance for.

---

## 6. Next

- **A frame loop.** Everything so far runs once and stops. M5 needs the engine's
  own dirty-rectangle discipline driving `PORT_PresentRect`, so that a frame is
  the background rectangles the actors dirtied plus the primitives over them,
  rather than 300 KB of full-screen present at 122 ms a go.
- **Measure the actors on real timing.** DuckStation or hardware. This is now
  blocking M0's fill rate, M3's handler verification and M4's frame budget at
  once, and it is the single highest-value thing left that is not code.
- **Tele as a texture.** ~300 polygons and a 32x32 upload, once the VRAM
  staging page has a neighbour to spare.
- **The costume.** See §5.
