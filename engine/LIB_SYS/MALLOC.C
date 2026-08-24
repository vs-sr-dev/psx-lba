#include <stdio.h>
#include <stdlib.h>

#include "ADELINE.H"
#include "LIB_SYS.H"

#ifdef DEBUG_MALLOC
void PORT_Diag(const char *fmt, ...); /* PORT: nds_sys.c, tees to the SD log */

LONG ModeTraceMalloc = FALSE;
#endif

/* Only compile these functions for non-DOS platforms */
/* DOS builds use the full DPMI implementation from LIB_SYS_DOS/MALLOC.C */
#ifndef __DOS__

void *Malloc(LONG lenalloc)
{
	void *ptr;

	if (lenalloc == -1)
	{
#ifdef PORT_PSX
		/* PORT: PERSO.C sizes the sprite, animation and sample pools as
		 * fractions of this, so answering 0 collapses all three onto their
		 * clamped floors — which on the DS meant constant eviction, and
		 * eviction compacts the pool under pointers callers are still
		 * holding. psx_sys.c bisects on what the allocator will actually
		 * grant, which is the honest answer on a heap with no free-list
		 * walk. */
		return (void *)PORT_HeapLargestFree();
#else
		return 0; /* Query memory not supported */
#endif
	}

	ptr = malloc(lenalloc);

	if (ptr == NULL)
	{
		PORT_Diag("ERROR: MemoryNotAlloc (Malloc): Size = %d\n", lenalloc);
	}

	return ptr;
}

void *SmartMalloc(LONG lenalloc)
{
	/* SmartMalloc is just an alias for Malloc */
	return Malloc(lenalloc);
}

void Free(void *buffer)
{
	if (buffer != NULL)
	{
		free(buffer);
	}
}

void *Mshrink(void *buffer, ULONG taille)
{
	void *new_buffer;

	if (buffer == NULL)
	{
		return NULL;
	}

	new_buffer = realloc(buffer, taille);
	return new_buffer ? new_buffer : buffer;
}

#endif /* !__DOS__ */
