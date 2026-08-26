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

#ifndef PSX_M3_CUBE
#define PSX_M3_CUBE 0
#endif

#define REPORT_EVERY 25         /* half a second at the regulator's 50 Hz */

static unsigned long frame_no;
static unsigned long last_us;
static unsigned long win_us, win_min, win_max;
static unsigned long win_present_us, win_pixels;
static int win_rects;

/* Where a frame stops, when it stops. Only for the first few frames: the TTY
 * is a BIOS putchar per character and an instrument that prints every frame
 * measures itself. */
void PORT_M5_Mark(const char *what)
{
    if (frame_no < 5)
        PORT_Diag("[M5]   . %s\n", what);
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

        win_us = 0;
        win_min = 0xffffffffUL;
        win_max = 0;
        win_present_us = 0;
        win_pixels = 0;
        win_rects = 0;
    }
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
