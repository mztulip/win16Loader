/*
 * ICOVIEW.C - Icon Viewer for Win16 (Windows 3.1 / Win16Loader / Calmira II)
 *
 * Native 16-bit .ICO / .CUR viewer:
 *  - Parses single- and multi-image .ICO and .CUR files
 *  - Supports 1bpp, 4bpp, 8bpp, 24bpp, and 32bpp (BGRA) icon images
 *  - Left sidebar with thumbnails and format details (dimensions, bit depth)
 *  - Zoomable preview canvas (1x, 2x, 4x, 8x) with optional pixel grid
 *  - 1:1 actual-size inset preview when zoomed
 *  - Selectable background (Checkerboard, Desktop, White, Black, Gray)
 *  - Display modes: Composite (XOR+AND), XOR (Color) only, AND (Mask) only
 *  - Live pixel inspector (coordinates, RGB hex, Transparent/Invert) on hover
 *  - File -> Open (COMMDLG), Save as BMP (COMMDLG), Close, Exit
 *  - Command-line argument support for .ICO file association
 */

#include <windows.h>
#include <commdlg.h>
#include <string.h>
#include "icoview.h"

#define MAX_ICO_ENTRIES   32
#define SIDEBAR_WIDTH     148
#define STATUS_HEIGHT     20
#define ITEM_HEIGHT       44

#define BG_CHECKER        0
#define BG_DESKTOP        1
#define BG_WHITE          2
#define BG_BLACK          3
#define BG_GRAY           4

#define MODE_COMPOSITE    0
#define MODE_XOR          1
#define MODE_AND          2

typedef struct {
    int   width;
    int   height;
    int   bpp;
    int   colors;
    int   hotX;
    int   hotY;
    DWORD bytesInRes;
    DWORD imageOffset;
    BOOL  isPng;
    BOOL  unsupported;
    HBITMAP hThumb;      /* 32x32 max cached thumbnail (composited on light gray) */
    int   thumbW;
    int   thumbH;
} ICOENTRY;

typedef struct {
    int      width;
    int      height;
    int      stride24;
    BOOL     hasAlpha;
    HGLOBAL  hXorDib;    /* BITMAPINFOHEADER + 24bpp bottom-up BGR */
    HGLOBAL  hAndDib;    /* BITMAPINFOHEADER + 24bpp bottom-up (0xFFFFFF=transp, 0=opaque) */
    HGLOBAL  hAlpha;     /* width * height bytes (bottom-up), only if hasAlpha */
} DECODEDICO;

static HINSTANCE  g_hinst       = 0;
static HWND       g_hwndMain    = 0;
static HFONT      g_hFontUI     = 0;
static HFONT      g_hFontMono   = 0;

static char       g_szFilePath[128];
static char       g_szFileName[64];
static DWORD      g_dwFileSize  = 0;
static WORD       g_icoType     = 1;  /* 1 = ICO, 2 = CUR */
static int        g_entryCount  = 0;
static int        g_selEntry    = -1;
static ICOENTRY   g_entries[MAX_ICO_ENTRIES];
static DECODEDICO g_cur;

static int        g_zoom        = 4;
static int        g_bgMode      = BG_CHECKER;
static int        g_dispMode    = MODE_COMPOSITE;
static BOOL       g_showGrid    = FALSE;
static int        g_listScroll  = 0;

/* Hover pixel inspector state */
static BOOL       g_hoverValid  = FALSE;
static int        g_hoverX      = 0;
static int        g_hoverY      = 0;
static RECT       g_rcPreview   = {0, 0, 0, 0};

static const char g_szAppName[] = "IcoViewClass";

/* Little-endian helpers for far buffers */
static WORD rd16(const BYTE __far *p)
{
    return (WORD)((WORD)p[0] | ((WORD)p[1] << 8));
}

static DWORD rd32(const BYTE __far *p)
{
    return (DWORD)p[0] |
           ((DWORD)p[1] << 8) |
           ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

static void wr16(BYTE __far *p, WORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >> 8) & 0xFF);
}

static void wr32(BYTE __far *p, DWORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >> 8) & 0xFF);
    p[2] = (BYTE)((v >> 16) & 0xFF);
    p[3] = (BYTE)((v >> 24) & 0xFF);
}

static void FreeDecoded(DECODEDICO *d)
{
    if (d->hXorDib) { GlobalFree(d->hXorDib); d->hXorDib = 0; }
    if (d->hAndDib) { GlobalFree(d->hAndDib); d->hAndDib = 0; }
    if (d->hAlpha)  { GlobalFree(d->hAlpha);  d->hAlpha  = 0; }
    d->width = 0;
    d->height = 0;
    d->stride24 = 0;
    d->hasAlpha = FALSE;
}

static void FreeAllEntries(void)
{
    int i;
    FreeDecoded(&g_cur);
    for (i = 0; i < g_entryCount; i++) {
        if (g_entries[i].hThumb) {
            DeleteObject(g_entries[i].hThumb);
            g_entries[i].hThumb = 0;
        }
    }
    g_entryCount = 0;
    g_selEntry = -1;
    g_listScroll = 0;
    g_hoverValid = FALSE;
}

static void FillBih24(BYTE __far *bih, int w, int h, DWORD imgSize)
{
    int i;
    for (i = 0; i < 40; i++) bih[i] = 0;
    wr32(bih + 0,  40UL);
    wr32(bih + 4,  (DWORD)w);
    wr32(bih + 8,  (DWORD)h);
    wr16(bih + 12, 1);
    wr16(bih + 14, 24);
    wr32(bih + 16, 0UL);
    wr32(bih + 20, imgSize);
}

/* Read len bytes from hf into far buffer in safe 16KB chunks */
static BOOL ReadFarBytes(HFILE hf, BYTE __far *dst, DWORD len)
{
    DWORD rem = len;
    DWORD off = 0;
    while (rem > 0) {
        UINT chunk = (rem > 16384UL) ? 16384U : (UINT)rem;
        UINT got = _lread(hf, dst + off, chunk);
        if (got == HFILE_ERROR || got < chunk) return FALSE;
        off += chunk;
        rem -= chunk;
    }
    return TRUE;
}

