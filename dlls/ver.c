/* ver.c - VER.DLL: pliki bez zasobu VERSIONINFO (aplikacje traktuja to jako "brak wersji") */
#include "w16.h"

DWORD API GetFileVersionInfoSize(LPCSTR f, DWORD __far *h) { (void)f; if (h) *h = 0; return 0; }
BOOL  API GetFileVersionInfo(LPCSTR f, DWORD h, DWORD n, LPVOID d) { (void)f; (void)h; (void)n; (void)d; return FALSE; }
BOOL  API VerQueryValue(LPVOID b, LPCSTR s, LPVOID __far *p, WORD __far *n)
{
    (void)b; (void)s;
    if (p) *p = 0;
    if (n) *n = 0;
    return FALSE;
}
DWORD API VerLanguageName(WORD l, LPSTR s, WORD n) { (void)l; if (s && n) { w_strncpy(s, "English (United States)", n); return w_strlen(s); } return 0; }
DWORD API GetFileResourceSize(LPCSTR f, LPCSTR t, LPCSTR i, DWORD __far *o) { (void)f; (void)t; (void)i; if (o) *o = 0; return 0; }
BOOL  API GetFileResource(LPCSTR f, LPCSTR t, LPCSTR i, DWORD o, DWORD n, LPVOID d) { (void)f; (void)t; (void)i; (void)o; (void)n; (void)d; return FALSE; }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
