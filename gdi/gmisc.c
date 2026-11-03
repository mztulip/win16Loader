/* gmisc.c - GDI: palety (ekran true color), metapliki i drukowanie (brak),
 * Escape i drobne funkcje */
#include "gdi.h"

typedef struct { BYTE r, g, b, flags; } PALENTRY;

/* ordinal 360 */
HPALETTE API CreatePalette(WORD __far *lp)
{
    WORD h = obj_new(OT_PAL), n = lp[1], i;
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    if (!p) return 0;
    p->n = n;
    p->mem = GlobalAlloc(0, (DWORD)n * 4 + 4);
    if (p->mem) {
        PALENTRY __far *d = (PALENTRY __far *)MK_FP(p->mem, 0);
        PALENTRY __far *s = (PALENTRY __far *)(lp + 2);
        for (i = 0; i < n; i++) d[i] = s[i];
    }
    return h;
}

/* ordinal 363 */
WORD API GetPaletteEntries(HPALETTE h, WORD start, WORD n, PALENTRY __far *out)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    WORD i;
    if (!p) return 0;
    if (!p->mem) {                     /* paleta domyslna: 20 kolorow systemowych */
        static const BYTE sys[20][3] = {
            {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
            {192,220,192},{166,202,240},{255,251,240},{160,160,164},{128,128,128},{255,0,0},
            {0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
        if (!out) return 20;
        for (i = 0; i < n && start + i < 20; i++) {
            out[i].r = sys[start + i][0]; out[i].g = sys[start + i][1];
            out[i].b = sys[start + i][2]; out[i].flags = 0;
        }
        return i;
    }
    if (!out) return p->n;
    for (i = 0; i < n && start + i < p->n; i++)
        out[i] = ((PALENTRY __far *)MK_FP(p->mem, 0))[start + i];
    return i;
}

/* ordinal 364 */
WORD API SetPaletteEntries(HPALETTE h, WORD start, WORD n, PALENTRY __far *in)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    WORD i;
    if (!p || !p->mem) return 0;
    for (i = 0; i < n && start + i < p->n; i++)
        ((PALENTRY __far *)MK_FP(p->mem, 0))[start + i] = in[i];
    return i;
}

/* ordinal 375 */
WORD API GetSystemPaletteEntries(HDC hdc, WORD start, WORD n, PALENTRY __far *out)
{
    (void)hdc;
    return GetPaletteEntries(GetStockObject(15), start, n, out);
}

/* ordinal 361/362 (wolane przez USER SelectPalette/RealizePalette) */
HPALETTE API GDISelectPalette(HDC hdc, HPALETTE h, BOOL bg)
{
    DCOBJ __far *dc = dc_get(hdc);
    WORD o;
    (void)bg;
    if (!dc) return 0;
    o = dc->pal;
    dc->pal = h;
    return o;
}
WORD API GDIRealizePalette(HDC hdc)  { (void)hdc; return 0; }
WORD API RealizeDefaultPalette(HDC hdc) { (void)hdc; return 0; }
BOOL API AnimatePalette(HPALETTE h, WORD s, WORD n, LPVOID e) { (void)h; (void)s; (void)n; (void)e; return TRUE; }
BOOL API ResizePalette(HPALETTE h, WORD n) { (void)h; (void)n; return TRUE; }
int  API UpdateColors(HDC hdc)       { (void)hdc; return 0; }
WORD API GetNearestPaletteIndex(HPALETTE h, COLORREF c) { (void)h; (void)c; return 0; }
/* ordinal 154: ekran true color - kolor dokladny */
COLORREF API GetNearestColor(HDC hdc, COLORREF c) { (void)hdc; return c & 0xFFFFFFUL; }
WORD API SetSystemPaletteUse(HDC hdc, WORD u) { (void)hdc; (void)u; return 1; }
WORD API GetSystemPaletteUse(HDC hdc) { (void)hdc; return 1; }

/* ============================================================
 * Metapliki - nieobslugiwane (aplikacje sprawdzaja blad)
 * ============================================================ */
HDC    API CreateMetaFile(LPCSTR f)     { (void)f; return 0; }
HANDLE API CloseMetaFile(HDC h)         { (void)h; return 0; }
BOOL   API DeleteMetaFile(HANDLE h)     { (void)h; return TRUE; }
BOOL   API PlayMetaFile(HDC hdc, HANDLE h) { (void)hdc; (void)h; return FALSE; }
HANDLE API GetMetaFile(LPCSTR f)        { (void)f; return 0; }
HANDLE API CopyMetaFile(HANDLE h, LPCSTR f) { (void)h; (void)f; return 0; }
HANDLE API GetMetaFileBits(HANDLE h)    { return h; }
HANDLE API SetMetaFileBits(HANDLE h)    { (void)h; return 0; }
HANDLE API SetMetaFileBitsBetter(HANDLE h) { (void)h; return 0; }
BOOL   API EnumMetaFile(HDC hdc, HANDLE h, LPVOID p, DWORD d) { (void)hdc; (void)h; (void)p; (void)d; return FALSE; }

/* ============================================================
 * Drukowanie - brak drukarek
 * ============================================================ */
int API Escape(HDC hdc, int esc, int n, LPCSTR in, LPVOID out)
{
    (void)hdc; (void)n; (void)in; (void)out;
    if (esc == 8) return 0;            /* QUERYESCSUPPORT: nic nie wspieramy */
    return 0;
}
int API StartDoc(HDC hdc, LPVOID di)   { (void)hdc; (void)di; return -1; }
int API EndDoc(HDC hdc)                { (void)hdc; return -1; }
int API StartPage(HDC hdc)             { (void)hdc; return -1; }
int API EndPage(HDC hdc)               { (void)hdc; return -1; }
int API AbortDoc(HDC hdc)              { (void)hdc; return -1; }
int API SetAbortProc(HDC hdc, LPVOID p){ (void)hdc; (void)p; return -1; }

/* ordinal 1-200 rozne */
BOOL API FastWindowFrame(HDC hdc, LPRECT r, int w, int h, DWORD rop) { (void)hdc; (void)r; (void)w; (void)h; (void)rop; return FALSE; }
int  API GetEnvironment(LPCSTR port, LPVOID buf, WORD n) { (void)port; (void)buf; (void)n; return 0; }
int  API SetEnvironment(LPCSTR port, LPVOID buf, WORD n) { (void)port; (void)buf; (void)n; return 0; }

/* LibMain - nieuzywane (loader nie wola LibEntry systemowych DLL) */
int API LibMain(WORD a, WORD b, WORD c, LPCSTR d) { (void)a; (void)b; (void)c; (void)d; return 1; }
