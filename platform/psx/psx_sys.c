/*
 * psx_sys.c — diagnostics, heap, timing, input and the DOS system calls the
 * engine expects.
 *
 * Two things here are not conveniences, they are the instruments the rest of
 * the port is built with:
 *
 *   PORT_Diag  — everything goes to the BIOS TTY. PCSX-Redux prints it. On the
 *                DS this was the bottom-screen console and it was the single
 *                most valuable thing in the port; there is no reason to
 *                believe a machine with half the RAM will need it less.
 *
 *   the heap   — a canary per block and a running total. The DS port fitted
 *                LBA into 4 MB with 380 KB to spare; this machine has 2 MB.
 *                Knowing the number at every moment is the difference between
 *                a budget and a hope.
 *
 * The 50 Hz tick matters more than it looks. The engine's game logic runs on
 * TimerSystem, not on frames — the DOS build had no frame limiter because the
 * VGA card was the limiter — so the tick has to be an interrupt, not something
 * polled from the render loop, or the engine's wait loops spin forever.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <hwregs_c.h>

#include "port.h"
#include "LIB_SYS/SYS_FILESYSTEM.H"

/* ══════════════════════════════════════════════════════════════════════════
 * Diagnostics
 * ═════════════════════════════════════════════════════════════════════════ */

void PORT_Diag(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("%s", buf);
}

