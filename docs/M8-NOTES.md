# M8 — the black that was never drawn, and the seek behind every animation

M7 put the clean background in VRAM and left three things open: a shadow trail
that survived walking backwards, two modals that burned in, and a full-screen
copy path that had never been proved. M8 opened by proving that path, which
was the plan, and the proof came back clean — which was not the plan, and was
the useful part.

**The transfers were never the problem.** The background round-trips 307200
bytes without an error, in either direction, tiled or whole. What was wrong is
that **the present never wrote black**: a textured primitive skips any texel
whose CLUT entry is exactly `0x0000`, and the port built its CLUT without bit
15, so every pixel of `Log` that the palette mapped to black was left alone in
the framebuffer instead of drawn. The picture underneath survived. That is the
shadow trail, that is both modals burning in, and it was one line.

The second thing M8 found is not a rendering bug at all. **The first frame of
every animation cost 401 ms**, half of it a file read whose result was thrown
away and the rest a CD seek repeated because the sector cache held one sector.
It is 107 ms now, and 1.4 ms for most of them.

---

## 1. The self-test, extended, and what it ruled out

M7's self-test wrote 256 bytes into the VRAM background at boot and read them
back. That was enough to say VRAM reads work at all; it was not enough to say
that the two paths the engine uses work, because the full-screen pair
`PORT_BgStoreAll` / `PORT_BgFetchAll` — what `CopyScreen` becomes, and what
both modals call — had never been exercised by anything.

`-DPSX_BG_SELFTEST=ON` now runs four phases at boot, before a scene loads or a
frame is presented:

| phase | result |
|---|---|
| the M7 probe, 32x4 | 0 of 256 bytes wrong |
| **full-screen store and fetch** | **0 of 307200 wrong** |
| tiled fetch of 100,50..233,81 | 0 of 4288 wrong |
| …and the four bands around it | 0 wrong — it writes narrow, as documented |
| tiled store of the same rectangle | 0 wrong inside the widened span, 0 outside |

So the DMA is right, the widening to 64-pixel columns is right, and `read wide,
write narrow` does what its comment says. Every transfer-level explanation for
the trail and the burn-in was gone in one run.

The rectangle is deliberately awkward: 100..233 is on no 64-pixel column at
either end and 50..81 straddles the 32-line tile boundary at 64, so a transfer
that lands one column or one row out is a mismatch rather than a coincidence.

## 2. The present never wrote black

`ApplyPalRange` built each CLUT entry as five bits per gun and nothing else:

```c
clut[i] = (p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10);
```

A colour that quantises to (0,0,0) therefore became `0x0000`. On this machine
that is not a colour. **A textured primitive does not draw a texel whose CLUT
entry is `0x0000`** — that is how the GPU does sprite transparency, and it is
not optional. The present is a textured quad sampling `Log`, so every black
pixel in the frame was skipped, and the framebuffer kept whatever had been
there before.

Everything follows from that:

- the **shadow trail** — a shadow is dark pixels moving over a background, and
  the frame that should erase it is darker than the frame that drew it;
- **both modals** — their widgets are `Box(..., 0)`, black, presented onto a
  screen that already had something there;
- and it was **direction-dependent**, which is why M7 filed it as a per-box
  fetch bug: walking towards the camera the new frame is mostly lighter and
  overwrites, walking away it is mostly darker and does not.

The fix is bit 15:

```c
clut[i] = c ? c : 0x8000;       /* opaque black, not "do not draw" */
```

On an untextured or unblended primitive bit 15 is only the mask bit written
into VRAM, which nothing here reads, so the single entry it changes is the one
that was wrong.

**Confirmed on the machine.** Before the fix the last of the trail was, in the
player's words, *only the shadow, and only its black pixels* — which is the
diagnosis stated from the screen. After it, no trail anywhere.

## 3. The modals drew their bodies into a buffer nobody flushed

Separate bug, same screens, found while the trail was still open.

The actors are GPU primitives collected between `PORT_ActorBegin` and
`PORT_ActorEnd` and replayed after the frame's present, because the present
would otherwise erase them (M5 §6, psx_poly.c). That pair had exactly two
callers: the head and tail of `AffScene`, and the M4 harness.

`MenuComportement` and `Inventory` never go near `AffScene`. They draw their
own widgets and present their own rectangles, so every `AffObjetIso` they made
went into `prim_buf` and stayed there — and after about two widgets the buffer
was full and the rest went to `prim_dropped`. The behaviour panel's four
characters were not failing to animate. **They were never drawn at all.**

Each widget that presents a rectangle now brackets its own emit:
`DrawComportement` when it presents (`copyblock`), `DrawMenuComportement`
around the four it draws at once, and `DrawOneInventory`.

### And the clip that mattered here and not on DOS

`DrawObj3D` sets a clip to the cell it is about to present. `Draw3dObject`,
which the inventory uses, sets none. On DOS that was harmless: an object that
overflowed its cell went into `Log` and was simply never blitted, because the
widget presents only the cell. Here the body is a GPU primitive that goes
straight into the framebuffer, so the overflow is on screen and nothing will
ever present over it again. `DrawOneInventory` clips to its cell now.

## 4. 401 ms for forty bytes

The player reported a hitch on the first frame of each animation. It is a CD
read, in the middle of that frame, and `-DPSX_HQR_TRACE=ON` prices it.

