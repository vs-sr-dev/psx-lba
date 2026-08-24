/*
 * psx_exc.c — what a fault prints, and the code that arms it.
 *
 * The DS port's single most useful piece of infrastructure was the guru
 * handler that named the faulting address. The PlayStation makes the same
 * instrument more valuable and harder to get: there is no MMU, so a null
 * pointer write is a perfectly legal store into kernel RAM that corrupts
 * quietly and crashes somewhere else entirely. What the hardware WILL tell us
 * is alignment, and alignment is the specific hazard this port faces on the
 * way through the 1994 byte-stream code.
 *
 * So the report is built around the two questions that actually get asked:
 *   - where was the program counter, and
 *   - what address did it touch.
 *
 * Everything is printed on the BIOS TTY, in the same channel as the rest of
 * the port's diagnostics, and then the machine stops. Returning from an
 * Address Error would just take it again.
 */

#include <psxapi.h>

#include "port.h"

extern unsigned long PORT_ExcRegs[37];
extern unsigned long PORT_ExcBiosVector[4];
extern void PORT_ExcVectorEntry(void);

#define EXC_SR      31
#define EXC_CAUSE   32
#define EXC_EPC     33
#define EXC_BADVA   34
#define EXC_HI      35
#define EXC_LO      36

/* The general exception vector, for BEV=0. The BIOS owns it until we do. */
#define VECTOR      ((volatile unsigned long *)0x80000080UL)

static const char *const reg_name[31] = {
    "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "??", "??", "gp", "sp", "fp", "ra"
};

static const char *CauseName(unsigned long code)
{
    switch (code) {
    case 0:  return "Int      interrupt";
    case 4:  return "AdEL     address error on LOAD (unaligned or bad)";
    case 5:  return "AdES     address error on STORE (unaligned or bad)";
    case 6:  return "IBE      bus error, instruction fetch";
    case 7:  return "DBE      bus error, data";
    case 8:  return "Sys      syscall";
    case 9:  return "Bp       breakpoint";
    case 10: return "RI       reserved instruction";
    case 11: return "CpU      coprocessor unusable";
    case 12: return "Ovf      arithmetic overflow";
    default: return "?        unknown";
    }
}

/*
 * Entered from PORT_ExcVectorEntry with the faulting registers already in
 * PORT_ExcRegs and a private stack under our feet. This never returns.
 */
void PORT_ExcReport(void)
{
    unsigned long cause = PORT_ExcRegs[EXC_CAUSE];
    unsigned long code  = (cause >> 2) & 0x1f;
    unsigned long epc   = PORT_ExcRegs[EXC_EPC];
    int i;

    /* A branch delay slot reports the branch, not the instruction. */
    if (cause & 0x80000000UL)
        epc += 4;

    PORT_Diag("\n");
    PORT_Diag("*** EXCEPTION *** %s\n", CauseName(code));
    PORT_Diag("    epc      %08lx%s\n", epc,
              (cause & 0x80000000UL) ? "  (in a branch delay slot)" : "");
    PORT_Diag("    badvaddr %08lx\n", PORT_ExcRegs[EXC_BADVA]);
    PORT_Diag("    cause    %08lx    sr %08lx\n", cause, PORT_ExcRegs[EXC_SR]);
    PORT_Diag("    ra       %08lx    sp %08lx\n",
              PORT_ExcRegs[30], PORT_ExcRegs[28]);

    if (code == 4 || code == 5) {
        unsigned long va = PORT_ExcRegs[EXC_BADVA];

        if (va < 0x00010000UL || (va >= 0x80000000UL && va < 0x80010000UL))
            PORT_Diag("    -> kernel RAM / near null: a null or wild pointer\n");
        else if (va & 1)
            PORT_Diag("    -> odd address: a halfword or word read off a byte stream\n");
        else if (va & 3)
            PORT_Diag("    -> 2-aligned: a 32-bit read off a halfword-aligned stream\n");
    }

    for (i = 0; i < 31; i += 4) {
        int j;

        PORT_Diag("   ");
        for (j = i; j < i + 4 && j < 31; j++)
            PORT_Diag(" %s=%08lx", reg_name[j], PORT_ExcRegs[j]);
        PORT_Diag("\n");
    }

    PORT_Diag("    the four words at epc:");
    for (i = 0; i < 4; i++)
        PORT_Diag(" %08lx", ((unsigned long *)(epc & ~3UL))[i]);
    PORT_Diag("\n");

    PORT_HeapReport("exception");

    PORT_Diag("*** halted. Look up epc in build/lba1psx.map, or run\n");
    PORT_Diag("*** mipsel-none-elf-addr2line -e build/lba1psx.elf %08lx\n", epc);

    for (;;)
        __asm__ volatile("nop");
}

