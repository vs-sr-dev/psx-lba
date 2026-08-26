# M3 — the isometric background, off the disc

Session 2, 2026-08-24, after [M2](M2-NOTES.md).

> **A static LBA scene is on screen at 640x480.** The brick grid is composed
> into an 8bpp background by the inherited 1994 software path and presented by
> the GPU through the staging page and the CLUT. Everything in it came off a CD
> image built from a real DOS install.

M2 ended with the engine 337 KB short of its first scene and an argument that
the three moves the architecture already required would close the gap. M3 made
those moves, found that two of the three numbers were wrong, and got there with
39 KB to spare.

It also found that **PCSX-Redux's recompiler does not check MIPS alignment**,
which means the thing M3 existed to test had been invisible all along. That is
section 5 and it is the most important part of this file.

---

## 1. The measurements

Cube 0, Twinsen's house: the first scene of a new game, and the same picture
every other port in this family drew first.

| | |
|---|---|
| `ChangeCube` — scene, dialogue, grid, bricks | **3885 ms** |
| `AffGrille` — 64x25x64 cells into a 640x480 background | **583 ms** |
| `Flip` — 300 KB to VRAM through the 64 KB staging page | **121 ms** |
| HQM scene pool actually used | **141936** of 400000 bytes |
| camera start cell | 53, 4, 54 |

**`AffGrille` is 583 ms, not the ~300 ms predicted.** The prediction came from
scaling the DS's measured 207 ms by the 1.46x memory-bandwidth ratio M0 got
from `memset`. That was the wrong ratio to reach for. `memset` is a linear
write; a brick blit is a strided read of packed source against a strided write
into a 640-pixel pitch, with a transparency test per pixel. The right
comparison is the 2.21x M0 measured for *large* polygons — where it also
concluded the memory system rather than the ALU was the limit — and 207 x 2.21
= 458 ms, which is much closer. Call the real figure 2.8x the DS, and treat any
future estimate scaled off `memset` as a floor rather than a number.

583 ms is a scene-change cost, not a frame cost, so it is a load pause and not
a frame rate. But it happens on every door, and the dirty-rectangle discipline
planned for M5 does not touch it: this is the one full rebuild that always has
to happen.

The 3885 ms in `ChangeCube` is almost entirely the CD. `LBA_BRK.HQR` is 3.9 MB
and `LoadUsedBrick` seeks to and reads one brick at a time — several hundred
individual seeks for this scene. That is the first hard evidence for the
drive-contention risk M7 has to answer, and it arrived worse than expected.

---

## 2. Where the 337 KB came from

### `Log` and `Screen` become one buffer — 300 KB

The DOS engine keeps two 640x480 8bpp images: `Log`, the software render
target, and `Screen`, the clean composed background it restores dirty
rectangles from before each frame. The second exists only because the CPU
draws the actors into the first.

On this machine the CPU does not draw the actors, so `Log` never accumulates
anything that has to be erased, and the restore has nothing left to restore.
`PERSO.C` now points `Screen` at the allocation `InitGraphSvga` already made
for `Log`, and the port keeps one background image.

Every `CopyScreen(Log, Screen)` and `CopyBlock(..., Screen, ..., Log)` in the
engine then arrives with source and destination equal. `CopyScreen` returns
early on that — `memcpy` is not defined for identical pointers, and the copy is
not wanted either. `CopyBlock` copies a region onto itself, which is harmless.

This is the move M2 scored at 300 KB, and it cost one line in `PERSO.C` and one
guard in `S_SCREEN.C`. It is also the move that cannot be walked back without
the 300 KB coming with it: from here on, anything the port draws over the
background has to be a GPU primitive, not a write into `Log`.

### `BufSpeak` drops from 256 KB to 64 KB — 192 KB

Sized in 1994 for the largest voice sample in `VOX`. Voices on this machine are
ADPCM in the SPU's own 512 KB (M7), so this buffer never has to hold one. What
it still has to hold is everything that borrows it as scratch: `FIRE.C`'s two
320x50 planes (32000 bytes), `GAMEMENU`'s plasma, and `MESSAGE.C`'s 256x256 CD
staging. 64 KB covers all three.

