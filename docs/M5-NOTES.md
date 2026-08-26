# M5 — the frame loop, and the emulator that found a real bug first

M5 is the first build that keeps running. Before any of it, this session
installed DuckStation, which had been on the carried-forward list since M0 for
GPU fill rate and since M3 for the exception handler. It settled both, and
before either it found a fault in the port that three milestones of clean runs
had hidden.

---

## 1. DuckStation, and why the runs before it proved less than they looked

`F:\DuckStation`, in **portable** mode: `portable.txt` next to the executable
makes the user directory the program directory, which matters here because this
machine's `Documents` is redirected into OneDrive and a first run without it
lands the settings somewhere the emulator itself then cannot find. BIOS images
go in `F:\DuckStation\bios`; the disc has no licence string (`make_cd.py` does
not write one), so **Fast Boot is mandatory** — a retail BIOS refuses the disc
otherwise.

```sh
"F:/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe" \
    -batch -fastboot d:/Homebrew6/PSX-LBA/build/lba1psx.cue
```

The settings that matter, in `F:\DuckStation\settings.ini`:

| | |
|---|---|
| `[CPU] ExecutionMode = Interpreter` | same reason as PCSX-Redux (M3 §5) |
| `[BIOS] PatchFastBoot = true` | no licence string on our disc |
| `[BIOS] TTYLogging = true` | BIOS `putchar` into the log |
| `[Logging] LogToFile = true` | **the TTY becomes a file** |

