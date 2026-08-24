/* direct.h — a MinGW header, used by game/DISKFUNC.C for getcwd/chdir. There
 * is no working directory on a PlayStation; psx_libc.c answers with the CD
 * root and accepts any chdir. */
#ifndef PSX_COMPAT_DIRECT_H
#define PSX_COMPAT_DIRECT_H

char *getcwd(char *buf, unsigned int size);
int   chdir(const char *path);

#endif