That is a promise as much as a saving, and it is written here so the promise is
findable: when voices arrive they go to the SPU, not back into this buffer.

### `Phys` is allocated on demand — 62 KB

The 320x200 MCGA page. SVGA mode never touches it; the FLA player and the
zoomed camera are the only things that do. It is now allocated on entry to MCGA
mode.

### And `Malloc(-1)` now answers honestly — 0 KB

`PERSO.C` sizes the sprite, animation and sample pools as fractions of
`Malloc(-1)`, which the community source release answers with `return 0`, so
all three collapse onto their clamped floors. The DS port's DEVLOG is emphatic
about what those floors cost: constant eviction, and `HQR_Del_Bloc` compacts
the pool under pointers callers are still holding.

`psx_sys.c` now answers the question by bisecting on what the allocator will
actually grant — twenty-odd `malloc`/`free` pairs, once, and the answer
accounts for fragmentation that a running total would not.

**It changed nothing, and that is the finding.** By the time the question is
asked there are about 230 KB free, so `SpriteMem` computes to 29 KB and
`AnimMem` to 59 KB, and both clamp *up* to the floors they were already sitting
on. On the DS, answering honestly bought fully resident sprite and animation
pools. Here the floors are not a bug to be fixed, they are the size of the
machine. Sprites and animations will evict, and the eviction hazard the DS port
documented is inherited whole rather than avoided.

So `Malloc(-1)` is not a saving. It is the removal of a wrong number, and what
it revealed is that there is no headroom to be found by asking better questions.

---

## 3. The RAM verdict, second edition

The M3 build measures a **1656 KB** heap, which is 80 KB more than the real
one: the harness replaces `MainGameMenu` and the linker garbage-collects most
of `GAMEMENU.C` along with it. The honest figure is the full build's, and both
are worth recording so the difference is never mistaken for a saving:

| | text | data | bss | heap |
|---|---|---|---|---|
| full build (menu) | 221792 | 3304 | 128214 | **1574 KB** |
| M3 harness | 141212 | 3000 | 127346 | 1656 KB |

Measured at the first scene: **`use=1535K`, 19 blocks, largest free block
111 KB.** Against the full build's 1574 KB heap that is a fit with **39 KB
spare**; against the harness's own heap, 121 KB.

| block | KB | |
|---|---|---|
| HQM scene pool | 391 | 139 KB of it used by cube 0 |
| BufferBrick | 353 | |
| Log **and** Screen | 300 | was 600 |
| BufCube 64x25x64x2 | 200 | |
| AnimMem | 98 | at its floor |
| BufSpeak | 64 | was 256 |
| SpriteMem | 49 | at its floor |
| InventoryObj | 20 | |
| everything else | ~60 | |
| **total** | **1535** | |

**2258 KB was never the number.** M2's estimate was high in two places at once:
it counted `BufSpeak` at its DOS size and it assumed an honest `Malloc(-1)`
would inflate the pools. `SampleMem`'s 195 KB was not a saving either — with
`Wave_Driver_Enable` false, `HQR_Samples` is never allocated at all, so what M2
scored as a move was a cost that had never been paid. The three things that
actually made the port fit are `Log`, `BufSpeak` and `Phys`: **554 KB**, not
the 751 KB M2 credited.

Two caveats on the 39 KB. It is measured on one of the smallest scenes in the
game, and `HQM` is fixed at the inherited constant 400000 while cube 0 uses
139 KB of it, so the margin against the *largest* scene was unknown.

**It is not unknown any more** — `tools/scene_census.py` reproduces
`InitGrille` and `LoadUsedBrick` offline over all 120 scenes. See §7. Sizing `HQM` from a census of all 120 cubes is the obvious
next measurement, and it may well give a large part of that 391 KB back.

---

## 4. The exception handler

