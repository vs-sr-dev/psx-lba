/*
 * psx_poly.c — the actors, drawn by the GPU.
 *
 * The 1994 renderer builds a display list, sorts it back to front, and then
 * scan-converts each entity into `Log` with a per-fill-mode software filler
 * (translate/s_poly.c, translate/s_fillv.c). This file replaces the last of
 * those three steps and nothing else: the transform, the entity build and the
 * sort stay exactly as they were, and each entity is emitted as a GPU
 * primitive at the point where P_OB_ISO would have rasterised it.
 *
 * That the sort survives is the whole reason this is cheap. The PlayStation
 * has no depth buffer and the DOS engine had none either, so both machines
 * are already committed to a painter's algorithm — the engine's SergeSort IS
 * the ordering table, and primitives submitted in its order land in the right
 * order. No OT, no z, no reconciliation.
 *
 * Three things this file has to get right that the software path got for
 * free:
 *
 *   COLOUR. The engine works in 8bpp index space. A flat polygon carries a
 *   palette index; a gouraud polygon carries one per vertex, and the software
 *   filler interpolates the INDEX and writes it as a pixel. That only looks
 *   like shading because LBA's palette is laid out as 16-entry ramps. The GPU
 *   interpolates RGB instead, so each index is resolved through PORT_PalRGB
 *   at emit time. Inside one ramp the two are the same picture; across a ramp
 *   boundary the GPU is arguably the more correct of the two, and either way
 *   a palette fade now has to reach the actors as well as the CLUT.
 *
 *   CLIPPING. The GPU's vertices are 11-bit signed and it silently drops any
 *   primitive wider than 1023 or taller than 511. The engine happily produces
 *   screen coordinates in the tens of thousands for something close to the
 *   camera, so handing them over unfiltered loses whole polygons at exactly
 *   the moment they fill the screen. ComputePoly_A used to clip; it is not on
 *   this path any more, so the Sutherland-Hodgman below takes over. It clips
 *   against the engine's own ClipXmin/Ymin/Xmax/Ymax, which means SetClip
 *   keeps working for the water line, the zoom box and the rest.
 *
 *   WINDING. The engine's vertices go round the perimeter. A PlayStation
 *   POLY_F4 is a strip, not a loop, so a quad is emitted 0,1,3,2. Anything
 *   with more than four vertices — and clipping routinely makes those — is
 *   fanned from vertex 0.
 *
 * The fill modes are mapped in FillIsGouraud() below. M0's census: 98.03% of
 * the 19826 polygons in BODY.HQR are flat or gouraud and land exactly; the
 * awkward 1.78% is approximated here and noted in docs/M4-NOTES.md.
 */

#include <string.h>

#include <psxgpu.h>
#include <psxetc.h>

#include "port.h"

/* The engine's clip rectangle (psx_video.c). */
extern WORD ClipXmin, ClipYmin, ClipXmax, ClipYmax;

/*
 * A frame's primitives, held back until the end of it.
 *
 * There is one framebuffer -- 640x480 in 16 bits is 60% of VRAM and two do
 * not fit -- so everything drawn is drawn on the screen the viewer is looking
 * at. Submitting each primitive at the moment the engine computes it meant
 * the actor was absent for the 38 ms between the background present and the
 * first polygon, once every frame, which reads as a hard flicker (M5 �6).
 *
 * So they go in here instead, in the engine's own back-to-front order, and
 * PORT_ActorEnd replays the lot immediately after the background has been
 * presented. The hole shrinks from a frame to a few milliseconds.
 *
 * 8 KB is about 290 gouraud triangles. Cube 0's only visible actor emits 129
 * primitives, so the margin is wide, but a scene that overflows drops the
 * excess and says so rather than growing a buffer on a machine with 39 KB
 * spare.
 */
#define PRIM_WORDS  2048

static unsigned int prim_buf[PRIM_WORDS];
static int prim_used;
static int prim_dropped;

/* One primitive, deferred. The word count lives in the top byte of the tag,
 * which is where DrawPrim reads it from too. */
static void Emit(const void *pri)
{
    const unsigned int *p = (const unsigned int *)pri;
    int len = (int)((p[0] >> 24) & 0xff);

    if (prim_used + len + 1 > PRIM_WORDS) {
        prim_dropped++;
        return;
    }

    memcpy(&prim_buf[prim_used], p, (size_t)(len + 1) * 4);
    prim_used += len + 1;
}

/* Fill types, after P_OB_ISO's translation (translate/s_fillv.c's table):
 *   0 Triste  1 Tele  2 Copper  3 Bopper  4 Marbre
 *   5 Trans   6 Trame 7 Gouraud 8 Dith                                     */
