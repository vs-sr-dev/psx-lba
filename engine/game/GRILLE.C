
#include "c_extern.h"

#include <stdlib.h>
#include <stdio.h>

#include "grille.h"

/*══════════════════════════════════════════════════════════════════════════*
			  █▀▀▀▀ █▀▀▀█  █    █     █     █▀▀▀▀
			  ██ ▀█ ██▀█▀  ██   ██    ██    ██▀▀
			  ▀▀▀▀▀ ▀▀  ▀  ▀▀   ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

#define HEADER_BLOCK 3

#define DXBLOCK 0
#define DYBLOCK 1
#define DZBLOCK 2

/*-------------------------------------------------------------------------*/
/* prototype */

UBYTE *GetAdrBlock(LONG numblock);
UBYTE GetColBrick(WORD xm, WORD ym, WORD zm);
ULONG CreateMaskGph(UBYTE *pt, UBYTE *dest);
ULONG SizeMaskGph(UBYTE *pt);
/*-------------------------------------------------------------------------*/

LONG StartXCube = 0;
LONG StartYCube = 0;
LONG StartZCube = 0;

LONG WorldXCube = 0;
LONG WorldYCube = 0;
LONG WorldZCube = 0;

WORD XpOrgw;
WORD YpOrgw;

#define NB_COLON 28
#define MAX_BRICK 150

WORD NbBrickColon[NB_COLON];

typedef struct
{
	WORD Xm;
	WORD Ym;
	WORD Zm;
	WORD Ys;
	WORD Brick;

} T_COLONB;

T_COLONB ListBrickColon[NB_COLON][MAX_BRICK];

UBYTE *BufCube = 0;
UBYTE *BufMap = 0;
UBYTE *TabBlock;
UBYTE *BufferBrick;

UBYTE *BufferMaskBrick;

LONG XMap, YMap, ZMap;
LONG NbBlock;
LONG XScreen, YScreen;

LONG DxBlock, DyBlock, DzBlock;

// LONG	CoulFond=0		;

extern WORD Nxw, Nyw, Nzw;
void ReajustPos(UBYTE col);
/*--------------------------------------------------------------------------*/
#ifdef BRICK_HQR
/*══════════════════════════════════════════════════════════════════════════*
		 █   █ █▀▀▀█ █▀▀▀█       █▀▀█  █▀▀▀█  █    █▀▀▀▀ █  ▄▀
		 ██▀▀█ ██ ▄█ ██▀█▀       ██▀▀█ ██▀█▀  ██   ██    ██▀▄
		 ▀▀  ▀ ▀▀▀▀  ▀▀  ▀ ▀▀▀▀▀ ▀▀▀▀▀ ▀▀  ▀  ▀▀   ▀▀▀▀▀ ▀▀  ▀
 *══════════════════════════════════════════════════════════════════════════*/
#define MAX_BRICK_GAME 10000L
#ifdef PORT_PSX_BG_VRAM
/* PORT: `Screen` is 64 KB here, not 300 -- the background it used to double
 * as now lives in VRAM (PERSO.C, psx_video.c). This offset only has to clear
 * the LBA_BRK offset table that gets read into Screen just below, which is
 * 34864 bytes in the shipped archive; 40960 leaves room and still puts the
 * 20000-byte flag table inside the buffer. */
#define OFFSET_BUFFER_FLAG 40960L
#else
#define OFFSET_BUFFER_FLAG 153800L
#endif

char *NameHqrGri = PATH_RESSOURCE "LBA_GRI.HQR";
char *NameHqrBll = PATH_RESSOURCE "LBA_BLL.HQR";
char *NameHqrBrk = PATH_RESSOURCE "LBA_BRK.HQR";

#pragma pack(push, 1) /* PORT: on-disk struct (10 bytes packed, see HQ_RESS.C) */
typedef struct
{
	ULONG SizeFile;
	ULONG CompressedSizeFile;
	WORD CompressMethod; /* 0 stored */
						 /* 1 LZS */
} T_HEADER;
#pragma pack(pop)

