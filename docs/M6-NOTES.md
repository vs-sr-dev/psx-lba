# M6 — the pad, and the buffer that was never there

M5 left a game loop nobody could command. M6 is the controller, and the answer
to "does it work" is not a person saying yes: it is a script that holds the
d-pad for three seconds and a log line that says how far Twinsen got.

**It works.** All four directions, the four verbs, the behaviour panel and the
inventory, on a real scene off a real disc, at the same 34 ms frame M5
measured. The game is playable.

What playing it then found is a rendering bug that four milestones of static
scenes could not have found, and it has one cause with three faces.

---

## 1. The mapping

`Joy`, `Fire` and `Key` are DOS: a four-bit direction field, a bitfield of the
modifier keys, and the scancode of whatever is held down *right now*. MainLoop
samples all three into `MyJoy`/`MyFire`/`MyKey` once per iteration
(PERSO.C:402) and everything downstream reads those.

The pad is read as three groups — the face buttons are the four things Twinsen
does, the shoulders are the modifier the DOS player held, Start and Select are
the two menus:

| | | |
|---|---|---|
| d-pad, left stick | `Joy` | move, turn |
| Cross | `F_SPACE` | action — talk, search, jump, hit |
| Square | `F_ALT` | throw the magic ball |
| Triangle | `F_SHIFT` | inventory |
| Circle | `F_RETURN` | recentre the camera; validate, in menus |
| L1 | `F_CTRL` | behaviour panel, **held** open |
| R1 | `K_H` | holomap |
| Start | `K_ESC` | pause, save, quit |
| Select | `K_F4` | options |
| L2, R2 | — | free |

Three details are not arbitrary.

**LBA is tank-controlled**, so a d-pad is not an approximation of the DOS
arrows, it *is* them: up walks forward along Twinsen's own facing and
left/right rotate him. The left stick is OR-ed into the same four bits with a
deadzone at half deflection; nothing reads it as an analog axis, because
nothing in the engine wants one.

**`Key` is a level, not an edge.** The engine de-repeats it itself, with the
`flag` idiom in GAMEMENU.C — sample once, then blank everything until nothing
is held. Latching `Key` breaks `TimerPause()` permanently, which is the bug M1
left behind and M5 fixed. Only one scancode can be reported, so the three
buttons that map to one are ranked Start > Select > R1.

**L1 is a hold.** `MenuComportement()` spins `while (Fire & F_CTRL)` and reads
the d-pad inside it, so the panel is open exactly as long as the shoulder is
down. See §4 — this turned out to have a floor.

A pad that is unplugged mid-stride now **clears** `Joy`/`Fire`/`Key` instead of
returning early and leaving the last direction latched, and a device that is
not a pad (mouse, Guncon, nothing at all) is rejected on `type` rather than
producing `~0` — all buttons pressed — out of a zeroed buffer.

## 2. How it was verified: `-DPSX_M6_AUTOPILOT=ON`

Every number in this port came out of `grep TTY duckstation.log`. An input path
signed off by a human holding a controller would have been the one measurement
in the project that could not be repeated, diffed, or checked against the next
build.

So `platform/psx/psx_m6.c` gives the pad a stand-in: a table of
(direction, buttons, duration) that `PORT_ScanInput` consults instead of the
hardware, feeding the same globals at the same 50 Hz. Each step ends with a
line saying what was held and what the hero's state was before and after:

```
[M6]  1 forward      joy  1 fire 0 key 0 | pos 26880,768,29173 +0,+0,+1547 | beta   0 +  0 | anim 0->1
[M6]  3 backward     joy  2 fire 0 key 0 | pos 26880,768,27655 +0,+0,-1518 | beta   0 +  0 | anim 0->2
[M6]  5 turn left    joy  4 fire 0 key 0 | pos 26880,768,27655 +0,+0,+   0 | beta 204 +204 | anim 0->3
[M6]  9 turn right   joy  8 fire 0 key 0 | pos 29183,768,28433 +0,+0,+   0 | beta 729 +512 | anim 0->4
```

The script runs once and then hands the pad back, so an autopilot build is
still something to play.

## 3. What the log says

Cube 0, DuckStation, retail BIOS, interpreter.

| step | held | result |
|---|---|---|
| forward, 150 ticks | `J_UP` | **+1547** along +Z, `GEN_ANIM_MARCHE` |
| backward, 100 ticks | `J_DOWN` | **−1518**, back to **29 units** of where he started |
| turn left, 60 ticks | `J_LEFT` | beta **+204** of 1024, `GEN_ANIM_GAUCHE` |
| turn right, 120 ticks | `J_RIGHT` | beta **+512** wrapping — half a circle, `GEN_ANIM_DROITE` |
| forward again | `J_UP` | +2303 X, +778 Z — **along the new facing**, not along the axis |
| behaviour panel | `F_CTRL` + `J_RIGHT` | `Comportement` **0 → 1**, C_NORMAL to C_SPORTIF |
| inventory | `F_SHIFT` | opens, and closes on a second press |

The out-and-back is the strongest single line in it: three seconds forward and
two seconds back put Twinsen within 29 world units of his start, which is not
something a half-wired input path does by accident. The turns are the same
assertion about rotation, and the "forward again" line is the assertion that
movement is relative to the facing and not to the world axes.

**Input costs nothing.** Frames stayed at 34 ms and 29 fps for the whole walk,
1 rectangle and about 3000 pixels presented per frame against M5's 2700 idle.
`PORT_ScanInput` is thirty instructions in a timer interrupt that was already
running.

### Two measurements that were nearly wrong

