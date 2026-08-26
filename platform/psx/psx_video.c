/*
 * psx_video.c — the SVGA layer, on a machine whose CPU cannot touch the
 * framebuffer.
 *
 * The engine composes into `Log`, a 640x480 8bpp buffer in main RAM, and then
 * asks for a rectangle of it to appear on screen. On DOS that was a copy to
 * the VGA aperture. Here there is no aperture: everything reaches VRAM through
 * a GPU DMA.
 *
 * What makes that affordable is that the GPU can texture from a CLUT image. A
 * dirty rectangle is DMAed into a staging texture page as raw 8bpp pixels and
 * drawn as one textured quad; the palette lookup happens in the GPU for free,
 * and a palette fade is 256 halfwords rewritten rather than a full re-blit.
 *
 * The VRAM layout, and why the background is not resident in VRAM, is in
 * docs/M0-NOTES.md — the short version is that texture pages are 256x256
 * texels on a 64-halfword grid, so a resident 640x480 8bpp copy would cost
 * 393 KB and leave 40 KB for everything else.
 *
 * M1 scope: the path is real and the numbers are honest, but the dirty-
 * rectangle discipline the engine already has is not exploited yet, and MCGA
 * mode is presented 1:1 rather than scaled. M3 and M8 respectively.
 */

#include <stdlib.h>
#include <string.h>

#include <psxgpu.h>
#include <psxetc.h>
#include <hwregs_c.h>

#include "port.h"

/* ── VRAM map (see docs/M0-NOTES.md) ─────────────────────────────────────── */
#define SCR_W       640
#define SCR_H       480

/* The upload tile stays where M1 put it, at the left edge of the free VRAM:
 * one 8bpp texture page begins at x 640, and everything about the present
 * path is calibrated against it. It is 128 texels wide now rather than 256,
 * to leave the 320 halfwords the background needs to its right. */
#define STAGE_X     640
#define STAGE_Y     0

/* The clean background -- the engine's `Screen` -- 640x480 8bpp packed two
 * pixels to a halfword: 320 halfwords by 480 lines, and 704 + 320 is exactly
 * the 1024 halfwords VRAM has. See PORT_BgStore below for why it is here and
 * not in main RAM. */
#define BG_X        704
#define BG_Y        0
#define BG_HW       (SCR_W / 2) /* 320 halfwords wide, to x 1023             */

#define CLUT_X      0           /* under the framebuffer                     */
#define CLUT_Y      480

/* Staging is done in bands so the RAM-side gather buffer stays small: on a
 * 2 MB machine a full 256x256 tile would cost 64 KB to save a few DMAs. */
#define TILE_W      256
#define TILE_H      32

/* ── engine-visible state ────────────────────────────────────────────────── */
UBYTE *Log = 0;
UBYTE *MemoLog = 0;
UBYTE *Phys = 0;

WORD Screen_X = SCR_W;
WORD Screen_Y = SCR_H;
WORD SizeCar = 8;

UBYTE Text_Ink = 15;
UBYTE Text_Paper = 0;

ULONG TabOffLine[481];

WORD ClipXmin = 0, ClipYmin = 0;
WORD ClipXmax = SCR_W - 1, ClipYmax = SCR_H - 1;

static WORD MemoClipXmin, MemoClipYmin, MemoClipXmax, MemoClipYmax;

UBYTE PORT_PalRGB[768];

/* ── local state ─────────────────────────────────────────────────────────── */
static DISPENV disp;
static DRAWENV draw;
static int video_up;
static int mode_x = SCR_W, mode_y = SCR_H;
static int mcga_on;             /* the zoom, not a screen mode -- see below */

static unsigned short clut[256];
/* DMA'd in both directions, so it has to be word aligned: an array of
 * char is not, by anything but luck. */
static unsigned char  stage[TILE_W * TILE_H] __attribute__((aligned(4)));

/* engine LIB_SYS */
void *Malloc(LONG);
void Free(void *);

/* ── setup ───────────────────────────────────────────────────────────────── */
static void BuildTabOffLine(void)
{
    int i;

    for (i = 0; i <= 480; i++)
        TabOffLine[i] = (ULONG)i * (ULONG)mode_x;
}

