/* mmsystem.c - MMSYSTEM.DLL: brak urzadzen dzwiekowych */
#include "w16.h"

BOOL API sndPlaySound(LPCSTR s, WORD fl) { (void)s; (void)fl; return FALSE; }
BOOL API PlaySound(LPCSTR s, WORD m, DWORD fl) { (void)s; (void)m; (void)fl; return FALSE; }
WORD API waveOutGetNumDevs(void) { return 0; }
WORD API midiOutGetNumDevs(void) { return 0; }
WORD API auxGetNumDevs(void)     { return 0; }
DWORD API timeGetTime(void)      { return GetTickCount(); }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