**`J_DOWN` reported zero movement, and it was a wall.** The first version of
the script turned 180° and *then* reversed, into ground Twinsen had just walked
up to. Reordering it to reverse immediately after walking forward — retracing
known-clear ground — produced the −1518 above. A harness that measures the
scenery is worse than no harness.

**The script ran before the game could see it.** The first build armed the
autopilot the moment the timer did. `ChangeCube` is 18.8 seconds and the tick
counts straight through it, so twenty-five seconds of walking, turning and
pressing were delivered into a MainLoop that had not come back from the scene
load — and the log came back with one step reported, no movement, and the
inventory sitting open on screen. That reads exactly like a dead input path and
was nothing of the sort. The script now arms on the game loop's first frame,
and the report carries the step *index* so a misattributed line cannot look
plausible again.

## 4. The behaviour panel has a minimum hold, and it is 1.4 seconds

L1 held for 1.4 s: the panel opened, and `Comportement` did not change. Held
for 3.0 s: `C_NORMAL → C_SPORTIF`.

`MenuComportement()` draws four animated bodies before it looks at the pad for
the first time, and when it does it reads the **live `Fire`**, not the `MyFire`
that MainLoop sampled to get there. So a hold shorter than the drawing is a
panel that opens, closes, and changes nothing. On this machine that drawing is
seconds, not frames.

Which leads to the number nobody had measured:

| modal | worst frame |
|---|---|
| behaviour panel | **5851 ms** |
| inventory | **3276 ms** |

A behaviour change is something an LBA player does every thirty seconds. Six
seconds of it is not a rendering detail, it is the game.

## 5. The bug M6 found: `Screen` and `Log` are one allocation

Playing the build turns up three artefacts:

- **the shadow leaves a permanent trail** behind Twinsen as he walks,
- **the behaviour panel's text burns in** and its animated bodies never appear
  to move,
- **the inventory burns in** the same way.

One cause. In M3 the second 640x480 buffer was dropped and `Screen` was aliased
onto `Log` (PERSO.C, under `PORT_PSX`) — 300 KB of 1575, and the allocation
that decides whether the game fits at all. The reasoning at the time was
recorded in the source: *"Log is the composed background and the GPU draws the
actors over it, so there is nothing for a clean copy to restore."*

That was true of the actors, and only of the actors.

`Screen` is the engine's pristine copy of the composed background, and every
erase in the game restores from it:

```c
void ClsBoxes()                                     /* FLIPBOX.C:130 */
{
    for (n = 0; n < NbOptPhysBox; n++, ptr++)
        CopyBlock(ptr->x0, ptr->y0, ptr->x1, ptr->y1, Screen,
                  ptr->x0, ptr->y0, Log);
}
```

With `Screen == Log` that is a copy onto itself, and `CopyScreen` correctly
declines to do it. So **anything the software rasteriser draws into `Log`
during a frame is permanent**. The 3D actors are exempt because they are GPU
primitives that never touch `Log` — which is exactly why four milestones of
looking at a static scene with one actor on it saw nothing wrong.

The shadow is not exempt: it is `AffGraph(..., BufferShadow)`, a software
sprite, straight into `Log` (OBJECT.C:3013). Neither is the HUD, nor text, nor
any modal — `Inventory()` and `MenuComportement()` both open with
`CopyScreen(Log, Screen)` to save the background and then repaint over what
they think is a clean copy every iteration. That is the burn-in, and it is also
why the panel's bodies look static: each frame is composited on top of the last
one instead of replacing it.

### What it costs to fix

**300 KB**, for the buffer the engine was written to have. The heap has 39 KB
free.

Which is where M5's other carried item stops being an optimisation and becomes
the enabling one: **HQM is 400000 bytes allocated and 144280 used by the worst
scene measured**. Reclaiming it takes the margin from 39 KB to near 290 — and
290 KB against a 300 KB buffer is close enough that the exact figure has to be
worked, but the shape of the answer is clear. *The HQM reclaim is what pays for
a real `Screen`.*

The alternative is to recompose each dirty box from the brick grid instead of
copying it from a saved image — CPU instead of memory. `AffGrille` is 461 ms
for 307200 pixels, so a 3000-pixel box is a few milliseconds of rasterising,
but the function walks all 102400 grid cells to find the bricks that touch the
box and that walk does not get cheaper with a smaller box. It would need an
index the engine does not keep.

This is a memory decision, not a rendering one, and it belongs with M7's.

---

## Carried forward from M6

- **`Screen` must become a real buffer** (§5). Three visible artefacts, one
  cause, paid for out of the HQM reclaim.
- **A modal costs 3–6 seconds** (§4). The behaviour panel is a core verb and it
  is currently unusable at that price; most of it is `DrawMenuComportement`
  drawing four bodies and the `AffScene(TRUE)` full recompose behind it.
- **Nothing has been played past cube 0.** Walking through a door is
  `ChangeCube` from inside the loop, and that is still 18.8 seconds.
- **`GetAscii` returns nothing.** The save-name entry screen needs a character
  source; the memory card (M8) needs it first.
- **No rumble, no analog aiming, no L2/R2.** Deliberate: the engine has no
  concept of any of them.

## Rebuild, with the autopilot

```sh
taskkill //F //IM duckstation-qt-x64-ReleaseLTCG.exe
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" \
    -w /work/platform/psx psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DPSX_M5=ON -DPSX_M6_AUTOPILOT=ON -DPSX_SKIP_INTRO=ON \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'
```

then stage, master and run as in M5. `-DPSX_M6_AUTOPILOT=OFF` (the default) is
the same build with the pad and nothing else; the knob requires `PSX_M5` and
says so at configure time.