static void EnsureVideo(void)
{
    if (video_up)
        return;

    ResetGraph(0);

    SetDefDispEnv(&disp, 0, 0, SCR_W, SCR_H);
    SetDefDrawEnv(&draw, 0, 0, SCR_W, SCR_H);

    /* 480 lines is interlaced, and there is no room for a second buffer at
     * this size: draw and display are the same rectangle. The DOS build
     * blitted dirty rectangles straight to the VGA, so the tearing is parity
     * rather than a regression. */
    disp.isinter = 1;
    disp.isrgb24 = 0;
    draw.isbg = 0;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);

    video_up = 1;
}

static void UploadClut(void)
{
    RECT r = { CLUT_X, CLUT_Y, 256, 1 };

    LoadImage(&r, (const uint32_t *)clut);
    DrawSync(0);
}

/*
 * Phys is the 320x200 MCGA page, and SVGA mode never touches it: the FLA
 * player and the zoomed camera are the only things that do. 62 KB is not a
 * rounding error on a heap with 40 KB of slack, so it is allocated when MCGA
 * mode is entered and not before.
 */
static void InitSvga(void)
{
    EnsureVideo();
    mode_x = SCR_W;
    mode_y = SCR_H;
    Screen_X = SCR_W;
    Screen_Y = SCR_H;
    BuildTabOffLine();
    UnSetClip();
}

/*
 * MCGA, which in this engine is not a screen mode: it is a zoom.
 *
 * A scene's life script can raise LM_ZOOM (GERELIFE.C), and what that asks
 * for is the same 640x480 world, composed the same way, with a 320x200 window
 * of it on the screen -- so the world looks twice as big. The engine keeps
 * drawing at 640x480 throughout: CopyBlockPhysMCGA clamps its window to
 * 640-320 and 480-200, CopyBlockMCGA reads it with a stride of 640, and
 * AffScene centres the window on the followed actor's projected position.
 *
 * This used to set Screen_X/Screen_Y to 320x200 and rebuild TabOffLine, which
 * put the composition and the window on different surfaces: the scene was
 * drawn into the top-left 320x200 of Log and read back out with a 640 stride.
 * Cube 59 zooms on entry, had only ever been run under the M3 harness (which
 * runs no life scripts), and faulted the first time the game loop reached it.
 *
 * So the surface does not change. Only what is presented does: Phys, the
 * 320x200 crop, in the middle of the field. docs/M7-NOTES.md.
 */
void InitMcgaMode(void)
{
    EnsureVideo();
    if (!Phys)
        Phys = PSX_calloc(1, 64000);
    memset(Phys, 0, 64000);
    mcga_on = 1;
    PORT_Diag("[VID] zoom on: 320x200 of the 640x480 surface, Phys=%p\n",
              (void *)Phys);
}

void SimpleInitSvga(void)
{
    EnsureVideo();
    if (Phys)
        memset(Phys, 0, 64000);
    mcga_on = 0;
    mode_x = SCR_W;
    mode_y = SCR_H;
    Screen_X = SCR_W;
    Screen_Y = SCR_H;
    BuildTabOffLine();
}

/*
 * One 640x480 buffer, not two.
 *
 * The DOS engine keeps `Log` (the software render target) and `Screen` (the
 * clean composed background) as separate images and restores dirty rectangles
 * from the second into the first before each frame. That is 600 KB of a
 * 1575 KB heap, and the second copy exists only because the CPU draws the
 * actors into the first.
 *
 * On this machine the CPU does not draw the actors; the GPU does, over a
 * background it textures out of main RAM. So `Log` never accumulates anything
 * that has to be erased, and the restore it exists to serve has nothing left
 * to restore. PERSO.C therefore points `Screen` at this same allocation, and
 * the port keeps ONE background image.
 *
 * Every CopyScreen(Log, Screen) and CopyBlock(..., Screen, ..., Log) in the
 * engine then becomes a copy onto itself: not a coincidence, but the right
 * answer with the work taken out of it. The background is already where it
 * belongs.
 *
 * The +500 is the decompression margin the engine relies on: Load_HQR expands
 * in place and overruns by up to 500 bytes. PERSO.C asked for that slack on
 * Screen, so Log has to carry it now instead.
 */

#ifdef PORT_PSX_BG_SELFTEST
static void VramRead(const RECT *r, uint32_t *dst);

