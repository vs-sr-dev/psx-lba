/*
 * psx-lba — M0 probe: 640x480 interlaced, and the two GPU paths the port
 * depends on.
 *
 * What this is actually for. The feasibility study concluded that on a 2 MB
 * machine the RAM budget and the rendering architecture are the same decision:
 * the engine's `Log` (640x480 8bpp render target, 300 KB) and `Screen` (the
 * clean background it restores dirty rectangles from, another 300 KB) do not
 * both fit. The way out is to let the GPU do what the 1994 software renderer
 * did:
 *
 *   1. BACKGROUND RESTORE. The composed background is 8bpp paletted. The GPU
 *      can texture from a CLUT image, so a dirty rectangle can be restored by
 *      DMAing 8bpp pixels into a staging texture page and drawing one textured
 *      quad — palette expansion for free, no CPU pixel loop.
 *   2. ACTORS. Flat and gouraud triangles/quads, which is 98% of the polygons
 *      in BODY.HQR (see tools/poly_census.py).
 *
 * This probe measures both, and prints the VRAM map that comes out of the
 * 640x480 constraint. It draws nothing clever: the point is the numbers on the
 * TTY, which PCSX-Redux and no$psx show.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <psxgpu.h>
#include <psxgte.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxpad.h>
#include <hwregs_c.h>

/* ── the screen the DOS CD game actually runs at ─────────────────────────── */
#define SCR_W 640
#define SCR_H 480

/*
 * VRAM is 1024x512 halfwords = 1 MB, and a 640x480 16bpp framebuffer eats
 * 600 KB of it. Two facts decide the rest of the layout:
 *
 *   - texture pages are 256x256 *texels*; at 8bpp that is 128 halfwords wide,
 *     and the page origin snaps to a 64-halfword x 256-halfword grid.
 *   - a full 640x480 8bpp copy of the background would therefore need THREE
 *     pages across and two down: 384x512 halfwords, i.e. every column left of
 *     the framebuffer, leaving only the 640x32 strip under it for CLUTs.
 *
 * So the background does NOT live in VRAM. It stays in main RAM (where the
 * engine's `Screen` buffer already is) and dirty rectangles are DMAed into a
 * single staging page on their way to the framebuffer. One page costs 64 KB
 * instead of 393 KB and leaves real room for masks, sprites and fonts.
 */
#define FB_X      0
#define FB_Y      0
#define STAGE_X   640           /* one 8bpp texture page: 256x256 texels     */
#define STAGE_Y   0
#define STAGE_W   128           /* halfwords                                 */
#define STAGE_H   256
#define SPARE_X   768           /* 256x512 halfwords = 256 KB for the rest   */
#define SPARE_Y   0
#define CLUT_X    0             /* under the framebuffer: 640x32 halfwords   */
#define CLUT_Y    480

#define OT_LEN    8
#define PRIM_BYTES (32 * 1024)

static uint32_t ot[OT_LEN];
static uint8_t  prim_buf[PRIM_BYTES];
static uint8_t *next_prim;

static DISPENV disp;
static DRAWENV draw;

/* ── timing ──────────────────────────────────────────────────────────────────
 * Root counter 2 runs at the system clock / 8 = 4.233600 MHz on both NTSC and
 * PAL machines, so one tick is 236.2 ns and the 16-bit counter wraps after
 * 15.5 ms. Everything measured here is shorter than that; anything longer is
 * measured by counting whole frames instead.
 */
#define RCNT2_HZ 4233600

static inline void timer_start(void)
{
    TIMER_CTRL(2) = 0x0258;     /* sysclk/8, free running, no IRQ            */
    TIMER_VALUE(2) = 0;
}

static inline uint32_t timer_ticks(void)
{
    return TIMER_VALUE(2) & 0xFFFF;
}

static inline uint32_t ticks_to_us(uint32_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000000ULL) / RCNT2_HZ);
}

/* ── primitive allocation ────────────────────────────────────────────────── */
static void prim_reset(void)
{
    next_prim = prim_buf;
    ClearOTagR(ot, OT_LEN);
}

static void *prim_alloc(size_t n)
{
    void *p = next_prim;

    if (next_prim + n > prim_buf + PRIM_BYTES)
        return NULL;

    next_prim += n;
    return p;
}

/* ── test data ───────────────────────────────────────────────────────────────
 * A 256x256 8bpp tile and a 256-entry palette, standing in for a piece of a
 * composed LBA background. The content is irrelevant; the memory shape is not,
 * and it is exactly what the real path will push: one byte per pixel, packed
 * two pixels per VRAM halfword.
 */
