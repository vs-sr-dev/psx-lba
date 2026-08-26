# M7 — the buffer the machine did not have, and where it turned out to be

M6 found the bug and priced it: `Screen` and `Log` have been one allocation
since M3, so every background restore in the engine is a copy onto itself, and
the shadow trail, the burned-in behaviour panel and the burned-in inventory are
all that one line. The price was 300 KB and the heap had 39, with a note saying
the HQM reclaim would cover it.

**It does not.** The reclaim is real — 135 KB, measured — and it is still less
than half of what a second 640x480 buffer costs. Main RAM does not have the
buffer and no rearrangement of it does.

VRAM does. The framebuffer uses 640 of the 1024 halfwords across, and 640x480
at 8bpp is 320 halfwords by 480 lines, which is exactly what is left. The clean
background now lives beside the picture it is a copy of, and `CopyScreen` and
`CopyBlock` turn into DMA when either side of the copy is `Screen`.

Restoring the frame's dirty boxes out of it costs **1 ms**. The frame is still
34 ms and 29 fps.

**Partly.** Walking forward and turning leave no trail any more; walking
*backwards* still does, and both modals still burn in. §6 has what is known
about each. The per-box restore works and the full-screen one does not appear
to, which is a narrow enough gap to be the next thing anyone picks up.

And the bug that had to be found before any of this could be looked at: the
port has been presenting the background **while the palette was black** since
M1, and nobody saw it because the only builds anyone watched had a modal
forcing a redraw. §5.

---

## 1. HQM was 400 KB because of one function call

`tools/scene_census.py` used to bound a scene's pool usage and say so honestly:
the brick mask could not be computed offline, so it was counted at the full
size of the brick data it covers. That bound came out at 373878 bytes for the
worst scene against a 400000-byte pool, and the console then measured 144280
actually used. Two numbers a factor of 2.6 apart, and the gap was written off
as the estimate being pessimistic.

The estimate was not pessimistic. **It was measuring a different moment.**

```c
size = LoadUsedBrick(sizegri);          /* the brick bank: 351894 bytes  */
HQM_Alloc(size, &BufferMaskBrick);      /* ask the pool for all of it    */
size = CreateMaskGph(BufferBrick, BufferMaskBrick);   /* write a fifth   */
HQM_Shrink_Last(BufferMaskBrick, size); /* give the rest back            */
CHECK_MEMORY                            /* and only now, measure         */
```

The engine's own accounting samples after the shrink, so nothing it has ever
printed could see the peak. On cube 59 the pool holds **384696 of 400000
bytes** for the length of `CreateMaskGph`, and settles at 96628. Fifteen
kilobytes of margin on a pool everyone believed was two-thirds empty.

### Measuring the mask instead of over-allocating it

`translate/graphmsk.c:SizeGraphMsk` is `CalcGraphMsk` with the stores taken
out — the same walk of the same brick data, counting bytes. `GRILLE.C` runs it
first and allocates exactly that, then checks that `CreateMaskGph` wrote what
was reserved, because a disagreement would be a heap overrun into the bodies
allocated next rather than an error.

The census does the same walk in Python, so it now computes the mask exactly:

| cube | gri | bll | bank | mask | HQM peak | with the fix |
|---|---|---|---|---|---|---|
| 59 | 15970 | 6014 | 351894 | 63827 | **384696** | 96628 |
| 117 | 22932 | 6178 | 333206 | 69084 | 366672 | **102548** |
| 0 | 19732 | 6346 | 236142 | 56736 | 272348 | 92940 |

The console agrees to the byte. Cube 0 reports
`gri 19732 bll 6346 bank 236142 mask 56736 -- HQM 92940`, cube 59 reports
`96628`, and both are what the tool printed before the disc was mastered. The
remaining difference from the 141936 and 144280 the engine measures over a
session is BODY.HQR: `SearchBody` pulls a body into the pool the first time an
actor needs one, about 48 KB either way, and that is the one term a census
cannot bound because it depends on what happens in the scene.

