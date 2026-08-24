/*
 * stubs.c — neutral stubs for the DOS driver layers: the Watcom DLL loader,
 * AIL32 MIDI, the WAVE sample driver, the mixer, and the CD-ROM.
 *
 * Derived from the DS port's platform/nds/stubs.c, which is itself derived
 * from the SDL one. The pattern is the same in all three: report "success but
 * silent", and leave the *_Driver_Enable flags FALSE so the engine takes the
 * paths a DOS machine without that hardware would have taken. Those paths are
 * exercised code, not error handling, which is why this works at all.
 *
 * Note that LIB_MIDI/LIB_MIDI.C in the community source release has its entire
 * body commented out — the AIL32 library it wrapped was not open and could not
 * be released. So MIDI is not being stubbed out here so much as supplied for
 * the first time.
 *
 * All of this is temporary in a specific way: the CD functions become real in
 * M2, the wave and mixer ones in M7 against the SPU, and MIDI never does — the
 * plan is to pre-render the XMI music to XA-ADPCM at build time instead, which
 * is what the DS port does with WAV.
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"

void InitTimer(void);   /* psx_sys.c */

/* An empty identifier list: the engine's "while (**ptridentifier)" loops stop
 * immediately on it. */
static char *EmptyIdentList[] = { "" };
static LONG DummyVars[8];

/* ══════════════════════════════════════════════════════════════════════════
 * Watcom DLL loader (LIB_SYS/DLL.H)
 * ═════════════════════════════════════════════════════════════════════════ */

ULONG DLL_size(void *source, ULONG flags)
{
    (void)source; (void)flags;
    return 0;
}

void *DLL_load(void *source, ULONG flags, void *dll)
{
    (void)source; (void)flags; (void)dll;
    return 0;
}

void *FILE_read(BYTE *filename, void *dest)
{
    (void)filename; (void)dest;
    return 0;       /* only reached if LBA.CFG names a driver */
}

/* ══════════════════════════════════════════════════════════════════════════
 * CD-ROM (LIB_CD)
 *
 * The DOS CD release plays music from CD-DA tracks 1-9 and reads its data from
 * the same disc. Both become real in M2 and M7; the interesting problem there
 * is not the API but the drive head, which cannot be in two places at once.
 * ═════════════════════════════════════════════════════════════════════════ */

WORD DriveCDR = -1;
LONG FileCD_Start = 0;
LONG FileCD_Sect = 0;
LONG FileCD_Size = 0;

LONG InitCDR(char *nameid)
{
    (void)nameid;
    return 0;       /* no CD: the engine falls back to its no-music path */
}

void ClearCDR(void) {}

LONG PlayTrackCDR(LONG track)
{
    (void)track;
    return 0;
}

void StopCDR(void) {}

LONG GetLengthTrackCDR(LONG track)
{
    (void)track;
    return 0;
}

LONG ReadLongCDR(LONG start, LONG nbsect, void *buffer)
{
    (void)start; (void)nbsect; (void)buffer;
    return 0;
}