That last line is the workflow change. PCSX-Redux writes its TTY to the console
handle, which shell redirection cannot capture (M2's gotcha list); DuckStation
writes `duckstation.log`, so the port's own diagnostics can be read, grepped and
diffed without watching a terminal. Every number below came out of that file.

## 2. The bug: `mfc0` has a load delay slot

The port did not boot at all on a retail BIOS. It died inside
`PORT_ExcInstall`, and the report it printed was internally inconsistent — a
Cause of 0 (an interrupt, which the vector chains and never reports) arriving
through the path that only a genuine fault can reach.

An entry-time snapshot of the four COP0 registers (`-DPSX_EXC_ENTRYLOG=ON`,
kept) came back **shifted by one slot** against the same registers read a few
instructions later. That is the whole answer:

```
    mfc0    $k1, $13        /* Cause                     */
    andi    $k1, $k1, 0x7c  /* <- reads the OLD $k1      */
```

On the R3000A a move from coprocessor 0 has a load delay slot exactly as `lw`
does: the destination is not readable by the next instruction. So the triage at
the top of the vector was testing **whatever `k1` happened to hold**, not Cause.
When that leftover was a multiple of 4 in the low seven bits the exception
chained correctly by accident; when it was not, an ordinary interrupt fell into
the fault path and halted the machine. In the failing build the leftover was 4,
from the instrumentation's own counter — the three exceptions before it had
chained on luck and the fourth did not.

Every `mfc0` in `psx_exc.S` now has a `nop` behind it.

**Neither PCSX-Redux mode models this.** Not the recompiler, and not the
`-interpreter` that M3 switched to precisely because the recompiler was hiding
alignment faults. DuckStation models it, and named it on the first run. The M3
rule stands and gets a second half: *run under `-interpreter`, and run under
DuckStation as well, because the two emulators hide different things.*

The self-test then closed the item M3 left open — the fatal path had never been
seen to fire, because PCSX-Redux with OpenBIOS answered a real address error
from its own handler and halted before ours. On DuckStation with a retail BIOS:

```
*** EXCEPTION *** AdEL     address error on LOAD (unaligned or bad)
    epc      8001102c        badvaddr 80032979
    cause    30000010        sr 00000404
    -> odd address: a halfword or word read off a byte stream
    the four words at epc: 8c420000 ...          <- lw $v0, 0($v0)
    exceptions through the vector so far: 9148
```

Right code, right address, right instruction, and 9148 interrupts and syscalls
chained through the vector before it without a scratch. The handler is an
instrument now, not a claim.

## 3. The same scene, on a second emulator

Cube 0, retail BIOS, software renderer, interpreter:

| | PCSX-Redux + OpenBIOS | DuckStation + SCPH-1001 |
|---|---|---|
| heap at the scene | 1535 KB | **1535 KB** |
| HQM used | 141936 | **141936** |
| `ChangeCube` | 3894 ms | **17012 ms** |
| `AffGrille` | 588 ms | **461 ms** |
| `Flip` | 122 ms | **119 ms** |
| the actor, transformed and emitted | ~95 ms | **94 ms** |

Everything the CPU decides agrees. Everything the CD decides does not, and the
gap is 4.4x.

**`ChangeCube` is 17 seconds.** PCSX-Redux's drive model is optimistic;
DuckStation's models seek latency, and `LoadUsedBrick` does one seek per brick
over a 3.9 MB archive. Seventeen seconds to enter a room is not a load time, it
is a defect, and it moves the M7 drive-contention work forward: the archive
layout has to change, not just the streaming policy.

**The actor costs 94 ms and both emulators agree.** M4 declined to report this
figure because PCSX-Redux models no GPU timing — but the two now agree to within
1%, which says the cost is not in the GPU at all. It is 129 primitives against a
20 ms frame at 50 Hz: **one actor is 4.7 frames**. Whatever M5's frame loop
does, it does not start from a working budget, and the first job inside it is to
find where 94 ms goes for 129 primitives.

---

## 4. The loop runs

`-DPSX_M5=ON` replaces the M3 harness with the engine's own `MainLoop`, on a
fixed cube, with neither the menu nor the introduction in front of it
(`platform/psx/psx_m5.c`, hooked in `PERSO.C` beside the M3 hook). Nothing
inside the loop is modified: the 50 Hz regulator, `AffScene`'s dirty-box
discipline, `ChangeCube` and the fade are all the 1994 code.

Cube 0, DuckStation, retail BIOS, 690 frames:

| | |
|---|---|
| frame time | **40–42 ms**, and 690 of 693 frames are inside that band |
| frame rate | **23–24 fps** |
| rectangles presented per frame | **1** |
| pixels presented per frame | **~2600** |
| time in present | **1–2 ms** |
| the scene-load frame | 18808 ms (`ChangeCube` plus a full compose) |
| the fade frame | 858 ms (51 palette steps, each waiting a field) |

**The dirty-rectangle discipline pays for itself by a factor of a hundred.**
A full present is 307200 pixels and 120 ms; a frame of this game is one
rectangle of about 2600 pixels and 1–2 ms. M3 measured the cost of `Flip` and
called 122 ms per frame the thing standing between the port and playability;
it is not in the way at all, because the engine already knew since 1994 which
thirty by ninety pixels had changed and `CopyBlockPhys` was already
`PresentRect`. Wiring the two together was one `#ifdef`.

**The animation is running.** The pixel count moves frame to frame — 2580,
2610, 2640, 2464, 2759 — which is Twinsen's silhouette changing shape. That is
`SetInterAnimObjet2` advancing, which M4 listed as the next thing to do and
which turns out to have been waiting only for a loop to be called from.

**So the frame is 40 ms and 38 of them are the actors.** The present is 2 ms
and the logic is small; what is left is `AffObjetIso`, which §3 already
measured at 89 of the M4 harness's 94 ms. The port's frame budget problem is
one specific function, and the eleven objects it transforms include ten that
emit no primitives at all.

## 5. The present that had never happened

The loop did not run first time. It reached the second frame, entered
`AffScene`, and stopped dead inside `FlipBoxes` on the first dirty rectangle it
ever tried to present — 30 pixels wide by 87 high.

`PresentTile` stages a tile of `Log` into VRAM with `LoadImage`, which goes out
on DMA channel 2 **in blocks of 16 words**. A transfer that is not a whole
number of blocks leaves the GPU waiting for data that never arrives, and
`DrawSync` never returns. Every present in the port until this milestone was
full-screen, which tiles into 256x32 and 128x32 pieces — always a whole number
of blocks, by accident. The first present of an arbitrary rectangle hung the
machine.

The fix is one line: stage each row padded to a multiple of 64 pixels, which is
32 halfwords, which is 16 words. Any height is then a whole number of blocks.
The padding is never sampled — the quad's UVs still stop at `w - 1`.

The class is worth naming, because it is the third time this port has hit it:
**a path that has only ever been exercised with one shape is not a working
path.** M2 had a directory listing that only worked at the root, M3 had a
`Malloc` canary that only worked on even sizes, and M5 had a present that only
worked full-screen.

## 6. The flicker, and why it is structural

The loop is visibly correct and visibly flickering. The reason is not the box
list; it is that there is only one framebuffer and the frame has a hole in it.

640x480 in 16-bit is 614400 bytes of a 1 MB VRAM, so **two of them do not
fit** — this port is single-buffered by arithmetic, not by choice. Everything
drawn is drawn on the screen the viewer is looking at. That would still be
tolerable if the erase and the redraw were adjacent, but they are not:
`AffScene` presents the background box at the top of the frame and then spends
**38 ms transforming actors** before the first primitive lands. For 38 of every
41 ms Twinsen is not on the screen at all.

The fix is not double buffering, which does not fit. It is to stop submitting
primitives as they are computed: buffer them during the transform and replay
the whole frame — background boxes first, then every primitive — in one burst
at the end. The hole then shrinks from 38 ms to about 3, which is a tear rather
than a flash. That is the next piece of work, and it is also the shape the
PlayStation wanted in the first place: build an ordering table, hand it to the
GPU once.

---

## 7. Where a frame actually goes

Two instruments went in, both under `-DPSX_M5=ON` and both kept:
`translate/p_ob_iso.c` splits `AffObjetIso` four ways, and `psx_m5.c` turns the
debug marks that found the stall in §5 into a frame clock. Between them a frame
is fully accounted for, and the first thing they did was correct §4.

**`AffObjetIso` is not 38 ms in the loop. It is 16.** The engine has a preclip:
`AffScene` projects each object's origin and tests it against the view
rectangle before the object reaches the sort list, so cube 0 transforms **one**
object, not eleven. M4's 94 ms was the harness brute-forcing all 23, which the
game never does. The claim in §4 — that the frame was 40 ms and 38 of them were
the actors — was arithmetic on a number measured under different conditions,
and it was wrong.

What a frame is, cube 0, with vsync and the ordering table both in:

| | |
|---|---|
| MainLoop's game logic | 4 ms |
| `AffScene`, entry to present | 28 ms, of which ~9 is the vsync wait |
| the actor replay | 0 ms |
| the fade | 0 ms |
| **frame** | **34 ms, 29 fps** |

and inside `AffObjetIso`, for 136 entities:

| | |
|---|---|
| transform (`AnimNuage`) | 2 ms |
| entity build | 5 ms |
| sort (`SergeSort`) | 1 ms |
| **emit** | **10 ms** |

## 8. One DMA instead of 137

The profile's real finding was not in the engine at all. Handing 137 primitives
to the GPU one `DrawPrim` at a time cost **13 ms of a 50 ms frame** — more than
the transform, the entity build and the sort together.

The packets were already contiguous in the frame buffer and already in the
engine's back-to-front order. Writing the address of the next one into the low
24 bits of each tag makes that buffer a linked list, which is what the
PlayStation calls an ordering table, and `DrawOTag` sends the frame in a single
DMA with the CPU out of the loop.

| | frame | fps | replay |
|---|---|---|---|
| a `DrawPrim` per primitive | 50 ms | 19 | 13 ms |
| one `DrawOTag` | **34 ms** | **29** | **0 ms** |

The frame crossed 33.3 ms, so it lands on two fields instead of three: **the
picture is locked to 30 fps**. The occasional 51 ms frame is one that slipped a
field.

M4 wrote that "the engine's sort IS the ordering table" and meant it as an
observation about ordering. It was also an observation about submission, and
nobody noticed for a milestone.

## 9. What M5 leaves

- **The emit is 10 of `AffObjetIso`'s 18 ms** — `PORT_ActorPoly`'s
  Sutherland-Hodgman clip and a palette lookup per vertex. The next frame time
  is in there.
- **`ChangeCube` is still 18.8 s.** Nothing here touched it. What got faster
  this session is the *boot*, via `-DPSX_SKIP_INTRO=ON`: the bumpers, their
  fades and an FLA intro that is not on the disc cost about ninety seconds of
  emulated CD before any measurement started.
- **One actor is not a crowd.** Every number above is cube 0 with a single
  object on camera. The primitive buffer holds about 290 gouraud triangles and
  drops the excess; nothing has yet been run that comes near it.
- **The 51 ms frames.** Work is ~24 ms against a 33.3 ms budget, so the margin
  is 9 ms and something occasionally eats it.
- **Nothing here has been run under PCSX-Redux** since the `mfc0` fix. It
  should still work; it has not been checked.

### A gotcha, at the cost of one wasted run

**mkpsxiso cannot overwrite the disc image while the emulator has it open, and
says so on a channel nobody was reading.** Twenty minutes of debugging a build
that had never been written. Kill the emulator *before* the build, not after —
same family as session 2's "`make_cd.py` runs after the build and before
mkpsxiso".
