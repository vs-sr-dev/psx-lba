/*
 * port.h — internal header for the platform/psx shim.
 *
 * Mirrors platform/nds/port.h from the DS port: deliberately does NOT include
 * the engine headers, and re-declares the Adeline base types and the engine
 * variables the shim implements. ILP32 on MIPS, same as the DOS/Watcom build.
 */
#ifndef PORT_H
#define PORT_H

typedef unsigned long ULONG;
typedef signed long LONG;
typedef unsigned short UWORD;
typedef signed short WORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* ---- diagnostics (psx_sys.c) ------------------------------------------- *
 * Everything goes to the BIOS TTY, which PCSX-Redux prints. This is the
 * PlayStation's equivalent of the DS port's bottom-screen console and it is
 * the only channel this port has during bring-up.
 */
void PORT_Diag(const char *fmt, ...);
void PORT_Panic(const char *fmt, ...);

/* ---- exceptions (psx_exc.c / psx_exc.S) -------------------------------- *
 * Takes over the general exception vector so an Address Error names a pc and
 * a faulting address. Install this before anything else runs.
 */
void PORT_ExcInstall(void);
void PORT_ExcVectorDump(const char *when);
#ifdef PSX_EXC_SELFTEST
void PORT_ExcSelfTest(void);
#endif

/* ---- video (psx_video.c) ----------------------------------------------- */
extern UBYTE PORT_PalRGB[768];  /* current 8-bit palette, DAC-quantised     */
void PORT_PresentAll(void);     /* full Log -> VRAM                         */
void PORT_PresentRect(LONG x0, LONG y0, LONG x1, LONG y1);
/* Rectangles, pixels and microseconds sent to VRAM since the last call, which
 * this resets. The frame loop's bill; see psx_m5.c. */
void PORT_PresentStats(int *rects, unsigned long *pixels, unsigned long *us);

extern UBYTE *Log;              /* 640x480 8bpp composition buffer          */
extern UBYTE *MemoLog;
extern UBYTE *Phys;             /* 320x200 MCGA buffer (FLA, SceZoom)       */
extern WORD Screen_X;
extern WORD Screen_Y;
extern ULONG TabOffLine[481];
extern WORD ClipXmin, ClipYmin, ClipXmax, ClipYmax;

/* ---- input / timer (psx_sys.c) ----------------------------------------- */
extern volatile UWORD Key;
extern volatile UWORD FuncKey;
extern volatile UWORD Joy;
extern volatile UWORD Fire;
extern UWORD AsciiMode;
extern volatile LONG Click;
extern volatile LONG Mouse_X;
extern volatile LONG Mouse_Y;
extern volatile ULONG TimerSystem;
extern volatile ULONG TimerRef;
extern UWORD NbFramePerSecond;
extern UWORD WaitNbTicks;
extern UWORD CmptFrame;
extern UWORD Cmpt_18;

void PORT_ScanInput(void);
unsigned long PORT_Micros(void);

/* ---- heap (psx_sys.c) --------------------------------------------------- *
 * The instrumented heap from the DS port, which mattered on 4 MB and matters
 * more on 2. Canary per block, running total, PORT_HeapReport() on demand.
 */
void  PORT_HeapInit(void);
void  PORT_HeapReport(const char *where);
void  PORT_HeapDump(unsigned long min_bytes);
unsigned long PORT_HeapLargestFree(void);
void *PSX_malloc(unsigned int size);
void *PSX_calloc(unsigned int n, unsigned int size);
void *PSX_realloc(void *p, unsigned int size);
void  PSX_free(void *p);

/* ---- CD (psx_cd.c) ------------------------------------------------------ */
void PORT_CdInit(void);

/* ---- entry point ------------------------------------------------------- */
int lba_main(int argc, char *argv[]);

#endif /* PORT_H */