/*--------------------------------------------------------------------------*/
LONG LoadUsedBrick(ULONG size)
{
	ULONG i, j, b;
	UBYTE *pt;
	UBYTE *ptb;
	UWORD *ptw;
	ULONG *ptoff;
	UBYTE *ptdata;
	UBYTE *ptsrc;
	UBYTE *ptused;
	ULONG offset;
	ULONG maxbrk;
	ULONG nbbrick;
	ULONG brick;
	ULONG offseek;
	ULONG *ptseek;
	FILE *handle;
	ULONG nbentity;
	UWORD *tabflag;
	UWORD *ptflag;
	UWORD min, max;
	T_HEADER header;
	UBYTE *ptdecomp;

	tabflag = (UWORD *)(Screen + OFFSET_BUFFER_FLAG);

	RazMem(tabflag, MAX_BRICK_GAME * 2L); /* Table de UWORD pour NewNumBrick */

	min = 60000;
	max = 0;

	/*-------------------------------------- Premiere Passe, Préparation ptflag */

	pt = ptused = BufMap + (size - 32); /* Debut de Used Block */

	for (i = 1; i < 256; i++)
	{
		b = pt[i >> 3] & (1 << (7 - (i & 7))); /*	Recup Bit Block	*/
		if (!b)
			continue;

		ptb = TabBlock + *(ULONG *)(TabBlock + ((i - 1) << 2));
		ptw = (UWORD *)(ptb + 5);				 /* Jump dx dy dz et collis */
		maxbrk = *ptb * *(ptb + 1) * *(ptb + 2); /* dx*dy*dz*/

		for (j = 0; j < maxbrk; j++, ptw += 2)
		{
			brick = LE_RU16(ptw); /* PORT: block entries sit at odd offsets (bll offsets are 3+4n) */

			if (brick)
			{
				brick--;

				if (brick < min)
					min = brick;
				if (brick > max)
					max = brick;

				tabflag[brick] = 1;
			}
		}
	}

	/*-------------------------------------- Deuxieme Passe, On compte les Bricks*/
	ptflag = tabflag + min;

	nbbrick = 0;

	for (i = min; i <= max; i++, ptflag++)
	{
		if (*ptflag)
			nbbrick++;
	}

	/*-------------------------------------- Troisieme Passe, Load Brick         */

	handle = OpenRead(NameHqrBrk);
	if (!handle)
		return (0L);

	Read(handle, &nbentity, 4L);
	Seek(handle, 0L, SEEK_START);

	/* PORT: the archive's whole offset table is staged in Screen below the
	 * flag table, and Screen is 64 KB on this machine rather than 300 (see
	 * OFFSET_BUFFER_FLAG above). 34864 bytes in the shipped LBA_BRK, but a
	 * modded archive is a heap overrun and not an error message. */
	if (nbentity > OFFSET_BUFFER_FLAG)
	{
		PORT_Diag("ERROR: LBA_BRK's offset table is %lu bytes, Screen stages "
				  "%lu\n", (unsigned long)nbentity,
				  (unsigned long)OFFSET_BUFFER_FLAG);
		Close(handle);
		return (0L);
	}

	Read(handle, Screen, nbentity);
	nbentity >>= 2;

	ptflag = tabflag + min;

	ptoff = (ULONG *)BufferBrick;

	offset = (nbbrick + 1) * 4L;

	*ptoff++ = offset; /* First Offset        */

	ptdata = BufferBrick + offset; /* Jump nbbrick+1 Offset	*/

	nbbrick = 0;

	ptseek = (ULONG *)Screen;

	for (i = min; i <= max; i++, ptflag++)
	{
		if (*ptflag)
		{
			nbbrick++;		   /*	One More*/
			*ptflag = nbbrick; /*	Brick+1	*/
			offseek = *(ptseek + i);
			Seek(handle, offseek, SEEK_START);
			Read(handle, &header, sizeof(header));

			/* PORT: BufferBrick is a fixed MAX_SIZE_BRICK_CUBE allocation and
			 * nothing here ever compared the running total against it -- the
			 * 1994 constant was tuned against the shipped data and simply
			 * held. It still holds, but only just: tools/scene_census.py puts
			 * the worst scene (cube 59) at 351894 bytes of 361472, a margin of
			 * 2.6%. Overrunning it would write past a 353 KB heap block and
			 * surface as corruption somewhere else entirely, which on a
			 * machine with no MMU is the expensive kind of bug.
			 *
			 * The +500 is the LZS case below, which stages the compressed
			 * bytes at ptdata + SizeFile - CompressedSizeFile + 500 and
			 * therefore touches SizeFile + 500 bytes from ptdata. */
			if (offset + header.SizeFile + 500 > MAX_SIZE_BRICK_CUBE)
			{
				PORT_Diag("ERROR: BufferBrick overflow at brick %d: "
						  "%d + %d > %d\n",
						  (int)i, (int)offset, (int)header.SizeFile,
						  (int)MAX_SIZE_BRICK_CUBE);
				Close(handle);
				return (0L);
			}
			switch (header.CompressMethod)
			{
			case 0:
				Read(handle, ptdata, header.SizeFile);
				break;
			case 1:
				ptdecomp = ptdata + header.SizeFile - header.CompressedSizeFile + 500;
				Read(handle, ptdecomp, header.CompressedSizeFile);
				Expand(ptdecomp, ptdata, header.SizeFile);
				break;
			}
			ptdata += header.SizeFile;
			offset += header.SizeFile;
			*ptoff++ = offset;
		}
	}

	/*-------------------------------------- Quatrieme Passe, Rename Block      */

	pt = ptused; /* Debut de Used Block */

	for (i = 1; i < 256; i++)
	{
		b = pt[i >> 3] & (1 << (7 - (i & 7))); /*	Recup Bit Block	*/
		if (!b)
			continue;

		ptb = TabBlock + *(ULONG *)(TabBlock + ((i - 1) << 2));
		ptw = (UWORD *)(ptb + 5);				 /* Jump dx dy dz et collis */
		maxbrk = *ptb * *(ptb + 1) * *(ptb + 2); /* dx*dy*dz*/

		for (j = 0; j < maxbrk; j++, ptw += 2)
		{
			brick = LE_RU16(ptw); /* PORT: odd offsets — LE access */
			if (brick)
				LE_W16(ptw, tabflag[brick - 1]);
		}
	}

	Close(handle);

	return (offset);
}
/*-------------------------------------------------------------------------*/
LONG InitGrille(UWORD numcube)
{
	ULONG i, b, j;
	UBYTE *pt;
	ULONG sizegri;
	ULONG sizebll;
	ULONG size;
	ULONG bank;
	ULONG masksize;

	//	BufCube = Malloc(SIZE_CUBE_X*SIZE_CUBE_Y*SIZE_CUBE_Z*2L ) ;
	//	if ( BufCube == 0L )	return(0L)		;

	sizegri = Size_HQR(NameHqrGri, numcube); /*	GRI	*/
	HQM_Alloc(sizegri, (void **)&BufMap);
	CHECK_MEMORY
	//	BufMap = Malloc( sizegri+500 ) 			;
	if (BufMap == 0L)
		return (0L);
	Load_HQR(NameHqrGri, BufMap, numcube);
	//	Mshrink( BufMap, sizegri )			;

	sizebll = Size_HQR(NameHqrBll, numcube); /*	BLL	*/
	HQM_Alloc(sizebll, (void **)&TabBlock);
	CHECK_MEMORY
	//	TabBlock = Malloc( sizebll+500 )		;	bordel !
	if (TabBlock == 0L)
		return (0L);
	Load_HQR(NameHqrBll, TabBlock, numcube);
	//	Mshrink( TabBlock, sizebll )			;

	/*----------------------------------------------	NEW		*/
	/*	Message("Loading Brick, Please Wait...", FALSE );*/

	size = LoadUsedBrick(sizegri);
	if (!size)
		return (0L);
	bank = size;

	/*	Message("     End Loading Brick       ", FALSE );*/
	/*----------------------------------------------*/

	/* PORT: measure the mask, then allocate it.
	 *
	 * The 1994 code asked HQM for `size` -- the whole brick bank, 351894 bytes
	 * on cube 59 -- built a mask a fifth of that into it, and shrank. The pool
	 * never held the difference for more than one call, but it had to be able
	 * to, and that is the only reason HQM was 400000 bytes: the census puts the
	 * peak on the worst scene at 384696 of it, and the settled figure at 96628.
	 * The engine's own CHECK_MEMORY samples after the shrink and never saw it.
	 *
	 * SizeMaskGph is the same walk as CreateMaskGph with the stores removed, so
	 * the two cannot disagree -- but a silent disagreement would be a heap
	 * overrun into the bodies allocated next, so it is checked.
	 * tools/scene_census.py, docs/M7-NOTES.md. */
	masksize = SizeMaskGph(BufferBrick);

	HQM_Alloc(masksize, (void **)&BufferMaskBrick);
	if (!BufferMaskBrick)
	{
		PORT_Diag("ERROR: HQM has no room for a %lu-byte brick mask "
				  "(%lu of %lu used)\n",
				  (unsigned long)masksize,
				  (unsigned long)(Size_HQM_Memory - Size_HQM_Free),
				  (unsigned long)Size_HQM_Memory);
		return (0L);
	}

	size = CreateMaskGph(BufferBrick, BufferMaskBrick);
	if (size != masksize)
	{
		PORT_Diag("ERROR: brick mask is %lu bytes, %lu were reserved\n",
				  (unsigned long)size, (unsigned long)masksize);
		return (0L);
	}
	HQM_Shrink_Last(BufferMaskBrick, size);
	CHECK_MEMORY

	PORT_Diag("[MEM] cube %d: gri %lu bll %lu bank %lu mask %lu -- "
			  "HQM %lu of %lu\n",
			  (int)numcube, (unsigned long)sizegri, (unsigned long)sizebll,
			  (unsigned long)bank, (unsigned long)masksize,
			  (unsigned long)(Size_HQM_Memory - Size_HQM_Free),
			  (unsigned long)Size_HQM_Memory);

	/*	BufferMaskBrick = Malloc( size ) 	;
		size = CreateMaskGph( BufferBrick, BufferMaskBrick ) ;
		Mshrink( BufferMaskBrick, size )	;// Reduc no test
	*/
	NbBlock = (*(ULONG *)TabBlock) / 4;

	CopyMapToCube();

	return (1L);
}
/*--------------------------------------------------------------------------*/
void FreeGrille()
{
	/*	if( BufMap )
		{
			Free( BufferMaskBrick ) ;
			Free( TabBlock )	;
			Free( BufMap )  	;
	//		Free( BufCube )		;

			BufMap = 0 ;
		}
	*/
}
/*-------------------------------------------------------------------------*/
/*══════════════════════════════════════════════════════════════════════════*
		   █▀▀▀▀ ██▄ █ █▀▀▀▄       █   █ █▀▀▀█ █▀▀▀█
		   ██▀▀  ██▀██ ██  █       ██▀▀█ ██ ▄█ ██▀█▀
		   ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀        ▀▀  ▀ ▀▀▀▀  ▀▀  ▀
 *══════════════════════════════════════════════════════════════════════════*/
