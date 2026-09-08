/*
 * psx_depth.c — the bricks that stand in front of an actor.
 *
 * On DOS this is three lines of GRILLE.C. The actor is rasterised into `Log`,
 * and `DrawOverBrick` then copies the bricks in front of it out of the clean
 * background, through each brick's own mask, back over him. Depth, for the
 * price of a masked memcpy.
 *
 * Here the actor is not in `Log` to be covered up. It is a GPU primitive
 * drawn after the frame has been presented (psx_poly.c), so nothing a
 * software copy puts into `Log` can get in front of it. The occlusion has to
 * become a primitive too, and that is what this file builds:
 *
 *   1. DrawOverBrick's loop is bracketed by PORT_DepthBegin/End. Begin takes
 *      the actor's screen box from the engine's clip -- OBJECT.C has already
 *      set it to exactly that -- and End is where the box becomes a quad.
 *   2. Each brick in front of the actor is fetched out of the clean
 *      background, which lives in VRAM (psx_video.c), one brick-sized
 *      rectangle at a time, and its masked pixels are written into a small
 *      overlay buffer by CopyMaskTo -- which is CopyMask with both ends
 *      unhitched from a full frame (engine/game/CPYMASK.C).
 *   3. Everything the bricks did not cover stays 0, and 0 is a hole: the
 *      overlay is drawn through the second CLUT, the one that kept index 0 at
 *      0x0000 when the present's palette gave it bit 15 (psx_video.c,
 *      ApplyPalRange). One palette cannot be both opaque and transparent in
 *      black, so there are two.
 *   4. The quad is emitted INTO the actor buffer, at the point in the
 *      back-to-front order where DrawOverBrick was called. That matters: an
 *      actor standing in front of the brick has to stay in front of it, and
 *      it will, because its polygons come later in the same chain.
 *
 * Step 2 reads VRAM rather than `Log` for one reason, and it is the shadow.
 * `Log` holds the clean background inside an actor's box, because ClsBoxes
 * restored it there at the top of the frame -- but TYPE_SHADOW draws into Log
 * BEFORE calling DrawOverBrick, so for a shadow the pixels under a brick are
 * the shadow itself. Sourcing from Log made Twinsen's occlusion work and left
 * the shadow lying on top of everything, which is what the screen showed.
 * The background is the right source for both, and it costs one small
 * rectangle read per brick.
 */

#include <string.h>

#include <psxgpu.h>

#include "port.h"

#ifdef PORT_PSX_DEPTH

/* engine */
extern WORD ClipXmin, ClipYmin, ClipXmax, ClipYmax;
void CopyMaskTo(LONG nummask, LONG x, LONG y, void *bankmask,
                UBYTE *srcbase, LONG srcstride, LONG srcx, LONG srcy,
                UBYTE *dstbase, LONG dststride, LONG dstx, LONG dsty);

/* psx_video.c */
int  PORT_OverlayClut(void);
int  PORT_OverlayUpload(const unsigned char *buf, int h, int *tpage, int *v);
int  PORT_OverlayLost(void);

/* psx_poly.c */
void PORT_ActorTexQuad(int x, int y, int w, int h,
                       int tpage, int clut, int u, int v);

/* OVL_STRIDE and OVL_H have to match psx_video.c's, and the reason both are
 * 128 is VRAM: the scratch column between the framebuffer and the background
 * is 64 halfwords, which is 128 texels at 8bpp. An actor whose screen box is
 * bigger than this gets no overlay -- Twinsen is about 60 by 110, and cube 0
 * and cube 59 have never reached the cap. */
#define OVL_STRIDE  128
#define OVL_H       128

/* One brick's window on the background. A brick is 24 wide and under 40 tall;
 * this is generous so that an unusual mask is handled rather than counted. */
#define BRK_W       64
#define BRK_H       64

static unsigned char ovl_buf[OVL_STRIDE * OVL_H] __attribute__((aligned(4)));
static unsigned char brk_buf[BRK_W * BRK_H] __attribute__((aligned(4)));

static int ovl_active;          /* the box fits and we are inside a bracket */
static int ovl_written;         /* at least one brick has been copied in    */
static int box_x0, box_y0, box_w, box_h;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static int stat_quads, stat_skipped, stat_bigbrick;
static unsigned long stat_us;