static uint8_t  tile[256 * 256];
static uint16_t palette[256];

static void make_test_data(void)
{
    int x, y, i;

    for (y = 0; y < 256; y++)
        for (x = 0; x < 256; x++)
            tile[y * 256 + x] = (uint8_t)(((x >> 3) ^ (y >> 3)) & 0xFF);

    /* A 6-bit-DAC-shaped ramp, the way the engine's palettes come out. */
    for (i = 0; i < 256; i++) {
        int r = (i & 0x1F) << 1;
        int g = ((i >> 3) & 0x1F);
        int b = 31 - ((i >> 3) & 0x1F);
        palette[i] = (uint16_t)((r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10));
    }
}

/* ── uploads ─────────────────────────────────────────────────────────────── */
static uint32_t upload_clut(void)
{
    RECT r = { CLUT_X, CLUT_Y, 256, 1 };
    uint32_t t0, t1;

    t0 = timer_ticks();
    LoadImage(&r, (const uint32_t *)palette);
    DrawSync(0);
    t1 = timer_ticks();

    return t1 - t0;
}

/* Push `h` rows of a `w`-pixel-wide 8bpp rectangle into the staging page. */
static uint32_t upload_tile(int w, int h)
{
    RECT r = { STAGE_X, STAGE_Y, w / 2, h };   /* w texels = w/2 halfwords   */
    uint32_t t0, t1;

    t0 = timer_ticks();
    LoadImage(&r, (const uint32_t *)tile);
    DrawSync(0);
    t1 = timer_ticks();

    return t1 - t0;
}

/* ── the background-restore primitive ────────────────────────────────────── */
static void draw_clut_quad(int dx, int dy, int w, int h)
{
    DR_TPAGE *tp = prim_alloc(sizeof(DR_TPAGE));
    POLY_FT4 *q  = prim_alloc(sizeof(POLY_FT4));

    if (!tp || !q)
        return;

    setPolyFT4(q);
    setRGB0(q, 128, 128, 128);          /* 128 = texture passed through      */
    setXY4(q, dx, dy, dx + w, dy, dx, dy + h, dx + w, dy + h);
    setUV4(q, 0, 0, w - 1, 0, 0, h - 1, w - 1, h - 1);
    q->clut = getClut(CLUT_X, CLUT_Y);
    q->tpage = getTPage(1, 0, STAGE_X, STAGE_Y);    /* 1 = 8bpp CLUT         */
    addPrim(ot, q);

    setDrawTPage(tp, 0, 1, getTPage(1, 0, STAGE_X, STAGE_Y));
    addPrim(ot, tp);
}

/* ── the actor primitive ─────────────────────────────────────────────────── */
static void draw_gouraud_tri(int x, int y, int size, int seed)
{
    POLY_G3 *p = prim_alloc(sizeof(POLY_G3));

    if (!p)
        return;

    setPolyG3(p);
    setXY3(p, x, y, x + size, y + (size >> 1), x + (size >> 1), y + size);
    setRGB0(p, (uint8_t)(seed * 37), (uint8_t)(seed * 61), 200);
    setRGB1(p, 220, (uint8_t)(seed * 17), (uint8_t)(seed * 53));
    setRGB2(p, (uint8_t)(seed * 91), 160, (uint8_t)(seed * 29));
    addPrim(ot, p);
}

/* ── measurements ────────────────────────────────────────────────────────── */
static void bench_restore(void)
{
    static const int sizes[] = { 32, 64, 128, 256 };
    int i;

    printf("\n-- background restore: 8bpp DMA + one CLUT quad\n");
    printf("   rect      bytes    upload us   blit us   total us\n");

    for (i = 0; i < 4; i++) {
        int s = sizes[i];
        uint32_t up, t0, t1;

        timer_start();
        up = upload_tile(s, s);

        prim_reset();
        draw_clut_quad(0, 0, s, s);
        t0 = timer_ticks();
        DrawOTag(ot + OT_LEN - 1);
        DrawSync(0);
        t1 = timer_ticks();

        printf("   %3dx%-3d  %7d   %9lu %9lu %10lu\n",
               s, s, s * s,
               (unsigned long)ticks_to_us(up),
               (unsigned long)ticks_to_us(t1 - t0),
               (unsigned long)ticks_to_us(up + (t1 - t0)));
    }
}

