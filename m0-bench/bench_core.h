/*
 * psx-lba — M0 calibration benchmark, portable core.
 *
 * The feasibility study extrapolated the PSX software-rendering cost from the
 * DS port's measured 6.3-6.6 ms per actor with a hand-waved "2.5-3x slower"
 * factor. That factor is the weakest number in the whole document, and it is
 * the one the architecture decision rests on.
 *
 * So: compile THIS FILE for both machines, feed it identical synthetic work,
 * and read the ratio off the two consoles instead of guessing it. The code
 * under test is the real thing — translate/s_poly.c and translate/s_fillv.c,
 * the C translations of Adeline's S_POLY.ASM and S_FILLV.ASM, unmodified.
 *
 * The host supplies a microsecond clock and a print function; everything else
 * is shared.
 */
#ifndef BENCH_CORE_H
#define BENCH_CORE_H

typedef unsigned long (*bench_clock_fn)(void);      /* free-running us */
typedef void (*bench_print_fn)(const char *line);

void bench_run(bench_clock_fn clock_us, bench_print_fn print,
               const char *machine);

#endif /* BENCH_CORE_H */