void PORT_DepthBegin(void)
{
    ovl_active = 0;
    ovl_written = 0;

    box_x0 = ClipXmin;
    box_y0 = ClipYmin;
    box_w  = ClipXmax - ClipXmin + 1;
    box_h  = ClipYmax - ClipYmin + 1;

    if (box_w <= 0 || box_h <= 0)
        return;
    if (box_w > OVL_STRIDE || box_h > OVL_H) {
        stat_skipped++;
        return;
    }

    ovl_active = 1;
}

/*
 * One brick, into the overlay.
 *
 * The buffer is cleared here rather than in Begin, because Begin runs for
 * every actor in the scene and almost none of them have a brick in front of
 * them: a memset of 16 KB per entity would be milliseconds a frame to erase
 * something nothing was going to write.
 */
void PORT_DepthMask(LONG nummask, LONG x, LONG y, void *bankmask)
{
    const unsigned char *pmask;
    const unsigned long *pbank = (const unsigned long *)bankmask;
    unsigned long t0;
    int mx, my, mw, mh;

    if (!ovl_active)
        return;

    /* The first four bytes of a mask are its size and its offset, and
     * CopyMaskTo is about to read the same four. Reading them here is how the
     * background window and the dirty rectangle are known without the copy
     * having to report either. */
    pmask = (const unsigned char *)bankmask + pbank[nummask];
    mw = pmask[0];
    mh = pmask[1];
    mx = (int)x + pmask[2];
    my = (int)y + pmask[3];

    if (mw > BRK_W || mh > BRK_H) {
        stat_bigbrick++;
        return;
    }

    t0 = PORT_Micros();

    if (!ovl_written) {
        memset(ovl_buf, 0, sizeof(ovl_buf));
        dirty_x0 = box_w;
        dirty_y0 = box_h;
        dirty_x1 = -1;
        dirty_y1 = -1;
        ovl_written = 1;
    }

    PORT_BgFetchTo(mx, my, mx + mw - 1, my + mh - 1, brk_buf, BRK_W);

    CopyMaskTo(nummask, x, y, bankmask,
               brk_buf, BRK_W, mx, my,
               ovl_buf, OVL_STRIDE, box_x0, box_y0);

    mx -= box_x0;
    my -= box_y0;

    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx < dirty_x0) dirty_x0 = mx;
    if (my < dirty_y0) dirty_y0 = my;
    if (mx + mw - 1 > dirty_x1)
        dirty_x1 = (mx + mw - 1 > box_w - 1) ? box_w - 1 : mx + mw - 1;
    if (my + mh - 1 > dirty_y1)
        dirty_y1 = (my + mh - 1 > box_h - 1) ? box_h - 1 : my + mh - 1;

    stat_us += PORT_Micros() - t0;
}

void PORT_DepthEnd(void)
{
    unsigned long t0;
    int tpage, v, h, w;

    if (!ovl_active || !ovl_written)
        return;

    ovl_active = 0;

    if (dirty_x1 < dirty_x0 || dirty_y1 < dirty_y0)
        return;

    t0 = PORT_Micros();

    /* Whole rows go up -- 64 halfwords by any height is a whole number of
     * 16-word DMA blocks and a narrower span would not be -- but only the
     * rows that carry something, and the quad is the dirty rectangle. */
    h = dirty_y1 - dirty_y0 + 1;
    w = dirty_x1 - dirty_x0 + 1;

    if (PORT_OverlayUpload(ovl_buf + dirty_y0 * OVL_STRIDE, h, &tpage, &v)) {
        PORT_ActorTexQuad(box_x0 + dirty_x0, box_y0 + dirty_y0, w, h,
                          tpage, PORT_OverlayClut(), dirty_x0, v);
        stat_quads++;
    }

    stat_us += PORT_Micros() - t0;
}

void PORT_DepthStats(int *quads, int *skipped, int *lost, unsigned long *us)
{
    if (quads)   *quads   = stat_quads;
    if (skipped) *skipped = stat_skipped + stat_bigbrick;
    if (lost)    *lost    = PORT_OverlayLost();
    if (us)      *us      = stat_us;

    stat_quads = 0;
    stat_skipped = 0;
    stat_bigbrick = 0;
    stat_us = 0;
}

#endif /* PORT_PSX_DEPTH */