/* Decode a single ICOENTRY from an open file handle into DECODEDICO */
static BOOL DecodeEntryFromFile(HFILE hf, const ICOENTRY *e, DECODEDICO *out)
{
    HGLOBAL hRaw = 0;
    BYTE __far *raw = NULL;
    BYTE __far *xorDib = NULL;
    BYTE __far *andDib = NULL;
    BYTE __far *alphaBuf = NULL;
    DWORD biSize, biComp, clrUsed;
    long biW, biH;
    WORD bpp;
    int w, h, x, y;
    DWORD xorStride, andStride, xorOff, andOff, dibImgSize;
    int stride24;
    const BYTE __far *pal;
    BOOL anyAlpha = FALSE;

    FreeDecoded(out);
    if (e->isPng || e->unsupported || e->bytesInRes < 40 || e->bytesInRes > 65000UL) {
        return FALSE;
    }

    hRaw = GlobalAlloc(GHND, e->bytesInRes);
    if (!hRaw) return FALSE;
    raw = (BYTE __far *)GlobalLock(hRaw);
    if (!raw) { GlobalFree(hRaw); return FALSE; }

    _llseek(hf, (long)e->imageOffset, 0);
    if (!ReadFarBytes(hf, raw, e->bytesInRes)) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    biSize = rd32(raw + 0);
    biW    = (long)rd32(raw + 4);
    biH    = (long)rd32(raw + 8);
    bpp    = rd16(raw + 14);
    biComp = rd32(raw + 16);
    clrUsed = rd32(raw + 32);

    if (biSize < 40 || biComp != 0) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    w = (int)biW;
    if (biH < 0) biH = -biH;
    h = (int)(biH / 2);
    if (w <= 0) w = e->width;
    if (h <= 0) h = e->height;
    if (w <= 0 || h <= 0 || w > 128 || h > 128) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    if (bpp <= 8 && clrUsed == 0) {
        clrUsed = 1UL << bpp;
    }
    if (bpp > 8) clrUsed = 0;

    pal = raw + biSize;
    xorOff = biSize + clrUsed * 4UL;
    xorStride = (((DWORD)w * (DWORD)bpp + 31UL) >> 5) << 2;
    andOff = xorOff + xorStride * (DWORD)h;
    andStride = (((DWORD)w + 31UL) >> 5) << 2;

    if (xorOff + xorStride * (DWORD)h > e->bytesInRes) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    stride24 = ((w * 3 + 3) & ~3);
    dibImgSize = (DWORD)stride24 * (DWORD)h;
    if (40UL + dibImgSize > 65000UL) {
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    out->hXorDib = GlobalAlloc(GHND, 40UL + dibImgSize);
    out->hAndDib = GlobalAlloc(GHND, 40UL + dibImgSize);
    if (!out->hXorDib || !out->hAndDib) {
        FreeDecoded(out);
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
        return FALSE;
    }

    if (bpp == 32) {
        out->hAlpha = GlobalAlloc(GHND, (DWORD)w * (DWORD)h);
        if (out->hAlpha) {
            alphaBuf = (BYTE __far *)GlobalLock(out->hAlpha);
        }
    }

    xorDib = (BYTE __far *)GlobalLock(out->hXorDib);
    andDib = (BYTE __far *)GlobalLock(out->hAndDib);

    FillBih24(xorDib, w, h, dibImgSize);
    FillBih24(andDib, w, h, dibImgSize);

    /* Decode XOR rows (bottom-up, same order as ICO DIB) */
    for (y = 0; y < h; y++) {
        const BYTE __far *srcRow = raw + xorOff + (DWORD)y * xorStride;
        BYTE __far *dstRow = xorDib + 40 + (DWORD)y * (DWORD)stride24;
        for (x = 0; x < w; x++) {
            BYTE r = 0, g = 0, b = 0;
            if (bpp == 1) {
                BYTE idx = (BYTE)((srcRow[x >> 3] >> (7 - (x & 7))) & 1);
                if (idx < clrUsed) {
                    b = pal[idx * 4 + 0];
                    g = pal[idx * 4 + 1];
                    r = pal[idx * 4 + 2];
                } else {
                    r = g = b = idx ? 255 : 0;
                }
            } else if (bpp == 4) {
                BYTE bv = srcRow[x >> 1];
                BYTE idx = (x & 1) ? (BYTE)(bv & 0x0F) : (BYTE)(bv >> 4);
                if (idx < clrUsed) {
                    b = pal[idx * 4 + 0];
                    g = pal[idx * 4 + 1];
                    r = pal[idx * 4 + 2];
                }
            } else if (bpp == 8) {
                BYTE idx = srcRow[x];
                if (idx < clrUsed) {
                    b = pal[idx * 4 + 0];
                    g = pal[idx * 4 + 1];
                    r = pal[idx * 4 + 2];
                }
            } else if (bpp == 24) {
                b = srcRow[x * 3 + 0];
                g = srcRow[x * 3 + 1];
                r = srcRow[x * 3 + 2];
            } else if (bpp == 32) {
                BYTE a;
                b = srcRow[x * 4 + 0];
                g = srcRow[x * 4 + 1];
                r = srcRow[x * 4 + 2];
                a = srcRow[x * 4 + 3];
                if (a != 0) anyAlpha = TRUE;
                if (alphaBuf) alphaBuf[(DWORD)y * (DWORD)w + (DWORD)x] = a;
            }
            dstRow[x * 3 + 0] = b;
            dstRow[x * 3 + 1] = g;
            dstRow[x * 3 + 2] = r;
        }
    }

    /* Decode AND mask (bottom-up, 1bpp: 1 = transparent/white, 0 = opaque/black) */
    for (y = 0; y < h; y++) {
        BYTE __far *dstRow = andDib + 40 + (DWORD)y * (DWORD)stride24;
        BOOL hasAndRow = (andOff + (DWORD)(y + 1) * andStride <= e->bytesInRes);
        const BYTE __far *srcRow = hasAndRow ? (raw + andOff + (DWORD)y * andStride) : NULL;
        for (x = 0; x < w; x++) {
            BYTE bit = 0;
            if (srcRow) {
                bit = (BYTE)((srcRow[x >> 3] >> (7 - (x & 7))) & 1);
            }
            dstRow[x * 3 + 0] = bit ? 255 : 0;
            dstRow[x * 3 + 1] = bit ? 255 : 0;
            dstRow[x * 3 + 2] = bit ? 255 : 0;
        }
    }

    if (alphaBuf) {
        GlobalUnlock(out->hAlpha);
        if (!anyAlpha) {
            GlobalFree(out->hAlpha);
            out->hAlpha = 0;
        }
    }
    GlobalUnlock(out->hXorDib);
    GlobalUnlock(out->hAndDib);
    GlobalUnlock(hRaw);
    GlobalFree(hRaw);

    out->width = w;
    out->height = h;
    out->stride24 = stride24;
    out->hasAlpha = anyAlpha;
    return TRUE;
}

/* Render a DECODEDICO into hdc at (dx, dy, dw, dh) using mode & bg */
static void DrawBackground(HDC hdc, int x, int y, int w, int h, int bgMode)
{
    RECT rc;
    rc.left = x; rc.top = y; rc.right = x + w; rc.bottom = y + h;

    if (bgMode == BG_CHECKER) {
        HBRUSH brLight = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH brDark  = CreateSolidBrush(RGB(204, 204, 204));
        int tile = 8;
        int ty, tx;
        FillRect(hdc, &rc, brLight);
        for (ty = 0; ty < h; ty += tile) {
            for (tx = 0; tx < w; tx += tile) {
                if (((tx / tile) + (ty / tile)) & 1) {
                    RECT tr;
                    tr.left   = x + tx;
                    tr.top    = y + ty;
                    tr.right  = x + tx + tile;
                    tr.bottom = y + ty + tile;
                    if (tr.right > x + w) tr.right = x + w;
                    if (tr.bottom > y + h) tr.bottom = y + h;
                    FillRect(hdc, &tr, brDark);
                }
            }
        }
        DeleteObject(brLight);
        DeleteObject(brDark);
    } else {
        COLORREF col;
        HBRUSH br;
        switch (bgMode) {
        case BG_DESKTOP: col = GetSysColor(COLOR_BACKGROUND); break;
        case BG_WHITE:   col = RGB(255, 255, 255); break;
        case BG_BLACK:   col = RGB(0, 0, 0); break;
        case BG_GRAY:    col = RGB(192, 192, 192); break;
        default:         col = RGB(255, 255, 255); break;
        }
        br = CreateSolidBrush(col);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
    }
}

static COLORREF GetSolidBgColor(int bgMode)
{
    switch (bgMode) {
    case BG_DESKTOP: return GetSysColor(COLOR_BACKGROUND);
    case BG_WHITE:   return RGB(255, 255, 255);
    case BG_BLACK:   return RGB(0, 0, 0);
    case BG_GRAY:    return RGB(192, 192, 192);
    default:         return RGB(230, 230, 230);
    }
}

static void RenderDecodedIcon(HDC hdc, const DECODEDICO *d, int dx, int dy, int dw, int dh,
                              int dispMode, int bgMode)
{
    BYTE __far *xorDib;
    BYTE __far *andDib;

    if (!d->hXorDib || !d->hAndDib || d->width <= 0 || d->height <= 0) return;

    xorDib = (BYTE __far *)GlobalLock(d->hXorDib);
    andDib = (BYTE __far *)GlobalLock(d->hAndDib);

    if (dispMode == MODE_XOR) {
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, d->width, d->height,
                      xorDib + 40, (BITMAPINFO FAR *)xorDib, DIB_RGB_COLORS, SRCCOPY);
    } else if (dispMode == MODE_AND) {
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, d->width, d->height,
                      andDib + 40, (BITMAPINFO FAR *)andDib, DIB_RGB_COLORS, SRCCOPY);
    } else if (d->hasAlpha && d->hAlpha) {
        /* 32-bit alpha blend against chosen background into a temporary 24-bit DIB */
        DWORD dibImgSize = (DWORD)d->stride24 * (DWORD)d->height;
        HGLOBAL hComp = GlobalAlloc(GHND, 40UL + dibImgSize);
        if (hComp) {
            BYTE __far *comp = (BYTE __far *)GlobalLock(hComp);
            BYTE __far *alpha = (BYTE __far *)GlobalLock(d->hAlpha);
            COLORREF bg = GetSolidBgColor(bgMode);
            int x, y;
            FillBih24(comp, d->width, d->height, dibImgSize);
            for (y = 0; y < d->height; y++) {
                const BYTE __far *sr = xorDib + 40 + (DWORD)y * (DWORD)d->stride24;
                BYTE __far *dr = comp + 40 + (DWORD)y * (DWORD)d->stride24;
                for (x = 0; x < d->width; x++) {
                    BYTE a = alpha[(DWORD)y * (DWORD)d->width + (DWORD)x];
                    BYTE br, bgc, bb;
                    if (bgMode == BG_CHECKER) {
                        BYTE c = (((x >> 2) + (y >> 2)) & 1) ? 204 : 255;
                        br = bgc = bb = c;
                    } else {
                        br  = GetRValue(bg);
                        bgc = GetGValue(bg);
                        bb  = GetBValue(bg);
                    }
                    dr[x * 3 + 0] = (BYTE)(((WORD)sr[x * 3 + 0] * a + (WORD)bb  * (255 - a)) / 255);
                    dr[x * 3 + 1] = (BYTE)(((WORD)sr[x * 3 + 1] * a + (WORD)bgc * (255 - a)) / 255);
                    dr[x * 3 + 2] = (BYTE)(((WORD)sr[x * 3 + 2] * a + (WORD)br  * (255 - a)) / 255);
                }
            }
            StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, d->width, d->height,
                          comp + 40, (BITMAPINFO FAR *)comp, DIB_RGB_COLORS, SRCCOPY);
            GlobalUnlock(d->hAlpha);
            GlobalUnlock(hComp);
            GlobalFree(hComp);
        }
    } else {
        /* Standard Win16 icon composite: Background -> SRCAND(AND) -> SRCINVERT(XOR) */
        DrawBackground(hdc, dx, dy, dw, dh, bgMode);
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, d->width, d->height,
                      andDib + 40, (BITMAPINFO FAR *)andDib, DIB_RGB_COLORS, SRCAND);
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, d->width, d->height,
                      xorDib + 40, (BITMAPINFO FAR *)xorDib, DIB_RGB_COLORS, SRCINVERT);
    }

    GlobalUnlock(d->hAndDib);
    GlobalUnlock(d->hXorDib);
}