#endif
/*--------------------------------------------------------------------------*/
/*══════════════════════════════════════════════════════════════════════════*
		 █▀▀▀▀ █   █ ██▄ █ █▀▀▀▀ ▀▀█▀▀  █    █▀▀▀█ ██▄ █ ██▀▀▀
		 ██▀▀  ██  █ ██▀██ ██      ██   ██   ██  █ ██▀██ ▀▀▀▀█
		 ▀▀    ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀   ▀▀   ▀▀   ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/*--------------------------------------------------------------------------*/
UBYTE *GetAdrColonneMap(LONG x, LONG z)
{
	ULONG offset;

	offset = (x + (z * SIZE_CUBE_Z)) * 2;
	offset = *(UWORD *)(BufMap + offset);

	return (BufMap + offset);
}
/*--------------------------------------------------------------------------*/
UBYTE *GetAdrColonneCube(LONG x, LONG z)
{
	ULONG offset;

	offset = (x * SIZE_CUBE_Y * 2) + (z * SIZE_CUBE_Z * SIZE_CUBE_Y * 2);

	return (BufCube + offset);
}

/*--------------------------------------------------------------------------*/

void GetShadow(WORD xw, WORD yw, WORD zw)
{
	WORD xm, ym, zm;
	UBYTE *ptc;
	WORD y;
	UBYTE *adr;
	LONG block;

	xm = (xw + DEMI_BRICK_XZ) / SIZE_BRICK_XZ;
	ym = yw / SIZE_BRICK_Y;
	zm = (zw + DEMI_BRICK_XZ) / SIZE_BRICK_XZ;

	ptc = BufCube + ym * 2 + (xm * SIZE_CUBE_Y * 2) + (zm * SIZE_CUBE_X * SIZE_CUBE_Y * 2);

	for (y = ym; y > 0; y--)
	{
		if (*(WORD *)ptc != 0)
			break;
		ptc -= 2;
	}

	XMap = xm;
	YMap = y;
	ZMap = zm;

	Nxw = xw;
	Nyw = (y + 1) * SIZE_BRICK_Y;
	Nzw = zw;

	ShadowCol = 0;

	if (*ptc != 0)
	{
		block = (*ptc++) - 1;
		adr = GetAdrBlock(block);
		adr += HEADER_BLOCK;
		adr += (*ptc) << 2; /* 4 Bytes to Jump	*/
		ShadowCol = *adr;
		ReajustPos(ShadowCol);
		/*		ShadowCol-- ;	*/ /* pour num gph */
	}

	ShadowX = Nxw;
	ShadowY = Nyw;
	ShadowZ = Nzw;
}