static void bench_gouraud(void)
{
    static const int counts[] = { 50, 200, 500, 1000 };
    int i;

    /*
     * BODY.HQR holds 19826 polygons across 132 bodies: 79.7% triangles, 19.2%
     * quads, and a mean of 150 polygons per body. A few hundred triangles is
     * therefore the whole on-screen actor budget of a busy LBA scene, not a
     * synthetic stress figure.
     */
    printf("\n-- actors: gouraud triangles, ~40 px on a side\n");
    printf("   count   us    us/tri\n");

    for (i = 0; i < 4; i++) {
        int n = counts[i], j;
        uint32_t t0, t1, us;

        prim_reset();
        for (j = 0; j < n; j++)
            draw_gouraud_tri(20 + ((j * 13) % 560), 20 + ((j * 29) % 400), 40, j);

        timer_start();
        t0 = timer_ticks();
        DrawOTag(ot + OT_LEN - 1);
        DrawSync(0);
        t1 = timer_ticks();

        us = ticks_to_us(t1 - t0);
        printf("   %5d %6lu  %6lu\n", n, (unsigned long)us,
               (unsigned long)(n ? us / n : 0));
    }
}

static void bench_fullscreen_clear(void)
{
    uint32_t t0, t1;
    FILL *f;

    prim_reset();
    f = prim_alloc(sizeof(FILL));
    setFill(f);
    setRGB0(f, 20, 20, 40);
    setXY0(f, FB_X, FB_Y);
    setWH(f, SCR_W, SCR_H);
    addPrim(ot, f);

    timer_start();
    t0 = timer_ticks();
    DrawOTag(ot + OT_LEN - 1);
    DrawSync(0);
    t1 = timer_ticks();

    printf("\n-- full 640x480 clear: %lu us\n",
           (unsigned long)ticks_to_us(t1 - t0));
}

/* ── main ────────────────────────────────────────────────────────────────── */
static void video_init(void)
{
    ResetGraph(0);

    SetDefDispEnv(&disp, FB_X, FB_Y, SCR_W, SCR_H);
    SetDefDrawEnv(&draw, FB_X, FB_Y, SCR_W, SCR_H);

    /* 480 lines means interlaced: there is no room for a second buffer at
     * this size, so draw and display are the same rectangle. The DOS build
     * blitted dirty rectangles straight to the VGA too — the tearing is
     * parity, not a regression. */
    disp.isinter = 1;
    disp.isrgb24 = 0;
    draw.isbg = 0;

    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);
}

static void vram_map(void)
{
    printf("psx-lba M0 probe — %dx%d interlaced, %s\n",
           SCR_W, SCR_H, GetVideoMode() == MODE_PAL ? "PAL" : "NTSC");
    printf("\n-- VRAM map (1024x512 halfwords = 1024 KB)\n");
    printf("   framebuffer  (%4d,%3d)  %4dx%3d  %4d KB\n",
           FB_X, FB_Y, SCR_W, SCR_H, (SCR_W * SCR_H * 2) / 1024);
    printf("   stage 8bpp   (%4d,%3d)  %4dx%3d  %4d KB   one texture page\n",
           STAGE_X, STAGE_Y, STAGE_W, STAGE_H, (STAGE_W * STAGE_H * 2) / 1024);
    printf("   spare        (%4d,%3d)  %4dx%3d  %4d KB   masks/sprites/font\n",
           SPARE_X, SPARE_Y, 256, 512, (256 * 512 * 2) / 1024);
    printf("   cluts        (%4d,%3d)  %4dx%3d  %4d KB\n",
           CLUT_X, CLUT_Y, 640, 32, (640 * 32 * 2) / 1024);
}

int main(void)
{
    int frame = 0;

    video_init();
    make_test_data();
    timer_start();

    vram_map();

    printf("\n-- CLUT upload (256 entries): %lu us\n",
           (unsigned long)ticks_to_us(upload_clut()));

    bench_fullscreen_clear();
    bench_restore();
    bench_gouraud();

    printf("\n-- probe done, showing the composite\n");

    /* Leave something on screen: the restored background tile plus a handful
     * of actor-sized triangles over it, which is the shape of a real frame. */
    for (;;) {
        int i;

        prim_reset();
        draw_clut_quad(0, 0, 256, 256);
        draw_clut_quad(256, 0, 256, 256);
        for (i = 0; i < 24; i++)
            draw_gouraud_tri(40 + ((i * 47) % 500),
                             40 + ((i * 71) % 380), 48, i + frame);

        DrawSync(0);
        VSync(0);
        DrawOTag(ot + OT_LEN - 1);
        frame++;
    }

    return 0;
}