#define POLY_TRISTE     0
#define POLY_TELE       1
#define POLY_COPPER     2
#define POLY_BOPPER     3
#define POLY_MARBRE     4
#define POLY_TRANS      5
#define POLY_TRAME      6
#define POLY_GOURAUD_   7
#define POLY_DITH       8

#define MAX_V           24      /* 16 source vertices, plus clipping slack  */

typedef struct {
    WORD x, y;
    WORD i;                     /* palette index, 8 bits used              */
} Vtx;

static int stats_polys;
static int stats_dropped;
static int stats_lines;
static int stats_spheres;

/* ── colour ──────────────────────────────────────────────────────────────── */
static void IndexRGB(int idx, UBYTE *r, UBYTE *g, UBYTE *b)
{
    const UBYTE *p = PORT_PalRGB + (idx & 0xFF) * 3;

    *r = p[0];
    *g = p[1];
    *b = p[2];
}

static int FillIsGouraud(int type)
{
    /* Gouraud and Dith both interpolate; Dith only differs in how the
     * software filler hid banding on a 256-colour DAC, which is not a problem
     * the GPU has. Marbre is a gradient along the span — gouraud in index
     * space, 8 polygons in the whole game — and is close enough here. */
    return (type == POLY_GOURAUD_ || type == POLY_DITH || type == POLY_MARBRE);
}

static int FillIsTransparent(int type)
{
    /* Trans is the engine's translucency and Trame is its 50% dither, which
     * existed because a paletted framebuffer cannot blend. This one can. */
    return (type == POLY_TRANS || type == POLY_TRAME);
}

/* ── Sutherland-Hodgman against the engine's clip rectangle ─────────────── *
 * Four passes, one per edge. The intensity is interpolated at each crossing
 * the same way the polygon's own scan conversion would have done it: linearly
 * in index space, which is what makes a ramp look like a ramp.
 */
static int ClipEdge(const Vtx *in, int n, Vtx *out, int axis, int lim, int keep_ge)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const Vtx *a = &in[i];
        const Vtx *b = &in[(i + 1) % n];
        LONG av = axis ? a->y : a->x;
        LONG bv = axis ? b->y : b->x;
        int ain = keep_ge ? (av >= lim) : (av <= lim);
        int bin = keep_ge ? (bv >= lim) : (bv <= lim);

        if (ain)
            out[m++] = *a;

        if (ain != bin) {
            LONG d = bv - av;
            LONG t;             /* 0..4096 along a->b                       */

            if (d == 0)
                continue;
            t = ((LONG)(lim - av) * 4096L) / d;

            out[m].x = (WORD)(a->x + (((LONG)(b->x - a->x) * t) >> 12));
            out[m].y = (WORD)(a->y + (((LONG)(b->y - a->y) * t) >> 12));
            out[m].i = (WORD)(a->i + (((LONG)(b->i - a->i) * t) >> 12));
            m++;
        }

        if (m >= MAX_V - 1)
            return m;           /* degenerate input; take what we have      */
    }

    return m;
}

/*
 * Whole-polygon reject before the four clipping passes. Most of the work this
 * saves is not near the screen edge at all: an LBA scene holds twenty-odd
 * actors and the camera frames one of them, so the majority of what reaches
 * here is an object that is entirely somewhere else. Four Sutherland-Hodgman
 * passes to discover that is four too many.
 */
static int BoxReject(const Vtx *v, int n)
{
    WORD xmin = v[0].x, xmax = v[0].x;
    WORD ymin = v[0].y, ymax = v[0].y;
    int i;

    for (i = 1; i < n; i++) {
        if (v[i].x < xmin) xmin = v[i].x;
        if (v[i].x > xmax) xmax = v[i].x;
        if (v[i].y < ymin) ymin = v[i].y;
        if (v[i].y > ymax) ymax = v[i].y;
    }

    return (xmax < ClipXmin || xmin > ClipXmax ||
            ymax < ClipYmin || ymin > ClipYmax);
}

static int ClipPoly(Vtx *v, int n)
{
    Vtx tmp[MAX_V];

    if (BoxReject(v, n))
        return 0;

    n = ClipEdge(v, n, tmp, 0, ClipXmin, 1);   if (n < 3) return 0;
    n = ClipEdge(tmp, n, v, 0, ClipXmax, 0);   if (n < 3) return 0;
    n = ClipEdge(v, n, tmp, 1, ClipYmin, 1);   if (n < 3) return 0;
    n = ClipEdge(tmp, n, v, 1, ClipYmax, 0);   if (n < 3) return 0;

    return n;
}

