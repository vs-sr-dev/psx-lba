# M2 — the CD, and the RAM verdict

Session 1, 2026-08-24, after [M1](M1-NOTES.md). M2 exists to answer the
question the whole project rests on:

> Does Little Big Adventure fit in 2 MB?

**Not as it stands. It is short, and the three moves the architecture already
requires are almost exactly enough to close the gap.** The numbers are below.

---

## 1. The disc

`tools/make_cd.py` stages a DOS install and writes the mkpsxiso project; the
ISO9660 file names are the DOS ones unchanged, because every archive is already
8.3 and uppercase. That is one of the small mercies of moving a 1994 DOS game
onto a 1994 console.

`platform/psx/psx_cd.c` is the stdio the engine is built on. libpsn00b has no
`FILE` and no `fopen`, so the whole surface is ours: `fopen` resolves through
`CdSearchFile`, and `fread` either DMAs whole sectors straight into the
caller's buffer or goes through a one-sector cache when the read is not sector-
aligned — or when the destination is not 32-bit aligned, which the DMA requires
and the engine does not guarantee.

Two bugs are worth recording, because both were mine and both looked like
someone else's.

**The padded file names.** The engine asked for `LBA.CFG`, the disc said it was
not there, and the root directory listing showed:

```
[FS]   LBA.CFG     ;1     3097
[FS]   SYSTEM.CNF;1         60
```

The staged files had five spaces in their names and the two files the tool
wrote itself did not. The cause was a `{name:<12}` field width in the XML
generator, put there to line up the columns — mkpsxiso takes the `name`
attribute literally, so the alignment went onto the disc. A cosmetic choice in
a build script became a file the game could not open.

**The directory listing that returned nothing.** Before that, the same listing
reported zero entries, which pointed at the disc. It was not the disc:
`CdOpenDir` returns `CdlDIR *` and I had assigned it to a `CdlDIR`, which is
`void *`. The types are compatible enough for the compiler to shrug and the
listing to walk a pointer to a local variable.

The lesson in both is the same and it is worth stating: **a diagnostic that is
itself wrong costs more than no diagnostic**, because it moves the search to
the wrong place. What eventually settled it was a check that could not lie —
reading sector 16 directly and printing the five bytes that have to be `CD001`.

---

## 2. It loads

```
Copyright (c) Adeline Software International 1994, All Rights Reserved.

Please wait, loading drivers using LBA.CFG...
Initialising Midi device. Please wait...
Initialising SVGA device. Please wait...
Initialising Mixer device. Please wait...
Initialising Wave device. Please wait...

[VID] SVGA 640x480i, Log=80075af8 (300 KB)
PS-X Control PAD Driver
```

The engine reads its configuration off the disc, brings up all four driver
layers, initialises SVGA at 640x480 interlaced, allocates its 300 KB
composition buffer, and starts the pad.

Two adjustments were needed to get that far, and both follow the DS port:

- `InitMidiDLL`, `InitMidi`, `WaveInitDLL` and `InitWave` report success.
  ADELINE.C calls `exit(1)` when a driver named in `LBA.CFG` fails to load, and
  a config copied from a DOS install always names one. Refusing here would mean
  the port could only boot from a configuration file it had written itself.
- `MixerDriver` is rewritten to `NoMixer` in the staged copy of `LBA.CFG`.
  LIB_MIX/MIXER.C is real code, not a stub — it loads the DLL itself, and there
  is no DLL. A PlayStation has no mixer chip, so that is the value DOS
  SETUP.EXE would have written on a machine without one. Nothing else in the
  user's config is touched.

The intro movie (`FLA_GIF.HQR`, `DRAGON3.FLA`) is not on the disc yet and the
engine steps over its absence exactly as it was written to. FLA playback is M8.

---

## 3. The RAM verdict

```
[MEM] heap 1579 KB at 80065090 (bss ends 80065084, 64 KB stack held back)
...
[MEM] malloc(400500) FAILED, use=1525K
[MEM] live blocks >= 32K, newest first:
[MEM]     361472     353K  80186358
[MEM]     204800     200K  80154330
[MEM]     262178     256K  8010bd38
[MEM]     307700     300K  800c0b20
[MEM]     307200     300K  80075af8
[MEM]      64000      62K  800660d0
[MEM]   6 blocks listed, 1472K of 1525K total
ERROR: MemoryNotAlloc (Malloc): Size = 400500
Not Enough Memory: HQMemory (SEE README.TXT)
```