```
[HQR] miss Anim.hqr[100] 40 bytes -- probe 200776 us, load 200839 us, total 401615 us
```

### Half of it was a number nobody used

`HQR_Get`'s miss path opened with

```c
size = Size_HQR(header->Name, index);   /* open, seek, seek, read, close */
```

and then twenty lines later did the same walk again and overwrote `size` with
`lzssheader.SizeFile`. A whole CD open, two seeks and a close, for a value
that was discarded. Deleting the call is the entire fix and it is worth
**200 ms of every 401**.

### The other half was a one-sector cache

With the probe gone the trace attributes what is left, and it is not where
anyone would have guessed:

```
[HQR] miss Anim.hqr[100] 40 bytes -- open 890 us, index 199568 us, payload 63 us
```

Opening the file costs 0.9 ms. Reading the forty bytes costs 0.06 ms. **Finding
them costs 200 ms**, and it is two sector reads: the archive's offset table at
sector 0, and the entry itself tens of thousands of sectors away. The shared
sector cache in `psx_cd.c` held **one** sector, so the second read evicted the
first and every miss paid for sector 0 again.

A sector that has to be sought to costs about 100 ms on this drive. That is the
measurement, not an estimate: with the cache widened, `index` halves exactly.

### What the cache is now

Eight windows of two consecutive sectors, least recently used, 32 KB. Three
shapes were measured on the same autopilot script:

| cache | typical miss | share arriving cached |
|---|---|---|
| 1 sector | 201 ms | none |
| 8 x 1 sector, 16 KB | 101 ms | 5 of 12 |
| 4 x 4 sectors, 32 KB | 120–241 ms | 14 of 22 |
| **8 x 2 sectors, 32 KB** | **107 ms** | **13 of 21** |

Two things the numbers say. Independent ways matter more than depth — four
windows are not enough to hold the index sectors of `Anim`, `sprites` and
`invobj` at once, and the 241 ms entries are two window reads. And the read
ahead is not free: a four-sector read costs about 20 ms more than a one-sector
read, which is worth two extra sectors and not worth four.

**401 ms to 107 ms, and 1.4 ms for thirteen animations in twenty-one.** The
remaining 107 is one real seek to the entry, and the only way past it is the
archive layout — the same answer `ChangeCube` wants, and still M8's largest
open number.

## 5. What this cost in memory

The cache is 32 KB of bss, so the heap ceiling drops from 1571 KB to 1540. The
build reports `use=1465K` before the loop, which leaves **75 KB free** — and
that number wants checking against M7's `1400 KB in use`, because the two are
sampled at different moments and nobody should plan the holomap's workspace
against either until they are measured on one build.

## 6. What is left, from the screen

Confirmed by playing the build, not by reading the log:

| | |
|---|---|
| shadow trail, any direction | **gone** |
| behaviour panel and inventory | **no burn-in, and their bodies animate** |
| **Twinsen is not occluded by scenery** | still there, and now the only visible artefact |
| the first frame of an animation | 107 ms worst, 1.4 ms typical |

### The depth, and what the CLUT fix changed about it

`DrawOverBrick` redraws the bricks in front of an actor by copying them out of
the clean background through their own mask. It cannot work here: the actor is
not in `Log` to be covered up. `GRILLE.C:PORT_CopyMaskBg` is still the hole
left for it.

What M8 changes is the shape of the answer. The occlusion has to be an overlay
drawn **after** the actors, and an overlay needs a transparent index — which is
exactly what §2 just took away, on purpose, for the present. So it wants **two
CLUTs**: the one the present uses, with entry 0 opaque, and a second one
identical but with entry 0 back to `0x0000`, for the masked overlay. There is
room for it beside the first at (0,480), and it costs one more 256-halfword
upload per palette change.

The rest of the shape:

- the region is small. `DrawOverBrick` runs with the clip set to the actor's
  screen box, so the overlay is that box — tens of pixels, not a screen;
- `Log` inside that box already holds the clean background, because `ClsBoxes`
  restored it there at the top of the frame. So the pixels are in RAM already
  and only the mask has to be walked;
- what is needed is a `CopyMask` variant that writes into a scratch buffer
  cleared to 0 rather than into `Log`, and one textured quad per actor drawn
  through the second CLUT after `PORT_ActorEnd`;
- the ordering is the awkward part. `DrawOverBrick` is called per actor inside
  a back-to-front loop, so a faithful version emits each overlay into
  `prim_buf` in that order rather than drawing them all at the end — and each
  needs its own texture staging, in a VRAM that has 128 texels of scratch.

## 7. The rules

**A path proved for one shape is not a proved path** — sessions 1-5 already
say this, and it caught the CLUT: the present was exercised by hundreds of
frames and never once asked to write a black pixel over a different one.

**When the self-test comes back clean, believe it.** Four phases said the
transfers were right. The temptation was to distrust the test and keep
bisecting the DMA; the value of it was that it made every remaining
explanation a non-transfer one, and there was only one.

**The player's description was the diagnosis.** *Only the shadow, and only its
black pixels* is not a bug report, it is the mechanism. A screen watched by a
person says things no log line was built to say — and this whole session's
findings began with someone noticing a hitch that no measurement had been
pointed at.

**Measure the halves before optimising the whole.** Splitting a 401 ms miss
into probe and load said one half was free to delete. Splitting the rest into
open, index and payload said the cost was in none of the places the code looks
expensive: not the open, not the decompression, but two sector reads.
