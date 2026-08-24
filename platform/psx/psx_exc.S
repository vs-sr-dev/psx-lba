/*
 * psx_exc.S — the general exception vector, taken over.
 *
 * The R3000A raises a real exception where the ARM9 silently rotated: an
 * unaligned halfword load is an Address Error, not a rotated word. The 1994
 * code walks byte streams and pulls words out of them in at least seven
 * places (the DS port's DEVLOG names GERELIFE, GERETRAK, FICHE, GRILLE,
 * GRILLE_A, P_ANIM and DISKFUNC), so this port is going to take a lot of
 * those. Each one is worth a line number rather than an afternoon.
 *
 * The retail BIOS puts a four-instruction trampoline at 0x80000080 and it is
 * position independent (lui / addiu / jr / nop), so it can be relocated
 * wholesale. PORT_ExcInstall copies it into PORT_ExcBiosVector and writes an
 * equivalent trampoline to ours in its place. Everything the BIOS and
 * libpsn00b need still reaches them: interrupts, and the syscall and break
 * instructions the BIOS uses for EnterCriticalSection and friends, are handed
 * straight over. Only a genuine fault stops here.
 *
 * k0 and k1 are the two registers the MIPS ABI reserves for exactly this, and
 * the BIOS handler treats them as scratch too, so the triage below costs the
 * interrupt path nothing it can notice.
 */

.set noreorder
.set noat

.section .text

.global PORT_ExcVectorEntry
.type PORT_ExcVectorEntry, @function

PORT_ExcVectorEntry:
    mfc0    $k1, $13                /* Cause                                */
    andi    $k1, $k1, 0x7c          /* ExcCode * 4                          */
    beqz    $k1, .Lchain            /* 0  Int      -> BIOS / libpsn00b      */
    nop
    addiu   $k1, $k1, -0x20
    beqz    $k1, .Lchain            /* 8  Sys      -> BIOS (critical secs)  */
    nop
    addiu   $k1, $k1, -0x04
    beqz    $k1, .Lchain            /* 9  Bp       -> BIOS                  */
    nop

    /* ---- a real fault. Nothing has been clobbered but k0/k1. ---------- */
    la      $k0, PORT_ExcRegs

    sw      $1,   0*4($k0)
    sw      $2,   1*4($k0)
    sw      $3,   2*4($k0)
    sw      $4,   3*4($k0)
    sw      $5,   4*4($k0)
    sw      $6,   5*4($k0)
    sw      $7,   6*4($k0)
    sw      $8,   7*4($k0)
    sw      $9,   8*4($k0)
    sw      $10,  9*4($k0)
    sw      $11, 10*4($k0)
    sw      $12, 11*4($k0)
    sw      $13, 12*4($k0)
    sw      $14, 13*4($k0)
    sw      $15, 14*4($k0)
    sw      $16, 15*4($k0)
    sw      $17, 16*4($k0)
    sw      $18, 17*4($k0)
    sw      $19, 18*4($k0)
    sw      $20, 19*4($k0)
    sw      $21, 20*4($k0)
    sw      $22, 21*4($k0)
    sw      $23, 22*4($k0)
    sw      $24, 23*4($k0)
    sw      $25, 24*4($k0)
    /* 26, 27 are k0/k1 and are gone by construction; slots stay zero.     */
    sw      $28, 27*4($k0)          /* gp                                   */
    sw      $29, 28*4($k0)          /* sp                                   */
    sw      $30, 29*4($k0)          /* fp                                   */
    sw      $31, 30*4($k0)          /* ra                                   */

    mfc0    $k1, $12
    sw      $k1, 31*4($k0)          /* SR                                   */
    mfc0    $k1, $13
    sw      $k1, 32*4($k0)          /* Cause                                */
    mfc0    $k1, $14
    sw      $k1, 33*4($k0)          /* EPC                                  */
    mfc0    $k1, $8
    sw      $k1, 34*4($k0)          /* BadVAddr                             */
    mfhi    $k1
    sw      $k1, 35*4($k0)
    mflo    $k1
    sw      $k1, 36*4($k0)

#ifdef PSX_EXC_TRACE
    /* Diagnosing the handler itself: one character straight through the BIOS
     * putchar, then stop. If it appears we are here and the TTY works; if the
     * machine merely hangs we are here and it does not; if the BIOS prints its
     * own unresolved-exception dump instead, we never arrived. */
    li      $a0, 0x58               /* 'X' */
    li      $t1, 0x3d               /* std_out_putchar */
    li      $k1, 0xb0
    jalr    $k1
    nop
1:  b       1b
    nop
#endif

    /* A private stack: the fault may well BE the stack, and a report that
     * faults on its way out is worse than no report at all. */
    la      $sp, PORT_ExcStackTop
    la      $gp, _gp
    la      $k0, PORT_ExcReport
    jr      $k0
    nop

.Lchain:
    la      $k0, PORT_ExcBiosVector
    jr      $k0
    nop

.size PORT_ExcVectorEntry, . - PORT_ExcVectorEntry

/* ------------------------------------------------------------------------ */

.section .bss
.align 4

.global PORT_ExcRegs
PORT_ExcRegs:
    .space 37 * 4

.global PORT_ExcBiosVector
PORT_ExcBiosVector:
    .space 4 * 4

.align 3
PORT_ExcStack:
    .space 2048
.global PORT_ExcStackTop
PORT_ExcStackTop:
