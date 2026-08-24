# psx-lba — Little Big Adventure on the PlayStation

A port of the 1994 DOS engine to the original PlayStation, built from the GPL
source release. The reference is the **DOS CD version at 640x480**, not the
1996 console port — that one exists, and this project deliberately ignores it.

**No game data is included.** This repository holds source only. You need your
own copy of Little Big Adventure; the GOG release works and is what the port is
developed against.

---

## Status

Session 2 of the project. There is no playable build yet, but **a Little Big
Adventure scene is on screen at 640x480 with Twinsen standing in it** — the
background built out of the brick grid by the 1994 software path, the actor
rasterised by the GPU — off a CD image made from your own copy of the game.

It fits in 2 MB. The engine reaches its first scene with 1535 KB allocated
against 1574 KB of heap, once three things move: `Log` and `Screen` become one
640x480 buffer (the GPU draws the actors, so there is no second image to
restore from), `BufSpeak` drops from 256 KB to 64 KB (voices are going to the
SPU), and the MCGA page is allocated on demand. That is 554 KB, and 39 KB
spare. The GPU actor path then costs **zero** further bytes of main RAM, which
is what the first of those three moves was betting on.

A scene load costs 3.9 s and composing the background costs 588 ms. Both are
measured, both are worse than predicted, and neither is a frame rate — there is
no frame loop yet.

What exists:

- [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) — the architecture study, with
  the RAM and VRAM arithmetic that decides how the port has to be built.
- [`docs/M0-NOTES.md`](docs/M0-NOTES.md) — the M0 experiments and what they
  measured.
- [`docs/M1-NOTES.md`](docs/M1-NOTES.md) — what it took to get the engine
  compiling, linking and booting on MIPS.
- [`docs/M2-NOTES.md`](docs/M2-NOTES.md) — the CD, and the first RAM verdict.
- [`docs/M3-NOTES.md`](docs/M3-NOTES.md) — the scene on screen, the corrected
  RAM verdict, and why the emulator had been hiding the one bug class this port
  was most afraid of.
- [`docs/M4-NOTES.md`](docs/M4-NOTES.md) — the actors on the GPU, and why
  replacing a rasteriser turned out to be the smallest change in the project.
- [`engine/`](engine/) and [`translate/`](translate/) — the 1994 engine and the
  C translations of its assembly modules, inherited from the DS port.
- [`platform/psx/`](platform/psx/) — the PlayStation HAL: the CD, the video
  path, the instrumented heap, the exception handler, and the GPU actor
  renderer.
- [`tools/`](tools/) — an HQR reader and the polygon census that answered the
  first architectural question.
- [`m0-hello/`](m0-hello/) — 640x480 interlaced, the CLUT blit path, the VRAM
  map.
- [`m0-bench/`](m0-bench/) — the real 1994 rasteriser, compiled for both the
  PlayStation and the Nintendo DS, running identical work so the two machines
  can be compared instead of guessed at.

Next is M5: a frame loop, driven by the engine's own dirty rectangles.

## The short version of the plan

The engine composes a 640x480 8bpp frame in software: an isometric background
built from bricks, 3D actors rasterised over it, foreground masks blitted back
on top. On a 2 MB machine that does not fit, and at 33 MHz it does not run.

So the PlayStation build splits the work the way the hardware wants it:

| Element | Where it runs |
|---|---|
| Brick grid to background image | software, inherited, once per scene |
| Background restore of dirty rectangles | GPU, 8bpp CLUT texture |
| 3D actor transforms | GTE |
| Actor rasterisation | GPU, flat and gouraud primitives |
| Occlusion masks, sprites, fonts | GPU |
| FLA movies, holomap | software, inherited |

Both halves of that table were measured in M0 rather than assumed. See
[`docs/M0-NOTES.md`](docs/M0-NOTES.md).

## Build

Everything builds in Docker; nothing needs to be installed on the host.

```sh
docker build -t psx-lba-psn00b -f tools/docker/Dockerfile.psn00b .

docker run --rm -v ${PWD}:/work -w /work/m0-hello psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'
```

The result is a PS-EXE that PCSX-Redux, DuckStation and no$psx all load
directly. PCSX-Redux is the reference for bring-up because it prints the BIOS
TTY, which is where every diagnostic in this project goes:

```sh
pcsx-redux -run -stdout -loadexe m0-hello/build/m0hello.exe
```

**Always pass `-interpreter`.** PCSX-Redux's recompiler does not raise MIPS
address errors: an unaligned load returns the correctly assembled value instead
of faulting. Since unaligned access off a byte stream is the single most likely
bug in a 1994 DOS engine moved to MIPS, running under the recompiler means not
testing for it at all. See [`docs/M3-NOTES.md`](docs/M3-NOTES.md) §5.

The full game builds from `platform/psx`:

```sh
docker run --rm -v ${PWD}:/work -w /work/platform/psx psx-lba-psn00b sh -c     'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release         -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake &&      cmake --build build'

python tools/make_cd.py "/path/to/Little Big Adventure/Speedrun/Windows"
docker run --rm -v ${PWD}:/work -w /work psx-lba-psn00b     mkpsxiso -y -o build/lba1psx.bin -c build/lba1psx.cue build/iso.xml

pcsx-redux -run -stdout -interpreter -iso build/lba1psx.cue
```

`make_cd.py` copies the freshly built PS-EXE onto the staged disc, so it has to
run *after* the build and *before* `mkpsxiso`. Two build knobs matter:

| | |
|---|---|
| `-DPSX_M3=ON` (default) | stop at one static scene instead of the main menu |
| `-DPSX_M4=ON` (default) | draw the scene's actors on the GPU after it |
| `-DPSX_EXC_SELFTEST=ON` | fault on purpose, to prove the crash handler is armed |

The DS half of the calibration benchmark needs devkitARM instead:

```sh
docker run --rm -v ${PWD}:/work -w /work/m0-bench/nds \
    devkitpro/devkitarm:latest make
```

## Bring your own assets

The port reads the **DOS** data. In a GOG install that is `Speedrun/Windows/` —
the `.HQR` archives, `SETUP.LST`, `LBA.CFG` and `VOX/`. Do **not** point the
tools at `Common/`: that holds the 2023 remaster's archives, which carry the
same file names and completely different contents.

```sh
python tools/poly_census.py "/path/to/Little Big Adventure/Speedrun/Windows"
```

## Licence

GPL v2, following the [engine source
release](https://github.com/2point21/lba1-classic-community). The toolchain is
open for the same reason: PSn00bSDK is MPL-2.0 and GCC is GPL, whereas the
original Psy-Q SDK could be used neither legally nor compatibly.

## Related ports

This is one of a family. The Nintendo DS port is the direct parent — same
little-endian ILP32 target, same translated assembly modules, and every
alignment bug it uncovered in the 1994 code is a bug this port would have hit
too.
