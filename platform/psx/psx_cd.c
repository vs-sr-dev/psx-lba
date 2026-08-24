/*
 * psx_cd.c — the stdio the engine is built on, over the CD.
 *
 * libpsn00b has no FILE and no fopen, so the whole surface is ours. The engine
 * opens a dozen HQR archives, seeks around them and reads compressed blocks
 * out of the middle, so this has to be a real random-access file layer, not a
 * load-it-all shim.
 *
 * M1 scope: the handle bookkeeping is real, the ISO9660 backing is not. Every
 * open fails cleanly and says so on the TTY, which is exactly what M1 wants —
 * the engine boots, walks its init path, and tells us where it needs data.
 * The CD itself is M2.
 *
 * The handle validation is not defensive programming for its own sake. The DS
 * port found that the engine relies on msvcrt's tolerance of stale and
 * already-closed FILE pointers, where newlib simply data-aborts; a 1994
 * codebase that shipped against one libc has that behaviour baked into it.
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "psx_file.h"

#define MAX_FILES 8
#define FILE_MAGIC 0x1BA1F11EUL

struct PSX_FILE {
    unsigned long magic;
    long          pos;
    long          size;
    int           in_use;
    char          name[32];
};

static PSX_FILE files[MAX_FILES];

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
 * The engine speaks DOS: backslashes, mixed case, and the occasional ".\" in
 * front. ISO9660 wants none of that. Normalising here rather than at every
 * call site is what the DS port did, for the same reason.
 */
static void normalise(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    if (src[0] == '.' && (src[1] == '\\' || src[1] == '/'))
        src += 2;

    while (*src && i < n - 1) {
        char c = *src++;

        if (c == '\\')
            c = '/';
        if (c >= 'a' && c <= 'z')
            c = (char)(c - ('a' - 'A'));
        dst[i++] = c;
    }
    dst[i] = 0;
}

PSX_FILE *PSX_fopen(const char *name, const char *mode)
{
    char clean[32];

    (void)mode;

    if (!name)
        return 0;

    normalise(clean, sizeof(clean), name);

    /* M2 opens this for real. Until then, say what was wanted: the list of
     * names the engine asks for, in order, is the specification for the CD
     * image the build tools will have to produce. */
    PORT_Diag("[FS] fopen(\"%s\") -> not present (M1: no CD yet)\n", clean);

    return 0;
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
    (void)p;
    if (!valid(f, "fread"))
        return 0;
    (void)sz; (void)n;

    return 0;
}

size_t PSX_fwrite(const void *p, size_t sz, size_t n, PSX_FILE *f)
{
    (void)p; (void)sz; (void)n;
    if (!valid(f, "fwrite"))
        return 0;

    /* A CD is read-only. Saves go to the Memory Card in M8, through their own
     * path, not through this one. */
    return 0;
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
    if (!valid(f, "fgetc"))
        return EOF;

    return EOF;
}

char *PSX_fgets(char *buf, int n, PSX_FILE *f)
{
    (void)buf; (void)n;
    if (!valid(f, "fgets"))
        return 0;

    return 0;
}

int PSX_feof(PSX_FILE *f)
{
    if (!valid(f, "feof"))
        return 1;

    return f->pos >= f->size;
}
