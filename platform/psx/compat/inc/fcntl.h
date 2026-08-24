/* fcntl.h — libpsn00b has no POSIX file layer, and the engine does not
 * actually need one: eight LIB_SYS translation units include this header but
 * none of them uses an O_* flag or a raw descriptor. They are all built on
 * stdio, which platform/psx provides in psx_file.h. Empty on purpose. */
#ifndef PSX_COMPAT_FCNTL_H
#define PSX_COMPAT_FCNTL_H
#endif
