/*
 * psx_m5.c — M5: the engine's own game loop, running.
 *
 * Everything up to M4 ran once and stopped. This harness hands control to
 * MainLoop on a fixed cube, skipping the menu and the introduction, and then
 * gets out of the way: the loop, the 50 Hz regulator, AffScene's dirty-box
 * discipline and ChangeCube are all the 1994 code, unmodified.
 *
 * What this file adds is the bill. PORT_M5_Frame is called once per iteration
 * from the top of MainLoop (PERSO.C, under PORT_PSX_M5) and reports, every
 * REPORT_EVERY frames:
 *
 *   - the frame time the machine actually achieved, against the 20 ms the
 *     regulator is asking for,
 *   - how many rectangles reached VRAM and how many pixels they covered,
 *     which is the whole question the dirty-box path exists to answer, and
 *   - how much of the frame those presents cost.
 *
 * A full-screen present is 307200 pixels and 120 ms (M3). If the box list is
 * tight the number below it will be a small fraction of that, and if it is not
 * then FlipBoxes is presenting most of the screen anyway and the discipline is
 * worth nothing here. That was not knowable before the loop ran.
 *
 * The report is deliberately one line per 25 frames rather than one per frame:
 * the TTY is a BIOS putchar per character and costs the machine real time, so
 * an instrument that prints every frame measures itself.
 */

#include "port.h"

void InitGame(int argc, UBYTE *argv[]);
LONG MainLoop(void);

extern WORD NumCube, NewCube;
extern WORD DisableAutoSave;

/* AffObjetIso, split four ways (translate/p_ob_iso.c). It is 18 ms of a 34 ms
 * frame, for the one object the engine's preclip lets through, and the split
 * says which part -- the emit, as it turns out, not the transform. */
extern unsigned long PORT_IsoTXform, PORT_IsoTBuild, PORT_IsoTSort, PORT_IsoTDraw;
extern unsigned long PORT_IsoObjects, PORT_IsoEntities;

#ifdef PORT_PSX_M6_AUTOPILOT
/* M6's scripted pad, reported once per frame (psx_m6.c). It rides on the M5
 * loop rather than replacing it: the input is the thing under test, the frame
 * around it is not. */
void PORT_M6_Frame(void);
#endif

#ifndef PSX_M3_CUBE
#define PSX_M3_CUBE 0
#endif

#define REPORT_EVERY 25         /* half a second at the regulator's 50 Hz */

static unsigned long frame_no;
static unsigned long last_us;
static unsigned long win_us, win_min, win_max;
static unsigned long win_present_us, win_pixels;
static int win_rects;

/*
 * The frame, in named pieces.
 *
 * The marks are called in a fixed order every frame -- MainLoop's logic, then
 * AffScene's clean, its present and its actor replay -- so they are recorded
 * by position and named by the first frame to reach them. Each measures the
 * interval since the previous mark, which makes the report below the answer
 * to "where does a frame go".
 *
 * This started as a printf at each point, to find where the loop stopped
 * dead. It is more useful as a clock.
 */
#define MARKS 6

static const char *mark_name[MARKS];
static unsigned long mark_us[MARKS];
static unsigned long mark_last;
static int mark_i;

void PORT_M5_Mark(const char *what)
{
    unsigned long now = PORT_Micros();

    if (mark_i < MARKS) {
        mark_name[mark_i] = what;
        mark_us[mark_i] += now - mark_last;
        mark_i++;
    }
    mark_last = now;
}

void PORT_M5_Frame(void)
{
    unsigned long now = PORT_Micros();
    unsigned long dt;
    int rects;
    unsigned long pixels, present_us;

    PORT_PresentStats(&rects, &pixels, &present_us);

    if (frame_no == 0) {
        /* The first frame carries ChangeCube and the full compose behind it,
         * which is a scene load and not a frame. It is reported and then left
         * out of the averages. */
        PORT_Diag("[M5] first frame (scene load, full compose): %lu ms, "
                  "%d rects, %lu pixels\n",
                  (now - last_us) / 1000UL, rects, pixels);
        win_min = 0xffffffffUL;
        win_max = 0;
        last_us = now;
        frame_no++;
        return;
    }

    dt = now - last_us;
    last_us = now;

    mark_i = 0;
    mark_last = now;

    win_us += dt;
    if (dt < win_min) win_min = dt;
    if (dt > win_max) win_max = dt;
    win_present_us += present_us;
    win_pixels += pixels;
    win_rects += rects;

    if (++frame_no % REPORT_EVERY == 0) {
        unsigned long n = REPORT_EVERY;

        PORT_Diag("[M5] f%-5lu %lu ms/frame (%lu..%lu), %lu fps | "
                  "%d rects %lu px %lu ms in present\n",
                  frame_no,
                  win_us / n / 1000UL, win_min / 1000UL, win_max / 1000UL,
                  win_us ? (1000000UL * n / win_us) : 0,
                  win_rects / (int)n, win_pixels / n, win_present_us / n / 1000UL);

        PORT_Diag("[M5]        iso: %lu objects %lu entities | xform %lu ms "
                  "build %lu ms sort %lu ms draw %lu ms\n",
                  PORT_IsoObjects / n, PORT_IsoEntities / n,
                  PORT_IsoTXform / n / 1000UL, PORT_IsoTBuild / n / 1000UL,
                  PORT_IsoTSort / n / 1000UL, PORT_IsoTDraw / n / 1000UL);

        {
            int m;

            PORT_Diag("[M5]        frame:");
            for (m = 0; m < MARKS && mark_name[m]; m++) {
                PORT_Diag(" %s %lu ms |", mark_name[m], mark_us[m] / n / 1000UL);
                mark_us[m] = 0;
            }
            PORT_Diag("\n");
        }

        PORT_IsoTXform = PORT_IsoTBuild = PORT_IsoTSort = PORT_IsoTDraw = 0;
        PORT_IsoObjects = PORT_IsoEntities = 0;

        win_us = 0;
        win_min = 0xffffffffUL;
        win_max = 0;
        win_present_us = 0;
        win_pixels = 0;
        win_rects = 0;
    }

#ifdef PORT_PSX_M6_AUTOPILOT
    /* Last, and after the frame's own accounting: what it prints is one line
     * per script step, twenty of them in half a minute, and charging that to
     * the frame it happened to land in would be noise either way. */
    PORT_M6_Frame();
#endif
}

void PORT_M5_Game(void)
{
    PORT_Diag("\n[M5] the game loop, cube %d\n", PSX_M3_CUBE);

    /* Same reason as M3: there is no Memory Card until M8 and ChangeCube
     * would autosave into one. */
    DisableAutoSave = 1;

    InitGame(1, 0);

    /* InitGame leaves NewCube 0 and NumCube -1, which is a new game at
     * Twinsen's house. PSX_M3_CUBE overrides where it starts. */
    NewCube = PSX_M3_CUBE;
    NumCube = -1;

    PORT_HeapReport("before the loop");
    last_us = PORT_Micros();

    MainLoop();

    PORT_Diag("[M5] MainLoop returned after %lu frames\n", frame_no);
}
