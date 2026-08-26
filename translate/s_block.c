/*----------------------------------------------------------------------------
 *  s_block.c — C translation of LIB386/LIB_SVGA/S_BLOCK.ASM (Adeline 1993)
 *
 *  Plain rectangle copies (the 2-lines-per-iteration unrolling of the
 *  original is a pure optimisation: a per-line memcpy is byte identical).
 *  No clipping: coordinates must be valid.
 *  Note: in SaveBlock/RestoreBlock the last two parameters are named dx/dy in
 *  the header but are actually used as x1/y1 (width = dx - x + 1), exactly
 *  like the ASM.
 *---------------------------------------------------------------------------*/
#include <string.h>

#include "translate.h"

#ifdef PORT_PSX_BG_VRAM
extern UBYTE *Screen;
extern UBYTE *Log;
void PORT_BgStore(LONG x0, LONG y0, LONG x1, LONG y1);
void PORT_BgFetch(LONG x0, LONG y0, LONG x1, LONG y1);
#endif

/* ASM: CopyBlock */
void CopyBlock(LONG x0, LONG y0, LONG x1, LONG y1, void *src,
               LONG xd, LONG yd, void *dst)
{
	ULONG *pTabOffLine = TABOFFLINE;
	UBYTE *pSrc;
	UBYTE *pDest;
	LONG width, lines;

#ifdef PORT_PSX_BG_VRAM
	/* PORT: `Screen`, the clean background, is in VRAM on this machine --
	 * see psx_video.c. ClsBoxes restores dirty boxes out of it every frame
	 * and AffScene bakes OBJ_BACKGROUND actors into it, both through here.
	 *
	 * The destination offset is the source offset at every one of those call
	 * sites (a background copy is in place by definition), so the DMA path
	 * only handles that case and anything else falls through to the memcpy,
	 * which on a 64 KB scratch Screen would at least stay inside it. */
	if (src == (void *)Screen && dst == (void *)Log && xd == x0 && yd == y0)
	{
		PORT_BgFetch(x0, y0, x1, y1);
		return;
	}
	if (dst == (void *)Screen && src == (void *)Log && xd == x0 && yd == y0)
	{
		PORT_BgStore(x0, y0, x1, y1);
		return;
	}
#endif

	pSrc = (UBYTE *)src + pTabOffLine[y0] + x0;
	pDest = (UBYTE *)dst + pTabOffLine[yd] + xd;

	width = x1 - x0 + 1;
	lines = y1 - y0 + 1;

	for (; lines; lines--)
	{
		memcpy(pDest, pSrc, (size_t)width);
		pSrc += Screen_X;
		pDest += Screen_X;
	}
}

/* ASM: SaveBlock — screen rectangle -> linear buffer */
void SaveBlock(void *screen, void *buffer, LONG x, LONG y, LONG dx, LONG dy)
{
	ULONG *pTabOffLine = TABOFFLINE;
	UBYTE *pSrc;
	UBYTE *pDest;
	LONG width, lines;

	width = dx - x + 1;
	lines = dy - y + 1;

	pSrc = (UBYTE *)screen + pTabOffLine[y] + x;
	pDest = (UBYTE *)buffer;

	for (; lines; lines--)
	{
		memcpy(pDest, pSrc, (size_t)width);
		pDest += width;
		pSrc += Screen_X;
	}
}

/* ASM: RestoreBlock — linear buffer -> screen rectangle */
void RestoreBlock(void *screen, void *buffer, LONG x, LONG y, LONG dx, LONG dy)
{
	ULONG *pTabOffLine = TABOFFLINE;
	UBYTE *pSrc;
	UBYTE *pDest;
	LONG width, lines;

	width = dx - x + 1;
	lines = dy - y + 1;

	pSrc = (UBYTE *)buffer;
	pDest = (UBYTE *)screen + pTabOffLine[y] + x;

	for (; lines; lines--)
	{
		memcpy(pDest, pSrc, (size_t)width);
		pSrc += width;
		pDest += Screen_X;
	}
}
