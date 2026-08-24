/*
 * PSX host for the M0 rasteriser calibration.
 *
 * No video is set up: the benchmark writes into a plain main-RAM buffer, which
 * is exactly what the engine's `Log` is. Output goes to the BIOS TTY, which
 * PCSX-Redux and no$psx print.
 *
 * Timing: root counter 1 counts hblanks. That is 63.56 us per tick on NTSC,
 * coarse, but the 16-bit counter only wraps after 4.16 seconds -- and a timed
 * block here runs for tens of milliseconds, which the sysclk/8 counter (15.5 ms
 * to a wrap) cannot span without being polled from inside the block.
 */

#include <stdio.h>
#include <stdint.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <hwregs_c.h>

#include "bench_core.h"

static uint32_t hblank_hz;
static uint32_t last_raw;
static uint64_t accumulated;

static void clock_init(void)
{
    hblank_hz = (GetVideoMode() == MODE_PAL) ? 15625 : 15734;

    TIMER_CTRL(1) = 0x0100;     /* hblank source, free running, no IRQ */
    TIMER_VALUE(1) = 0;
    last_raw = 0;
    accumulated = 0;
}

static unsigned long clock_us(void)
{
    uint32_t raw = TIMER_VALUE(1) & 0xFFFF;

    if (raw < last_raw)
        accumulated += 0x10000;     /* wrapped since the last call */
    last_raw = raw;

    return (unsigned long)(((accumulated + raw) * 1000000ULL) / hblank_hz);
}

static void print_line(const char *line)
{
    printf("%s\n", line);
}

int main(void)
{
    ResetGraph(0);
    clock_init();

    /* Prime last_raw: wraps are only detected between two calls, and no timed
     * block below comes close to the 4.16 s the hblank counter takes to wrap. */
    (void)clock_us();

    bench_run(clock_us, print_line, "PlayStation, R3000A 33.87 MHz");

    for (;;)
        __asm__ volatile("nop");

    return 0;
}