/*──────────────────────────────────────────────────────────────────────────*/

/*══════════════════════════════════════════════════════════════════════════*
		   █▄ ▄█ █▀▀▀▀ █▄ ▄█ █▀▀▀█  █    █▀▀▀█ █▀▀▀▀
		   ██▀ █ ██▀▀  ██▀ █ ██  █  ██   ██▀█▀ ██▀▀
		   ▀▀  ▀ ▀▀▀▀▀ ▀▀  ▀ ▀▀▀▀▀  ▀▀   ▀▀  ▀ ▀▀▀▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/* PORT: what CreateMaskGph below is about to write, without writing it.
 *
 * The mask buffer used to be allocated at the size of the brick bank and
 * shrunk afterwards, which meant HQM had to be large enough for a buffer that
 * was five times bigger than its own contents, for the length of one call.
 * See translate/graphmsk.c:SizeGraphMsk and docs/M7-NOTES.md. */
ULONG SizeMaskGph(UBYTE *ptsrc)
{
	ULONG nbg, off, i;

	off = *(ULONG *)ptsrc; /*	First Offset Src	*/

	nbg = (off - 4) >> 2; /*	Nombre de Graph	*/

	for (i = 0; i < nbg; i++)
	{
		off += SizeGraphMsk(i, ptsrc);
	}
	return (off);
}

