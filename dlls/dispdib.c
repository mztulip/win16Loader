/* dispdib.c - DISPDIB.DLL: DisplayDib (stub z logowaniem) */
#include "w16.h"

#define DISPLAYDIB_NOERROR 0

/* ordinal 1 */
WORD API DisplayDib(LPVOID lpbi, LPVOID lpBits, WORD wFlags)
{
    dbg("DISPDIB: DisplayDib flags="); dbg_hex(wFlags, 4);
    dbg(" bi="); dbg_hex((DWORD)lpbi, 8);
    dbg(" bits="); dbg_hex((DWORD)lpBits, 8);
    dbg("\n");
    return DISPLAYDIB_NOERROR;
}

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd)
{
    (void)hi; (void)ds; (void)heap; (void)cmd;
    return 1;
}