/*
 * Does a rectangle written into the background come back?
 *
 * The write half works -- a full 640x480 upload lands and the frame after it
 * presents normally. The read half is VramRead below, which had to be
 * written by hand. This runs the round trip at boot with nothing else
 * happening: no present queued, no scene load, no palette upload since the
 * one InitSvga did. Whatever it says separates "VRAM reads do not work here"
 * from "VRAM reads do not work while the frame is doing something else".
 */
static void BgSelfTest(void)
{
    static unsigned char probe[256] __attribute__((aligned(4)));
    RECT r;
    int i, bad = 0;

    for (i = 0; i < 256; i++)
        probe[i] = (unsigned char)i;

    r.x = BG_X;
    r.y = BG_Y;
    r.w = 32;                       /* 64 pixels ... */
    r.h = 4;                        /* ... by 4 lines is 256 bytes */

    PORT_Diag("[BG] selftest: writing %d,%d %dx%d from %p\n",
              r.x, r.y, r.w, r.h, (void *)probe);
    DrawSync(0);
    LoadImage(&r, (const uint32_t *)probe);
    DrawSync(0);
    PORT_Diag("[BG] selftest: written\n");

    for (i = 0; i < 256; i++)
        probe[i] = 0xAA;

    PORT_Diag("[BG] selftest: reading back\n");
    VramRead(&r, (uint32_t *)probe);
    PORT_Diag("[BG] selftest: read returned\n");

    for (i = 0; i < 256; i++)
        if (probe[i] != (unsigned char)i)
            bad++;

    PORT_Diag("[BG] selftest: %d of 256 bytes wrong (first four %02x %02x "
              "%02x %02x)\n", bad, probe[0], probe[1], probe[2], probe[3]);
}
#endif

void InitGraphSvga(void)
{
    InitSvga();
    Log = Malloc(640L * 480L + 500L);
    MemoLog = Log;
    PORT_Diag("[VID] SVGA 640x480i, Log=%p (300 KB); clean background in "
              "VRAM at %d,%d\n", (void *)Log, BG_X, BG_Y);
#ifdef PORT_PSX_BG_SELFTEST
    BgSelfTest();
#endif
}

void ClearGraphSvga(void)
{
    Free(MemoLog);
    Log = 0;
    MemoLog = 0;
}

void InitGraphMcga(void)
{
    InitMcgaMode();
    Log = Malloc(320L * 200L);
    MemoLog = Log;
    PORT_Diag("[VID] MCGA 320x200, Log=%p\n", (void *)Log);
}

void ClearGraphMcga(void)
{
    Free(MemoLog);
    Log = 0;
    MemoLog = 0;
}

LONG SvgaInitDLL(char *driverpathname)
{
    (void)driverpathname;
    return 1;       /* no SVGA driver DLLs here; report success */
}

/* ── clip (INITSVGA.ASM) ─────────────────────────────────────────────────── */
void SetClip(LONG x0, LONG y0, LONG x1, LONG y1)
{
    ClipXmin = (WORD)((x0 < 0) ? 0 : x0);
    ClipYmin = (WORD)((y0 < 0) ? 0 : y0);
    ClipXmax = (WORD)((x1 >= Screen_X) ? Screen_X - 1 : x1);
    ClipYmax = (WORD)((y1 >= Screen_Y) ? Screen_Y - 1 : y1);
}

void UnSetClip(void)
{
    ClipXmin = 0;
    ClipYmin = 0;
    ClipXmax = Screen_X - 1;
    ClipYmax = Screen_Y - 1;
}

void MemoClip(void)
{
    MemoClipXmin = ClipXmin;
    MemoClipYmin = ClipYmin;
    MemoClipXmax = ClipXmax;
    MemoClipYmax = ClipYmax;
}

void RestoreClip(void)
{
    ClipXmin = MemoClipXmin;
    ClipYmin = MemoClipYmin;
    ClipXmax = MemoClipXmax;
    ClipYmax = MemoClipYmax;
}

/* ── palette (S_PAL.ASM) ─────────────────────────────────────────────────── *
 * The DAC keeps 6 bits per gun, and the engine's fades depend on that
 * quantisation: reproducing it is what makes a fade look the same as it did in
 * 1994 rather than merely similar. From 6 bits the GPU's 5 are one more shift.
 */
static UBYTE Dac6(UBYTE v)
{
    UBYTE v6 = v >> 2;

    return (UBYTE)((v6 << 2) | (v6 >> 4));
}

