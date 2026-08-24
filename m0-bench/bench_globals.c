/*
 * Globals the SVGA layer expects the platform to provide.
 *
 * They live in their own translation unit because translate.h declares
 * TabOffLine as a scalar (`extern ULONG TabOffLine`) while the engine defines
 * it as the 480-entry line offset table and reads it back through the
 * TABOFFLINE cast. That mismatch is original 1994 behaviour, it works, and the
 * DS port keeps it -- but GCC will not accept both spellings in one file.
 */

typedef unsigned long int ULONG;
typedef signed short int  WORD;
typedef unsigned char     UBYTE;

WORD ClipXmin = 0;
WORD ClipYmin = 0;
WORD ClipXmax = 639;
WORD ClipYmax = 479;

WORD Screen_X = 640;
WORD Screen_Y = 480;

UBYTE Text_Ink = 15;
UBYTE Text_Paper = 0;

ULONG TabOffLine[480];

/* The engine's 640x480 8bpp render target: 300 KB, 15% of a PlayStation. */
UBYTE Log_buffer[640 * 480];
UBYTE *Log = Log_buffer;