void PORT_Panic(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("\n*** PANIC: %s\n", buf);
    PORT_HeapReport("panic");

    for (;;)
        __asm__ volatile("nop");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Instrumented heap
 *
 * Every block carries a 16-byte header (size, magic, and a guard word) and a
 * 4-byte canary past its end. PORT_HeapCheck walks the live list and reports
 * the first damaged canary at the moment the damage is noticed rather than
 * three crashes later, which is how the DS port found several of the 1994
 * overruns.
 * ═════════════════════════════════════════════════════════════════════════ */

#define HEAP_MAGIC   0x1BA10001UL
#define HEAP_CANARY  0xDEADBEEFUL

typedef struct HeapHdr {
    unsigned long   size;
    unsigned long   magic;
    struct HeapHdr *next;
    struct HeapHdr *prev;
} HeapHdr;

static HeapHdr      *heap_head;
static unsigned long heap_used;
static unsigned long heap_peak;
static unsigned long heap_blocks;

extern char _end[];         /* end of .bss, from the SDK link script */

/* Main RAM is 2 MB at 0x80000000. The stack lives at the top and grows down;
 * leave it room. */
#define RAM_TOP       0x80200000UL
#define STACK_RESERVE (64 * 1024)

void PORT_HeapInit(void)
{
    unsigned long base = ((unsigned long)_end + 15UL) & ~15UL;
    unsigned long size = RAM_TOP - STACK_RESERVE - base;

    InitHeap((void *)base, size);

    heap_head = 0;
    heap_used = 0;
    heap_peak = 0;
    heap_blocks = 0;

    PORT_Diag("[MEM] heap %lu KB at %08lx (bss ends %08lx, %u KB stack held back)\n",
              size / 1024UL, base, (unsigned long)_end, STACK_RESERVE / 1024);
}

/*
 * The canary sits past the end of the block, and blocks are whatever size the
 * engine asked for -- 361472, but also 3097 and 25500. On the DS an odd offset
 * here was a rotated word nobody noticed. On MIPS it is a Store Address Error,
 * and this was the first one the port found: the instrument faulting on its
 * own instrumentation.
 *
 * Round up to a word. PSX_malloc reserves the padding to match.
 */
#define CANARY_OFF(sz)  (((sz) + 3UL) & ~3UL)

static unsigned long *canary_of(HeapHdr *h)
{
    return (unsigned long *)((char *)(h + 1) + CANARY_OFF(h->size));
}

int PORT_HeapCheck(const char *tag)
{
    HeapHdr *h = heap_head;
    int bad = 0;

    while (h) {
        if (h->magic != HEAP_MAGIC) {
            PORT_Diag("[HEAP] %s: header magic gone at %p\n", tag, (void *)h);
            return 1;
        }
        if (*canary_of(h) != HEAP_CANARY) {
            PORT_Diag("[HEAP] %s: OVERFLOW after %lu-byte block at %p\n",
                      tag, h->size, (void *)(h + 1));
            bad = 1;
        }
        h = h->next;
    }

    return bad;
}

void PORT_HeapReport(const char *where)
{
    PORT_Diag("[MEM] %s: use=%luK peak=%luK blocks=%lu\n",
              where, heap_used / 1024UL, heap_peak / 1024UL, heap_blocks);
    PORT_HeapCheck(where);
}

/*
 * Every live block above a threshold, newest first. On a 2 MB machine the
 * question is never "how much is used" — the running total answers that — but
 * "by what", and the answer has to be available at the moment an allocation
 * fails rather than reconstructed afterwards.
 */
void PORT_HeapDump(unsigned long min_bytes)
{
    HeapHdr *h = heap_head;
    unsigned long shown = 0, counted = 0;

    PORT_Diag("[MEM] live blocks >= %luK, newest first:\n", min_bytes / 1024UL);
    while (h) {
        if (h->magic != HEAP_MAGIC) {
            PORT_Diag("[MEM]   list corrupt at %p\n", (void *)h);
            break;
        }
        if (h->size >= min_bytes) {
            PORT_Diag("[MEM]   %8lu  %6luK  %p\n",
                      h->size, h->size / 1024UL, (void *)(h + 1));
            shown += h->size;
            counted++;
        }
        h = h->next;
    }
    PORT_Diag("[MEM]   %lu blocks listed, %luK of %luK total\n",
              counted, shown / 1024UL, heap_used / 1024UL);
}

/*
 * The largest block that can still be allocated — the answer to Malloc(-1),
 * which the community source release answers with "not supported" so that
 * every pool in PERSO.C collapses to its clamped floor.
 *
 * There is no free-list walk to be had: libpsn00b's allocator does not expose
 * one. So the question is asked the only way it can be answered exactly, by
 * bisecting on what the allocator will actually grant. Twenty-odd malloc/free
 * pairs, once, at startup — and the number that comes back accounts for the
 * fragmentation a running total never would.
 */
unsigned long PORT_HeapLargestFree(void)
{
    unsigned long lo = 0;
    unsigned long hi = RAM_TOP - STACK_RESERVE - (unsigned long)_end;

    while (lo < hi) {
        unsigned long mid = lo + (hi - lo + 1) / 2;
        void *p = malloc((size_t)mid);

        if (p) {
            free(p);
            lo = mid;
        } else {
            hi = mid - 1;
        }

        if (hi - lo < 1024)     /* KB resolution is all anyone reads */
            break;
    }

    return lo;
}

void *PSX_malloc(unsigned int size)
{
    HeapHdr *h = (HeapHdr *)malloc(sizeof(HeapHdr) + CANARY_OFF(size)
                                  + sizeof(unsigned long));

    if (!h) {
        PORT_Diag("[MEM] malloc(%u) FAILED, use=%luK\n", size, heap_used / 1024UL);
        PORT_HeapDump(32 * 1024);
        return 0;
    }

    h->size = size;
    h->magic = HEAP_MAGIC;
    h->prev = 0;
    h->next = heap_head;
    if (heap_head)
        heap_head->prev = h;
    heap_head = h;

    *canary_of(h) = HEAP_CANARY;

    heap_used += size;
    heap_blocks++;
    if (heap_used > heap_peak)
        heap_peak = heap_used;

    return h + 1;
}

void *PSX_calloc(unsigned int n, unsigned int size)
{
    unsigned int total = n * size;
    void *p = PSX_malloc(total);

    if (p)
        memset(p, 0, total);

    return p;
}

void PSX_free(void *p)
{
    HeapHdr *h;

    if (!p)
        return;

    h = (HeapHdr *)p - 1;
    if (h->magic != HEAP_MAGIC) {
        PORT_Diag("[HEAP] free(%p): not one of ours, or already freed\n", p);
        return;
    }
    if (*canary_of(h) != HEAP_CANARY)
        PORT_Diag("[HEAP] free: OVERFLOW after %lu-byte block at %p\n",
                  h->size, p);

    if (h->prev)
        h->prev->next = h->next;
    else
        heap_head = h->next;
    if (h->next)
        h->next->prev = h->prev;

    heap_used -= h->size;
    heap_blocks--;
    h->magic = 0;

    free(h);
}

/*
 * The engine shrinks blocks in place and keeps using the old pointer:
 * Mshrink() and LoadMalloc_HQR() both do it. On the DS a reallocating shrink
 * moved the data and produced corruption that took a long time to find, so a
 * shrink here is a no-op that only updates the accounting.
 */
void *PSX_realloc(void *p, unsigned int size)
{
    HeapHdr *h;
    void *fresh;

    if (!p)
        return PSX_malloc(size);

    h = (HeapHdr *)p - 1;
    if (h->magic != HEAP_MAGIC) {
        PORT_Diag("[HEAP] realloc(%p): not one of ours\n", p);
        return 0;
    }

    if (size <= h->size) {
        heap_used -= h->size - size;
        h->size = size;
        *canary_of(h) = HEAP_CANARY;
        return p;
    }

    fresh = PSX_malloc(size);
    if (!fresh)
        return 0;
    memcpy(fresh, p, h->size);
    PSX_free(p);

    return fresh;
}

/*
 * DosMalloc/DosFree: the engine's DOS conventional-memory allocator, used for
 * BufSpeak and a few fixed blocks. Plain heap here; the handle is the pointer.
 */
void *DosMalloc(LONG size, ULONG *handle)
{
    void *p = PSX_malloc((unsigned int)size);

    if (handle)
        *handle = (ULONG)p;

    return p;
}

void DosFree(ULONG handle)
{
    PSX_free((void *)handle);
}

/* ══════════════════════════════════════════════════════════════════════════
 * The 50 Hz tick
 *
 * Root counter 1 counts hblanks: 15734 Hz on NTSC, 15625 on PAL. Divided down
 * it gives 49.95 / 49.92 Hz, against the 50.005 Hz the DS port produces from a
 * bus-clock divider. The engine's logic is written around a 50 Hz tick and is
 * not sensitive at this precision, but the number is recorded here because the
 * next person to chase a timing drift will want it.
 *
 * Timers 0 and 2 are left alone: psxgpu and psxspu use them.
 * ═════════════════════════════════════════════════════════════════════════ */

volatile ULONG TimerSystem = 0;
volatile ULONG TimerRef = 0;

UWORD NbFramePerSecond = 0;
UWORD WaitNbTicks = 0;
UWORD CmptFrame = 0;
UWORD Cmpt_18 = 0;

/*
 * Both clocks, not one.
 *
 * The engine has two, and on DOS the same interrupt drove both: TimerSystem
 * is the free-running one (AMBIANCE.C times CD tracks against it) and
 * TimerRef is the GAME clock, the one SaveTimer/RestoreTimer in P_ANIM.C
 * freeze by snapshotting and rolling back so a pause does not advance the
 * world.
 *
 * Only TimerSystem was being incremented, which meant TimerRef sat at zero
 * forever -- and TimerPause() spins on TimerRef with only `if (Key OR Fire OR
 * Joy) return;` for company. So the port hung on the first bumper screen and
 * could only be got past by pressing a pad button, which is exactly what it
 * had been doing since M1. MainLoop's frame regulator waits on TimerRef too,
 * so this was going to stop the game dead the moment the loop ran.
 */
static void tick_isr(void)
{
    TimerSystem++;
    TimerRef++;
    PORT_ScanInput();
}

void InitTimer(void)
{
    int target = (GetVideoMode() == MODE_PAL) ? 313 : 315;

    TimerSystem = 0;
    TimerRef = 0;

    TIMER_CTRL(1)   = 0x0158;   /* hblank source, reset on target, IRQ on target */
    TIMER_RELOAD(1) = (unsigned short)target;
    TIMER_VALUE(1)  = 0;

    InterruptCallback(IRQ_TIMER1, &tick_isr);
}

void ClearTimer(void)
{
    InterruptCallback(IRQ_TIMER1, 0);
    TIMER_CTRL(1) = 0;
}

void SetTimer(WORD divisor)
{
    (void)divisor;      /* the engine never calls this in the CD build */
}

WORD GetTimer(void)
{
    return (WORD)TimerSystem;
}

/*
 * Microseconds, for the measurements the port is being built on.
 *
 * Timer 1 already counts hblanks and is reset to zero on every 50 Hz tick, so
 * the two together are one clock: TimerSystem gives the whole ticks and the
 * counter gives the 63.6 us hblanks inside the current one. Timers 0 and 2
 * belong to psxgpu and psxspu, and the CPU has no cycle counter, so this is
 * the whole of what the machine offers.
 *
 * Resolution is one hblank. That is coarse against a 20 ms frame and perfectly
 * adequate against AffGrille, which is expected to take fifteen of them.
 */
unsigned long PORT_Micros(void)
{
    unsigned long ticks, raw, hz, per_tick;

    hz = (GetVideoMode() == MODE_PAL) ? 15625UL : 15734UL;
    per_tick = (GetVideoMode() == MODE_PAL) ? 313UL : 315UL;

    /* The tick can land between the two reads, and then the counter belongs
     * to the new tick while `ticks` belongs to the old one. Re-read. */
    do {
        ticks = TimerSystem;
        raw = TIMER_VALUE(1) & 0xffffUL;
    } while (ticks != TimerSystem);

    return (unsigned long)((((unsigned long long)ticks * per_tick + raw)
                            * 1000000ULL) / hz);
}

void InitSystem(void) {}
void ClearSystem(void) {}

/* ══════════════════════════════════════════════════════════════════════════
 * Input
 *
 * The engine speaks DOS: a keyboard scancode, a joystick bitfield and a fire
 * bitfield that doubles as the modifier keys. The DS port mapped those onto a
 * d-pad and six buttons; the same mapping works here, and the PlayStation pad
 * has the buttons the DS was short of.
 *
 * M1 keeps the plumbing and leaves the mapping for M6, where it can be tried
 * against a running game instead of guessed at.
 * ═════════════════════════════════════════════════════════════════════════ */

volatile UWORD Key = 0;
volatile UWORD FuncKey = 0;
volatile UWORD Joy = 0;
volatile UWORD Fire = 0;
UWORD AsciiMode = 0;
volatile LONG Click = 0;
volatile LONG Mouse_X = 0;
volatile LONG Mouse_Y = 0;

static unsigned char pad_buff[2][34];
static int pad_started;

static UWORD ascii_ring[16];
static int ascii_r, ascii_w;

void PORT_ScanInput(void)
{
    const PADTYPE *pad = (const PADTYPE *)pad_buff[0];
    UWORD joy = 0, fire = 0;
    unsigned short btn;

    if (!pad_started || pad->stat)
        return;

    btn = ~pad->btn;

    if (btn & PAD_UP)    joy |= 1;
    if (btn & PAD_DOWN)  joy |= 2;
    if (btn & PAD_LEFT)  joy |= 4;
    if (btn & PAD_RIGHT) joy |= 8;

    if (btn & PAD_CROSS)    fire |= 1;      /* action (DOS space)         */
    if (btn & PAD_CIRCLE)   fire |= 2;      /* validate (DOS return)      */
    if (btn & PAD_L1)       fire |= 4;      /* DOS ctrl: behaviour panel  */
    if (btn & PAD_R1)       fire |= 8;      /* DOS alt: throw             */
    if (btn & PAD_TRIANGLE) fire |= 32;     /* DOS shift: inventory       */

    Joy = joy;
    Fire = fire;

    /* Esc, and it has to go back to zero on release. The engine reads Key as
     * "the scancode down right now", and a latched value makes every
     * TimerPause() after the first one return instantly -- which is why one
     * press appeared to unstick the whole boot sequence rather than just the
     * screen it was on. */
    Key = (UWORD)((btn & PAD_START) ? 1 : 0);
}

void InitKeyboard(void)
{
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    pad_started = 1;
    ascii_r = ascii_w = 0;
}

void ClearKeyboard(void)
{
    pad_started = 0;
}

UWORD GetAscii(void)
{
    UWORD v;

    if (ascii_r == ascii_w)
        return 0;
    v = ascii_ring[ascii_r];
    ascii_r = (ascii_r + 1) & 15;

    return v;
}

void ClearAsciiBuffer(void)
{
    ascii_r = ascii_w = 0;
}

/* Mouse: there is none, and the engine only needs it not to crash. */
void GetMouseDep(void) {}
void ShowMouse(long flag) { (void)flag; }
void AffMouse(void) {}
void SetMouse(short flag) { (void)flag; }
void InitMouse(void) {}
void ClearMouse(void) {}
void SetMousePos(long x, long y) { Mouse_X = x; Mouse_Y = y; }
void SetMouseBox(long x0, long y0, long x1, long y1)
{
    (void)x0; (void)y0; (void)x1; (void)y1;
}
void SetMouseSpeed(long sx, long sy) { (void)sx; (void)sy; }

/* ══════════════════════════════════════════════════════════════════════════
 * The DOS filesystem calls
 *
 * SYS_FindFirst enumerates save games and voice banks. On a CD there is
 * nothing to enumerate that is not known at build time, and saves live on the
 * Memory Card, so these report an empty directory until M8 wires the card in.
 * ═════════════════════════════════════════════════════════════════════════ */

int SYS_FindFirst(const char *pattern, unsigned attr, SYS_FileInfo *info)
{
    (void)pattern; (void)attr;
    if (info)
        memset(info, 0, sizeof(*info));

    return 1;       /* no match */
}

int SYS_FindNext(SYS_FileInfo *info)
{
    (void)info;
    return 1;
}

void SYS_FindClose(SYS_FileInfo *info)
{
    (void)info;
}

unsigned SYS_GetDrive(void)
{
    return 3;       /* C: — the engine only ever compares it to itself */
}

void SYS_SetDrive(unsigned drive, unsigned *total_drives)
{
    (void)drive;
    if (total_drives)
        *total_drives = 1;
}

unsigned long SYS_GetDiskFreeSpace(void)
{
    /* One Memory Card block per save, and the engine only checks that this is
     * larger than the save it is about to write. */
    return 128UL * 1024UL;
}

unsigned long SYS_ComputeTime(void)
{
    /* No RTC. A monotonic counter keeps save ordering consistent within a
     * session, which is all the engine uses the value for. */
    return (unsigned long)TimerSystem;
}

void Touch(UBYTE *filename)
{
    (void)filename;
}
