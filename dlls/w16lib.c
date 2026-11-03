/* w16lib.c - pomocnicze procedury wspolne dla malych DLL (bez clib) */
#include "w16.h"

void dbg(LPCSTR s) { OutputDebugString(s); }

void dbg_hex(DWORD v, int digits)
{
    char b[9];
    int i;
    if (digits > 8) digits = 8;
    for (i = digits - 1; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    b[digits] = 0;
    OutputDebugString((LPCSTR)(char __far *)b);
}

int w_strlen(LPCSTR s) { int n = 0; while (s[n]) n++; return n; }

void w_strcpy(LPSTR d, LPCSTR s) { while ((*d++ = *s++) != 0) ; }

void w_strncpy(LPSTR d, LPCSTR s, int n)
{
    if (n <= 0) return;
    while (--n > 0 && *s) *d++ = *s++;
    *d = 0;
}

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int w_stricmp(LPCSTR a, LPCSTR b)
{
    while (*a && up(*a) == up(*b)) { a++; b++; }
    return up((BYTE)*a) - up((BYTE)*b);
}

int w_strnicmp(LPCSTR a, LPCSTR b, int n)
{
    for (; n > 0; n--, a++, b++) {
        if (up(*a) != up(*b)) return up((BYTE)*a) - up((BYTE)*b);
        if (!*a) return 0;
    }
    return 0;
}

void w_memcpy(LPVOID d, const void __far *s, WORD n)
{
    BYTE __far *dp = (BYTE __far *)d;
    const BYTE __far *sp = (const BYTE __far *)s;
    while (n--) *dp++ = *sp++;
}

void w_memset(LPVOID d, BYTE v, WORD n)
{
    BYTE __far *dp = (BYTE __far *)d;
    while (n--) *dp++ = v;
}
