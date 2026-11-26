/* wing.c - WING.DLL: Microsoft WinG (ordinale 1001..1010)
 *
 * Implementacja 8bpp DIB-section nad GDI.EXE:
 *  - WinGCreateDC() zwraca prawdziwy DC pamieci (CreateCompatibleDC(0))
 *  - WinGCreateBitmap() alokuje bufor pikseli w XMS (GlobalAlloc, rowniez >64KB)
 *    i tworzy 1x1 dummy HBITMAP w GDI, zeby SelectObject/DeleteObject dzialaly
 *    natywnie po stronie GDI.EXE.
 *  - WinGSetDIBColorTable/WinGGetDIBColorTable modyfikuja tablice RGBQUAD[256]
 *  - WinGBitBlt/WinGStretchBlt przerzucaja bufor przez StretchDIBits.
 */
#include "w16.h"

#define MAX_WING_BMPS   64
#define SRCCOPY         0x00CC0020UL
#define DIB_RGB_COLORS  0
#define DIB_DEVIDX      0x8000   /* prywatne GDI: indeksy = sloty DAC na ekranie 8bpp */
#define GMEM_ZEROINIT   0x0040

typedef struct {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[256];
} BITMAPINFO256;

typedef struct {
    HBITMAP hbm;        /* uchwyt dummy HBITMAP w GDI */
    HDC     last_dc;    /* ostatni WinG DC (fallback przed SelectObject) */
    WORD    hbits;      /* GlobalAlloc uchwyt pikseli */
    LPVOID  bits;       /* GlobalLock wskaznik pikseli */
    WORD    hbi;        /* GlobalAlloc uchwyt BITMAPINFO256 */
    LONG    w, h;       /* wymiary (h moze byc ujemne = top-down) */
    BOOL    blitted;    /* czy ta bitmapa byla juz zblitowana na ekran */
} WINGBMP;

static WINGBMP g_bmps[MAX_WING_BMPS];
static HBITMAP g_probe_bmp = 0;

BOOL API WinGBitBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                    HDC hdcSrc, int xSrc, int ySrc);

static WINGBMP __far *find_by_hbm(HBITMAP hbm)
{
    int i;
    if (!hbm) return NULL;
    for (i = 0; i < MAX_WING_BMPS; i++)
        if (g_bmps[i].hbm == hbm) return &g_bmps[i];
    return NULL;
}

static WINGBMP __far *find_by_dc(HDC hdc)
{
    int i;
    if (!hdc) return NULL;
    if (!g_probe_bmp) g_probe_bmp = CreateBitmap(1, 1, 1, 1, NULL);
    if (g_probe_bmp) {
        HBITMAP cur = (HBITMAP)SelectObject(hdc, g_probe_bmp);
        if (cur) {
            SelectObject(hdc, cur);
            {
                WINGBMP __far *b = find_by_hbm(cur);
                if (b) return b;
            }
        }
    }
    for (i = 0; i < MAX_WING_BMPS; i++)
        if (g_bmps[i].hbm && g_bmps[i].last_dc == hdc) return &g_bmps[i];
    return NULL;
}

/* ordinal 1001 */
HDC API WinGCreateDC(void)
{
    HDC dc = CreateCompatibleDC(0);
    dbg("WING: WinGCreateDC -> "); dbg_hex(dc, 4); dbg("\n");
    return dc;
}

/* ordinal 1002 */
BOOL API WinGRecommendDIBFormat(LPVOID pFormat)
{
    BITMAPINFOHEADER __far *bh = (BITMAPINFOHEADER __far *)pFormat;
    if (!bh) return FALSE;
    w_memset(bh, 0, sizeof(BITMAPINFOHEADER));
    bh->biSize = sizeof(BITMAPINFOHEADER);
    bh->biWidth = 1;
    bh->biHeight = -1;      /* top-down 8bpp */
    bh->biPlanes = 1;
    bh->biBitCount = 8;
    bh->biCompression = 0;  /* BI_RGB */
    dbg("WING: WinGRecommendDIBFormat -> top-down 8bpp\n");
    return TRUE;
}

