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

And with the trail gone, the artefact underneath it was the last one left, so
M8 also did it: **actors are occluded by the scenery in front of them.** The
bricks become a textured primitive drawn after the actor, out of the clean
background, through a second palette whose index 0 is a hole -- which is the
opposite of what §2 had just made the first one do. Two overlays a frame, one
millisecond, no change to the frame time.

---

## 1. The self-test, extended, and what it ruled out

M7's self-test wrote 256 bytes into the VRAM background at boot and read them
back. That was enough to say VRAM reads work at all; it was not enough to say
that the two paths the engine uses work, because the full-screen pair
`PORT_BgStoreAll` / `PORT_BgFetchAll` — what `CopyScreen` becomes, and what
both modals call — had never been exercised by anything.

`-DPSX_BG_SELFTEST=ON` now runs five phases at boot, before a scene loads or a
frame is presented:

| phase | result |
|---|---|
| the M7 probe, 32x4 | 0 of 256 bytes wrong |
| **full-screen store and fetch** | **0 of 307200 wrong** |
| tiled fetch of 100,50..233,81 | 0 of 4288 wrong |
| …and the four bands around it | 0 wrong — it writes narrow, as documented |
| tiled store of the same rectangle | 0 wrong inside the widened span, 0 outside |
| the background after a full present | 0 wrong — **after §5b**; it was 4096 |

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

## 5b. The present had been writing into the background all along

Found while looking for somewhere to put the depth overlay, and worth its own
line because nothing had ever asked.

The staging tile starts at VRAM x 640 and the background at 704, which leaves
the stage 64 halfwords -- 128 texels at 8bpp. `TILE_W` said 256. M7 narrowed
the region and wrote the consequence into the comment above `STAGE_X` ("five
tiles per row instead of three", which is 640/128) and left the constant
alone, so every full-screen present uploaded 128 halfwords from x 640 and put
64 of them into the background.

A fifth self-test phase -- store a pattern, present a frame, fetch the
background back -- prices it exactly:

```
background after a full present -- 4096 of 307200 wrong, first at 0,0    (TILE_W 256)
background after a full present -- 0 of 307200 wrong                     (TILE_W 128)
```

4096 is 128x32, which is one staged tile. It was the top-left corner of the
screen, above the play area, which is why seven milestones of looking at the
picture never showed it.

## 6. Twinsen stands behind the scenery

`DrawOverBrick` copies the bricks in front of an actor out of the clean
background, through each brick's mask, back over the actor already rasterised
into `Log`. Here the actor is a GPU primitive and is not in `Log`, so the
occlusion has to become a primitive too. `platform/psx/psx_depth.c`:

1. `DrawOverBrick`'s loop is bracketed by `PORT_DepthBegin` / `PORT_DepthEnd`.
   Begin takes the actor's screen box straight from the engine's clip, which
   `OBJECT.C` has already set to exactly that box.
2. Each brick in front of the actor is fetched out of the VRAM background --
   one brick-sized rectangle, through a new `PORT_BgFetchTo` -- and its masked
   pixels are written into a 128x128 overlay buffer.
3. `CopyMask` grew both ends: `CopyMaskTo` takes a base, a stride and an
   origin for source and destination instead of `Screen` and `Log` at
   `Screen_X`. `CopyMask` is now a one-line wrapper and the walk is untouched.
4. Everything the bricks did not cover stays 0, **and 0 is a hole** -- through
   the second CLUT. §2 made black opaque for the present, which is exactly
   what an overlay must not have, so there are two palettes: index 0 carries
   bit 15 in the one the present samples and 0x0000 in the one the overlay
   does. They are 256 halfwords each and both fit under the framebuffer.
5. The quad is emitted **into the actor buffer**, at the point in the
   back-to-front order where `DrawOverBrick` was called -- not after all the
   actors. An actor standing in front of that brick stays in front of it,
   because its polygons come later in the same chain.

The overlays live in the scratch column between the framebuffer and the
background: `PresentTile` uses lines 0..31 of it and nothing else ever has, so
lines 32..511 are 128 texels by 480 lines, allocated one overlay under the
next and reset every burst. An overlay never straddles line 256, so one quad
never needs two texture pages.

### The shadow, and why the source is VRAM and not Log

The first version read the source from `Log`, on the reasoning that `ClsBoxes`
restored the clean background into the actor's box at the top of the frame.
That is true of an actor and false of a shadow: `TYPE_SHADOW` draws into `Log`
*before* calling `DrawOverBrick`, so under a brick the pixels are the shadow
itself, and copying them changes nothing. On screen that was exactly what
happened -- Twinsen went behind the scenery on the first try and his shadow
stayed on top of everything.

Reading the background instead is correct for both and costs one small
rectangle per brick. It is not free but it is not visible either:

| | |
|---|---|
| overlays | **2 a frame** (Twinsen and his shadow), 0 skipped, 0 out of scratch |
| the whole depth pass, VRAM reads included | **1 ms** |
| the frame | **34 ms, 28 fps** -- unchanged |
| worst frame | 66 ms, against 51 before; unexplained, and the average did not move |

### What it cost in memory

The overlay buffer is 16 KB and the per-brick window 4 KB, so the heap ceiling
falls from 1540 KB to 1521 and `use=1465K` leaves **56 KB free**. Between this
and the CD cache, M8 spent 50 KB of headroom on two features. That is the
number M9 has to look at first, and the holomap's workspace is on the other
side of it.

## 6b. What the screen shows now

Confirmed by playing the build, not by reading the log:

| | |
|---|---|
| shadow trail, any direction | **gone** |
| behaviour panel and inventory | **no burn-in, and their bodies animate** |
| Twinsen occluded by scenery | **works** |
| his shadow occluded by scenery | **works**, once the source became VRAM |
| the first frame of an animation | 107 ms worst, 1.4 ms typical |
| the frame | 34 ms, 28 fps |

Nothing visible is outstanding. What is left is time and memory: `ChangeCube`
at 18.8 s, the modals at 3-6 s, the fade at 270 ms, cube 59 at 100 ms a frame,
and 56 KB of heap.

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

**The engine's own comment is sometimes the bug report.** `psx_cd.c` said that
if the engine ever stopped reading one file at a time the symptom would be
slow loading. It had. `psx_video.c` said the stage was 128 texels wide and
five tiles per row; the constant next to it said 256. Twice in one session the
prose was right and the code was not — so when a comment states a constraint,
check the line under it.

**Two things cannot share one transparent colour.** The present needs black
opaque and the depth overlay needs black to be a hole, and the moment §2 fixed
the first it made the second impossible. A second CLUT is 256 halfwords; the
half-hour was spent realising it was needed at all.
