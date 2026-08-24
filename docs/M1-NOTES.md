# M1 — the engine on MIPS

Session 1, 2026-08-24, straight after [M0](M0-NOTES.md). The question M1 was
supposed to answer:

> How much of the 1994 code objects to a machine that raises an exception on an
> unaligned load instead of quietly rotating the result?

The honest answer is **less than expected, and M1 could not fully ask the
question** — for a reason that turns out to be the interesting part.

---

## 1. What it took to compile

The whole engine — 59 translation units, 31171 lines — compiles for
`mipsel-none-elf` with:

- **four compat headers**, each a few lines: `fcntl.h` (eight LIB_SYS files
  include it and none uses an `O_*` flag, so it is empty), `math.h` (the
  engine's entire use of libm is one `pow(2, n)` in GIF.C), `process.h` and
  `direct.h` (MinGW/DOS headers, stubbed as the DS port stubs them).
- **two engine edits**, both one line, both extending a guard the DS port
  already wrote:

  ```c
  #if !defined(PORT_SDL) && !defined(PORT_NDS) && !defined(PORT_PSX)
  ```

  `LIB_SVGA/MASKGPH.C` and `game/GRILLE.C` both define `CreateMaskGph`;
  `LIB_MENU/MENUFUNC.C` and `game/PERSO.C` both define `Message`. These are
  1994 duplicates that the Watcom librarian resolved by only pulling the
  unresolved one out of the library. No modern linker does that. The DS port
  hit it first and left the comment explaining why; this port adds itself to
  the guard.

That is the entire source cost of moving a 1994 x86 DOS codebase to MIPS.
Everything else was inherited: the sixteen assembly modules were already
translated to C for the DS, and MIPS shares the DS's endianness and data model.

MIPS gives one thing back for free that ARM had to be argued into: `char` is
signed by default, which is what the 1994 code assumes. The build passes
`-fsigned-char` anyway, because relying on a target default is how that class
of bug returns.

## 2. What it took to link

The link named **130 undefined symbols**, and that list *is* the HAL
specification. Sorted:

| group | count | where it went |
|---|---|---|
| video / SVGA layer | 26 | `psx_video.c` — real, including the CLUT blit path |
| input, timing, DOS system calls | 34 | `psx_sys.c` |
| C library gaps | 17 | `psx_libc.c` |
| stdio | 12 | `psx_cd.c` — handles real, CD backing is M2 |
| audio: WAVE, mixer, MIDI | 32 | `stubs.c` |
| CD-ROM | 11 | `stubs.c` |

Two of those groups deserve a note.

**libpsn00b is not newlib.** It has `memcpy`, `snprintf` and `malloc`, and
stops: no `FILE`, no `fopen`, no `qsort`, no `atoi`. The engine was written
against Watcom's DOS libc, which is wider, and uses names (`itoa`,
`_splitpath`, `stricmp`) that were never standard outside DOS. `psx_libc.c`
supplies exactly what the call sites need and says where it is narrower than
the real function — `pow`, for instance, handles integer exponents because
GIF.C is the only caller and passes `pow(2, n)`.

**MIDI was never released.** `LIB_MIDI/LIB_MIDI.C` in the community source has
its entire body commented out: the AIL32 library it wrapped was not open. So
the MIDI functions in `stubs.c` are not stubbing something out, they are
supplying it for the first time. The plan does not restore it: the music will
be pre-rendered to XA-ADPCM at build time, as the DS port pre-renders to WAV.

## 3. The binary is smaller than the estimate

```
   text     data      bss      dec
 209424     3292   121870   334586
```

**335 KB**, against the 450-550 KB the feasibility study assumed for a MIPS
build (extrapolated from the DS's 414 KB of Thumb). Function- and data-section
garbage collection plus the absence of libnds and calico more than pay for
MIPS's fixed 32-bit encoding.

That matters directly, because it is 150 KB the RAM budget did not expect to
have. Measured at boot:

```
[MEM] heap 1593 KB at 80061b10 (bss ends 80061b08, 64 KB stack held back)
```

**1593 KB of heap on a 2 MB machine.** The feasibility study's working set —
BufferBrick 353 KB, BufCube 200 KB, HQM 391 KB, Phys 62 KB, roughly 1.0 MB —
fits inside that with ~590 KB left for the anim and sprite pools, text and
working allocations. It is tight, it is not obviously impossible, and M2 will
turn "not obviously impossible" into a number.

## 4. It boots

```
psx-lba — Little Big Adventure, PlayStation
engine: lba1-classic-community (GPL v2), 1994 Adeline Software
video : NTSC
bss   : ends at 80061b08

[MEM] heap 1593 KB at 80061b10 (bss ends 80061b08, 64 KB stack held back)
[BOOT] entering lba_main

Copyright (c) Adeline Software International 1994, All Rights Reserved.

[FS] fopen("LBA.CFG") -> not present (M1: no CD yet)
Error: Cannot find configuration file LBA.CFG.

*** PANIC: exit(1) — the engine asked to quit
```

Adeline's 1994 startup banner, printed by a PlayStation. The engine runs its
own initialisation, reaches its configuration file, does not find it, reports
the error the way it always did, and exits — through the port's panic handler,
which prints the heap state on the way out.

Nothing here is pretending. That banner comes from the engine's own code path,
not from the shim.

## 5. What M1 could not answer

The premise was that MIPS would start throwing Address Error exceptions at the
1994 code. It has not, and the reason is worth stating plainly rather than
claiming a clean bill of health:

**every alignment bug the DS port found is in code that walks a byte stream** —
the LZSS decompressor pulling a 16-bit control token from an odd offset, the
actor script interpreter reading a long out of a packed record, `GERELIFE`,
`GERETRAK`, `FICHE`, `GRILLE`, `P_ANIM`, `DISKFUNC`. None of that runs until
there is data to walk. M1 has no data.

So the alignment gauntlet does not start here. It starts the moment M2 opens
the first HQR archive — and MIPS will be louder about it than ARM was, because
an exception is a fact and a rotated word is a rumour.

## 6. Next

M2: the CD. An ISO9660 file layer behind `psx_cd.c`, a staging tool that builds
the disc image from a DOS install, and then the two things that have been
waiting:

- the real RAM verdict, from `[MEM]` at the first scene;
- the first data the engine has ever parsed on this machine.