LONG GetFileCDR(char *name)
{
    (void)name;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * MIDI (LIB_MIDI / AIL32)
 * ═════════════════════════════════════════════════════════════════════════ */

WORD Midi_Driver_Enable = 0;
LONG MaxVolume = 100;

void AskMidiVars(char ***listidentifier, long **ptrvars)
{
    *listidentifier = EmptyIdentList;
    *ptrvars = (long *)DummyVars;
}

/*
 * These two report success even though there is no MIDI device. ADELINE.C
 * calls exit(1) when a driver named in LBA.CFG fails to load, and a config
 * copied from a DOS install always names one. The DS port made the same
 * choice, for the same reason: failing here would mean the port could only
 * boot from a config file it had written itself.
 */
long InitMidiDLL(char *driverpathname)
{
    (void)driverpathname;
    return 1;
}

long InitMidi(void)
{
    return 1;
}

void InitPathMidiSampleFile(unsigned char *path)
{
    (void)path;
}

void ClearMidi(void) {}
void PlayMidi(unsigned char *xmi) { (void)xmi; }
void StopMidi(void) {}
long IsMidiPlaying(void) { return 0; }
void FadeMidiDown(short speed) { (void)speed; }
void FadeMidiUp(short speed) { (void)speed; }
void WaitFadeMidi(void) {}
void VolumeMidi(short vol) { (void)vol; }
void SetLoopMidi(short flag) { (void)flag; }
void DoLoopMidi(void) {}
void InitMidiTimer(void)
{
    InitTimer();        /* the engine's tick, whoever asks for it */
}

/* ══════════════════════════════════════════════════════════════════════════
 * WAVE samples (LIB_SAMP)
 *
 * Real implementation in M7: SAMPLES.HQR converted to VAG ADPCM with an LRU
 * pool in the SPU's 512 KB, following the DS port's 320 KB pool.
 * ═════════════════════════════════════════════════════════════════════════ */

LONG Wave_Driver_Enable = 0;

LONG WaveInitDLL(char *dlldriver)
{
    (void)dlldriver;
    return 1;           /* as with MIDI above: a failure here is exit(1) */
}

void WaveAskVars(char ***listidentifier, LONG **ptrvars)
{
    *listidentifier = EmptyIdentList;
    *ptrvars = DummyVars;
}

ULONG InitWave(void)
{
    return 1;
}

void ClearWave(void) {}

ULONG WavePlay(UWORD Handle, UWORD Pitchbend, UWORD Repeat, UBYTE Follow,
               UWORD VolLeft, UWORD VolRight, void *Buffer)
{
    (void)Handle; (void)Pitchbend; (void)Repeat; (void)Follow;
    (void)VolLeft; (void)VolRight; (void)Buffer;
    return 0;
}

void WaveGiveInfo0(ULONG LongHandle, ULONG Info0)
{
    (void)LongHandle; (void)Info0;
}

void WaveStop(void) {}
void WaveStopOne(UWORD Handle) { (void)Handle; }
void WaveStopOneLong(ULONG LongHandle) { (void)LongHandle; }
int  WaveInList(UWORD handle) { (void)handle; return 0; }
int  WavePause(void) { return 0; }
void WaveContinue(void) {}

/*
 * WaveMove exists because the DOS driver could be reading the source while the
 * engine moved it. Nothing plays yet, so a plain memmove is exactly right —
 * and the DS port found that it stays right even with real audio, as long as
 * the hardware never reads the engine heap.
 */
void WaveMove(void *DestAddr, void *SrcAddr, ULONG Size)
{
    memmove(DestAddr, SrcAddr, (size_t)Size);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Mixer (LIB_MIX)
 * ═════════════════════════════════════════════════════════════════════════ */

LONG Mixer_Driver_Enable = 0;
void *Mixer_listfcts = 0;

void MixerAskVars(char ***listidentifier, LONG **ptrvars)
{
    *listidentifier = EmptyIdentList;
    *ptrvars = DummyVars;
}

void MixerChangeVolume(LONG VolWave, LONG VolMidi, LONG VolCD,
                       LONG VolLine, LONG VolMaster)
{
    (void)VolWave; (void)VolMidi; (void)VolCD;
    (void)VolLine; (void)VolMaster;
}

void MixerGetVolume(LONG *VolWave, LONG *VolMidi, LONG *VolCD,
                    LONG *VolLine, LONG *VolMaster)
{
    if (VolWave)   *VolWave = 255;
    if (VolMidi)   *VolMidi = 255;
    if (VolCD)     *VolCD = 255;
    if (VolLine)   *VolLine = 0;
    if (VolMaster) *VolMaster = 255;
}

void MixerGetInfo(LONG *VolWave, LONG *VolMidi, LONG *VolCD,
                  LONG *VolLine, LONG *VolMaster)
{
    if (VolWave)   *VolWave = 0;
    if (VolMidi)   *VolMidi = 0;
    if (VolCD)     *VolCD = 0;
    if (VolLine)   *VolLine = 0;
    if (VolMaster) *VolMaster = 0;
}