/* ── emit ────────────────────────────────────────────────────────────────── */
static void FlatTri(const Vtx *a, const Vtx *b, const Vtx *c, int idx, int trans)
{
    POLY_F3 p;
    UBYTE r, g, bl;

    IndexRGB(idx, &r, &g, &bl);

    setPolyF3(&p);
    setRGB0(&p, r, g, bl);
    setXY3(&p, a->x, a->y, b->x, b->y, c->x, c->y);
    if (trans)
        setSemiTrans(&p, 1);

    Emit(&p);
}

static void GouraudTri(const Vtx *a, const Vtx *b, const Vtx *c, int trans)
{
    POLY_G3 p;
    UBYTE r, g, bl;

    setPolyG3(&p);
    IndexRGB(a->i, &r, &g, &bl); setRGB0(&p, r, g, bl);
    IndexRGB(b->i, &r, &g, &bl); setRGB1(&p, r, g, bl);
    IndexRGB(c->i, &r, &g, &bl); setRGB2(&p, r, g, bl);
    setXY3(&p, a->x, a->y, b->x, b->y, c->x, c->y);
    if (trans)
        setSemiTrans(&p, 1);

    Emit(&p);
}

/*
 * One polygon, as P_OB_ISO would have handed it to ComputePoly_A: `tri` is
 * nbp (colour, x, y) WORD triplets, and `coul` is the flat colour word whose
 * low byte is the palette index. For gouraud the per-vertex index is the low
 * byte of each triplet's colour word — the high byte is the 8.8 fraction the
 * software filler used and the GPU does not need.
 */
void PORT_ActorPoly(LONG type, LONG coul, LONG nbp, const WORD *tri)
{
    Vtx v[MAX_V];
    int n, i;
    int gouraud = FillIsGouraud((int)type);
    int trans = FillIsTransparent((int)type);
    int flat_idx = (int)(coul & 0xFF);

    if (nbp < 3)
        return;
    if (nbp > 16)
        nbp = 16;               /* the engine's own limit is 32; no body
                                 * in BODY.HQR goes past 16 (M0 census)    */

    for (i = 0; i < (int)nbp; i++) {
        v[i].i = (WORD)(gouraud ? (tri[i * 3] & 0xFF) : flat_idx);
        v[i].x = tri[i * 3 + 1];
        v[i].y = tri[i * 3 + 2];
    }

    n = ClipPoly(v, (int)nbp);
    if (n < 3) {
        stats_dropped++;
        return;
    }

    /* Fan from vertex 0. A quad could go out as one POLY_F4/G4 in strip
     * order, but clipping turns most quads into 5- and 6-gons anyway, and one
     * path that is always right beats two paths where one is usually. */
    for (i = 1; i < n - 1; i++) {
        if (gouraud)
            GouraudTri(&v[0], &v[i], &v[i + 1], trans);
        else
            FlatTri(&v[0], &v[i], &v[i + 1], flat_idx, trans);
    }

    stats_polys++;
}

/*
 * A line entity. The colour word arrives byte-swapped from P_OB_ISO — the
 * 1994 code did `mov ax,[esi] ; xchg al,ah` — so the index is the low byte
 * after the swap the caller already performed.
 */
void PORT_ActorLine(LONG x0, LONG y0, LONG x1, LONG y1, LONG coul)
{
    LINE_F2 l;
    UBYTE r, g, b;

    /* Cheap reject; the GPU would drop an out-of-range primitive silently. */
    if ((x0 < ClipXmin && x1 < ClipXmin) || (x0 > ClipXmax && x1 > ClipXmax) ||
        (y0 < ClipYmin && y1 < ClipYmin) || (y0 > ClipYmax && y1 > ClipYmax))
        return;
    if (x0 < ClipXmin) x0 = ClipXmin;
    if (x1 < ClipXmin) x1 = ClipXmin;
    if (x0 > ClipXmax) x0 = ClipXmax;
    if (x1 > ClipXmax) x1 = ClipXmax;
    if (y0 < ClipYmin) y0 = ClipYmin;
    if (y1 < ClipYmin) y1 = ClipYmin;
    if (y0 > ClipYmax) y0 = ClipYmax;
    if (y1 > ClipYmax) y1 = ClipYmax;

    IndexRGB((int)(coul & 0xFF), &r, &g, &b);

    setLineF2(&l);
    setRGB0(&l, r, g, b);
    setXY2(&l, (short)x0, (short)y0, (short)x1, (short)y1);

    Emit(&l);
    stats_lines++;
}