/* Create a cached thumbnail HBITMAP (up to 32x32) for sidebar display */
static void BuildEntryThumbnail(HWND hwnd, HFILE hf, ICOENTRY *e)
{
    DECODEDICO tmp;
    HDC hdc, mdc;
    HBITMAP hbm, old;
    int tw, th;

    tmp.hXorDib = 0; tmp.hAndDib = 0; tmp.hAlpha = 0;
    if (!DecodeEntryFromFile(hf, e, &tmp)) return;

    tw = tmp.width;
    th = tmp.height;
    if (tw > 32 || th > 32) {
        if (tw >= th) {
            th = (int)((long)th * 32L / tw);
            tw = 32;
        } else {
            tw = (int)((long)tw * 32L / th);
            th = 32;
        }
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }

    hdc = GetDC(hwnd);
    mdc = CreateCompatibleDC(hdc);
    hbm = CreateCompatibleBitmap(hdc, tw, th);
    if (hbm) {
        old = SelectObject(mdc, hbm);
        RenderDecodedIcon(mdc, &tmp, 0, 0, tw, th, MODE_COMPOSITE, BG_GRAY);
        SelectObject(mdc, old);
        e->hThumb = hbm;
        e->thumbW = tw;
        e->thumbH = th;
    }
    DeleteDC(mdc);
    ReleaseDC(hwnd, hdc);
    FreeDecoded(&tmp);
}

