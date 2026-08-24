/*
 * Nintendo DS host for the M0 rasteriser calibration.
 *
 * This exists so the PSX numbers have a reference instead of a guess. The DS
 * port of LBA1 is the parent of this project and its per-actor cost is
 * measured (6300-6600 us for AffObjetIso, ARM9 at 67 MHz, hot code in ITCM) --
 * but that figure covers transform, sort and fill of a real body, so it cannot
 * be divided into the PSX figure directly. Running the SAME synthetic
 * workload, through the SAME translate/ sources, on both machines gives a
 * ratio that means something.
 *
 * Deliberately built WITHOUT -DPORT_NDS: PORT_FASTCODE/PORT_FASTBSS collapse
 * to nothing, so the rasteriser runs from main RAM exactly as it does on the
 * PlayStation. The shipping DS port puts it in ITCM and gains ~23%; that gain
 * is a DS advantage the PSX cannot copy (its 1 KB scratchpad will not hold
 * this code), so leaving it out is the fair comparison, not a handicap.
 *
 * Output goes to the bottom screen. There is no stdout on a DS.
 */

#include <stdio.h>
#include <nds.h>

#include "bench_core.h"

static unsigned long clock_us(void)
{
    /* cpuStartTiming(0) cascades timers 0 and 1 at BUS_CLOCK/1; only TM0/TM1
     * are free on the ARM9 -- calico owns TM2 (the system tick) and TM3. */
    return (unsigned long)timerTicks2usec(cpuGetTiming());
}

static void print_line(const char *line)
{
    printf("%s\n", line);
}

int main(void)
{
    consoleDemoInit();
    cpuStartTiming(0);

    bench_run(clock_us, print_line, "Nintendo DS, ARM9 67 MHz, main RAM");

    while (1)
        swiWaitForVBlank();

    return 0;
}
