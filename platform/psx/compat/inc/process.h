/* process.h — a MinGW/DOS header. ADELINE.C only uses spawnl() to launch the
 * external DOS driver setup executables (MidiExec/WaveExec). There is no such
 * thing on a PlayStation: report failure and the engine skips the exec. */
#ifndef PSX_COMPAT_PROCESS_H
#define PSX_COMPAT_PROCESS_H

#define P_WAIT 0
#define spawnl(...) (-1)

#endif