/*-------------------------------------------------------------------------*/

ULONG CreateMaskGph(UBYTE *ptsrc, UBYTE *ptdst)
{
	UBYTE *ptd;
	ULONG nbg, off;
	ULONG *ptoff;
	ULONG size, i;

	ptoff = (ULONG *)ptdst;

	off = *(ULONG *)ptsrc; /*	First Offset Src	*/

	*ptoff++ = off; /*	First Offset	*/

	ptd = ptdst + off;

	nbg = (off - 4) >> 2; /*	Nombre de Graph	*/

	for (i = 0; i < nbg; i++)
	{
		size = CalcGraphMsk(i, ptsrc, ptd);

		off += size;	/*	Maj Offset	*/
		*ptoff++ = off; /*	Write Offset 	*/
		ptd += size;	/*	Maj Pt Dest	*/
	}
	return (off);
}

/*-------------------------------------------------------------------------*/
void MixteMapToCube(UBYTE *map)
{
	UBYTE *ptb;
	LONG blk, pos, nb;
	UBYTE *pts, *ptd;
	LONG x, y, z;
	ULONG offset, flg = 1;

	for (z = 0; z < SIZE_CUBE_Z; z++)
	{
		for (x = 0; x < SIZE_CUBE_X; x++)
		{
			offset = (x + (z * SIZE_CUBE_Z)) * 2;
			offset = *(UWORD *)(map + offset);

			pts = map + offset;
			ptd = GetAdrColonneCube(x, z);
			MixteColonne(pts, ptd);
		}
	}
}
/*--------------------------------------------------------------------------*/
void CopyMapToCube()
{
	UBYTE *pts, *ptd, *ptb;
	LONG x, y, z, blk, pos, nb;
	LONG flg = 1;

	/*-------------- Copy -------------------------*/
	for (z = 0; z < SIZE_CUBE_Z; z++)
	{
		for (x = 0; x < SIZE_CUBE_X; x++)
		{
			pts = GetAdrColonneMap(x, z);
			ptd = GetAdrColonneCube(x, z);
			DecompColonne(pts, ptd);
		}
	}
}
/*──────────────────────────────────────────────────────────────────────────*/

void InitBufferCube()
{
	BufCube = Malloc(SIZE_CUBE_X * SIZE_CUBE_Y * SIZE_CUBE_Z * 2L);
	if (BufCube == 0L)
		TheEnd(NOT_ENOUGH_MEM, "BufCube");

	//	Init Buffer Brick
	BufferBrick = Malloc(MAX_SIZE_BRICK_CUBE);
	if (BufferBrick == 0L)
		TheEnd(NOT_ENOUGH_MEM, "BufferBrick");
}