/* ordinal 1003 */
HBITMAP API WinGCreateBitmap(HDC hWinGDC, LPVOID pHeader, LPVOID __far *ppBits)
{
    BITMAPINFO256 __far *src = (BITMAPINFO256 __far *)pHeader;
    BITMAPINFO256 __far *dst;
    LONG w, h, ah;
    WORD bpp;
    DWORD stride, img_sz;
    WORD hbits, hbi;
    LPVOID bits;
    HBITMAP hbm;
    int slot = -1, i;

    if (!src || src->bmiHeader.biSize < sizeof(BITMAPINFOHEADER)) return 0;
    w = src->bmiHeader.biWidth;
    h = src->bmiHeader.biHeight;
    ah = h < 0 ? -h : h;
    bpp = src->bmiHeader.biBitCount ? src->bmiHeader.biBitCount : 8;
    if (w <= 0 || ah <= 0) return 0;

    for (i = 0; i < MAX_WING_BMPS; i++)
        if (!g_bmps[i].hbm) { slot = i; break; }
    if (slot < 0) return 0;

    stride = (((DWORD)w * bpp + 31UL) >> 5) << 2;
    img_sz = stride * (DWORD)ah;

    hbits = GlobalAlloc(GMEM_ZEROINIT, img_sz);
    if (!hbits) {
        dbg("WING: WinGCreateBitmap GlobalAlloc FAIL sz="); dbg_hex(img_sz, 8); dbg("\n");
        return 0;
    }
    bits = GlobalLock(hbits);

    hbi = GlobalAlloc(GMEM_ZEROINIT, sizeof(BITMAPINFO256));
    if (!hbi) {
        GlobalFree(hbits);
        return 0;
    }
    dst = (BITMAPINFO256 __far *)GlobalLock(hbi);
    w_memcpy(&dst->bmiHeader, &src->bmiHeader, sizeof(BITMAPINFOHEADER));
    dst->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dst->bmiHeader.biPlanes = 1;
    dst->bmiHeader.biBitCount = bpp;
    dst->bmiHeader.biCompression = 0;
    dst->bmiHeader.biSizeImage = img_sz;
    if (bpp <= 8) {
        WORD nc = (WORD)(src->bmiHeader.biClrUsed ? src->bmiHeader.biClrUsed : (1u << bpp));
        if (nc > 256) nc = 256;
        w_memcpy(dst->bmiColors, src->bmiColors, nc * sizeof(RGBQUAD));
    }

    hbm = CreateBitmap(1, 1, 1, 1, NULL);
    if (!hbm) {
        GlobalFree(hbi);
        GlobalFree(hbits);
        return 0;
    }

    /* Jesli GDI oddalo ten sam uchwyt po DeleteObject(), zwolnij stary slot i uzyj go ponownie */
    for (i = 0; i < MAX_WING_BMPS; i++) {
        if (g_bmps[i].hbm == hbm) {
            if (g_bmps[i].hbits) GlobalFree(g_bmps[i].hbits);
            if (g_bmps[i].hbi) GlobalFree(g_bmps[i].hbi);
            slot = i;
            break;
        }
    }

    g_bmps[slot].hbm = hbm;
    g_bmps[slot].last_dc = hWinGDC;
    g_bmps[slot].hbits = hbits;
    g_bmps[slot].bits = bits;
    g_bmps[slot].hbi = hbi;
    g_bmps[slot].w = w;
    g_bmps[slot].h = h;
    g_bmps[slot].blitted = FALSE;

    if (ppBits) *ppBits = bits;

    dbg("WING: WinGCreateBitmap hbm="); dbg_hex(hbm, 4);
    dbg(" "); dbg_hex((DWORD)w, 4); dbg("x"); dbg_hex((DWORD)ah, 4);
    dbg(h < 0 ? " top-down" : " bot-up");
    dbg(" bits="); dbg_hex((DWORD)bits, 8); dbg("\n");
    return hbm;
}

/* ordinal 1004 */
LPVOID API WinGGetDIBPointer(HBITMAP hWinGBitmap, LPVOID pHeader)
{
    WINGBMP __far *b = find_by_hbm(hWinGBitmap);
    if (!b) return NULL;
    if (pHeader) {
        BITMAPINFO256 __far *bi = (BITMAPINFO256 __far *)GlobalLock(b->hbi);
        if (bi) w_memcpy(pHeader, &bi->bmiHeader, sizeof(BITMAPINFOHEADER));
    }
    return b->bits;
}