`psx_exc.S` takes over the general exception vector at `0x80000080`. The BIOS
puts four position-independent instructions there (`lui` / `addiu` / `jr` /
`nop`), so they are relocated into a buffer rather than reimplemented, and an
equivalent trampoline to ours is written in their place. Interrupts, and the
`syscall` and `break` the BIOS uses for `EnterCriticalSection` and friends, are
handed straight over — six instructions of triage on `k0`/`k1`, which the BIOS
treats as scratch too. Only a genuine fault stops.

A fault gets `epc` (corrected for a branch delay slot), `cause` decoded by
name, `badvaddr`, the 29 saved registers, the four instruction words at `epc`,
and a heap report — printed from a private 2 KB stack, because the fault may
well *be* the stack. It classifies the faulting address too, since on this
machine three cases are distinguishable and each points at a different bug:

- below `0x00010000`, or `0x80000000`–`0x80010000` — kernel RAM: a null or wild
  pointer, which is the hazard the PlayStation has and the DS did not.
- odd — a halfword or word pulled off a byte stream.
- 2-aligned but not 4 — a 32-bit read off a halfword-aligned stream.

`cmake -DPSX_EXC_SELFTEST=ON` makes the build read a word off an odd address on
purpose, at the end of M3. A crash handler nobody has watched fire is a claim
rather than an instrument, and this project has already paid once for a
diagnostic that was itself wrong.

### What the self-test actually established

Three things are proven and one is not, and the difference matters:

- **The vector is ours.** `PORT_ExcVectorDump` reads `0x80000080` back rather
  than believing the write, at install time and again at the moment of the
  fault. Both report `3c1a8001 375a09fc 03400008 00000000` — `lui k0,0x8001` /
  `ori k0,0x09fc` / `jr k0`, and `0x800109fc` is `PORT_ExcVectorEntry`.
- **The chaining path works.** Every interrupt and every `syscall` in a full M3
  run — the 50 Hz tick, the CD, the pad, `EnterCriticalSection` — passes
  through our triage and out to the relocated BIOS trampoline. A three-second
  scene load is a lot of evidence.
- **The fatal path is correct as written.** Disassembled: the triage, the 29
  register stores, the CP0 reads, the private stack, `$gp` reloaded from `_gp`,
  and the jump to `PORT_ExcReport`.
- **The fatal path has never been observed to run.** On PCSX-Redux with
  OpenBIOS, an address error is answered by OpenBIOS's own handler at
  `0x00002974`, which calls `A0:40` (`SystemErrorUnresolvedException`) and
  halts — *with our vector still installed*. `PSX_EXC_TRACE=ON` puts a single
  BIOS `putchar` at the very top of the fatal path, before any C runs, and it
  never appears.

So this emulator does not dispatch CPU exceptions through the general vector.
Not "our handler is wrong" and not "our handler is right" — it is never
reached, and no experiment available here can distinguish further.

The practical consequence is mild, and worth stating so nobody re-fights this:
**OpenBIOS's dump carries the same information ours would.** `epc`, `status`,
`cause`, `badvaddr` and all 32 registers, which is what the handler exists to
provide. Bring-up is not blocked. What is blocked is knowing the handler works
on the machine that matters, and that has to be settled on DuckStation, on a
retail BIOS image, or on hardware.

The setting worth knowing about while chasing this: PCSX-Redux stores a
`Debug/FirstChanceException` bitmask in `pcsx.json` (`7408` by default, `0` to
stop breaking on exceptions and let the program see them). It did not change
this outcome, but it is the first thing anyone will reach for.

---

## 5. The thing that was invisible

**PCSX-Redux's recompiler does not check MIPS alignment.** The self-test
compiles to a plain `lw` at an odd address:

```
80010eec:	8c420000 	lw	v0,0(v0)
```

Under the default dynarec that returns the correctly assembled unaligned value
and does not fault:

```
[EXC] self-test: 32-bit read at 800333d9, which is odd
[EXC] self-test FAILED: read 05040302 and no fault was raised.
```

