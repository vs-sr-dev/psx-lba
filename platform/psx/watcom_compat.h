/*
 * watcom_compat.h (PSX) — force-included (gcc -include) in front of every
 * ENGINE translation unit only, never the platform/psx shim files.
 *
 * Same job as the DS port's copy: kill the Watcom keywords, restore min/max,
 * and fill the gaps between the 1994 DOS libc and the one actually present.
 * The gaps are wider here than on the DS, because libpsn00b is not newlib: it
 * has no FILE, no fopen, no ctype tables worth the name, no qsort. Those come
 * from psx_file.h and psx_libc.h.
 *
 * One thing MIPS gets for free that ARM did not: `char` is already signed, so
 * the 1994 assumption holds without -fsigned-char having to fight the ABI.
 * (The flag is passed anyway - PSn00bSDK sets it - because relying on a
 * target default is how this class of bug gets back in.)
 */
#ifndef WATCOM_COMPAT_H
#define WATCOM_COMPAT_H

/* Watcom calling-convention / memory-model keywords -> nothing */
#define cdecl
#define __far
#define __near
#define __loadds

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* GIF.C and PCX.C use `index` as a variable name; BSD index() is declared in
 * some string.h flavours. Rename after the header, as the DS port does, so a
 * later libc header never sees the macro. */
#define index lba1_index

#ifndef _MAX_PATH
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256
#endif

/* stdio: entirely ours (see psx_file.h). */
#include "psx_file.h"

/* the rest of the C library gaps (see psx_libc.h). */
#include "psx_libc.h"

/* Instrumented heap. On 2 MB this is not optional. */
void *PSX_malloc(unsigned int size);
void *PSX_calloc(unsigned int n, unsigned int size);
void *PSX_realloc(void *p, unsigned int size);
void  PSX_free(void *p);

#define malloc  PSX_malloc
#define calloc  PSX_calloc
#define realloc PSX_realloc
#define free    PSX_free

/* Engine TUs carrying PORT: diagnostics reach this through gnu89's implicit
 * declaration otherwise, which assumes int f() -- passing varargs through that
 * is undefined. One prototype here covers every engine TU. */
void PORT_Diag(const char *fmt, ...);

/* The real answer to Malloc(-1); see psx_sys.c and LIB_SYS/MALLOC.C. */
unsigned long PORT_HeapLargestFree(void);

#ifdef PORT_PSX_M3
/* The M3 milestone harness, called from PERSO.C in place of MainGameMenu. */
void PORT_M3_StaticScene(void);
#endif

#endif /* WATCOM_COMPAT_H */
