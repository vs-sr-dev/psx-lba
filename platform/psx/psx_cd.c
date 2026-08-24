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

/* One shared sector cache. Per-file caches would cost 2 KB each and the engine
 * reads one archive at a time; if that ever stops being true, the symptom is
 * slow loading, not wrong data. */
static unsigned long cache_buf[SECTOR / 4];
static int cache_lba = -1;

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

void PORT_CdInit(void)
{
    if (cd_up)
        return;

    if (!CdInit()) {
        PORT_Diag("[FS] CdInit failed — no drive?\n");
        return;
    }
    cd_up = 1;
    cache_lba = -1;
    PORT_Diag("[FS] CD ready\n");

    /* Sector 16 is the ISO9660 primary volume descriptor and its bytes 1..5
     * are the literal "CD001". Reading it directly separates two failures that
     * look identical from the outside: a drive that cannot read, and a file
     * system layer that cannot parse. */
    if (read_sectors(16, cache_buf, 1)) {
        const unsigned char *pvd = (const unsigned char *)cache_buf;

        PORT_Diag("[FS] PVD type=%d id=%c%c%c%c%c\n",
                  pvd[0], pvd[1], pvd[2], pvd[3], pvd[4], pvd[5]);
        cache_lba = 16;
    } else {
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

static int cache_sector(int lba)
{
    if (cache_lba == lba)
        return 1;
    if (!read_sectors(lba, cache_buf, 1)) {
        PORT_Diag("[FS] read error at sector %d\n", lba);
        cache_lba = -1;
        return 0;
    }
    cache_lba = lba;

    return 1;
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
            if (cache_lba >= sector && cache_lba < sector + count)
                cache_lba = -1;

            done += (long)count * SECTOR;
            dst += (long)count * SECTOR;
            f->pos += (long)count * SECTOR;
        } else {
            long chunk = SECTOR - offset;

            if (chunk > left)
                chunk = left;
            if (!cache_sector(sector))
                break;
            memcpy(dst, (const unsigned char *)cache_buf + offset,
                   (size_t)chunk);
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
