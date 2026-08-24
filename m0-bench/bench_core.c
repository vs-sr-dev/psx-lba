#include <stdio.h>
#include <string.h>

#include "bench_core.h"
#include "translate.h"

/* Declared in translate/lib3d_p.h, which drags in the whole 3D pipeline; the
 * two entry points the rasteriser needs are all this benchmark uses. */
LONG ComputePoly_A(void);

/* Provided by bench_globals.c, which owns them because translate.h declares
 * TabOffLine as a scalar while the engine defines it as a 480-entry array --
 * see the note in that file. */
extern UBYTE Log_buffer[];

/* Defined by translate/s_poly.c: the polygon about to be scan-converted, in
 * the (intensity, x, y) triplet layout that P_OB_ISO fills in. */
extern WORD TypePoly;
extern WORD NbPolyPoints;
extern WORD TabPoly[];

/* ── workload ────────────────────────────────────────────────────────────────
 * TabPoly holds (intensity, x, y) triplets, exactly as P_OB_ISO fills it from
 * a body's polygon record. NbPolyPoints and TypePoly select the shape and the
 * fill routine.
 *
 * Sizes come from the real data. tools/poly_census.py over BODY.HQR:
 *   19826 polygons in 132 bodies      -> 150 polygons per body
 *   79.7% triangles, 19.2% quads
 *   98.0% flat / gouraud / gouraud+dither
 * An actor the size of Twinsen covers roughly 100x150 px at 640x480, so ~100
 * px per polygon is the honest mean. POLY_R is tuned to land there.
 */
#define POLY_R      12          /* ~100 px triangles                         */
#define POLYS_PER_ACTOR 150

static unsigned long rng_state = 0x13375EEDUL;

static unsigned long rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

static void make_poly(int cx, int cy, int radius, int nb_points)
{
    /* A convex fan around (cx, cy). The exact shape does not matter; the
     * scanline count and the span widths do, and those follow the radius. */
    static const signed char unit_x[8] = { 0,  7, 10,  7,  0, -7, -10, -7 };
    static const signed char unit_y[8] = { -10, -7, 0,  7, 10,  7,  0, -7 };
    int i;

    NbPolyPoints = (WORD)nb_points;

    for (i = 0; i < nb_points; i++) {
        int k = (i * 8) / nb_points;
        WORD *p = TabPoly + i * 3;

        p[0] = (WORD)(0x0800 + (WORD)(rng() & 0x07FF));     /* 8.8 intensity */
        p[1] = (WORD)(cx + (unit_x[k] * radius) / 10);
        p[2] = (WORD)(cy + (unit_y[k] * radius) / 10);
    }
}

struct fill_case {
    int   type;
    const char *name;
};

static const struct fill_case cases[] = {
    { 0, "Triste  (flat)   " },
    { 7, "Gouraud          " },
    { 8, "Dith             " },
    { 5, "Trans            " },
    { 4, "Marbre           " },
};

#define NB_CASES 5

/* ── the benchmark ───────────────────────────────────────────────────────── */
static unsigned long run_polys(int type, int radius, int nb_points, int count,
                               bench_clock_fn clock_us, unsigned long *pixels)
{
    unsigned long t0, t1;
    unsigned long px = 0;
    int i;

    TypePoly = (WORD)type;
    SetFillDetails(2);          /* full detail: the shipping DOS setting */

    t0 = clock_us();
    for (i = 0; i < count; i++) {
        int cx = 40 + (int)(rng() % 560);
        int cy = 40 + (int)(rng() % 400);

        make_poly(cx, cy, radius, nb_points);

        if (ComputePoly_A()) {
            FillVertic_A((LONG)type, 0x2010);
            px += (unsigned long)(radius * radius * 3);  /* rough, for ns/px */
        }
    }
    t1 = clock_us();

    if (pixels)
        *pixels = px;

    return t1 - t0;
}

void bench_run(bench_clock_fn clock_us, bench_print_fn print,
               const char *machine)
{
    char line[128];
    int i;

    for (i = 0; i < 480; i++)
        TABOFFLINE[i] = (ULONG)i * 640UL;

    memset(Log_buffer, 0, 640 * 480);

    snprintf(line, sizeof(line),
             "psx-lba M0 rasteriser calibration -- %s", machine);
    print(line);
    print("");
    print("s_poly.c ComputePoly_A + s_fillv.c FillVertic_A, unmodified,");
    print("640x480 8bpp target, ~100 px polygons (the BODY.HQR mean).");
    print("");

    /* Warm up: first pass touches 300 KB of cold buffer. */
    run_polys(7, POLY_R, 3, 200, clock_us, 0);

    print("  fill type            polys      us    us/poly    ns/px");
    for (i = 0; i < NB_CASES; i++) {
        unsigned long px = 0;
        unsigned long us = run_polys(cases[i].type, POLY_R, 3, 2000,
                                     clock_us, &px);

        snprintf(line, sizeof(line),
                 "  %s %6d %7lu %8lu.%02lu %8lu",
                 cases[i].name, 2000, us,
                 us / 2000, ((us % 2000) * 100) / 2000,
                 px ? (us * 1000UL) / px : 0UL);
        print(line);
    }

    print("");
    print("  polygon size sweep (gouraud, triangles)");
    print("    radius   polys      us    us/poly");
    {
        static const int radii[] = { 6, 12, 24, 48 };
        int r;

        for (r = 0; r < 4; r++) {
            unsigned long us = run_polys(7, radii[r], 3, 1000, clock_us, 0);

            snprintf(line, sizeof(line), "    %6d %7d %7lu %8lu.%02lu",
                     radii[r], 1000, us, us / 1000, ((us % 1000) * 100) / 1000);
            print(line);
        }
    }

    print("");
    print("  ONE ACTOR: 150 polygons, the BODY.HQR mean");
    print("    (averaged over 50 actors -- the clock is coarse)");
    print("    mix                  us      note");
    {
        const int reps = 50;
        unsigned long us;

        us = run_polys(7, POLY_R, 3, POLYS_PER_ACTOR * reps, clock_us, 0) / reps;
        snprintf(line, sizeof(line),
                 "    all gouraud    %8lu      DS ARM9 67 MHz: 6300-6600",
                 us);
        print(line);

        us = run_polys(0, POLY_R, 3, POLYS_PER_ACTOR * reps, clock_us, 0) / reps;
        snprintf(line, sizeof(line), "    all flat       %8lu", us);
        print(line);

        /* 20% of BODY.HQR is quads; they cost more per polygon but there are
         * fewer of them per unit of screen area. */
        us = run_polys(7, POLY_R, 4, POLYS_PER_ACTOR * reps, clock_us, 0) / reps;
        snprintf(line, sizeof(line), "    all gouraud q4 %8lu", us);
        print(line);
    }

    print("");
    print("  full-screen memset of Log (640x480, the Cls path)");
    {
        unsigned long t0 = clock_us();
        int n;

        for (n = 0; n < 10; n++)
            memset(Log_buffer, (int)n, 640 * 480);

        snprintf(line, sizeof(line), "    %lu us per clear",
                 (clock_us() - t0) / 10);
        print(line);
    }

    print("");
    print("-- done");
}