/* ordinal 1005 */
UINT API WinGGetDIBColorTable(HDC hWinGDC, UINT StartIndex, UINT NumberOfEntries, LPVOID pColors)
{
    WINGBMP __far *b = find_by_dc(hWinGDC);
    BITMAPINFO256 __far *bi;
    UINT n;
    if (!b || !pColors || StartIndex >= 256) return 0;
    bi = (BITMAPINFO256 __far *)GlobalLock(b->hbi);
    if (!bi) return 0;
    n = NumberOfEntries;
    if (StartIndex + n > 256) n = 256 - StartIndex;
    w_memcpy(pColors, &bi->bmiColors[StartIndex], (WORD)(n * sizeof(RGBQUAD)));
    return n;
}

/* ordinal 1006 */
UINT API WinGSetDIBColorTable(HDC hWinGDC, UINT StartIndex, UINT NumberOfEntries, LPVOID pColors)
{
    WINGBMP __far *b = find_by_dc(hWinGDC);
    BITMAPINFO256 __far *bi;
    UINT n;
    if (!b || !pColors || StartIndex >= 256) return 0;
    bi = (BITMAPINFO256 __far *)GlobalLock(b->hbi);
    if (!bi) return 0;
    n = NumberOfEntries;
    if (StartIndex + n > 256) n = 256 - StartIndex;
    w_memcpy(&bi->bmiColors[StartIndex], pColors, (WORD)(n * sizeof(RGBQUAD)));
    return n;
}

/* ordinal 1007 */
HPALETTE API WinGCreateHalfTonePalette(void)
{
    WORD hmem = GlobalAlloc(GMEM_ZEROINIT, 4 + 256 * 4);
    HPALETTE hpal;
    WORD __far *lp;
    BYTE __far *e;
    int r, g, b, idx = 0;
    if (!hmem) return 0;
    lp = (WORD __far *)GlobalLock(hmem);
    lp[0] = 0x0300;
    lp[1] = 256;
    e = (BYTE __far *)(lp + 2);
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++) {
                e[idx * 4 + 0] = (BYTE)(r * 51);
                e[idx * 4 + 1] = (BYTE)(g * 51);
                e[idx * 4 + 2] = (BYTE)(b * 51);
                e[idx * 4 + 3] = 0;
                idx++;
            }
    hpal = CreatePalette(lp);
    GlobalFree(hmem);
    return hpal;
}

/* ordinal 1008 */
HBRUSH API WinGCreateHalfToneBrush(HDC Context, COLORREF crColor, UINT DitherType)
{
    (void)Context; (void)DitherType;
    return CreateSolidBrush(crColor);
}

/* ordinal 1009 */
BOOL API WinGStretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                        HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc)
{
    static int log_n = 0;
    WINGBMP __far *b = find_by_dc(hdcSrc);
    BITMAPINFO256 __far *bi;
    LONG ah;
    int sy_dib;
    if (log_n < 5) {
        log_n++;
        dbg("WING: Blt dst="); dbg_hex(hdcDest, 4);
        dbg(" src="); dbg_hex(hdcSrc, 4);
        dbg(" b="); dbg_hex((DWORD)b, 8);
        dbg(" wh="); dbg_hex(wDest, 4); dbg("x"); dbg_hex(hDest, 4); dbg("\n");
    }
    if (!b) return FALSE;
    b->blitted = TRUE;
    bi = (BITMAPINFO256 __far *)GlobalLock(b->hbi);
    if (!bi) return FALSE;
    ah = b->h < 0 ? -b->h : b->h;
    /* StretchDIBits przyjmuje ySrc liczone od dolu DIB; WinG liczy od gory */
    sy_dib = (int)(ah - (LONG)ySrc - (LONG)hSrc);
    return StretchDIBits(hdcDest, xDest, yDest, wDest, hDest,
                         xSrc, sy_dib, wSrc, hSrc,
                         b->bits, bi, DIB_RGB_COLORS | DIB_DEVIDX, SRCCOPY) > 0;
}

/* ordinal 1010 */
BOOL API WinGBitBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                    HDC hdcSrc, int xSrc, int ySrc)
{
    return WinGStretchBlt(hdcDest, xDest, yDest, wDest, hDest,
                          hdcSrc, xSrc, ySrc, wDest, hDest);
}

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd)
{
    (void)hi; (void)ds; (void)heap; (void)cmd;
    return 1;
}