Under `-interpreter` the same instruction raises the exception it should:

```
Unaligned address 0x80033419 in LW from 0x80010eec
First chance exception: LoadAddressError from 0x80010eec
```

This matters more than the handler does. The entire hazard M3 was built to
expose — the 1994 code walking byte streams and pulling words out of them — is
**silently absorbed by the emulator this project has been using for bring-up
since M0**. Every clean run up to now proved less than it looked like it did.

**So: `-interpreter` is not a debugging option here, it is the correctness
configuration.** Anything that has only been run under the recompiler has not
been tested for alignment at all.

Switching to it found a fault on the first attempt, and it was the port's own:

```
Unaligned address 0x800ae8da in SW from 0x80011f40   -> PSX_malloc, psx_sys.c:232
```

The instrumented heap writes a canary immediately past the end of each block,
and blocks are whatever size the engine asked for — 361472, but also 3097 and
25500. An odd size puts the canary on an odd address. On the DS that was a
rotated word nobody noticed; here it is a Store Address Error on every single
odd-sized allocation. The instrument was faulting on its own instrumentation,
and the recompiler had been hiding it since M1.

Fixed by rounding the canary offset up to a word, with `PSX_malloc` reserving
the padding to match.

---

## 6. What did not happen, and why that is still worth something

With the canary fixed and the interpreter running, the whole scene-load chain
walks through clean: `LoadScene` and `FICHE`, `GERETRAK`, `GERELIFE`,
`InitDial` over `TEXT.HQR`, `InitGrille` and `LoadUsedBrick` over `LBA_GRI` /
`LBA_BLL` / `LBA_BRK`, then `AffGrille`. Those are exactly the modules the DS
port's DEVLOG named as the alignment gauntlet, and now they have been walked on
a machine that actually enforces alignment.

That is not the 1994 code being well behaved. It is the DS port: it is the
direct parent of this one, it hit these on an ARM9 that rotated unaligned loads
silently, and it left the fixes in the shared engine tree — `EXPAND.C` pulling
its LZSS control tokens through two byte loads, `LE_RU16`/`LE_W16` in
`GRILLE.C` where block entries sit at offsets `3+4n`. This port inherited a
solved problem, and the honest thing to record is that the gauntlet was fought
somewhere else and won there.

What is left is the class the DS could not have found: where the two machines
differ rather than agree. One turned up immediately.

`GAMEMENU.C`'s zoomed-camera path had `PORT_SDL` and `PORT_NDS` branches
copying into `Phys`, and a DOS `#else` writing 64000 bytes to the literal
address `0xA0000` — the VGA aperture. On a PlayStation `0xA0000` is not an
aperture. It is an ordinary main-RAM address 640 KB up, in the middle of the
heap, and the write would have succeeded and corrupted something to be
discovered three crashes later. Exactly the class M2 flagged, arriving from a
direction M2 did not look: not a null pointer, but a *valid* one that means
something entirely different here.

That is the shape of the remaining work. The alignment faults were the DS's
problem and are already fixed. The PlayStation's own problem is that every
address is legal.

---

## 7. Every scene, measured without a console

`tools/scene_census.py` walks the used-block bitmap in each `.gri`, follows it
into the `.bll` block records, collects every brick each one references and
sums their decompressed sizes out of `LBA_BRK.HQR`. That is `LoadUsedBrick`,
in Python, over all 120 scenes at once — and it answers two questions the
milestone left open without needing hardware.

**HQM is safe.** The pool holds the map, the block table and the brick mask.
The mask cannot be computed offline — `CreateMaskGph` emits a run-length mask
that `HQM_Shrink_Last` then trims — so the census counts it at the full size of
the brick data it covers, which is a deliberate over-estimate. Even so:

| | gri | bll | brick bytes | HQM upper bound |
|---|---|---|---|---|
| cube 59, the worst | 15970 | 6014 | 351894 | **373878** |
| cube 117 | 22932 | 6178 | 333206 | 362316 |
| cube 0, the one measured | 19732 | 6346 | 236142 | 262220 |

