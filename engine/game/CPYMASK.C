#include "C_EXTERN.H"

/*
 * The mask copy, with both ends free to be somewhere other than a full frame.
 *
 * The walk is unchanged from CopyMask.asm: per line a block count, then
 * alternating skip and copy runs. What is new is that source and destination
 * are each a base pointer, a stride and an origin, rather than Screen and Log
 * at Screen_X.
 *
 * The PlayStation port needs both ends moved. The actor it has to cover is a
 * GPU primitive that is not in Log at all, so the bricks in front of it have
 * to become a primitive too: they are copied into a small overlay buffer, out
 * of one brick-sized window on the clean background — which on that machine
 * lives in VRAM and arrives a rectangle at a time.
 * platform/psx/psx_depth.c, docs/M8-NOTES.md.
 */
void CopyMaskTo(LONG nummask, LONG x, LONG y, void *bankmask,
				UBYTE *srcbase, LONG srcstride, LONG srcx, LONG srcy,
				UBYTE *dstbase, LONG dststride, LONG dstx, LONG dsty)
{
	UBYTE *pMask;
	UBYTE *pSrc;
	UBYTE *pDest;
	LONG dx, dy;
	LONG x1, y1;
	LONG n;
	UBYTE NbBlock;
	ULONG *pBank = (ULONG *)bankmask;

	pMask = (UBYTE *)bankmask + pBank[nummask];

	dx = pMask[0];
	dy = pMask[1];
	x += pMask[2];
	y += pMask[3];

	pMask += 4;

	x1 = dx + x - 1;
	y1 = dy + y - 1;

	if ((x < ClipXmin) || (y < ClipYmin) || (x1 > ClipXmax) || (y1 > ClipYmax))
	{
		UBYTE *pSrcLine;
		UBYTE *pDestLine;
		LONG OffsetBegin = 0;
		LONG NbPix, pixLeft, offset;

		if ((x > ClipXmax) || (y > ClipYmax) || (x1 < ClipXmin) || (y1 < ClipYmin))
		{
			return;
		}

		if (y < ClipYmin)
		{
			for (n = ClipYmin - y; n > 0; n--)
			{
				NbBlock = *pMask;
				pMask += NbBlock + 1;
			}
			y = ClipYmin;
		}

		if (y1 > ClipYmax)
		{
			y1 = ClipYmax;
		}

		if (x < ClipXmin)
		{
			OffsetBegin = ClipXmin - x;
		}

		NbPix = x1 - x - OffsetBegin + 1;
		if (x1 > ClipXmax)
		{
			NbPix -= x1 - ClipXmax;
		}

		pSrcLine = srcbase + (y - srcy) * srcstride + (x + OffsetBegin - srcx);
		pDestLine = dstbase + (y - dsty) * dststride + (x + OffsetBegin - dstx);

		for (dy = y1 - y + 1; dy; dy--)
		{
			offset = OffsetBegin;
			pixLeft = NbPix;

			pSrc = pSrcLine;
			pDest = pDestLine;

			NbBlock = *pMask++;

			while ((NbBlock > 1) && (pixLeft > 0))
			{
				n = *pMask++;
				NbBlock--;

				if (offset)
				{
					offset -= n;

					if (offset < 0)
					{
						pSrc -= offset;
						pDest -= offset;
						pixLeft += offset;
						offset = 0;
					}
				}
				else if (n)
				{
					pDest += n;
					pSrc += n;
					pixLeft -= n;
				}

				if (pixLeft > 0)
				{
					n = *pMask++;
					NbBlock--;

					if (offset)
					{
						offset -= n;
						if (offset <= 0)
						{
							n = -offset;
							offset = 0;
						}
					}

					if (!offset && n > 0)
					{
						if (n > pixLeft)
						{
							n = pixLeft;
						}
						pixLeft -= n;

						memcpy(pDest, pSrc, n);
						pDest += n;
						pSrc += n;
					}
				}
			}

			pMask += NbBlock;
			pDestLine += dststride;
			pSrcLine += srcstride;
		}
	}
	else
	{
		LONG dstadv, srcadv;

		pSrc = srcbase + (y - srcy) * srcstride + (x - srcx);
		pDest = dstbase + (y - dsty) * dststride + (x - dstx);

		dstadv = dststride - dx;
		srcadv = srcstride - dx;

		for (; dy; dy--)
		{
			NbBlock = *pMask++;
			while (NbBlock)
			{
				n = *pMask++;
				pDest += n;
				pSrc += n;
				NbBlock--;

				if (NbBlock)
				{
					n = *pMask++;

					memcpy(pDest, pSrc, n);
					pDest += n;
					pSrc += n;
					NbBlock--;
				}
			}

			pDest += dstadv;
			pSrc += srcadv;
		}
	}
}

/*══════════════════════════════════════════════════════════════════════════*/

void CopyMask(LONG nummask, LONG x, LONG y, void *bankmask, void *screen)
{
	CopyMaskTo(nummask, x, y, bankmask,
			   (UBYTE *)screen, Screen_X, 0, 0,
			   Log, Screen_X, 0, 0);
}