static void ApplyPalRange(int start, int count)
{
    const UBYTE *p = PORT_PalRGB + start * 3;
    int i;

    for (i = start; i < start + count; i++, p += 3)
        clut[i] = (unsigned short)((p[0] >> 3)
                                | ((p[1] >> 3) << 5)
                                | ((p[2] >> 3) << 10));

    if (!video_up)
        return;

    UploadClut();

    /*
     * And put the picture back, because on this machine a palette is not a
     * palette.
     *
     * On DOS the VGA DAC held the palette and applied it on the way out to
     * the monitor, so a fade was 256 register writes and every pixel already
     * on screen changed colour. Here the CLUT is applied by the GPU when a
     * rectangle is blitted, and what lands in the framebuffer is RGB. A fade
     * changes the tint of everything drawn *after* it and nothing drawn
     * before -- so the game composes a scene, fades to black, presents it
     * (black), fades up, and the screen stays black until something happens
     * to redraw over it. Which is exactly what it looked like: Twinsen
     * visible because he is a GPU primitive redrawn every frame, the dialogue
     * box visible because it is presented when it opens, and the whole
     * background missing.
     *
     * So a palette change has to re-present. Single-colour writes do not --
     * PalOne is used for HUD accents with their own CopyBlockPhys behind
     * them, and a full 640x480 re-blit per accent would be 16 ms each.
     *
     * This was in the port from M1 and nothing had caught it, because every
     * milestone up to M5 looked at a scene that was composed and presented
     * once, with the fade before it rather than after.
     */
    if (count > 1)
        PORT_PresentAll();
}

void Palette(void *pal)
{
    const UBYTE *src = pal;
    int i;

    for (i = 0; i < 768; i++)
        PORT_PalRGB[i] = Dac6(src[i]);
    ApplyPalRange(0, 256);
}

void PalMulti(WORD startcoul, WORD nbcoul, UBYTE *pal)
{
    UBYTE *dst = PORT_PalRGB + startcoul * 3;
    int i;

    for (i = 0; i < nbcoul * 3; i++)
        dst[i] = Dac6(pal[i]);
    ApplyPalRange(startcoul, nbcoul);
}

void PalOne(UBYTE coul, UBYTE r, UBYTE v, UBYTE b)
{
    UBYTE *dst = PORT_PalRGB + coul * 3;

    dst[0] = Dac6(r);
    dst[1] = Dac6(v);
    dst[2] = Dac6(b);
    ApplyPalRange(coul, 1);
}

/* ── the clean background ────────────────────────────────────────────────── *
 *
 * `Screen` in the engine is the pristine composed background: AffScene saves
 * it once per full recompose and every erase in the game restores a rectangle
 * of it into `Log`. M3 dropped it -- 300 KB of a 1575 KB heap -- and aliased
 * Screen onto Log, on the reasoning that the actors are GPU primitives and
 * never dirty Log. That was true of the actors and of nothing else: the
 * shadow, the HUD, the text and every modal are software sprites, and with
 * Screen == Log the restore was a copy onto itself. Three visible artefacts,
 * one cause. docs/M6-NOTES.md §5.
 *
 * M7 costed the buffer honestly. Reclaiming the HQM pool gives the heap 135 KB
 * back and merging BufferBrick into it would give maybe 80 more; the buffer
 * needs 300. It is not there and no rearrangement of main RAM puts it there.
 *
 * VRAM has it. The framebuffer is 640x480 16bpp at (0,0), which leaves the
 * right-hand 384 halfwords of a 1024x512 VRAM unused, and a 640x480 8bpp
 * image is 320 halfwords by 480 lines -- it fits at x 640 with the upload
 * tile moved underneath it. So the background lives in VRAM and the two
 * operations the engine does to it become DMA:
 *
 *     CopyScreen(Log, Screen)      -> PORT_BgStore, RAM to VRAM
 *     CopyBlock(.., Screen, .., Log) -> PORT_BgFetch, VRAM to RAM
 *
 * Full screen is one transfer either way: 640 bytes per row is exactly the
 * 320-halfword width of the region, so Log is already in the layout VRAM
 * wants. A rectangle is tiled through the same 256x32 staging buffer the
 * present uses, on 64-pixel column boundaries -- the DMA block rule in
 * PresentTile applies in both directions.
 */

