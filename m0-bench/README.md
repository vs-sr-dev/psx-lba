# M0 — rasteriser calibration

The same rasteriser, the same synthetic work, on both machines, so the
PlayStation numbers have a reference instead of a guess.

`bench_core.c` drives `translate/s_poly.c` and `translate/s_fillv.c` — the C
translations of Adeline's `S_POLY.ASM` and `S_FILLV.ASM`, unmodified — over
polygons sized from `tools/poly_census.py` (~100 px, the BODY.HQR mean) into a
640x480 8bpp buffer, which is what the engine's `Log` is.

The DS build omits `-DPORT_NDS` on purpose: `PORT_FASTCODE` collapses to
nothing and the rasteriser runs from main RAM, as it must on the PlayStation.
The shipping DS port puts it in ITCM and gains ~23%, which the PlayStation
cannot copy — its 1 KB scratchpad will not hold the fillers. Both sides build
with GCC -O2, 32-bit instruction encoding and `-fsigned-char`.

## Build and run

```sh
# PlayStation
docker run --rm -v ${PWD}:/work -w /work/m0-bench psx-lba-psn00b sh -c \
    'cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake && \
     cmake --build build'
pcsx-redux -run -stdout -loadexe build/m0bench.exe

# Nintendo DS
docker run --rm -v ${PWD}:/work -w /work/m0-bench/nds \
    devkitpro/devkitarm:latest make
# then open nds/build/m0bench.nds in melonDS and read the bottom screen
```

Results and what they mean: [`../docs/M0-NOTES.md`](../docs/M0-NOTES.md).
