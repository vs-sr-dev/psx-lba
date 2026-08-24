/*
 * main_psx.c — entry point.
 *
 * Two things happen before the engine gets control, and both are instruments
 * rather than features:
 *
 *   - the exception handler, so a crash prints a program counter and a cause
 *     instead of freezing a black screen. The DS port's equivalent was the
 *     single most useful thing in it. The PlayStation makes this MORE
 *     important, not less: it has no MMU, so address 0 is kernel RAM rather
 *     than an unmapped page, and a null pointer write corrupts quietly where
 *     the DS raised a data abort. Anything the hardware will tell us, we want
 *     to hear.
 *
 *   - the heap, instrumented from the first allocation. On 4 MB the DS port
 *     finished with 380 KB spare; this machine has 2 MB, and the RAM verdict
 *     that M2 has to deliver is only as good as the accounting behind it.
 */

#include <stdio.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

#include "port.h"

extern char _end[];

static void banner(void)
{
    printf("\n");
    printf("psx-lba — Little Big Adventure, PlayStation\n");
    printf("engine: lba1-classic-community (GPL v2), 1994 Adeline Software\n");
    printf("video : %s\n", GetVideoMode() == MODE_PAL ? "PAL" : "NTSC");
    printf("bss   : ends at %08lx\n", (unsigned long)_end);
    printf("\n");
}

int main(void)
{
    ResetGraph(0);

    banner();
    PORT_HeapInit();

    PORT_Diag("[BOOT] entering lba_main\n");
    lba_main(0, 0);

    PORT_Diag("[BOOT] lba_main returned\n");
    PORT_HeapReport("exit");

    for (;;)
        __asm__ volatile("nop");

    return 0;
}