/*
 * Read a rectangle of VRAM back into main RAM, by hand.
 *
 * PSn00bSDK 0.24's StoreImage and StoreImage2 both hang here, and the
 * disassembly says why: they route through the same `_dma_transfer` as the
 * upload, and it waits on GPUSTAT bit 28, "ready to receive DMA block",
 * before starting. Bit 28 is the wrong bit for a transfer going the other
 * way -- the one that says the GPU has pixels waiting is bit 27, "ready to
 * send VRAM to CPU" -- so on a read the wait never ends. Same shape as the
 * 16-word block rule in PresentTile: the write path is exercised by
 * everything and the read path by nothing.
 *
 * So the read is done directly: DMA off, GP0(C0h) with the rectangle, wait
 * for bit 27, pull words out of GPUREAD. A dirty box is a few hundred words
 * and this is not the expensive part of a frame.
 */
static void VramRead(const RECT *r, uint32_t *dst)
{
    int words = ((int)r->w * (int)r->h + 1) / 2;
    int i;

    DrawSync(0);

    GPU_GP1 = 0x04000000;                       /* DMA direction: off        */
    GPU_GP0 = 0xc0000000;                       /* copy rectangle, VRAM->CPU */
    GPU_GP0 = ((uint32_t)(uint16_t)r->y << 16) | (uint32_t)(uint16_t)r->x;
    GPU_GP0 = ((uint32_t)(uint16_t)r->h << 16) | (uint32_t)(uint16_t)r->w;

    while (!(GPU_GP1 & (1 << 27)))
        ;

    for (i = 0; i < words; i++)
        dst[i] = GPU_GP0;
}

static int bg_ready;

void PORT_BgStoreAll(void)
{
    RECT r;

    if (!Log || !video_up)
        return;

    r.x = BG_X;
    r.y = BG_Y;
    r.w = BG_HW;
    r.h = SCR_H;
    DrawSync(0);
    LoadImage(&r, (const uint32_t *)Log);
    DrawSync(0);
    bg_ready = 1;
}

void PORT_BgFetchAll(void)
{
    RECT r;

    if (!Log || !video_up || !bg_ready)
        return;

    r.x = BG_X;
    r.y = BG_Y;
    r.w = BG_HW;
    r.h = SCR_H;
    VramRead(&r, (uint32_t *)Log);
}

/* Clamp a rectangle to the surface and widen it to whole 64-pixel columns.
 * 640 is ten of those, so the widened span never leaves the image. */
static int BgClip(LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > SCR_W - 1) *x1 = SCR_W - 1;
    if (*y1 > SCR_H - 1) *y1 = SCR_H - 1;
    if (*x1 < *x0 || *y1 < *y0)
        return 0;

    *x0 &= ~63L;
    *x1 |= 63L;
    return 1;
}

/*
 * A rectangle of Log into the background.
 *
 * The widening to 64-pixel columns copies more of Log than was asked for, and
 * that is safe for exactly one reason: everywhere this is called from, Log
 * outside the rectangle already holds the background. AffScene calls it on a
 * freshly composed frame; OBJECT.C calls it to bake an OBJ_BACKGROUND actor
 * in; MESSAGE.C calls it on the dialogue band. Nothing calls it with a dirty
 * Log around the edges of its rectangle.
 */
void PORT_BgStore(LONG x0, LONG y0, LONG x1, LONG y1)
{
    RECT r;
    int ty, tx, row;

    if (!Log || !video_up)
        return;
    if (!BgClip(&x0, &y0, &x1, &y1))
        return;

    if (x0 == 0 && x1 == SCR_W - 1 && y0 == 0 && y1 == SCR_H - 1) {
        PORT_BgStoreAll();
        return;
    }

    DrawSync(0);

    for (ty = (int)y0; ty <= (int)y1; ty += TILE_H) {
        int h = (int)y1 - ty + 1;

        if (h > TILE_H)
            h = TILE_H;

        for (tx = (int)x0; tx <= (int)x1; tx += TILE_W) {
            int w = (int)x1 - tx + 1;

            if (w > TILE_W)
                w = TILE_W;

            for (row = 0; row < h; row++)
                memcpy(stage + row * w,
                       Log + (ULONG)(ty + row) * SCR_W + tx, (size_t)w);

            r.x = BG_X + tx / 2;
            r.y = BG_Y + ty;
            r.w = (short)(w / 2);
            r.h = (short)h;
            LoadImage(&r, (const uint32_t *)stage);
            DrawSync(0);
        }
    }

    bg_ready = 1;
}