/*
 * What is actually at 0x80000080 right now.
 *
 * The vector is shared with the BIOS and with libpsn00b, and either can put
 * something back. Whether ours is still there is a fact, not an assumption,
 * so it gets read rather than believed -- once at install time and again at
 * the moment of a self-test, which is the only way to tell "the handler is
 * wrong" apart from "the handler is not there".
 */
void PORT_ExcVectorDump(const char *when)
{
    PORT_Diag("[EXC] vector %s: %08lx %08lx %08lx %08lx\n", when,
              VECTOR[0], VECTOR[1], VECTOR[2], VECTOR[3]);
}

/*
 * Take the vector over. The BIOS trampoline there is four position-independent
 * instructions, so it is relocated rather than reimplemented — whatever the
 * BIOS wanted to do about interrupts it still does, one jump later.
 */
void PORT_ExcInstall(void)
{
    unsigned long entry = (unsigned long)&PORT_ExcVectorEntry;
    int i;

    for (i = 0; i < 4; i++)
        PORT_ExcBiosVector[i] = VECTOR[i];

    PORT_Diag("[EXC] BIOS vector was %08lx %08lx %08lx %08lx\n",
              PORT_ExcBiosVector[0], PORT_ExcBiosVector[1],
              PORT_ExcBiosVector[2], PORT_ExcBiosVector[3]);

    EnterCriticalSection();

    VECTOR[0] = 0x3C1A0000UL | (entry >> 16);           /* lui   $k0, hi   */
    VECTOR[1] = 0x375A0000UL | (entry & 0xffffUL);      /* ori   $k0, lo   */
    VECTOR[2] = 0x03400008UL;                           /* jr    $k0       */
    VECTOR[3] = 0x00000000UL;                           /* nop             */

    FlushCache();

    ExitCriticalSection();

    PORT_Diag("[EXC] handler at %08lx, BIOS chain at %p\n",
              entry, (void *)PORT_ExcBiosVector);
    PORT_ExcVectorDump("after install");
}

#ifdef PSX_EXC_SELFTEST
/*
 * A crash handler nobody has seen fire is a claim, not an instrument, and this
 * project has already paid once for a diagnostic that was itself wrong (see
 * docs/M2-NOTES.md on the directory listing that returned nothing). So: read a
 * 32-bit word off an odd address, which is the exact shape of the fault the
 * 1994 byte-stream code is expected to produce, and check that the report
 * arrives with the right pc and the right badvaddr.
 *
 * The pointer goes through a volatile global so the compiler cannot prove the
 * alignment and fold the load away.
 */
static unsigned char selftest_stream[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
volatile unsigned char *PORT_ExcSelfTestPtr = selftest_stream + 1;

void PORT_ExcSelfTest(void)
{
    volatile unsigned long v;

    PORT_ExcVectorDump("at the self-test");
    PORT_Diag("[EXC] self-test: 32-bit read at %p, which is odd\n",
              (void *)PORT_ExcSelfTestPtr);

    v = *(volatile unsigned long *)PORT_ExcSelfTestPtr;

    PORT_Diag("[EXC] self-test FAILED: read %08lx and no fault was raised.\n", v);
    PORT_Diag("[EXC] the handler is NOT armed. Nothing below this is trustworthy.\n");
}
#endif