**HQM is now 262144.** Worst case is 102548 of grille and scene, leaving 155 KB
for a body term that has never exceeded 49. `-DPSX_HQM_MEMORY=<bytes>` bisects
it, and a refusal is now reported by name instead of returning a null pointer
into `CreateMaskGph`.

Heap in use at the first scene: **1400 KB, down from 1535.**

## 2. Why 300 KB is not there, with the arithmetic

| | |
|---|---|
| heap | 1574 KB |
| in use, before M7 | 1535 KB |
| in use, after the HQM reclaim | **1400 KB** |
| free | **174 KB** |
| a second 640x480 buffer | **300 KB** |

The next-largest thing to reclaim is `BufferBrick`, 353 KB, and cube 59 uses
351894 of it — 2.6% spare. Folding it into HQM and sizing one pool from the
census saves perhaps 80 KB more, against a worst-case scene that would then
have no margin at all for bodies. That is 255 KB for a 300 KB buffer, bought
with the port's entire safety margin.

So the answer is not in main RAM, and M6's "290 KB against a 300 KB buffer,
close enough that the exact figure has to be worked" was optimistic in two
directions at once: the reclaim is smaller than it thought, and it did not know
about the scene, fiche and body terms sharing the pool.

## 3. The background, in VRAM

```
        0                     640   704                    1023
      0 +-----------------------+-----+----------------------+
        |                       |stage|                      |
        |    framebuffer        +-----+   clean background   |
        |    640x480 16bpp      |     |   640x480 8bpp       |
        |                       |     |   320 hw x 480       |
    479 |                       |     |                      |
    480 +--CLUT--+--------------+-----+----------------------+
    511 |        |                                           |
```

704 + 320 is exactly 1024, so the background is a contiguous rectangle and a
full save or restore is one transfer: 640 bytes per row of `Log` is precisely
the 320 halfwords the region is wide, so main RAM is already in the layout VRAM
wants. The staging tile stays where M1 put it, at x 640, narrowed from 256
texels to 128 to leave room — the present pays for that with five tiles per row
instead of three, which costs nothing measurable.

The engine reaches all of this without knowing:

| engine | port |
|---|---|
| `CopyScreen(Log, Screen)` | `PORT_BgStoreAll` — one upload |
| `CopyScreen(Screen, Log)` | `PORT_BgFetchAll` — one download |
| `CopyBlock(.., Screen, .., Log)` | `PORT_BgFetch` — tiled |
| `CopyBlock(.., Log, .., Screen)` | `PORT_BgStore` — tiled |

Tiles move on 64-pixel column boundaries because the DMA block rule from
`PresentTile` applies in both directions. A store copies the widened span
straight out of `Log`, which is safe because every caller has a clean `Log`
around its rectangle. A fetch reads wide and **writes narrow** — restoring more
than was asked would erase whatever a modal drew just outside its own
rectangle, and `GAMEMENU.C` does that between widgets.

### `StoreImage` does not come back, and the disassembly says why

Both `StoreImage` and `StoreImage2` hang. PSn00bSDK 0.24 routes them through
the same `_dma_transfer` as the upload, and before it starts the transfer it
waits:

```asm
8003e6c0:  lw   v0, 0xbf8010a8      ; DMA2 CHCR — still busy?
8003e6cc:  bnez ...                 ; then keep waiting
8003e6d4:  lw   v0, 0xbf801814      ; GPUSTAT
8003e6dc:  and  v0, v0, 0x10000000  ; bit 28, "ready to receive DMA block"
8003e6e0:  bnez ...                 ; go
           b    8003e6c0            ; else keep waiting
```

Bit 28 is the right bit for a transfer into VRAM. For one coming out, the bit
that says the GPU has pixels waiting is **27**, "ready to send VRAM to CPU",
and bit 28 never rises — so the wait never ends. The direction register, the
command and the DMA setup a few instructions later are all correct; only the
handshake is the upload's.