/*
 * A rectangle of the background into Log.
 *
 * Read wide, write narrow: the tile read is on 64-pixel columns because the
 * DMA says so, but only the columns the caller asked for are copied into Log.
 * Restoring more than was asked would erase whatever a modal had drawn just
 * outside its own rectangle, and GAMEMENU.C does exactly that between widgets.
 */
void PORT_BgFetch(LONG x0, LONG y0, LONG x1, LONG y1)
{
    RECT r;
    LONG rx0 = x0, ry0 = y0, rx1 = x1, ry1 = y1;
    int ty, tx, row;

    if (!Log || !video_up || !bg_ready)
        return;
    if (!BgClip(&rx0, &ry0, &rx1, &ry1))
        return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    if (y1 > SCR_H - 1) y1 = SCR_H - 1;

    if (rx0 == 0 && rx1 == SCR_W - 1 && ry0 == 0 && ry1 == SCR_H - 1
        && x0 == 0 && x1 == SCR_W - 1) {
        PORT_BgFetchAll();
        return;
    }

    DrawSync(0);

    for (ty = (int)ry0; ty <= (int)ry1; ty += TILE_H) {
        int h = (int)ry1 - ty + 1;

        if (h > TILE_H)
            h = TILE_H;

        for (tx = (int)rx0; tx <= (int)rx1; tx += TILE_W) {
            int w = (int)rx1 - tx + 1;
            int cx0, cx1, cw;

            if (w > TILE_W)
                w = TILE_W;

            r.x = BG_X + tx / 2;
            r.y = BG_Y + ty;
            r.w = (short)(w / 2);
            r.h = (short)h;
            VramRead(&r, (uint32_t *)stage);

            /* the part of this tile the caller actually asked for */
            cx0 = (int)x0 > tx ? (int)x0 : tx;
            cx1 = (int)x1 < tx + w - 1 ? (int)x1 : tx + w - 1;
            cw  = cx1 - cx0 + 1;
            if (cw <= 0)
                continue;

            for (row = 0; row < h; row++) {
                int line = ty + row;

                if (line < (int)y0 || line > (int)y1)
                    continue;
                memcpy(Log + (ULONG)line * SCR_W + cx0,
                       stage + row * w + (cx0 - tx), (size_t)cw);
            }
        }
    }
}

/* ── Log -> VRAM (S_PHYS.ASM) ────────────────────────────────────────────── */
static void PresentTile(const UBYTE *src, int stride,
                        int dx, int dy, int w, int h)
{
    RECT r;
    POLY_FT4 q;
    DR_TPAGE tp;
    int row, pitch;

    /* Two pixels per VRAM halfword, so the staged width has to be even. */
    if (w & 1)
        w++;

    /* And the staged ROW has to be a multiple of 64 pixels.
     *
     * A VRAM upload goes out on DMA channel 2 in blocks of 16 words, and a
     * transfer that is not a whole number of blocks leaves the GPU waiting
     * for data that never arrives -- DrawSync then never returns. Every
     * present until M5 was full-screen, which tiles into 256x32 and 128x32
     * pieces and is always a whole number of blocks by accident; the first
     * dirty rectangle the frame loop ever presented was 30 pixels wide, and
     * the machine stopped dead inside it.
     *
     * 64 pixels is 32 halfwords is 16 words, so a row padded to a multiple of
     * 64 makes ANY height a whole number of blocks. The padding is staged but
     * never sampled -- the quad's UVs still stop at w - 1. */
    pitch = (w + 63) & ~63;

    for (row = 0; row < h; row++)
        memcpy(stage + row * pitch, src + row * stride, (size_t)w);

    r.x = STAGE_X;
    r.y = STAGE_Y;
    r.w = (short)(pitch / 2);
    r.h = (short)h;
    LoadImage(&r, (const uint32_t *)stage);
    DrawSync(0);

    setDrawTPage(&tp, 0, 1, getTPage(1, 0, STAGE_X, STAGE_Y));  /* 1 = 8bpp */
    DrawPrim((const uint32_t *)&tp);

    setPolyFT4(&q);
    setRGB0(&q, 128, 128, 128);         /* 128 passes the texture through */
    setXY4(&q, dx, dy, dx + w, dy, dx, dy + h, dx + w, dy + h);
    setUV4(&q, 0, 0, w - 1, 0, 0, h - 1, w - 1, h - 1);
    q.clut = getClut(CLUT_X, CLUT_Y);
    q.tpage = getTPage(1, 0, STAGE_X, STAGE_Y);
    DrawPrim((const uint32_t *)&q);
    DrawSync(0);
}

