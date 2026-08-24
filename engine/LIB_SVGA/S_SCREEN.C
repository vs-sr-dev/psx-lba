#include "LIB_SYS/ADELINE.H"
#include "LIB_SYS/LIB_SYS.H"
#include "LIB_SVGA/LIB_SVGA.H"

void Cls(void)
{
	memset(Log, 0, Screen_X * Screen_Y);
}

void CopyScreen(void *src, void *dst)
{
	/* PORT: on the PlayStation Log and Screen are the same allocation, so
	 * every background save/restore in the engine arrives here with src ==
	 * dst. memcpy is not defined for that, and it is not wanted either. */
	if (src == dst)
		return;

	memcpy(dst, src, Screen_X * Screen_Y);
}
