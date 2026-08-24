/*
 * psx_m3.c — M3: the first thing this port draws that came off the disc.
 *
 * The goal is one static LBA scene at 640x480: the brick grid composed into
 * the 8bpp background by the inherited software path (AffGrille, unchanged
 * 1994 code), then put on the framebuffer by the GPU through the staging page
 * and the CLUT. M0 proved that second half with synthetic data; M3 is the same
 * path with the real thing behind it.
 *
 * Getting there means walking the whole scene-load chain, which is the point:
 *
 *     InitGame     -> the game lists, the perso, the starting cube
 *     ChangeCube   -> LoadScene (FICHE), the tracks and life scripts
 *                     (GERETRAK, GERELIFE), InitDial (TEXT.HQR), and
 *                     InitGrille -> LoadUsedBrick (LBA_GRI, LBA_BLL, LBA_BRK)
 *     AffGrille    -> 64x25x64 cube cells, each a brick blit into Log
 *
 * Every one of those parses an LZSS-compressed byte stream and pulls words
 * out of it at whatever offset the data lands on. On the ARM9 that silently
 * rotated; on this machine it is an Address Error, and psx_exc.c is here to
 * name it. That is why this harness exists as a milestone rather than as part
 * of the game loop: it walks the gauntlet with nothing else moving.
 *
 * This file goes away when MainLoop runs. Until then PERSO.C calls it instead
 * of MainGameMenu, under PORT_PSX_M3.
 */

#include "port.h"

/* Engine entry points. port.h deliberately does not include the engine
 * headers, so the handful this needs are declared here, as the shim does
 * everywhere else. */
void InitGame(int argc, UBYTE *argv[]);
void ChangeCube(void);
void AffGrille(void);
void Cls(void);
void Flip(void);
void Palette(void *pal);
void Vsync(void);

extern UBYTE *PtrPal;
extern WORD NumCube, NewCube;
extern WORD DisableAutoSave;
extern WORD FlagAffGrille;
extern LONG StartXCube, StartYCube, StartZCube;
extern ULONG Size_HQM_Memory, Size_HQM_Free;

/* The scene to open. 0 is Twinsen's house, the first cube of a new game and
 * the one every other port in the family has used as its first picture. */
#ifndef PSX_M3_CUBE
#define PSX_M3_CUBE 0
#endif

static void report(const char *stage)
{
    PORT_Diag("[M3] %-22s ", stage);
    PORT_Diag("largest free block %lu KB\n", PORT_HeapLargestFree() / 1024UL);
    PORT_HeapReport(stage);
}

void PORT_M3_StaticScene(void)
{
    unsigned long t0, t1;

    PORT_Diag("\n[M3] static scene, cube %d\n", PSX_M3_CUBE);
    report("at the menu");

    /* The autosave in ChangeCube writes a save file. There is no Memory Card
     * support until M8, and a scene load is not the place to find that out. */
    DisableAutoSave = 1;

    InitGame(1, 0);
    report("after InitGame");

    NewCube = PSX_M3_CUBE;
    NumCube = -1;

    t0 = PORT_Micros();
    ChangeCube();
    t1 = PORT_Micros();
    PORT_Diag("[M3] ChangeCube (scene + dial + grid): %lu ms\n",
              (t1 - t0) / 1000UL);
    PORT_Diag("[M3] cube %d, camera start %ld,%ld,%ld\n",
              (int)NumCube, StartXCube, StartYCube, StartZCube);
    PORT_Diag("[M3] scene pool: %lu of %lu bytes of HQM used\n",
              (unsigned long)(Size_HQM_Memory - Size_HQM_Free),
              (unsigned long)Size_HQM_Memory);
    report("after ChangeCube");

    /* The composition itself. The DS measures the equivalent rebuild at
     * 207 ms; scaled by the 1.46x memory-bandwidth ratio M0 measured, the
     * prediction for this machine is near 300 ms. This is the line that
     * either confirms that or does not. */
    FlagAffGrille = 1;

    t0 = PORT_Micros();
    Cls();
    AffGrille();
    t1 = PORT_Micros();
    PORT_Diag("[M3] AffGrille (640x480 background compose): %lu ms\n",
              (t1 - t0) / 1000UL);

    /* And on screen. Palette first: the fade machinery lives in MainLoop,
     * which is not running, so the scene palette is applied outright. */
    Palette(PtrPal);

    t0 = PORT_Micros();
    Flip();
    t1 = PORT_Micros();
    PORT_Diag("[M3] Flip (300 KB to VRAM through the staging page): %lu ms\n",
              (t1 - t0) / 1000UL);

    report("on screen");
    PORT_Diag("[M3] done. The scene is up; nothing is animating it.\n");

#ifdef PSX_EXC_SELFTEST
    /* And now, on purpose. The scene above went through every byte-stream
     * parser in the engine without faulting, which is either because the
     * DS port already fixed them or because the handler is not armed. This
     * settles which. */
    PORT_ExcSelfTest();
#endif

    for (;;)
        Vsync();
}
