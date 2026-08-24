# psx-lba — Little Big Adventure on the PlayStation

A port of the 1994 DOS engine to the original PlayStation, built from the GPL
source release. The reference is the **DOS CD version at 640x480**, not the
1996 console port — that one exists, and this project deliberately ignores it.

**No game data is included.** This repository holds source only. You need your
own copy of Little Big Adventure; the GOG release works and is what the port is
developed against.

---

## Status

Session 1 of the project. There is no playable build yet. What exists:

- [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) — the architecture study, with
  the RAM and VRAM arithmetic that decides how the port has to be built.
- [`docs/M0-NOTES.md`](docs/M0-NOTES.md) — the M0 experiments and what they
  measured.
- [`tools/`](tools/) — an HQR reader and the polygon census that answered the
  first architectural question.
- [`m0-hello/`](m0-hello/) — 640x480 interlaced, the CLUT blit path, the VRAM
  map.
- [`m0-bench/`](m0-bench/) — the real 1994 rasteriser, compiled for both the
  PlayStation and the Nintendo DS, running identical work so the two machines
  can be compared instead of guessed at.

The engine itself is not ported yet. That is M1.

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
