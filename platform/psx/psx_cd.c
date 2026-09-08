/*
 * psx_cd.c — the stdio the engine is built on, over ISO9660.
 *
 * libpsn00b has no FILE and no fopen, so the whole surface is ours. The engine
 * opens a dozen HQR archives, seeks into the middle of them and reads one
 * compressed block at a time, so this has to be a real random-access file
 * layer rather than a load-it-all shim — and on a 2 MB machine, "load it all"
 * was never available anyway.
 *
 * Three things shape the implementation:
 *
 *   - A CD reads in 2048-byte sectors. Every seek the engine makes lands in
 *     the middle of one, so there is a one-sector cache; but the common case
 *     is an HQR block of several KB, and that bypasses the cache and DMAs
 *     straight into the caller's buffer.
 *
 *   - The DMA needs a 32-bit aligned destination. The engine's buffers usually
 *     are, since they come from malloc, but "usually" is not a guarantee to
 *     build on: an unaligned destination falls back to the cached path.
 *
 *   - The handle validation is not defensive programming for its own sake. The
 *     DS port found that the engine relies on msvcrt's tolerance of stale and
 *     already-closed FILE pointers, where newlib simply data-aborts. A 1994
 *     codebase that shipped against one libc has that behaviour baked in.
 */

#include <stdlib.h>
#include <string.h>

#include <psxcd.h>

#include "port.h"
#include "psx_file.h"

#define MAX_FILES   8
#define FILE_MAGIC  0x1BA1F11EUL
#define SECTOR      2048

struct PSX_FILE {
    unsigned long magic;
    int           lba;          /* first sector of the file */
    long          size;
    long          pos;
    int           in_use;
    char          name[20];
};

static PSX_FILE files[MAX_FILES];

/*
 * A shared sector cache: four windows of four consecutive sectors, LRU.
 *
 * It held ONE sector until M8, with a note saying that if the engine ever
 * stopped reading one thing at a time the symptom would be slow loading. It
 * does not read one thing at a time, and the loading was slow.
 *
 * Two things were wrong and both were measured on an HQR miss, which is what
 * the first frame of every animation costs:
 *
 *   one sector was not enough. A miss reads the archive's offset table --
 *   sector 0 -- and then the entry, tens of thousands of sectors away. The
 *   second read evicted the first, so every miss paid for sector 0 again.
 *   A sector that has to be sought to costs about 100 ms here, so that was
 *   200 ms of a 201 ms miss, against 0.06 ms actually reading the 40 to 1400
 *   bytes wanted. Eight independent ways took it to 101.
 *
 *   one sector at a time was not enough either. The 100 ms is the drive
 *   command and the seek, not the transfer: the sectors after it are nearly
 *   free. HQR entries for consecutive indices are consecutive in the archive
 *   and an animation is about a kilobyte, so a four-sector window holds
 *   roughly the next eight animations the game will ask for.
 *
 * Windows are aligned to four sectors so that a lookup is one comparison and
 * the same sector is never held twice.
 */
#define CACHE_WIN       2       /* sectors per window, and their alignment  */
#define CACHE_WINDOWS   8       /* 32 KB, all told                          */