static void UpdateMenuChecks(HWND hwnd)
{
    HMENU hm = GetMenu(hwnd);
    if (!hm) return;

    CheckMenuItem(hm, IDM_VIEW_ZOOM1, (g_zoom == 1) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_VIEW_ZOOM2, (g_zoom == 2) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_VIEW_ZOOM4, (g_zoom == 4) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_VIEW_ZOOM8, (g_zoom == 8) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_VIEW_GRID,  g_showGrid    ? MF_CHECKED : MF_UNCHECKED);

    CheckMenuItem(hm, IDM_BG_CHECKER, (g_bgMode == BG_CHECKER) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_BG_DESKTOP, (g_bgMode == BG_DESKTOP) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_BG_WHITE,   (g_bgMode == BG_WHITE)   ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_BG_BLACK,   (g_bgMode == BG_BLACK)   ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_BG_GRAY,    (g_bgMode == BG_GRAY)    ? MF_CHECKED : MF_UNCHECKED);

    CheckMenuItem(hm, IDM_MODE_COMPOSITE, (g_dispMode == MODE_COMPOSITE) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_MODE_XOR,       (g_dispMode == MODE_XOR)       ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hm, IDM_MODE_AND,       (g_dispMode == MODE_AND)       ? MF_CHECKED : MF_UNCHECKED);
}

static void UpdateTitle(HWND hwnd)
{
    char szTitle[140];
    if (g_szFileName[0]) {
        wsprintf(szTitle, "Icon Viewer - %s (%d %s)",
                 (LPSTR)g_szFileName,
                 g_entryCount,
                 (LPSTR)(g_entryCount == 1 ? "image" : "images"));
    } else {
        lstrcpy(szTitle, "Icon Viewer");
    }
    SetWindowText(hwnd, szTitle);
}