Every block is identifiable, and the list is the feasibility study's table with
measured numbers in place of the DS's:

| block | KB | fate |
|---|---|---|
| BufferBrick | 353 | stays |
| BufCube 64x25x64x2 | 200 | stays |
| BufSpeak (voice, DosMalloc) | 256 | **moves to SPU RAM** |
| Screen 640x480 (clean background) | 300 | stays in main RAM |
| Log 640x480 (software render target) | 300 | **disappears with the GPU actor path** |
| Phys 320x200 (MCGA / FLA) | 62 | stays |
| everything else | 53 | |
| **allocated** | **1525** | |
| HQM (scene / grid / mask pool) — *requested, failed* | 391 | stays |

So the engine needs **1916 KB** to reach its first scene, and the heap is
**1579 KB**. It is short by **337 KB**.

And there is more still to come. `PERSO.C` sizes the sprite, animation and
sample pools as fractions of `Malloc(-1)`, a "how much is free" probe that the
community source implements as `return 0; /* Query memory not supported */`.
Every pool therefore collapses to its clamped floor — SpriteMem 50000,
SampleMem 200000, AnimMem 100000, about **342 KB** — and none of that was
allocated before the failure. It is read off the source rather than measured,
but it is real, and the DS DEVLOG is emphatic about what those floors cost:
missing 3D objects, lost animations, and gurus in three unrelated functions,
because the pool compacts on eviction while callers still hold pointers into it.

**The honest total is about 2258 KB against 1579 KB of heap.**

### What closes it

The moves are not new. They are the ones the architecture already requires, and
this is the first time they can be scored:

| move | frees | why it was already required |
|---|---|---|
| `Log` disappears | 300 KB | the GPU draws the actors; there is no CPU render target |
| `BufSpeak` to SPU RAM | 256 KB | voices are ADPCM in the SPU's 512 KB |
| `SampleMem` to SPU RAM | 195 KB | so are the sound effects |
| **total** | **751 KB** | |

2258 − 751 = **1507 KB against 1579 KB. It fits, with 72 KB to spare.**

That is not comfortable. It is floor-sized pools, no eviction headroom, and a
margin thinner than a single scene's brick data. But it is a fit, arrived at by
measurement, and it says something more useful than "it fits": **the GPU
rendering path and the SPU audio path are not optimisations that can be
deferred. They are what makes the game fit at all.** The feasibility study
argued that from the DS numbers; M2 has now watched it happen.

Everything above floor-sized pools has to come from paging archives off the CD
rather than holding them resident — which is what the drive-contention problem
in M7 is really about, and why it is the one open risk with no precedent in the
family of ports.

---

## 4. One more thing the hardware said

After the out-of-memory message the engine's error path jumps to `0x800001a4`
and executes garbage:

```
Unknown instruction for dynarec - address 800001a8, instruction fa00fa00
First chance exception: ReservedInstruction from 0x800001a4
```

That is the hazard the feasibility study flagged, arriving on schedule:
**address 0 on a PlayStation is kernel RAM, not an unmapped page.** On the DS a
null pointer produced a guru meditation and named itself. Here it is a valid
address, the CPU dutifully executes what it finds, and the only reason we see
anything at all is that the emulator's recompiler refuses to translate it.

This particular instance is on an error path and does not matter. The class
does. The countermeasure stands: a watchpoint over `0x00000000-0x00010000` in
PCSX-Redux for the whole of the bring-up.

---

## 5. Next

- **M3**: `AffGrille` to an 8bpp background, staged to VRAM, on screen — the
  first thing this port will draw that came off the disc.
- The alignment gauntlet is now open. HQR archives are being decompressed by
  LZSS code that pulls 16-bit control tokens from odd offsets, and MIPS will
  raise an exception where the ARM silently rotated. Nothing has faulted yet
  because nothing has walked far enough.
- `Malloc(-1)` needs a real answer before the pools can be sized honestly.
