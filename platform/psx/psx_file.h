/*
 * psx_file.h — the stdio surface the engine uses, which PSn00bSDK does not
 * have.
 *
 * libpsn00b ships snprintf and malloc and stops there: there is no FILE, no
 * fopen, no fread. The engine's file layer (LIB_SYS/FILES.C, HQR.C,
 * LOADSAVE.C, DEF_FILE.C) is built on all of them, so the platform provides
 * them here and implements them over the CD in psx_cd.c.
 *
 * The DS port learned the hard way that the engine relies on msvcrt's
 * tolerance of bad or already-closed handles, where newlib data-aborts. This
 * implementation validates every handle for the same reason.
 *
 * M1 note: the bodies are stubs that fail cleanly. Real ISO9660 access is M2.
 */
#ifndef PSX_FILE_H
#define PSX_FILE_H

#include <stdlib.h>     /* size_t */

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef EOF
#define EOF (-1)
#endif

typedef struct PSX_FILE PSX_FILE;

#define FILE PSX_FILE

PSX_FILE *PSX_fopen(const char *name, const char *mode);
int       PSX_fclose(PSX_FILE *f);
size_t    PSX_fread(void *p, size_t sz, size_t n, PSX_FILE *f);
size_t    PSX_fwrite(const void *p, size_t sz, size_t n, PSX_FILE *f);
int       PSX_fseek(PSX_FILE *f, long off, int whence);
long      PSX_ftell(PSX_FILE *f);
int       PSX_remove(const char *name);
int       PSX_fgetc(PSX_FILE *f);
char     *PSX_fgets(char *buf, int n, PSX_FILE *f);
int       PSX_feof(PSX_FILE *f);

#define fopen  PSX_fopen
#define fclose PSX_fclose
#define fread  PSX_fread
#define fwrite PSX_fwrite
#define fseek  PSX_fseek
#define ftell  PSX_ftell
#define remove PSX_remove
#define fgetc  PSX_fgetc
#define fgets  PSX_fgets
#define feof   PSX_feof

#endif /* PSX_FILE_H */