It is the same shape as the 16-word block rule this file already documents: the
write path is exercised by everything and the read path by nothing.

`psx_video.c:VramRead` does it directly — DMA off, `GP0(C0h)` with the
rectangle, wait on bit 27, pull words out of `GPUREAD`. A dirty box is a few
hundred words and it does not show up in the frame.

**`-DPSX_BG_SELFTEST=ON`** writes a known pattern into the background at boot
and reads it straight back, before the scene loads or a frame is presented.
That is what separated "VRAM reads do not work here" from "VRAM reads do not
work while the frame is busy", and it took one run to say `0 of 256 bytes
wrong` where four runs of guessing inside a frame had said nothing at all.

### What `Screen` is now

A 64 KB scratch buffer, and a distinct pointer — which is the part that makes
the two copy primitives able to tell a background operation from a plain one.

`Screen` was always two things wearing one name: the clean background, and a
300 KB scratch area that `LoadUsedBrick`, the holomap, the FLA player and the
voice loader all borrow. M3's alias served the scratch role perfectly and
silently broke the other one. M7 splits them, and the scratch role is sized for
its only surviving user: `LoadUsedBrick` wants 34864 bytes of brick offsets and
20000 of flags, so `OFFSET_BUFFER_FLAG` is 40960 here rather than 153800, and
both fit in 64 KB with room over. It is checked now, too — a modded LBA_BRK
with a bigger offset table is an error message rather than a heap overrun.

The other borrowers each got an answer:

- **the ten `.PCR` loads** — `Load_HQR` sees a destination of `Screen`, loads
  into `Log` instead, and stores the background after. The `CopyScreen(Screen,
  Log)` that follows every one of them then reads the picture back. No call
  site changed.
- **the holomap** lays a globe texture, coordinate table, altitude map and
  bodies out inside `Screen`. It never worked here — aliased onto `Log` it drew
  over the live background — and it now says so instead of writing 200 KB past
  a 64 KB buffer. It wants an allocation and a port of its own: M8.
- **packed voices** stage up to 256 KB. The VOX files are not on the disc and
  the voices are an SPU job when they arrive.
- **the FLA player** borrows `Log`, which is the surface it is about to
  overwrite anyway.

`-DPSX_BG_VRAM=OFF` puts `Screen` back on `Log` and switches all of it off,
which is how to tell a background bug from anything else.

## 4. Cube 59 runs, and it took two more bugs

The census's worst scene had only ever been run under the M3 harness, which
loads a scene and composes it. Under M5 it runs life scripts, and cube 59's
raises `LM_ZOOM` on entry.

**An unaligned load, immediately.** `CopyBlockPhysMCGA` picks a 320x200 window
out of the 640x480 surface centred on the hero, and hands `CopyBlockMCGA` an x
of whatever `x - 160` clamps to — odd as often as not. The 1993 assembly copied
dwords from there. On x86 that is slow; on MIPS it is an AdEL. `memcpy` is what
the loop was, and it knows about alignment.

**And MCGA is not a screen mode.** The port's `InitMcgaMode` set `Screen_X` and
`Screen_Y` to 320x200 and rebuilt `TabOffLine`, which put the composition and
the window on two different surfaces: the scene drawn into the top-left 320x200
of `Log`, read back out with a stride of 640. What the engine means by MCGA is
a **zoom** — same 640x480 world, composed the same way, with a 320x200 crop of
it on screen so everything looks twice as big. `CopyBlockPhysMCGA` clamps to
`640-320` and `480-200`, `CopyBlockMCGA` reads with a stride of 640, and
`AffScene` centres the crop on the followed actor's projected position. Three
witnesses, all saying the surface does not change.

So it does not, any more. Only what is presented does: `Phys`, the crop, in the
middle of the field — and on DOS filling `Phys` **was** presenting it, because
`Phys` was `0xA0000`. Here something has to push it, which is
`PORT_PresentMcga`.

Cube 59 then loads and runs. It is slow — around 100 ms a frame at best, with
192 entities and the crop re-presented whole every frame — and that is a
measurement for M8, not a milestone for M7.

## 5. The screen was black, and had been since M1

Every session before this one described the port as showing a scene. It does
not, and it never did on its own: it shows **whatever has been drawn since the
last palette change**, over black.

On DOS the palette lived in the VGA DAC and was applied on the way out to the
monitor, so a fade was 256 register writes and every pixel already on screen
changed colour with them. Here the CLUT is applied by the GPU at the moment a
rectangle is blitted, and what lands in the framebuffer is RGB. A fade tints
everything drawn *after* it and nothing drawn before.

So the sequence the engine runs at every scene change —

    compose into Log, fade to black, present, fade up

— presents 640x480 of black and then changes a palette that no longer applies
to it. From there the screen only fills in where something happens to redraw:
Twinsen, because he is a GPU primitive re-emitted every frame; a dirty box,
because `FlipBoxes` presents it; a dialogue window, because it presents itself
when it opens.

**Which is why nobody saw it.** M6's autopilot opens the inventory, a modal
forces a full recompose and present with the palette already up, and the scene
appears. Every screenshot anyone has looked at since M5 was taken after that
had happened.

`ApplyPalRange` now re-presents when more than one colour changes. Single
colour writes do not — `PalOne` is used for HUD accents that carry their own
`CopyBlockPhys`, and a 640x480 re-blit each would be 16 ms apiece.

It costs the fade: 52 full presents across one, about 270 ms of the frame it
lands in, and it looks slow. Presenting on every second or fourth step of a
fade would look the same and cost a quarter as much; nobody has tried it.

## 6. Where the three artefacts actually stand

| | |
|---|---|
| shadow trail, walking forward or turning | **gone** |
| shadow trail, walking backwards | still there |
| behaviour panel burn-in | still there, and its backdrop reads as transparent where it should be opaque |
| inventory burn-in | still there |
| Twinsen occluded by scenery | never worked here, and cannot without §7 |

The per-box path works — that is what the forward walk proves, and
`background cleaned` costs the 1 ms it should. So the reads come back correct
for a tiled rectangle. What is left is narrower than it looks:

- **Backwards.** One direction of one motion. Either the box the engine
  computes does not cover where the shadow was, or the widening in `BgClip`
  interacts with it; the fetch reads on 64-pixel columns and writes only the
  columns asked for, and that asymmetry is the first thing to check.
- **The modals** both go through the full-screen pair,
  `CopyScreen(Log, Screen)` on entry and `CopyScreen(Screen, Log)` per
  iteration, which is `PORT_BgStoreAll` / `PORT_BgFetchAll` — a different code
  path from the tiled one, and the one that has never been proved. The
  self-test at boot covers a 32x4 rectangle through `VramRead`; extending it
  to a full-screen store and fetch of a known pattern is one run and would say
  outright whether the 76800-word read comes back intact.

A transparent-looking backdrop on the behaviour panel is a hint worth keeping:
if the restore were failing outright the panel would get *darker* every
iteration, because `ShadeBox` would compound. Reading as too light points at
the restore putting something back, but not the right something.

## 7. What this leaves

- **Actors are not occluded by scenery**, which is the missing depth on
  Twinsen. `DrawOverBrick` redraws the bricks in front of an actor by copying
  them out of the background through their own mask, into `Log`. On this
  machine the actor is not in `Log` to be covered up: it is a GPU primitive
  drawn after the present. Doing it here means replaying those bricks as
  primitives after the actors, with the mask as a texture.
  `GRILLE.C:PORT_CopyMaskBg` is where it would go.
- **The fade is 52 full presents.** See §5.
- **Cube 59 is 100 ms a frame.** The zoom re-presents 64000 pixels every frame
  through the tile path, and the scene has 192 entities.
- **The holomap has no workspace.**
- **`ChangeCube` is still 18.8 s**, untouched by any of this.
- **The modals were 3–6 seconds in M6** and no work went into them here; what
  changed is that they no longer burn in.