/*══════════════════════════════════════════════════════════════════════════*
		   █▀▀▀▄  █    ██▀▀▀ █▀▀▀█ █     █▀▀▀█ █  ▄▀
		   ██  █  ██   ▀▀▀▀█ ██▀▀▀ ██    ██▀▀█ ██▀
		   ▀▀▀▀   ▀▀   ▀▀▀▀▀ ▀▀    ▀▀▀▀▀ ▀▀  ▀ ▀▀
 *══════════════════════════════════════════════════════════════════════════*/
/*──────────────────────────────────────────────────────────────────────────*/

/*
 * DrawOverBrick redraws the bricks standing in front of an actor, by copying
 * them out of the clean background through their own mask.
 *
 * On the PlayStation the actor is not in Log to be covered up: it is a GPU
 * primitive drawn after the frame is presented (psx_poly.c), so nothing a
 * software copy puts into Log can get in front of it. And since M7 `Screen`
 * is a 64 KB scratch buffer rather than a 640x480 image -- the background it
 * used to alias moved to VRAM -- so the copy would read 300 KB past its end.
 *
 * Occluding an actor with scenery on this machine means replaying those
 * bricks as GPU primitives after the actors, with the mask as a texture.
 * That is a real feature and it is M8's. This is where it would go.
 */
static void PORT_CopyMaskBg(LONG nummask, LONG x, LONG y, void *bankmask)
{
#ifdef PORT_PSX_BG_VRAM
	(void)nummask;
	(void)x;
	(void)y;
	(void)bankmask;
#else
	CopyMask(nummask, x, y, bankmask, Screen);
#endif
}

/*--------------------------------------------------------------------------*/

void DrawOverBrick(WORD xm, WORD ym, WORD zm)
{
	T_COLONB *ptrlbc;
	WORD col, i;
	WORD startcol, endcol;

	startcol = (ClipXmin + 24) / 24 - 1;
	endcol = (ClipXmax + 24) / 24;

	for (col = startcol; col <= endcol; col++)
	{
		ptrlbc = &ListBrickColon[col][0];

		for (i = 0; i < NbBrickColon[col]; i++)
		{
			/* bricks devant */
			if (ptrlbc->Ys + 38 > ClipYmin
									  AND ptrlbc->Ys <= ClipYmax)
			{
				if (ptrlbc->Ym >= ym)
				{
					if ((ptrlbc->Xm + ptrlbc->Zm) > (xm + zm))
					{
						PORT_CopyMaskBg(ptrlbc->Brick, col * 24 - 24, ptrlbc->Ys, BufferMaskBrick);
					}
				}
			}
			ptrlbc++;
		}
	}
}

/*--------------------------------------------------------------------------*/
/*
void	DrawOverBrick2( WORD xm, WORD ym, WORD zm )
{
	T_COLONB	*ptrlbc ;
	WORD		col, i ;
	WORD		startcol, endcol ;

	startcol = (ClipXmin+24)/24-1 ;
	endcol = (ClipXmax+24)/24 ;

	for( col = startcol; col <= endcol; col++ )
	{
		ptrlbc = &ListBrickColon[col][0] ;

		for( i=0; i<NbBrickColon[col]; i++ )
		{
			if( ptrlbc->Ys+38 > ClipYmin
			AND ptrlbc->Ys <= ClipYmax )
			{
			if( ptrlbc->Ym >= ym )
			{
			if( (ptrlbc->Zm >= zm)
			AND (ptrlbc->Xm >= xm) )
			{
				CopyMask( ptrlbc->Brick, col*24-24, ptrlbc->Ys, BufferMaskBrick, Screen ) ;
			}

			}
			}
			ptrlbc++ ;
		}
	}
}
*/
/*--------------------------------------------------------------------------*/
/* recouvrement reel pour obj qui ne depassent pas la ZV */

