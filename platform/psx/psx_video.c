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

#include "port.h"

/* ── VRAM map (see docs/M0-NOTES.md) ─────────────────────────────────────── */
#define SCR_W       640
#define SCR_H       480

#define STAGE_X     640         /* one 8bpp texture page: 256x256 texels     */
#define STAGE_Y     0
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

static unsigned short clut[256];
static unsigned char  stage[TILE_W * TILE_H];

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

static void InitSvga(void)
{
    EnsureVideo();
    if (!Phys)
        Phys = PSX_calloc(1, 64000);
    mode_x = SCR_W;
    mode_y = SCR_H;
    Screen_X = SCR_W;
    Screen_Y = SCR_H;
    BuildTabOffLine();
    UnSetClip();
}

void InitMcgaMode(void)
{
    EnsureVideo();
    if (!Phys)
        Phys = PSX_calloc(1, 64000);
    memset(Phys, 0, 64000);
    mode_x = 320;
    mode_y = 200;
    Screen_X = 320;
    Screen_Y = 200;
    BuildTabOffLine();
    UnSetClip();
}

void SimpleInitSvga(void)
{
    EnsureVideo();
    if (Phys)
        memset(Phys, 0, 64000);
    mode_x = SCR_W;
    mode_y = SCR_H;
    Screen_X = SCR_W;
    Screen_Y = SCR_H;
    BuildTabOffLine();
}

void InitGraphSvga(void)
{
    InitSvga();
    Log = Malloc(640L * 480L);
    MemoLog = Log;
    PORT_Diag("[VID] SVGA 640x480i, Log=%p (300 KB)\n", (void *)Log);
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

    if (video_up)
        UploadClut();
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

/* ── Log -> VRAM (S_PHYS.ASM) ────────────────────────────────────────────── */
static void PresentTile(const UBYTE *src, int stride,
                        int dx, int dy, int w, int h)
{
    RECT r;
    POLY_FT4 q;
    DR_TPAGE tp;
    int row;

    /* Two pixels per VRAM halfword, so the staged width has to be even. */
    if (w & 1)
        w++;

    for (row = 0; row < h; row++)
        memcpy(stage + row * w, src + row * stride, (size_t)w);

    r.x = STAGE_X;
    r.y = STAGE_Y;
    r.w = (short)(w / 2);
    r.h = (short)h;
    LoadImage(&r, (const uint32_t *)stage);
    DrawSync(0);

    setDrawTPage(&tp, 0, 1, getTPage(1, 0, STAGE_X, STAGE_Y));  /* 1 = 8bpp */
    DrawPrim(&tp);

    setPolyFT4(&q);
    setRGB0(&q, 128, 128, 128);         /* 128 passes the texture through */
    setXY4(&q, dx, dy, dx + w, dy, dx, dy + h, dx + w, dy + h);
    setUV4(&q, 0, 0, w - 1, 0, 0, h - 1, w - 1, h - 1);
    q.clut = getClut(CLUT_X, CLUT_Y);
    q.tpage = getTPage(1, 0, STAGE_X, STAGE_Y);
    DrawPrim(&q);
    DrawSync(0);
}

static void PresentRect(LONG x0, LONG y0, LONG x1, LONG y1)
{
    int ox = 0, oy = 0;
    int ty, tx;

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

void Flip(void)
{
    if (mode_x == 320 && Log && Phys)
        memcpy(Phys, Log, 64000);

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