/*
 * A sphere entity, as a fan. The 1994 renderer scan-converted a real circle;
 * spheres in LBA are shadows, magic balls and eyes, none of them large, so a
 * twelve-sided approximation is under a pixel of error at the sizes they
 * actually appear at. It is a decision rather than a shortcut, and if it ever
 * shows, the number below is the knob.
 */
#define SPHERE_SIDES    12

void PORT_ActorSphere(LONG x, LONG y, LONG r, LONG type, LONG coul)
{
    /* cos/sin of k*30 degrees, 12 steps, 4096 = 1.0 */
    static const short ctab[SPHERE_SIDES] = {
        4096, 3547, 2048, 0, -2048, -3547, -4096, -3547, -2048, 0, 2048, 3547
    };
    static const short stab[SPHERE_SIDES] = {
        0, 2048, 3547, 4096, 3547, 2048, 0, -2048, -3547, -4096, -3547, -2048
    };
    Vtx v[MAX_V];
    int i, n;
    int idx = (int)(coul & 0xFF);

    if (r < 1)
        r = 1;

    for (i = 0; i < SPHERE_SIDES; i++) {
        v[i].x = (WORD)(x + ((r * ctab[i]) >> 12));
        v[i].y = (WORD)(y + ((r * stab[i]) >> 12));
        v[i].i = (WORD)idx;
    }

    n = ClipPoly(v, SPHERE_SIDES);
    if (n < 3)
        return;

    for (i = 1; i < n - 1; i++)
        FlatTri(&v[0], &v[i], &v[i + 1], idx, FillIsTransparent((int)type));

    stats_spheres++;
}

/* ── frame bookkeeping ───────────────────────────────────────────────────── */
void PORT_ActorBegin(void)
{
    DR_TPAGE tp;

    stats_polys = 0;
    stats_dropped = 0;
    stats_lines = 0;
    stats_spheres = 0;

    /* An untextured primitive's semi-transparency takes its blend equation
     * from the GPU's current texture page, not from the primitive. Set it
     * explicitly: 0 is 0.5*back + 0.5*front, which is what Trans and Trame
     * were approximating. Without this the mode would be whatever psx_video.c
     * happened to leave behind after the last background blit -- correct
     * today, and a coupling nobody would find funny later. */
    setDrawTPage(&tp, 0, 0, 0);

    prim_used = 0;
    prim_dropped = 0;
    Emit(&tp);
}

/*
 * The frame's actors, all at once, on top of a background that has just been
 * presented. Called from the tail of AffScene -- after FlipBoxes, not before.
 */
void PORT_ActorEnd(void)
{
    int i = 0;

    if (prim_used == 0)
        return;

    /*
     * One DMA, not one per primitive.
     *
     * DrawPrim hands the GPU a single packet and waits for it, and at 137
     * entities a frame that measured 13 ms of the 50 -- more than the whole
     * transform. The packets are already contiguous and already in the
     * engine's back-to-front order, so all they need is the low 24 bits of
     * each tag pointing at the next one, and the chain IS an ordering table.
     * DrawOTag then sends the lot in linked-list mode and the CPU is out of
     * the loop.
     *
     * The address is masked to 24 bits because that is the field's width and
     * because RAM is mirrored at 0x00000000; the top byte stays the packet's
     * word count, which is where DrawOTag reads it from.
     */
    while (i < prim_used) {
        int len = (int)((prim_buf[i] >> 24) & 0xff);
        int next = i + len + 1;
        unsigned int link = (next < prim_used)
                          ? ((unsigned int)&prim_buf[next] & 0x00ffffffU)
                          : 0x00ffffffU;

        prim_buf[i] = (prim_buf[i] & 0xff000000U) | link;
        i = next;
    }

    DrawOTag((const uint32_t *)&prim_buf[0]);
    DrawSync(0);
    prim_used = 0;
}

/* Primitives that did not fit the frame's buffer. Zero everywhere so far. */
int PORT_ActorDropped(void)
{
    return prim_dropped;
}

void PORT_ActorStats(int *polys, int *dropped, int *lines, int *spheres)
{
    if (polys)   *polys = stats_polys;
    if (dropped) *dropped = stats_dropped;
    if (lines)   *lines = stats_lines;
    if (spheres) *spheres = stats_spheres;
}

/*
 * Primitives emitted so far. AffObjetIso reports success when it displayed at
 * least one ENTITY, which it decided before any of them met the clip
 * rectangle -- so an object entirely off screen still comes back "drawn". The
 * only honest count is the one taken here, and the harness reads it either
 * side of each object.
 */
int PORT_ActorEmitted(void)
{
    return stats_polys + stats_lines + stats_spheres;
}