void DrawOverBrick3(WORD xm, WORD ym, WORD zm)
{
	T_COLONB *ptrlbc;
	WORD col, i;
	WORD startcol, endcol;

	startcol = (ClipXmin + 24) / 24 - 1;
	endcol = (ClipXmax + 24) / 24;

	for (col = startcol; col <= endcol; col++)
	{
		ptrlbc = &ListBrickColon[col][0];

		for (i = 0; i < NbBrickColon[col]; i++)
		{
			/* bricks devant */
			if (ptrlbc->Ys + 38 > ClipYmin
									  AND ptrlbc->Ys <= ClipYmax)
			{
				if (ptrlbc->Ym >= ym)
				{

					if ((ptrlbc->Zm == zm)
							AND(ptrlbc->Xm == xm))
					{
						PORT_CopyMaskBg(ptrlbc->Brick, col * 24 - 24, ptrlbc->Ys, BufferMaskBrick);
					}

					if ((ptrlbc->Zm > zm)
							OR(ptrlbc->Xm > xm))
					{
						PORT_CopyMaskBg(ptrlbc->Brick, col * 24 - 24, ptrlbc->Ys, BufferMaskBrick);
					}
				}
			}
			ptrlbc++;
		}
	}
}

/*--------------------------------------------------------------------------*/
void AffBrickBlock(LONG block, LONG brick, LONG x, LONG y, LONG z)
{
	UBYTE *adr;
	LONG numbrick;
	LONG nb, col;
	WORD bc;

	adr = GetAdrBlock(block);
	adr += HEADER_BLOCK;

	adr += (brick << 2); /* 4 Bytes to Jump	*/
	adr += 2;

	numbrick = LE_RU16(adr); /* & 32767 */ /* PORT: odd offset — LE access */

	if (numbrick)
	{
		Map2Screen(x - StartXCube, y - StartYCube, z - StartZCube);

		if (XScreen >= -24 AND XScreen < 640 AND YScreen >= -38 AND YScreen < 480)
		{
			AffGraph(numbrick - 1, XScreen, YScreen, BufferBrick);

			col = (XScreen + 24) / 24; /* 48 / 2 colonne intercalée */

			nb = NbBrickColon[col];
			if (nb < MAX_BRICK)
			{
				// ca moche mettre PTR

				ListBrickColon[col][nb].Xm = x;
				ListBrickColon[col][nb].Ym = y;
				ListBrickColon[col][nb].Zm = z;
				ListBrickColon[col][nb].Ys = YScreen;
				ListBrickColon[col][nb].Brick = numbrick - 1;
				NbBrickColon[col]++;
			}
			else
				Message("Arg MAX_BRICK Z BUFFER atteint", TRUE);
		}
	}
}

/*--------------------------------------------------------------------------*/
/*ptc = BufCube + y*2 + (x*SIZE_CUBE_Y*2) + (z*SIZE_CUBE_X*SIZE_CUBE_Y*2) ;*/

void AffGrille()
{
	LONG z, y, x;
	LONG block;
	UBYTE *ptc;

	WorldXCube = StartXCube * SIZE_BRICK_XZ;
	WorldYCube = StartYCube * SIZE_BRICK_Y;
	WorldZCube = StartZCube * SIZE_BRICK_XZ;

	ProjettePoint(-WorldXCube, -WorldYCube, -WorldZCube);
	XpOrgw = Xp;
	YpOrgw = Yp;

	for (x = 0; x < NB_COLON; x++)
		NbBrickColon[x] = 0;

	if (!FlagAffGrille)
		return;

	ptc = BufCube;
	for (z = 0; z < SIZE_CUBE_Z; z++)
	{
		for (x = 0; x < SIZE_CUBE_X; x++)
		{
			for (y = 0; y < SIZE_CUBE_Y; y++)
			{
				block = *ptc++;

				if (block)
				{
					AffBrickBlock(block - 1, *ptc, x, y, z);
				}
				ptc++;
			}
		}
	}

	/*	y = 0 ;
		for( x=0; x<NB_COLON; x++ )
		{
			Text( 0,y, "%l", NbBrickColon[x] ) ;
			y += 8 ;
		}
	*/
}

void IncrustGrm(WORD numgrm)
{
	UBYTE *ptrgrm;

	ptrgrm = LoadMalloc_HQR(PATH_RESSOURCE "lba_gri.hqr",
							numgrm + OFFSET_GRM_HQR);
	if (!ptrgrm)
	{
		Message("arg grm not found in lba_gri", TRUE);
		return;
	}

	MixteMapToCube(ptrgrm);

	Free(ptrgrm);

	FirstTime = TRUE;
}
