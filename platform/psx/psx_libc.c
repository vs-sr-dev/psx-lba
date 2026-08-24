/*
 * psx_libc.c — the C library the engine expects and libpsn00b does not have.
 *
 * libpsn00b is a deliberately small freestanding library: it has memcpy,
 * snprintf and malloc, and stops. The 1994 engine was written against Watcom's
 * DOS libc and uses a wider surface than that — including a few names
 * (`itoa`, `_splitpath`, `stricmp`) that were never standard anywhere but DOS.
 *
 * Nothing here is a general-purpose implementation. Each function does what
 * the engine's actual call sites need and says so where that matters.
 *
 * The path helpers are derived from the DS port's platform/nds/nds_sys.c,
 * which had to solve the same problem against newlib.
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"

/* ── case-insensitive compares ───────────────────────────────────────────── */
static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int stricmp(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int strcmpi(const char *a, const char *b)
{
    return stricmp(a, b);
}

int strnicmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

/* ── number conversion ───────────────────────────────────────────────────── */
long atol(const char *s)
{
    long sign = 1, v = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');

    return v * sign;
}

int atoi(const char *s)
{
    return (int)atol(s);
}

char *ultoa(unsigned long value, char *buf, int radix)
{
    char tmp[33];
    int i = 0, j = 0;

    if (radix < 2 || radix > 36) {
        buf[0] = 0;
        return buf;
    }
    if (!value)
        tmp[i++] = '0';
    while (value) {
        unsigned long d = value % (unsigned long)radix;

        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        value /= (unsigned long)radix;
    }
    while (i)
        buf[j++] = tmp[--i];
    buf[j] = 0;

    return buf;
}

char *ltoa(long value, char *buf, int radix)
{
    if (radix == 10 && value < 0) {
        buf[0] = '-';
        ultoa((unsigned long)(-value), buf + 1, 10);
        return buf;
    }
    return ultoa((unsigned long)value, buf, radix);
}

char *itoa(int value, char *buf, int radix)
{
    return ltoa((long)value, buf, radix);
}

/*
 * pow() — the engine's entire use of libm is GIF.C computing a colour table
 * size as pow(2, n + 1.0), with n in 0..7. Repeated multiplication covers
 * that exactly and avoids linking a soft-float transcendental into a 2 MB
 * machine. A non-integer or negative exponent returns 0, loudly.
 */
double pow(double base, double exponent)
{
    int e = (int)exponent;
    double r = 1.0;

    if ((double)e != exponent || e < 0) {
        PORT_Diag("pow: unsupported exponent, GIF.C is the only caller\n");
        return 0.0;
    }
    while (e--)
        r *= base;

    return r;
}

/* ── qsort: insertion sort ───────────────────────────────────────────────── *
 * The engine sorts short lists (directory entries, menu items). Insertion
 * sort is smaller than any partitioning scheme and faster at these sizes; the
 * one sort that is performance-critical, the polygon depth sort, has its own
 * hand-written hybrid in p_ob_iso.c and does not come through here.
 */
void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *))
{
    char *a = (char *)base;
    size_t i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && cmp(a + (j - 1) * size, a + j * size) > 0; j--) {
            char *x = a + (j - 1) * size, *y = a + j * size;
            size_t k;

            for (k = 0; k < size; k++) {
                char t = x[k];

                x[k] = y[k];
                y[k] = t;
            }
        }
    }
}

/* ── process / environment ───────────────────────────────────────────────── */
char *getenv(const char *name)
{
    (void)name;
    return 0;       /* no environment: the engine falls back to its defaults */
}

void exit(int code)
{
    PORT_Panic("exit(%d) — the engine asked to quit", code);
}

/* ── working directory ───────────────────────────────────────────────────── *
 * DISKFUNC.C saves and restores the cwd around save-game access. There is no
 * such concept here; the CD root is the only directory there is.
 */
static char cwd[8] = "\\";

char *getcwd(char *buf, unsigned int size)
{
    if (!buf || size < sizeof(cwd))
        return 0;
    strcpy(buf, cwd);

    return buf;
}

int chdir(const char *path)
{
    (void)path;
    return 0;
}

/* ── DOS path splitting ──────────────────────────────────────────────────── *
 * Derived from platform/nds/nds_sys.c: same job, same call sites, and the
 * engine feeds it DOS paths with backslashes and drive letters throughout.
 */
static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void _splitpath(const char *path, char *drive, char *dir,
                char *fname, char *ext)
{
    const char *p, *slash, *dot, *body;

    if (drive)
        drive[0] = 0;
    if (dir)
        dir[0] = 0;
    if (fname)
        fname[0] = 0;
    if (ext)
        ext[0] = 0;
    if (!path)
        return;

    p = path;
    if (is_alpha((unsigned char)p[0]) && p[1] == ':') {
        if (drive) {
            drive[0] = p[0];
            drive[1] = ':';
            drive[2] = 0;
        }
        p += 2;
    }

    slash = 0;
    for (body = p; *body; body++)
        if (*body == '/' || *body == '\\')
            slash = body;

    if (slash) {
        if (dir) {
            size_t n = (size_t)(slash - p) + 1;

            if (n > 255)
                n = 255;
            memcpy(dir, p, n);
            dir[n] = 0;
        }
        p = slash + 1;
    }

    dot = 0;
    for (body = p; *body; body++)
        if (*body == '.')
            dot = body;

    if (dot) {
        if (fname) {
            size_t n = (size_t)(dot - p);

            if (n > 255)
                n = 255;
            memcpy(fname, p, n);
            fname[n] = 0;
        }
        if (ext) {
            strncpy(ext, dot, 255);
            ext[255] = 0;
        }
    } else if (fname) {
        strncpy(fname, p, 255);
        fname[255] = 0;
    }
}

void _makepath(char *path, const char *drive, const char *dir,
               const char *fname, const char *ext)
{
    path[0] = 0;
    if (drive && drive[0]) {
        strcat(path, drive);
        if (path[strlen(path) - 1] != ':')
            strcat(path, ":");
    }
    if (dir && dir[0]) {
        strcat(path, dir);
        if (path[strlen(path) - 1] != '/' && path[strlen(path) - 1] != '\\')
            strcat(path, "\\");
    }
    if (fname)
        strcat(path, fname);
    if (ext && ext[0]) {
        if (ext[0] != '.')
            strcat(path, ".");
        strcat(path, ext);
    }
}
