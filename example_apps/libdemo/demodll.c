/* demodll.c - DLL zapamietujaca wywolanie LibEntry. Ten sam kod jako DEMODLL.DLL (import
 * statyczny), DEMODYN.DLL (LoadLibrary) i z -DFAIL jako DEMOBAD.DLL (LibEntry zwraca blad). */
#include <windows.h>
#include "demodll.h"

static DEMOINFO g_i;

int FAR PASCAL LibMain(HINSTANCE hi, WORD ds, WORD heap, LPSTR cmd)
{
    HLOCAL l;
    (void)cmd;
    g_i.calls++;
    g_i.hinst = (WORD)hi;
    g_i.ds = ds;
    g_i.heap = heap;
    l = LocalAlloc(LMEM_FIXED, 16);                /* sterta DLL dziala dopiero po LibEntry */
    g_i.heap_ok = l != 0;
    if (l) LocalFree(l);
#ifdef FAIL
    return 0;
#else
    return 1;
#endif
}

void FAR PASCAL __export DemoInfo(DEMOINFO FAR *out)
{
    *out = g_i;
}

int FAR PASCAL __export WEP(int code)
{
    (void)code;
    return 1;
}
