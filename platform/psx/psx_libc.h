/*
 * psx_libc.h — declarations for the C library gaps filled in psx_libc.c.
 *
 * Every name here is a real gap in libpsn00b that the 1994 engine uses, not a
 * precaution: the DOS/Watcom libc it was written against is wider than a
 * freestanding PlayStation library, and a few of these (itoa, _splitpath,
 * stricmp) were never standard anywhere except DOS.
 */
#ifndef PSX_LIBC_H
#define PSX_LIBC_H

int   stricmp(const char *a, const char *b);
int   strcmpi(const char *a, const char *b);
int   strnicmp(const char *a, const char *b, unsigned int n);

int   atoi(const char *s);
long  atol(const char *s);
char *itoa(int value, char *buf, int radix);
char *ltoa(long value, char *buf, int radix);
char *ultoa(unsigned long value, char *buf, int radix);

char *getenv(const char *name);
void  exit(int code);

char *getcwd(char *buf, unsigned int size);
int   chdir(const char *path);

void _splitpath(const char *path, char *drive, char *dir,
                char *fname, char *ext);
void _makepath(char *path, const char *drive, const char *dir,
               const char *fname, const char *ext);

#endif /* PSX_LIBC_H */