static unsigned long cache_buf[CACHE_WINDOWS][CACHE_WIN * (SECTOR / 4)];
static int  cache_base[CACHE_WINDOWS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static unsigned long cache_used[CACHE_WINDOWS]; /* for the LRU              */
static unsigned long cache_clock;

static int cd_up;

static PSX_FILE *valid(PSX_FILE *f, const char *who)
{
    int i;

    if (!f) {
        PORT_Diag("[FS] %s(NULL)\n", who);
        return 0;
    }
    for (i = 0; i < MAX_FILES; i++)
        if (&files[i] == f)
            break;
    if (i == MAX_FILES || f->magic != FILE_MAGIC || !f->in_use) {
        PORT_Diag("[FS] %s: stale or foreign handle %p\n", who, (void *)f);
        return 0;
    }

    return f;
}

/*
 * The engine speaks DOS: backslashes, mixed case, sometimes a leading ".\".
 * ISO9660 wants uppercase, a leading backslash and a ";1" version suffix.
 * Normalising here rather than at forty call sites is what the DS port did,
 * for the same reason.
 */
static void normalise(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    while (*src == '.' && (src[1] == '\\' || src[1] == '/'))
        src += 2;
    while (*src == '\\' || *src == '/')
        src++;

    dst[i++] = '\\';
    while (*src && i < n - 3) {
        char c = *src++;

        if (c == '/')
            c = '\\';
        if (c >= 'a' && c <= 'z')
            c = (char)(c - ('a' - 'A'));
        dst[i++] = c;
    }
    dst[i++] = ';';
    dst[i++] = '1';
    dst[i] = 0;
}

static int read_sectors(int lba, void *dst, int count);
static const unsigned char *cache_sector(int lba);

void PORT_CdInit(void)
{
    int i;

    if (cd_up)
        return;

    if (!CdInit()) {
        PORT_Diag("[FS] CdInit failed — no drive?\n");
        return;
    }
    cd_up = 1;
    for (i = 0; i < CACHE_WINDOWS; i++)
        cache_base[i] = -1;
    PORT_Diag("[FS] CD ready\n");

    /* Sector 16 is the ISO9660 primary volume descriptor and its bytes 1..5
     * are the literal "CD001". Reading it directly separates two failures that
     * look identical from the outside: a drive that cannot read, and a file
     * system layer that cannot parse. */
    {
        const unsigned char *pvd = cache_sector(16);

        if (pvd)
            PORT_Diag("[FS] PVD type=%d id=%c%c%c%c%c\n",
                      pvd[0], pvd[1], pvd[2], pvd[3], pvd[4], pvd[5]);
        else
            PORT_Diag("[FS] cannot read sector 16 — the drive is not reading\n");
    }

    /* List the root once at boot. It costs one directory read, and it answers
     * in one line each the question that otherwise takes an afternoon: is this
     * the disc we think it is, and are the names what we think they are. */
    {
        CdlDIR *dir = CdOpenDir("\\");
        CdlFILE entry;
        int n = 0;

        if (!dir) {
            PORT_Diag("[FS] cannot open the root (iso error %d)\n",
                      CdIsoError());
            return;
        }
        while (CdReadDir(dir, &entry)) {
            PORT_Diag("[FS]   %-14s %8d\n", entry.name, entry.size);
            n++;
        }
        CdCloseDir(dir);
        PORT_Diag("[FS] %d entries in the root\n", n);
    }
}

/* ── one sector, cached ──────────────────────────────────────────────────── */
static int read_sectors(int lba, void *dst, int count)
{
    CdlLOC loc;

    CdIntToPos(lba, &loc);
    if (!CdControl(CdlSetloc, &loc, 0))
        return 0;
    if (!CdRead(count, (unsigned long *)dst, CdlModeSpeed))
        return 0;
    if (CdReadSync(0, 0) < 0)
        return 0;

    return 1;
}

/* The sector, from the cache or from the drive. Null if the drive refused. */
static const unsigned char *cache_sector(int lba)
{
    int base = lba & ~(CACHE_WIN - 1);
    int i, victim = 0;

    for (i = 0; i < CACHE_WINDOWS; i++)
        if (cache_base[i] == base) {
            cache_used[i] = ++cache_clock;
            return (const unsigned char *)cache_buf[i]
                 + (lba - base) * SECTOR;
        }

    /* an empty window if there is one, the oldest otherwise */
    for (i = 0; i < CACHE_WINDOWS; i++)
        if (cache_base[i] < 0) {
            victim = i;
            break;
        } else if (cache_used[i] < cache_used[victim]) {
            victim = i;
        }

    /* The read-ahead is what makes this worth having, but a window that runs
     * off the end of the disc would fail as a whole and lose the sector that
     * was actually asked for, so fall back to the one sector on any error. */
    if (read_sectors(base, cache_buf[victim], CACHE_WIN)) {
        cache_base[victim] = base;
    } else if (read_sectors(lba, cache_buf[victim], 1)) {
        cache_base[victim] = lba;
        base = lba;
    } else {
        PORT_Diag("[FS] read error at sector %d\n", lba);
        cache_base[victim] = -1;
        return 0;
    }
    cache_used[victim] = ++cache_clock;

    return (const unsigned char *)cache_buf[victim] + (lba - base) * SECTOR;
}

/* Whole sectors have just gone to the caller behind the cache's back. */
static void cache_drop(int lba, int count)
{
    int i;

    for (i = 0; i < CACHE_WINDOWS; i++)
        if (cache_base[i] >= 0
            && cache_base[i] + CACHE_WIN > lba
            && cache_base[i] < lba + count)
            cache_base[i] = -1;
}

/* ── the stdio surface ───────────────────────────────────────────────────── */
PSX_FILE *PSX_fopen(const char *name, const char *mode)
{
    char iso[24];
    CdlFILE entry;
    PSX_FILE *f;
    int i;

    if (!name)
        return 0;

    /* A CD is read-only. Saves go to the Memory Card through their own path
     * (M8), never through here, so a write open is a real error rather than
     * something to paper over. */
    if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
        PORT_Diag("[FS] fopen(\"%s\", \"%s\") — the disc is read-only\n",
                  name, mode);
        return 0;
    }

    if (!cd_up)
        PORT_CdInit();

    normalise(iso, sizeof(iso), name);

    if (!CdSearchFile(&entry, iso)) {
        PORT_Diag("[FS] fopen(\"%s\") -> %s not on the disc (iso error %d)\n",
                  name, iso, CdIsoError());
        return 0;
    }

    for (i = 0; i < MAX_FILES; i++)
        if (!files[i].in_use)
            break;
    if (i == MAX_FILES) {
        PORT_Diag("[FS] fopen(\"%s\") — all %d handles are open\n",
                  name, MAX_FILES);
        return 0;
    }

    f = &files[i];
    f->magic = FILE_MAGIC;
    f->lba = CdPosToInt(&entry.pos);
    f->size = entry.size;
    f->pos = 0;
    f->in_use = 1;
    strncpy(f->name, iso, sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = 0;

    return f;
}

int PSX_fclose(PSX_FILE *f)
{
    if (!valid(f, "fclose"))
        return EOF;

    f->in_use = 0;
    f->magic = 0;

    return 0;
}

size_t PSX_fread(void *p, size_t sz, size_t n, PSX_FILE *f)
{
    unsigned char *dst = (unsigned char *)p;
    long total, done = 0;

    if (!valid(f, "fread") || !p)
        return 0;

    total = (long)(sz * n);
    if (total <= 0)
        return 0;
    if (f->pos + total > f->size)
        total = f->size - f->pos;
    if (total <= 0)
        return 0;

    while (done < total) {
        int  sector = f->lba + (int)(f->pos / SECTOR);
        long offset = f->pos % SECTOR;
        long left = total - done;

        if (offset == 0 && left >= SECTOR && !(((unsigned long)dst) & 3)) {
            /* Whole sectors, aligned destination: straight to the caller. */
            int count = (int)(left / SECTOR);

            if (!read_sectors(sector, dst, count))
                break;
            /* The shared cache may now be stale for these sectors; it is not
             * wrong, but it is no longer the one we just bypassed. */
            cache_drop(sector, count);

            done += (long)count * SECTOR;
            dst += (long)count * SECTOR;
            f->pos += (long)count * SECTOR;
        } else {
            long chunk = SECTOR - offset;
            const unsigned char *src;

            if (chunk > left)
                chunk = left;
            if (!(src = cache_sector(sector)))
                break;
            memcpy(dst, src + offset, (size_t)chunk);
            done += chunk;
            dst += chunk;
            f->pos += chunk;
        }
    }

    return sz ? (size_t)(done / (long)sz) : 0;
}

size_t PSX_fwrite(const void *p, size_t sz, size_t n, PSX_FILE *f)
{
    (void)p; (void)sz; (void)n;
    if (!valid(f, "fwrite"))
        return 0;

    return 0;   /* read-only medium; see the note in PSX_fopen */
}

int PSX_fseek(PSX_FILE *f, long off, int whence)
{
    if (!valid(f, "fseek"))
        return -1;

    switch (whence) {
    case SEEK_SET: f->pos = off; break;
    case SEEK_CUR: f->pos += off; break;
    case SEEK_END: f->pos = f->size + off; break;
    default:       return -1;
    }
    if (f->pos < 0)
        f->pos = 0;
    if (f->pos > f->size)
        f->pos = f->size;

    return 0;
}

long PSX_ftell(PSX_FILE *f)
{
    if (!valid(f, "ftell"))
        return -1L;

    return f->pos;
}

int PSX_remove(const char *name)
{
    (void)name;
    return -1;
}

int PSX_fgetc(PSX_FILE *f)
{
    unsigned char c;

    if (!valid(f, "fgetc"))
        return EOF;
    if (PSX_fread(&c, 1, 1, f) != 1)
        return EOF;

    return (int)c;
}

char *PSX_fgets(char *buf, int n, PSX_FILE *f)
{
    int i = 0, c;

    if (!valid(f, "fgets") || n <= 1)
        return 0;

    while (i < n - 1) {
        c = PSX_fgetc(f);
        if (c == EOF)
            break;
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    buf[i] = 0;

    return i ? buf : 0;
}

int PSX_feof(PSX_FILE *f)
{
    if (!valid(f, "feof"))
        return 1;

    return f->pos >= f->size;
}