/* What a frame actually sends to VRAM. The engine has tracked its own dirty
 * boxes since 1994 (FLIPBOX.C) and CopyBlockPhys is PresentRect, so the
 * discipline is already wired; what nobody had was the bill. M5 counts it. */
static int present_rects;
static unsigned long present_pixels;
static unsigned long present_us;

void PORT_PresentStats(int *rects, unsigned long *pixels, unsigned long *us)
{
    if (rects)  *rects  = present_rects;
    if (pixels) *pixels = present_pixels;
    if (us)     *us     = present_us;

    present_rects  = 0;
    present_pixels = 0;
    present_us     = 0;
}

static void PresentRect(LONG x0, LONG y0, LONG x1, LONG y1)
{
    int ox = 0, oy = 0;
    int ty, tx;
    unsigned long t0;

    if (!Log || !video_up)
        return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= mode_x) x1 = mode_x - 1;
    if (y1 >= mode_y) y1 = mode_y - 1;
    if (x1 < x0 || y1 < y0)
        return;

    x0 &= ~1L;      /* the staged source must start on a halfword */

    /* MCGA is presented 1:1 in the middle of the 640x480 field. The DOS
     * behaviour was a scaled full-screen mode; matching it is M8, with the FLA
     * player that is the only thing which really uses it. */
    if (mode_x == 320) {
        ox = (SCR_W - 320) / 2;
        oy = (SCR_H - 200) / 2;
    }

    t0 = PORT_Micros();
    present_rects++;
    present_pixels += (unsigned long)(x1 - x0 + 1) * (unsigned long)(y1 - y0 + 1);

    for (ty = (int)y0; ty <= (int)y1; ty += TILE_H) {
        int h = (int)y1 - ty + 1;

        if (h > TILE_H)
            h = TILE_H;

        for (tx = (int)x0; tx <= (int)x1; tx += TILE_W) {
            int w = (int)x1 - tx + 1;

            if (w > TILE_W)
                w = TILE_W;

            PresentTile(Log + (ULONG)ty * (ULONG)mode_x + tx, mode_x,
                        ox + tx, oy + ty, w, h);
        }
    }

    present_us += PORT_Micros() - t0;
}

void PORT_PresentRect(LONG x0, LONG y0, LONG x1, LONG y1)
{
    PresentRect(x0, y0, x1, y1);
}

void PORT_PresentAll(void)
{
    PresentRect(0, 0, mode_x - 1, mode_y - 1);
}

void Vsync(void)
{
    if (video_up)
        VSync(0);
}

/* The 320x200 crop CopyBlockPhysMCGA just built, 1:1 in the middle of the
 * field. Scaling it to fill the screen -- which is what a real MCGA mode did
 * -- is M8, with the FLA player that is the only other thing wanting it. */
void PORT_PresentMcga(void)
{
    int ty, tx;

    if (!Phys || !video_up)
        return;

    for (ty = 0; ty < 200; ty += TILE_H) {
        int h = 200 - ty;

        if (h > TILE_H)
            h = TILE_H;

        for (tx = 0; tx < 320; tx += TILE_W) {
            int w = 320 - tx;

            if (w > TILE_W)
                w = TILE_W;

            PresentTile(Phys + ty * 320 + tx, 320,
                        (SCR_W - 320) / 2 + tx, (SCR_H - 200) / 2 + ty, w, h);
        }
    }
}

void Flip(void)
{
    if (mcga_on && Phys) {
        PORT_PresentMcga();
        return;
    }

    PORT_PresentAll();
}

void CopyBlockPhys(LONG x0, LONG y0, LONG x1, LONG y1)
{
    PresentRect(x0, y0, x1, y1);
}

void CopyBlockPhysClip(LONG x0, LONG y0, LONG x1, LONG y1)
{
    if (x0 < ClipXmin) x0 = ClipXmin;
    if (y0 < ClipYmin) y0 = ClipYmin;
    if (x1 > ClipXmax) x1 = ClipXmax;
    if (y1 > ClipYmax) y1 = ClipYmax;
    PresentRect(x0, y0, x1, y1);
}