**373878 against a 400000-byte pool.** Every scene in the game fits, at a bound
built to be pessimistic. The 39 KB margin is not a claim about one small scene
any more.

### Then cube 59 was actually run

`-DPSX_M3_CUBE=59` builds the harness against any scene, so the estimate did
not have to stand. The machine's answer:

| | cube 0 | cube 59 |
|---|---|---|
| HQM used | 141936 | **144280** of 400000 |
| heap in use at the scene | 1535 KB | **1535 KB** |
| `ChangeCube` | 3894 ms | 5775 ms |
| `AffGrille` | 589 ms | **152 ms** |

Three things, and the first two are the ones that matter.

**The heap total is identical.** 1535 KB on the game's heaviest scene and on
one of its lightest. The 39 KB margin was never a small-scene artefact — the
big allocations are all fixed-size buffers sized for the worst case up front,
which is exactly what a 1994 DOS engine would do and exactly what makes this
port's budget predictable.

**HQM uses 144 KB of 400 KB on the worst scene in the game.** My estimate above
said "near 200 KB" and was too high by a third; the census's upper bound of
373878 was too high by 2.6x. Both were wrong in the safe direction, and the
real figure means **roughly 250 KB of the HQM pool is dead weight**. Reclaiming
it would take the port's margin from 39 KB to something near 290 — which is the
difference between a fit that survives no surprises and one that survives
several. It wants one more measurement across all 120 scenes before the
constant is changed, and that is now a loop rather than a project.

**`AffGrille` varies fourfold between scenes**, and not in the direction the
brick totals suggest: cube 59 has 50% more brick data than cube 0 and composes
in a quarter of the time. The cost tracks how much of the grid is on camera,
not how much of it exists. So M3's 589 ms is not a worst case, and neither is
this — the honest statement is that a scene change costs somewhere between 150
and 600 ms of compose, and nobody has found the top of that range yet.

`ChangeCube` at 5775 ms does scale with brick data, which confirms it is the CD
and not the CPU. That is 5.8 seconds of load for one room.

**And the guard did not fire**, which is the census's arithmetic being
confirmed by the machine: cube 59's brick set fits `BufferBrick`, by 9578
bytes.

**`BufferBrick` has almost nothing left.** The brick set goes into a fixed
361472-byte allocation — `MAX_SIZE_BRICK_CUBE` in `GRILLE.H` — and cube 59
needs **351894 of it. 9578 bytes spare, 2.6%.**

`LoadUsedBrick` does not check. It writes brick after brick into the buffer and
returns the total, and nothing compares that total to the size of what it just
filled. On DOS that was a constant tuned against the shipped data and it held;
here it is 353 KB of a 1574 KB heap with a 2.6% margin and no guard, sitting
directly upstream of every scene load. Adding the check costs four lines and
turns a silent heap corruption into a named error. It is on the list.

---

## 8. Next

- **Run everything under `-interpreter` from here on.** Nothing that has only
  been run under the recompiler has been tested for alignment.
- **Settle the exception handler on something that dispatches through the
  vector** — DuckStation, a retail BIOS image, or hardware. Until then the
  emulator's own dump is the instrument, and it is good enough.
- **Size `HQM` from a census of all 120 cubes** instead of leaving it at the
  inherited 400000. It is 391 KB of a 1574 KB heap and cube 0 uses 139 KB.
- **The 3885 ms scene load.** `LoadUsedBrick` does one seek per brick over a
  3.9 MB archive. The M7 contention problem showing up early, and measurable
  now.
- **GPU fill rate is still unmeasured** and still needs DuckStation. The actor
  budget cannot be a number until it is.
- **M4**: the actors. `AffGrille` proved the background path; the GTE and the
  GPU primitive path are the other half of the table in the README — and the
  half that makes `Log` staying merged with `Screen` mandatory rather than
  convenient.