static void SelectEntry(HWND hwnd, int idx)
{
    HFILE hf;
    if (idx < 0 || idx >= g_entryCount) return;
    g_selEntry = idx;
    g_hoverValid = FALSE;

    hf = _lopen(g_szFilePath, 0);
    if (hf != HFILE_ERROR) {
        DecodeEntryFromFile(hf, &g_entries[idx], &g_cur);
        _lclose(hf);
    } else {
        FreeDecoded(&g_cur);
    }

    /* Pick a sensible default zoom if current zoom would overflow or be tiny */
    if (g_cur.width >= 64 && g_zoom > 4) g_zoom = 4;
    if (g_cur.width >= 128 && g_zoom > 2) g_zoom = 2;

    UpdateMenuChecks(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void CloseCurrentFile(HWND hwnd)
{
    FreeAllEntries();
    g_szFilePath[0] = 0;
    g_szFileName[0] = 0;
    g_dwFileSize = 0;
    UpdateTitle(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static BOOL OpenIconFile(HWND hwnd, LPCSTR szPath)
{
    HFILE hf;
    BYTE hdr[6];
    BYTE ent[16];
    BYTE imgHdr[40];
    WORD reserved, type, count;
    long fsz;
    int i, bestIdx;
    LPCSTR p, last;

    if (!szPath || !szPath[0]) return FALSE;

    hf = _lopen(szPath, 0 /* OF_READ */);
    if (hf == HFILE_ERROR) {
        char msg[160];
        wsprintf(msg, "Cannot open file:\n%s", (LPSTR)szPath);
        MessageBox(hwnd, msg, "Icon Viewer", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    fsz = _llseek(hf, 0L, 2);
    _llseek(hf, 0L, 0);

    if (fsz < 6 || _lread(hf, hdr, 6) != 6) {
        _lclose(hf);
        MessageBox(hwnd, "File is too small to be a valid icon.", "Icon Viewer", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    reserved = rd16(hdr + 0);
    type     = rd16(hdr + 2);
    count    = rd16(hdr + 4);

    if (reserved != 0 || (type != 1 && type != 2) || count == 0) {
        _lclose(hf);
        MessageBox(hwnd, "Not a valid .ICO or .CUR file.", "Icon Viewer", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    FreeAllEntries();
    g_icoType = type;
    g_dwFileSize = (DWORD)fsz;
    lstrcpy(g_szFilePath, szPath);

    last = szPath;
    for (p = szPath; *p; p++) {
        if (*p == '\\' || *p == '/' || *p == ':') last = p + 1;
    }
    lstrcpy(g_szFileName, last);

    if (count > MAX_ICO_ENTRIES) count = MAX_ICO_ENTRIES;

    for (i = 0; i < (int)count; i++) {
        ICOENTRY *e = &g_entries[i];
        _fmemset(e, 0, sizeof(ICOENTRY));
        _llseek(hf, 6L + (long)i * 16L, 0);
        if (_lread(hf, ent, 16) != 16) break;

        e->width       = ent[0] ? (int)ent[0] : 256;
        e->height      = ent[1] ? (int)ent[1] : 256;
        e->colors      = ent[2];
        e->hotX        = (int)rd16(ent + 4);
        e->hotY        = (int)rd16(ent + 6);
        e->bpp         = (type == 1) ? (int)rd16(ent + 6) : 0;
        e->bytesInRes  = rd32(ent + 8);
        e->imageOffset = rd32(ent + 12);

        /* Inspect embedded image header to refine dimensions & bpp */
        if (e->imageOffset + 8UL <= g_dwFileSize) {
            _llseek(hf, (long)e->imageOffset, 0);
            if (_lread(hf, imgHdr, 40) >= 8) {
                if (imgHdr[0] == 0x89 && imgHdr[1] == 'P' && imgHdr[2] == 'N' && imgHdr[3] == 'G') {
                    e->isPng = TRUE;
                    e->unsupported = TRUE;
                } else {
                    DWORD biSz = rd32(imgHdr + 0);
                    if (biSz >= 40) {
                        long bw = (long)rd32(imgHdr + 4);
                        long bh = (long)rd32(imgHdr + 8);
                        WORD bb = rd16(imgHdr + 14);
                        if (bw > 0) e->width = (int)bw;
                        if (bh < 0) bh = -bh;
                        if (bh > 0) e->height = (int)(bh / 2);
                        if (bb > 0) e->bpp = (int)bb;
                    }
                }
            }
        } else {
            e->unsupported = TRUE;
        }

        if (e->bpp == 0 && e->colors > 0) {
            if (e->colors <= 2) e->bpp = 1;
            else if (e->colors <= 16) e->bpp = 4;
            else e->bpp = 8;
        }
        if (e->width > 128 || e->height > 128) {
            e->unsupported = TRUE;
        }

        BuildEntryThumbnail(hwnd, hf, e);
        g_entryCount++;
    }

    /* Select best entry (prefer 32x32 highest bpp, else first) */
    bestIdx = 0;
    for (i = 0; i < g_entryCount; i++) {
        if (!g_entries[i].unsupported) {
            if (g_entries[i].width == 32 && g_entries[i].height == 32) {
                if (g_entries[bestIdx].width != 32 || g_entries[i].bpp > g_entries[bestIdx].bpp) {
                    bestIdx = i;
                }
            }
        }
    }

    _lclose(hf);

    if (g_entryCount > 0) {
        SelectEntry(hwnd, bestIdx);
    }
    UpdateTitle(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    return TRUE;
}

static void DoFileOpenDlg(HWND hwnd)
{
    OPENFILENAME ofn;
    char szFile[128];
    szFile[0] = 0;

    _fmemset(&ofn, 0, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "Icon Files (*.ico)\0*.ico\0Cursor Files (*.cur)\0*.cur\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn)) {
        OpenIconFile(hwnd, szFile);
    }
}

static void DoSaveBmpDlg(HWND hwnd)
{
    OPENFILENAME ofn;
    char szFile[128];
    HFILE hf;
    BYTE bfh[14];
    DWORD imgSize, totalSize;
    BYTE __far *dib;
    HGLOBAL hExport = 0;

    if (g_selEntry < 0 || !g_cur.hXorDib || !g_cur.hAndDib) {
        MessageBox(hwnd, "No icon image is currently loaded.", "Icon Viewer", MB_OK | MB_ICONINFORMATION);
        return;
    }

    lstrcpy(szFile, "ICON.BMP");
    _fmemset(&ofn, 0, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = sizeof(szFile);
    ofn.lpstrDefExt = "bmp";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileName(&ofn)) return;

    imgSize = (DWORD)g_cur.stride24 * (DWORD)g_cur.height;
    hExport = GlobalAlloc(GHND, 40UL + imgSize);
    if (!hExport) return;
    dib = (BYTE __far *)GlobalLock(hExport);

    if (g_dispMode == MODE_AND) {
        BYTE __far *src = (BYTE __far *)GlobalLock(g_cur.hAndDib);
        _fmemcpy(dib, src, (size_t)(40UL + imgSize));
        GlobalUnlock(g_cur.hAndDib);
    } else if (g_dispMode == MODE_XOR) {
        BYTE __far *src = (BYTE __far *)GlobalLock(g_cur.hXorDib);
        _fmemcpy(dib, src, (size_t)(40UL + imgSize));
        GlobalUnlock(g_cur.hXorDib);
    } else {
        /* Composite against solid background */
        BYTE __far *xorDib = (BYTE __far *)GlobalLock(g_cur.hXorDib);
        BYTE __far *andDib = (BYTE __far *)GlobalLock(g_cur.hAndDib);
        BYTE __far *alpha  = g_cur.hasAlpha && g_cur.hAlpha ? (BYTE __far *)GlobalLock(g_cur.hAlpha) : NULL;
        COLORREF bg = GetSolidBgColor(g_bgMode);
        BYTE br = GetRValue(bg), bgc = GetGValue(bg), bb = GetBValue(bg);
        int x, y;

        FillBih24(dib, g_cur.width, g_cur.height, imgSize);
        for (y = 0; y < g_cur.height; y++) {
            const BYTE __far *xr = xorDib + 40 + (DWORD)y * (DWORD)g_cur.stride24;
            const BYTE __far *ar = andDib + 40 + (DWORD)y * (DWORD)g_cur.stride24;
            BYTE __far *dr = dib + 40 + (DWORD)y * (DWORD)g_cur.stride24;
            for (x = 0; x < g_cur.width; x++) {
                if (alpha) {
                    BYTE a = alpha[(DWORD)y * (DWORD)g_cur.width + (DWORD)x];
                    dr[x * 3 + 0] = (BYTE)(((WORD)xr[x * 3 + 0] * a + (WORD)bb  * (255 - a)) / 255);
                    dr[x * 3 + 1] = (BYTE)(((WORD)xr[x * 3 + 1] * a + (WORD)bgc * (255 - a)) / 255);
                    dr[x * 3 + 2] = (BYTE)(((WORD)xr[x * 3 + 2] * a + (WORD)br  * (255 - a)) / 255);
                } else {
                    BYTE m = ar[x * 3 + 0];
                    dr[x * 3 + 0] = (BYTE)((bb  & m) ^ xr[x * 3 + 0]);
                    dr[x * 3 + 1] = (BYTE)((bgc & m) ^ xr[x * 3 + 1]);
                    dr[x * 3 + 2] = (BYTE)((br  & m) ^ xr[x * 3 + 2]);
                }
            }
        }
        if (alpha) GlobalUnlock(g_cur.hAlpha);
        GlobalUnlock(g_cur.hAndDib);
        GlobalUnlock(g_cur.hXorDib);
    }

    hf = _lcreat(szFile, 0);
    if (hf == HFILE_ERROR) {
        GlobalUnlock(hExport);
        GlobalFree(hExport);
        MessageBox(hwnd, "Cannot create output BMP file.", "Icon Viewer", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    totalSize = 14UL + 40UL + imgSize;
    bfh[0] = 'B'; bfh[1] = 'M';
    wr32(bfh + 2, totalSize);
    wr16(bfh + 6, 0);
    wr16(bfh + 8, 0);
    wr32(bfh + 10, 54UL);

    _lwrite(hf, (LPCSTR)bfh, 14);
    {
        DWORD rem = 40UL + imgSize;
        DWORD off = 0;
        while (rem > 0) {
            UINT chunk = (rem > 16384UL) ? 16384U : (UINT)rem;
            _lwrite(hf, (LPCSTR)(dib + off), chunk);
            off += chunk;
            rem -= chunk;
        }
    }
    _lclose(hf);

    GlobalUnlock(hExport);
    GlobalFree(hExport);
}

static void FormatBppLabel(int bpp, LPSTR out)
{
    switch (bpp) {
    case 1:  lstrcpy(out, "1bpp (2 col)"); break;
    case 4:  lstrcpy(out, "4bpp (16 col)"); break;
    case 8:  lstrcpy(out, "8bpp (256 col)"); break;
    case 24: lstrcpy(out, "24bpp (RGB)"); break;
    case 32: lstrcpy(out, "32bpp (ARGB)"); break;
    default: wsprintf(out, "%dbpp", bpp); break;
    }
}

static LPCSTR BgModeName(int bg)
{
    switch (bg) {
    case BG_CHECKER: return "Checker";
    case BG_DESKTOP: return "Desktop";
    case BG_WHITE:   return "White";
    case BG_BLACK:   return "Black";
    case BG_GRAY:    return "Gray";
    default:         return "?";
    }
}

static LPCSTR DispModeName(int m)
{
    switch (m) {
    case MODE_COMPOSITE: return "Icon";
    case MODE_XOR:       return "XOR";
    case MODE_AND:       return "AND";
    default:             return "?";
    }
}

static void PaintStatusBar(HDC hdc, const RECT *rcClient)
{
    RECT rcSt;
    HBRUSH brFace;
    HPEN hPenDark, hPenOld;
    HFONT hOldFont;
    char szStatus[160];

    rcSt.left   = 0;
    rcSt.top    = rcClient->bottom - STATUS_HEIGHT;
    rcSt.right  = rcClient->right;
    rcSt.bottom = rcClient->bottom;

    brFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(hdc, &rcSt, brFace);
    DeleteObject(brFace);

    hPenDark = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    hPenOld = SelectObject(hdc, hPenDark);
    MoveTo(hdc, rcSt.left, rcSt.top);
    LineTo(hdc, rcSt.right, rcSt.top);
    SelectObject(hdc, hPenOld);
    DeleteObject(hPenDark);

    hOldFont = SelectObject(hdc, g_hFontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

    if (g_selEntry >= 0 && g_selEntry < g_entryCount) {
        const ICOENTRY *e = &g_entries[g_selEntry];
        if (g_hoverValid && g_cur.hXorDib && g_cur.hAndDib &&
            g_hoverX >= 0 && g_hoverX < g_cur.width &&
            g_hoverY >= 0 && g_hoverY < g_cur.height) {
            /* Pixel inspector */
            int dibY = g_cur.height - 1 - g_hoverY;
            BYTE __far *xorDib = (BYTE __far *)GlobalLock(g_cur.hXorDib);
            BYTE __far *andDib = (BYTE __far *)GlobalLock(g_cur.hAndDib);
            const BYTE __far *xp = xorDib + 40 + (DWORD)dibY * (DWORD)g_cur.stride24 + g_hoverX * 3;
            const BYTE __far *ap = andDib + 40 + (DWORD)dibY * (DWORD)g_cur.stride24 + g_hoverX * 3;
            BYTE b = xp[0], g = xp[1], r = xp[2];
            BYTE m = ap[0];
            if (g_cur.hasAlpha && g_cur.hAlpha) {
                BYTE __far *al = (BYTE __far *)GlobalLock(g_cur.hAlpha);
                BYTE a = al[(DWORD)dibY * (DWORD)g_cur.width + (DWORD)g_hoverX];
                GlobalUnlock(g_cur.hAlpha);
                wsprintf(szStatus, " (%d, %d)  #%02X%02X%02X  A=%d  |  %dx%d %dbpp  Zoom:%dx",
                         g_hoverX, g_hoverY, (int)r, (int)g, (int)b, (int)a,
                         e->width, e->height, e->bpp, g_zoom);
            } else if (m != 0 && r == 0 && g == 0 && b == 0) {
                wsprintf(szStatus, " (%d, %d)  Transparent  |  %dx%d %dbpp  Zoom:%dx",
                         g_hoverX, g_hoverY, e->width, e->height, e->bpp, g_zoom);
            } else if (m != 0) {
                wsprintf(szStatus, " (%d, %d)  Invert (#%02X%02X%02X)  |  %dx%d %dbpp  Zoom:%dx",
                         g_hoverX, g_hoverY, (int)r, (int)g, (int)b,
                         e->width, e->height, e->bpp, g_zoom);
            } else {
                wsprintf(szStatus, " (%d, %d)  #%02X%02X%02X (R:%d G:%d B:%d)  |  %dx%d %dbpp  Zoom:%dx",
                         g_hoverX, g_hoverY, (int)r, (int)g, (int)b, (int)r, (int)g, (int)b,
                         e->width, e->height, e->bpp, g_zoom);
            }
            GlobalUnlock(g_cur.hAndDib);
            GlobalUnlock(g_cur.hXorDib);
        } else {
            wsprintf(szStatus, " Image %d/%d | %dx%d %dbpp | %lu B | Zoom: %dx | Bg: %s | Mode: %s",
                     g_selEntry + 1, g_entryCount,
                     e->width, e->height, e->bpp,
                     e->bytesInRes, g_zoom,
                     (LPSTR)BgModeName(g_bgMode),
                     (LPSTR)DispModeName(g_dispMode));
        }
    } else {
        lstrcpy(szStatus, " Ready - File -> Open (Ctrl+O) to view an .ICO or .CUR file");
    }

    rcSt.top += 3;
    rcSt.left += 4;
    DrawText(hdc, szStatus, -1, &rcSt, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOldFont);
}

static void PaintSidebar(HDC hdc, const RECT *rcClient)
{
    RECT rcSide;
    HBRUSH brWin, brHi;
    HPEN hPenSep, hPenOld;
    HFONT hOldFont;
    int i, y;
    int maxH = rcClient->bottom - STATUS_HEIGHT;

    rcSide.left   = 0;
    rcSide.top    = 0;
    rcSide.right  = SIDEBAR_WIDTH;
    rcSide.bottom = maxH;

    brWin = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(hdc, &rcSide, brWin);
    DeleteObject(brWin);

    brHi = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    hOldFont = SelectObject(hdc, g_hFontUI);
    SetBkMode(hdc, TRANSPARENT);

    y = 4 - g_listScroll * ITEM_HEIGHT;
    for (i = 0; i < g_entryCount; i++) {
        RECT rcItem;
        char szLine1[32], szLine2[32];
        const ICOENTRY *e = &g_entries[i];

        rcItem.left   = 2;
        rcItem.top    = y;
        rcItem.right  = SIDEBAR_WIDTH - 3;
        rcItem.bottom = y + ITEM_HEIGHT - 2;

        if (rcItem.bottom > 0 && rcItem.top < maxH) {
            if (i == g_selEntry) {
                FillRect(hdc, &rcItem, brHi);
                SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            } else {
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            }

            /* Thumbnail box (36x36) */
            {
                RECT rcThumb;
                rcThumb.left   = rcItem.left + 4;
                rcThumb.top    = rcItem.top + 3;
                rcThumb.right  = rcThumb.left + 36;
                rcThumb.bottom = rcThumb.top + 36;
                if (e->hThumb) {
                    HDC mdc = CreateCompatibleDC(hdc);
                    HBITMAP old = SelectObject(mdc, e->hThumb);
                    int tx = rcThumb.left + (36 - e->thumbW) / 2;
                    int ty = rcThumb.top  + (36 - e->thumbH) / 2;
                    BitBlt(hdc, tx, ty, e->thumbW, e->thumbH, mdc, 0, 0, SRCCOPY);
                    SelectObject(mdc, old);
                    DeleteDC(mdc);
                } else {
                    HBRUSH brGray = CreateSolidBrush(RGB(192, 192, 192));
                    FillRect(hdc, &rcThumb, brGray);
                    DeleteObject(brGray);
                }
            }

            /* Labels */
            wsprintf(szLine1, "%dx%d", e->width, e->height);
            if (e->isPng) {
                lstrcpy(szLine2, "PNG (unsup.)");
            } else {
                FormatBppLabel(e->bpp, szLine2);
            }

            TextOut(hdc, rcItem.left + 44, rcItem.top + 6,  szLine1, lstrlen(szLine1));
            TextOut(hdc, rcItem.left + 44, rcItem.top + 21, szLine2, lstrlen(szLine2));
        }

        y += ITEM_HEIGHT;
    }

    DeleteObject(brHi);
    SelectObject(hdc, hOldFont);

    /* Right divider line */
    hPenSep = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    hPenOld = SelectObject(hdc, hPenSep);
    MoveTo(hdc, SIDEBAR_WIDTH - 1, 0);
    LineTo(hdc, SIDEBAR_WIDTH - 1, maxH);
    SelectObject(hdc, hPenOld);
    DeleteObject(hPenSep);
}

static void PaintCanvas(HDC hdc, const RECT *rcClient)
{
    RECT rcCan;
    HBRUSH brCan;
    HFONT hOldFont;
    int canW, canH;

    rcCan.left   = SIDEBAR_WIDTH;
    rcCan.top    = 0;
    rcCan.right  = rcClient->right;
    rcCan.bottom = rcClient->bottom - STATUS_HEIGHT;
    canW = rcCan.right - rcCan.left;
    canH = rcCan.bottom - rcCan.top;

    brCan = CreateSolidBrush(RGB(224, 228, 232));
    FillRect(hdc, &rcCan, brCan);
    DeleteObject(brCan);

    g_rcPreview.left = g_rcPreview.top = g_rcPreview.right = g_rcPreview.bottom = 0;

    if (g_entryCount == 0 || g_selEntry < 0) {
        hOldFont = SelectObject(hdc, g_hFontUI);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 96, 104));
        DrawText(hdc, "No icon loaded.\n\nUse File -> Open (Ctrl+O) to open an .ICO or .CUR file.",
                 -1, &rcCan, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        SelectObject(hdc, hOldFont);
        return;
    }

    if (g_entries[g_selEntry].isPng || g_entries[g_selEntry].unsupported || !g_cur.hXorDib) {
        LPCSTR msg = g_entries[g_selEntry].isPng
            ? "PNG-compressed icon entry\n(not supported in 16-bit Windows)."
            : "Unsupported icon image format or size (>128x128).";
        hOldFont = SelectObject(hdc, g_hFontUI);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(128, 40, 40));
        DrawText(hdc, msg, -1, &rcCan, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        SelectObject(hdc, hOldFont);
        return;
    }

    /* Main zoomed preview centered in canvas */
    {
        int dw = g_cur.width * g_zoom;
        int dh = g_cur.height * g_zoom;
        int dx = rcCan.left + (canW - dw) / 2;
        int dy = rcCan.top  + (canH - dh) / 2;
        HPEN hPenFrm, hPenOld;

        if (dx < rcCan.left + 8) dx = rcCan.left + 8;
        if (dy < rcCan.top  + 8) dy = rcCan.top  + 8;

        g_rcPreview.left   = dx;
        g_rcPreview.top    = dy;
        g_rcPreview.right  = dx + dw;
        g_rcPreview.bottom = dy + dh;

        RenderDecodedIcon(hdc, &g_cur, dx, dy, dw, dh, g_dispMode, g_bgMode);

        /* Pixel grid overlay when enabled and zoom >= 4 */
        if (g_showGrid && g_zoom >= 4) {
            HPEN hGridPen = CreatePen(PS_SOLID, 1, RGB(160, 168, 176));
            HPEN hOld = SelectObject(hdc, hGridPen);
            int gx, gy;
            for (gx = 1; gx < g_cur.width; gx++) {
                MoveTo(hdc, dx + gx * g_zoom, dy);
                LineTo(hdc, dx + gx * g_zoom, dy + dh);
            }
            for (gy = 1; gy < g_cur.height; gy++) {
                MoveTo(hdc, dx, dy + gy * g_zoom);
                LineTo(hdc, dx + dw, dy + gy * g_zoom);
            }
            SelectObject(hdc, hOld);
            DeleteObject(hGridPen);
        }

        /* Crisp 1px border around preview */
        hPenFrm = CreatePen(PS_SOLID, 1, RGB(64, 80, 88));
        hPenOld = SelectObject(hdc, hPenFrm);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, dx - 1, dy - 1, dx + dw + 1, dy + dh + 1);
        SelectObject(hdc, hPenOld);
        DeleteObject(hPenFrm);

        /* 1:1 actual-size inset in top-right corner if zoomed and space permits */
        if (g_zoom > 1 && g_cur.width <= 64 && g_cur.height <= 64 && canW > dw + g_cur.width + 40) {
            int ix = rcCan.right - g_cur.width - 14;
            int iy = rcCan.top + 22;
            hOldFont = SelectObject(hdc, g_hFontUI);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(48, 64, 72));
            TextOut(hdc, ix, rcCan.top + 6, "1:1", 3);
            SelectObject(hdc, hOldFont);

            RenderDecodedIcon(hdc, &g_cur, ix, iy, g_cur.width, g_cur.height, g_dispMode, g_bgMode);

            hPenFrm = CreatePen(PS_SOLID, 1, RGB(64, 80, 88));
            hPenOld = SelectObject(hdc, hPenFrm);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, ix - 1, iy - 1, ix + g_cur.width + 1, iy + g_cur.height + 1);
            SelectObject(hdc, hPenOld);
            DeleteObject(hPenFrm);
        }
    }
}

LRESULT FAR PASCAL __export IcoViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hFontUI = GetStockObject(ANSI_VAR_FONT);
        if (!g_hFontUI) g_hFontUI = GetStockObject(SYSTEM_FONT);
        g_hFontMono = GetStockObject(SYSTEM_FIXED_FONT);
        if (!g_hFontMono) g_hFontMono = GetStockObject(ANSI_FIXED_FONT);
        UpdateMenuChecks(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        PaintSidebar(hdc, &rc);
        PaintCanvas(hdc, &rc);
        PaintStatusBar(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = (int)LOWORD(lParam);
        int my = (int)HIWORD(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (mx < SIDEBAR_WIDTH && my < rc.bottom - STATUS_HEIGHT && g_entryCount > 0) {
            int idx = (my - 4) / ITEM_HEIGHT + g_listScroll;
            if (idx >= 0 && idx < g_entryCount && idx != g_selEntry) {
                SelectEntry(hwnd, idx);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = (int)LOWORD(lParam);
        int my = (int)HIWORD(lParam);
        BOOL inside = (g_cur.width > 0 && g_cur.height > 0 &&
                       mx >= g_rcPreview.left && mx < g_rcPreview.right &&
                       my >= g_rcPreview.top  && my < g_rcPreview.bottom);
        if (inside) {
            int px = (mx - g_rcPreview.left) / g_zoom;
            int py = (my - g_rcPreview.top)  / g_zoom;
            if (!g_hoverValid || px != g_hoverX || py != g_hoverY) {
                RECT rc, rcSt;
                g_hoverValid = TRUE;
                g_hoverX = px;
                g_hoverY = py;
                GetClientRect(hwnd, &rc);
                rcSt.left = 0; rcSt.top = rc.bottom - STATUS_HEIGHT;
                rcSt.right = rc.right; rcSt.bottom = rc.bottom;
                InvalidateRect(hwnd, &rcSt, FALSE);
            }
        } else if (g_hoverValid) {
            RECT rc, rcSt;
            g_hoverValid = FALSE;
            GetClientRect(hwnd, &rc);
            rcSt.left = 0; rcSt.top = rc.bottom - STATUS_HEIGHT;
            rcSt.right = rc.right; rcSt.bottom = rc.bottom;
            InvalidateRect(hwnd, &rcSt, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_UP:
            if (g_selEntry > 0) SelectEntry(hwnd, g_selEntry - 1);
            return 0;
        case VK_DOWN:
            if (g_selEntry + 1 < g_entryCount) SelectEntry(hwnd, g_selEntry + 1);
            return 0;
        case '1': SendMessage(hwnd, WM_COMMAND, IDM_VIEW_ZOOM1, 0); return 0;
        case '2': SendMessage(hwnd, WM_COMMAND, IDM_VIEW_ZOOM2, 0); return 0;
        case '4': SendMessage(hwnd, WM_COMMAND, IDM_VIEW_ZOOM4, 0); return 0;
        case '8': SendMessage(hwnd, WM_COMMAND, IDM_VIEW_ZOOM8, 0); return 0;
        case VK_ADD:
        case 0xBB: /* '+' / '=' */
            if (g_zoom < 8) { g_zoom *= 2; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        case VK_SUBTRACT:
        case 0xBD: /* '-' */
            if (g_zoom > 1) { g_zoom /= 2; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
            return 0;
        case 'G':
            SendMessage(hwnd, WM_COMMAND, IDM_VIEW_GRID, 0);
            return 0;
        case 'B':
            g_bgMode = (g_bgMode + 1) % 5;
            UpdateMenuChecks(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case 'M':
            g_dispMode = (g_dispMode + 1) % 3;
            UpdateMenuChecks(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDM_FILE_OPEN:
            DoFileOpenDlg(hwnd);
            return 0;
        case IDM_FILE_SAVEBMP:
            DoSaveBmpDlg(hwnd);
            return 0;
        case IDM_FILE_CLOSE:
            CloseCurrentFile(hwnd);
            return 0;
        case IDM_FILE_EXIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case IDM_VIEW_ZOOM1: g_zoom = 1; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_VIEW_ZOOM2: g_zoom = 2; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_VIEW_ZOOM4: g_zoom = 4; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_VIEW_ZOOM8: g_zoom = 8; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_VIEW_GRID:
            g_showGrid = !g_showGrid;
            UpdateMenuChecks(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case IDM_BG_CHECKER: g_bgMode = BG_CHECKER; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_BG_DESKTOP: g_bgMode = BG_DESKTOP; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_BG_WHITE:   g_bgMode = BG_WHITE;   UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_BG_BLACK:   g_bgMode = BG_BLACK;   UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_BG_GRAY:    g_bgMode = BG_GRAY;    UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;

        case IDM_MODE_COMPOSITE: g_dispMode = MODE_COMPOSITE; UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_MODE_XOR:       g_dispMode = MODE_XOR;       UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        case IDM_MODE_AND:       g_dispMode = MODE_AND;       UpdateMenuChecks(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;

        case IDM_HELP_ABOUT:
            MessageBox(hwnd,
                "Icon Viewer for Win16\n\n"
                "Views .ICO and .CUR files (1/4/8/24/32 bpp).\n"
                "Keys: 1/2/4/8 zoom, G grid, B background, M mode, Up/Down image.",
                "About Icon Viewer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;

    case WM_DESTROY:
        FreeAllEntries();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ParseCmdLineArgs(LPCSTR lpCmdLine, LPSTR szOut, int maxLen)
{
    LPCSTR p = lpCmdLine;
    int i = 0;
    szOut[0] = 0;
    if (!p) return;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && i < maxLen - 1) {
            szOut[i++] = *p++;
        }
    } else {
        while (*p && *p != ' ' && *p != '\t' && i < maxLen - 1) {
            szOut[i++] = *p++;
        }
    }
    szOut[i] = 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    WNDCLASS wc;
    HACCEL hAccel;
    char szInitFile[128];

    g_hinst = hInstance;

    if (!hPrevInstance) {
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = IcoViewWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIcon(hInstance, "ICOVIEW_ICON");
        wc.hCursor       = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName  = "ICOVIEW_MENU";
        wc.lpszClassName = g_szAppName;

        if (!RegisterClass(&wc)) return FALSE;
    }

    g_hwndMain = CreateWindow(
        g_szAppName,
        "Icon Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        520, 380,
        0, 0, hInstance, NULL
    );

    if (!g_hwndMain) return FALSE;

    hAccel = LoadAccelerators(hInstance, "ICOVIEW_ACCEL");

    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    ParseCmdLineArgs(lpCmdLine, szInitFile, sizeof(szInitFile));
    if (szInitFile[0]) {
        OpenIconFile(g_hwndMain, szInitFile);
    }

    while (GetMessage(&msg, 0, 0, 0)) {
        if (!hAccel || !TranslateAccelerator(g_hwndMain, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}
