/*----------------------------------------------------------------------------
 *  graphmsk.c — C translation of LIB386/LIB_SVGA/GRAPHMSK.ASM (Adeline 1993)
 *
 *  Brick (graph) source format:
 *      DWORD TabOffset[]           (one offset per brick, at bank start)
 *      Brick:
 *          BYTE Delta X, BYTE Delta Y (= nb lines), BYTE Hot X, BYTE Hot Y
 *          per line: BYTE NbBlock, then blocks:
 *              00xxxxxx  (x+1) zeros to jump         (no data)
 *              01xxxxxx  (x+1) pixels to copy        ((x+1) data bytes)
 *              10xxxxxx  (x+1) pixels of same colour (1 data byte)
 *
 *  Output mask format (as consumed by AffMask / CopyMask):
 *      BYTE DX, DY, HotX, HotY, then per line: BYTE NbBlock followed by
 *      alternating counts: [skip][draw][skip][draw]...  A line always begins
 *      with a skip count (0 is inserted when needed).
 *---------------------------------------------------------------------------*/
#include "translate.h"

/* ASM: CalcGraphMsk — build the mask of brick num into ptmask,
   returns the size in bytes of the generated mask. */
LONG CalcGraphMsk(LONG num, void *bank, void *mask)
{
	UBYTE *pSrc;
	UBYTE *pDest;
	UBYTE *pStart;
	UBYTE *pNbBlockDst;
	UBYTE nbline;             /* bh */
	UBYTE nbblock;            /* bl */
	UBYTE nbblockdst;         /* dl */
	UBYTE nbdata;             /* ah — 8 bit accumulator, wraps like the ASM */
	UBYTE op, cl;

	pSrc = (UBYTE *)bank + ((ULONG *)bank)[num];
	pDest = (UBYTE *)mask;
	pStart = pDest;

	/* DX, DY, Hot X, Hot Y (copied byte by byte: no unaligned dword store) */
	pDest[0] = pSrc[0];
	pDest[1] = pSrc[1];
	pDest[2] = pSrc[2];
	pDest[3] = pSrc[3];

	nbline = pSrc[1];
	pSrc += 4;
	pDest += 4;

	do  /* NextLine — like the ASM "dec bh/jne", a DY of 0 runs 256 times */
	{
		nbblockdst = 0;
		nbdata = 0;
		pNbBlockDst = pDest++;      /* reserve NbBlockDst slot */

		nbblock = *pSrc++;

		if (*pSrc & 0xC0)           /* line MUST begin with a JumpZero */
		{
			*pDest++ = 0;
			nbblockdst++;
		}

		do  /* SameLine */
		{
			op = *pSrc++;
			cl = (UBYTE)((op & 0x3F) + 1);

			if (op & 0x80)          /* RepeatCol: cl pixels, 1 data byte */
			{
				nbdata = (UBYTE)(nbdata + cl);
				pSrc++;
			}
			else if (op & 0x40)     /* CopyCol: cl pixels, cl data bytes */
			{
				nbdata = (UBYTE)(nbdata + cl);
				pSrc += cl;
			}
			else                    /* Jump cl zeros */
			{
				if (nbdata)
				{
					*pDest++ = nbdata;
					nbblockdst++;
					nbdata = 0;
				}
				*pDest++ = cl;
				nbblockdst++;
			}
		} while (--nbblock);

		if (nbdata)                 /* Cloture eventuelle */
		{
			*pDest++ = nbdata;
			nbblockdst++;
		}

		*pNbBlockDst = nbblockdst;
	} while (--nbline);

	return (LONG)(pDest - pStart);
}

/* PORT: CalcGraphMsk with the stores taken out.
 *
 * GRILLE.C used to allocate the mask buffer at the size of the brick bank --
 * 351894 bytes on the heaviest scene -- build a mask about a fifth of that
 * into it, and shrink. The buffer only ever held its real contents; for the
 * length of one call HQM had to be big enough for the other four fifths, and
 * that transient is what pinned the pool at 400000 bytes. Measuring first
 * costs one extra walk of the brick data -- no decompression, no writes --
 * and lets the allocation be exact. docs/M7-NOTES.md, tools/scene_census.py.
 *
 * This must return exactly what CalcGraphMsk writes. The two walks are the
 * same walk; InitGrille compares the results and refuses the scene if they
 * ever disagree. */
LONG SizeGraphMsk(LONG num, void *bank)
{
	UBYTE *pSrc;
	LONG size;
	UBYTE nbline;
	UBYTE nbblock;
	UBYTE nbdata;
	UBYTE op, cl;

	pSrc = (UBYTE *)bank + ((ULONG *)bank)[num];

	size = 4;                       /* DX, DY, Hot X, Hot Y */
	nbline = pSrc[1];
	pSrc += 4;

	do  /* NextLine — a DY of 0 runs 256 times, as in the ASM */
	{
		nbdata = 0;
		size++;                     /* the NbBlockDst slot */

		nbblock = *pSrc++;

		if (*pSrc & 0xC0)           /* line MUST begin with a JumpZero */
			size++;

		do  /* SameLine */
		{
			op = *pSrc++;
			cl = (UBYTE)((op & 0x3F) + 1);

			if (op & 0x80)          /* RepeatCol: cl pixels, 1 data byte */
			{
				nbdata = (UBYTE)(nbdata + cl);
				pSrc++;
			}
			else if (op & 0x40)     /* CopyCol: cl pixels, cl data bytes */
			{
				nbdata = (UBYTE)(nbdata + cl);
				pSrc += cl;
			}
			else                    /* Jump cl zeros */
			{
				if (nbdata)
				{
					size++;
					nbdata = 0;
				}
				size++;
			}
		} while (--nbblock);

		if (nbdata)                 /* Cloture eventuelle */
			size++;

	} while (--nbline);

	return size;
}
