/* ures.c - USER: zasoby (ikony, kursory, bitmapy, stringi, akceleratory) */
#include "user.h"

/* Obiekt ikony/kursora: blok GlobalAlloc, HICON = selektor */
typedef struct {
    WORD magic;                  /* 'IC' */
    int  w, h, hx, hy;
    WORD andbm, xorbm;           /* bitmapy GDI: mono AND, kolor XOR */
    BYTE andb[128], xorb[128];   /* 32x32 mono (kursor dla GDI): wiersze po 4 B */
    BOOL cursor;
} ICONOBJ;

#define ICON_MAGIC 0x4349

static ICONOBJ __far *icon_ptr(HICON h)
{
    ICONOBJ __far *o;
    if (!h) return 0;
    o = (ICONOBJ __far *)GlobalLock(h);
    return (o && o->magic == ICON_MAGIC) ? o : 0;
}

/* Z masek 1bpp (wiersze wyrownane do WORD, gora->dol) i XOR (kolor albo mono) */
static HICON icon_make(int w, int h, int hx, int hy, BYTE __far *andm, WORD andstride,
                       WORD xorbm, BYTE __far *xorm1, BOOL cursor)
{
    HICON hi = GlobalAlloc(0x40, sizeof(ICONOBJ));
    ICONOBJ __far *o = (ICONOBJ __far *)GlobalLock(hi);
    int y, x;
    if (!o) return 0;
    o->magic = ICON_MAGIC; o->w = w; o->h = h; o->hx = hx; o->hy = hy; o->cursor = cursor;
    o->andbm = CreateBitmap(w, h, 1, 1, 0);
    {   /* bitmapa AND: przepisz wiersze do formatu DDB (WORD) */
        static BYTE tmp[32 * 4];
        WORD wb = (WORD)(((w + 15) >> 4) << 1);
        BYTE __far *buf = (BYTE __far *)GlobalLock(GlobalAlloc(0, (DWORD)wb * h));
        for (y = 0; y < h; y++)
            for (x = 0; x < (int)wb; x++)
                buf[y * wb + x] = x < (int)andstride ? andm[y * andstride + x] : 0xFF;
        {
            extern LONG API SetBitmapBits(WORD, DWORD, LPVOID);
            SetBitmapBits(o->andbm, (DWORD)wb * h, buf);
        }
        GlobalFree(FP_SEG(buf));
        (void)tmp;
    }
    o->xorbm = xorbm;
    for (y = 0; y < h && y < 32; y++)
        for (x = 0; x < 4 && x < (int)andstride; x++) {
            o->andb[y * 4 + x] = andm[y * andstride + x];
            o->xorb[y * 4 + x] = xorm1 ? xorm1[y * andstride + x] : 0;
        }
    return hi;
}

/* DIB ikony/kursora z zasobu: XOR (DIB) + AND (1bpp) */
static HICON icon_from_dib(BYTE __far *bi, DWORD size, int hx, int hy, BOOL cursor)
{
    DWORD hs = *(DWORD __far *)bi;
    int w = (int)*(long __far *)(bi + 4), h = (int)(*(long __far *)(bi + 8) / 2);
    WORD bpp = *(WORD __far *)(bi + 14), ncol = bpp <= 8 ? (1 << bpp) : 0;
    DWORD used = *(DWORD __far *)(bi + 32);
    WORD xstride, astride;
    BYTE __far *xbits, __far *abits;
    static BYTE andt[32 * 4], xort[32 * 4];
    WORD xbm;
    HDC dc;
    int y;
    (void)size;
    if (used) ncol = (WORD)used;
    xstride = (WORD)((((DWORD)w * bpp + 31) >> 5) << 2);
    astride = (WORD)((((DWORD)w + 31) >> 5) << 2);
    xbits = bi + (WORD)hs + ncol * 4;
    abits = xbits + xstride * h;
    /* XOR jako bitmapa kolorowa (naglowek z polowa wysokosci) */
    {
        BYTE hdr[40 + 256 * 4];
        WORD i;
        for (i = 0; i < 40 + ncol * 4 && i < sizeof(hdr); i++) hdr[i] = bi[i];
        *(long __far *)((BYTE __far *)hdr + 8) = h;
        dc = GetDC(0);
        xbm = CreateDIBitmap(dc, (LPVOID)hdr, 4, xbits, (LPVOID)hdr, 0);
        ReleaseDC(0, dc);
    }
    /* AND (DIB jest od dolu) -> od gory, wiersze 4 B (do 32 px) */
    for (y = 0; y < h && y < 32; y++) {
        WORD k;
        for (k = 0; k < 4; k++) {
            andt[y * 4 + k] = k < astride ? abits[(h - 1 - y) * astride + k] : 0xFF;
            xort[y * 4 + k] = (bpp == 1 && k < xstride) ? xbits[(h - 1 - y) * xstride + k] : 0;
        }
    }
    return icon_make(w, h, hx, hy, (BYTE __far *)andt, 4, xbm, (BYTE __far *)xort, cursor);
}

/* Wybierz najlepszy wpis grupy (32x32, najwiecej kolorow <= 24bpp) */
static HICON load_group(WORD hinst, LPCSTR name, BOOL cursor)
{
    WORD hr = FindResource(hinst, name, (LPCSTR)MAKELONG(cursor ? 12 : 14, 0));
    BYTE __far *g;
    WORD n, i, best = 0, bestid = 0;
    int bestscore = -1;
    if (!hr) return 0;
    g = (BYTE __far *)LockResource(LoadResource(hinst, hr));
    if (!g) return 0;
    n = *(WORD __far *)(g + 4);
    for (i = 0; i < n; i++) {
        BYTE __far *e = g + 6 + i * 14;
        int w = cursor ? *(WORD __far *)e : e[0];
        int bits = cursor ? *(WORD __far *)(e + 6) : *(WORD __far *)(e + 6);
        int score = (w == 32 ? 1000 : 0) + (bits <= 24 ? bits : 0);
        if (score > bestscore) { bestscore = score; best = i; bestid = *(WORD __far *)(e + 12); }
    }
    (void)best;
    hr = FindResource(hinst, (LPCSTR)MAKELONG(bestid, 0), (LPCSTR)MAKELONG(cursor ? 1 : 3, 0));
    if (!hr) return 0;
    {
        BYTE __far *d = (BYTE __far *)LockResource(LoadResource(hinst, hr));
        DWORD sz = SizeofResource(hinst, hr);
        if (!d) return 0;
        if (cursor) return icon_from_dib(d + 4, sz - 4, *(WORD __far *)d, *(WORD __far *)(d + 2), TRUE);
        return icon_from_dib(d, sz, 16, 16, FALSE);
    }
}

/* ============================================================
 * Systemowe kursory i ikony (rysowane w kodzie)
 * ============================================================ */
static HICON g_syscur[16], g_sysicon[8];

static HICON make_cursor(int kind)
{
    static BYTE a[128], x[128];
    int y, i, hx = 0, hy = 0;
    for (i = 0; i < 128; i++) { a[i] = 0xFF; x[i] = 0; }
#define PX(px, py, v) do { int _b = (py) * 32 + (px); a[_b >> 3] &= (BYTE)~(0x80 >> (_b & 7)); \
    if (v) x[_b >> 3] |= (BYTE)(0x80 >> (_b & 7)); } while (0)
    switch (kind) {
    case 0: {                                             /* strzalka */
        static const WORD ar[16][2] = {
            {0x3FFF,0x0000},{0x1FFF,0x4000},{0x0FFF,0x6000},{0x07FF,0x7000},{0x03FF,0x7800},
            {0x01FF,0x7C00},{0x00FF,0x7E00},{0x007F,0x7F00},{0x003F,0x7F80},{0x007F,0x7C00},
            {0x10FF,0x4C00},{0x30FF,0x0600},{0xF87F,0x0600},{0xF87F,0x0300},{0xFC3F,0x0300},{0xFE7F,0x0000}};
        for (y = 0; y < 16; y++) {
            a[y * 4] = (BYTE)(ar[y][0] >> 8); a[y * 4 + 1] = (BYTE)ar[y][0];
            x[y * 4] = (BYTE)(ar[y][1] >> 8); x[y * 4 + 1] = (BYTE)ar[y][1];
        }
        break;
    }
    case 1:                                               /* I-beam */
        for (y = 0; y < 16; y++) PX(4, y, 0);
        for (i = 1; i < 4; i++) { PX(4 - i, 0, 0); PX(4 + i, 0, 0); PX(4 - i, 15, 0); PX(4 + i, 15, 0); }
        hx = 4; hy = 8;
        break;
    case 2:                                               /* klepsydra */
        for (y = 0; y < 16; y++) {
            int half = y < 8 ? 7 - y : y - 8, xx;
            for (xx = 7 - half; xx <= 8 + half; xx++)
                PX(xx, y, (xx == 7 - half || xx == 8 + half || y == 0 || y == 15) ? 0 : 1);
        }
        hx = 8; hy = 8;
        break;
    case 3:                                               /* krzyzyk */
        for (i = 0; i < 16; i++) { PX(i, 8, 0); PX(8, i, 0); }
        hx = 8; hy = 8;
        break;
    default:                                              /* rozmiar: strzalki <-> */
        for (i = 0; i < 16; i++) {
            if (kind == 5 || kind == 6) PX(i, 8, 0);      /* WE */
            if (kind == 7 || kind == 6) PX(8, i, 0);      /* NS */
            if (kind == 4) PX(i, i, 0);                   /* NWSE */
            if (kind == 8) PX(15 - i, i, 0);              /* NESW */
        }
        hx = 8; hy = 8;
    }
#undef PX
    return icon_make(32, 32, hx, hy, (BYTE __far *)a, 4, CreateBitmap(32, 32, 1, 1, (LPVOID)x),
                     (BYTE __far *)x, TRUE);
}

static HICON make_sysicon(int kind)
{
    static BYTE a[128];
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    WORD bm = CreateCompatibleBitmap(sdc, 32, 32), ob = SelectObject(dc, bm);
    static const COLORREF col[5] = { RGB(255,255,255), RGB(255,0,0), RGB(0,0,255), RGB(255,255,0), RGB(0,0,255) };
    static const char ch[5] = { 0, 'X', '?', '!', 'i' };
    int i;
    ReleaseDC(0, sdc);
    PatBlt(dc, 0, 0, 32, 32, 0x00000042UL);              /* czarne tlo = przezroczyste (AND=1, XOR=0) */
    for (i = 0; i < 128; i++) a[i] = 0xFF;
    {
        WORD br = CreateSolidBrush(col[kind]), obr = SelectObject(dc, br);
        extern BOOL API Ellipse(HDC, int, int, int, int);
        if (kind == 0) Rectangle(dc, 2, 4, 30, 28);
        else Ellipse(dc, 2, 2, 30, 30);
        SelectObject(dc, obr);
        DeleteObject(br);
    }
    if (ch[kind]) {
        SetBkMode(dc, 1);
        SetTextColor(dc, kind == 3 ? RGB(0, 0, 0) : RGB(255, 255, 255));
        TextOut(dc, 12, 8, (LPCSTR)&ch[kind], 1);
    }
    {   /* maska AND z niezerowych pikseli */
        extern COLORREF API GetPixel(HDC, int, int);
        int y, x;
        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++)
                if (GetPixel(dc, x, y)) a[y * 4 + (x >> 3)] &= (BYTE)~(0x80 >> (x & 7));
    }
    SelectObject(dc, ob);
    DeleteDC(dc);
    return icon_make(32, 32, 16, 16, (BYTE __far *)a, 4, bm, 0, FALSE);
}

/* ============================================================
 * API
 * ============================================================ */
HICON API LoadIcon(WORD hinst, LPCSTR name)
{
    user_init();
    if (!hinst) {
        WORD id = LOWORD(name);
        int k = id >= 32512 && id <= 32516 ? id - 32512 : 0;
        if (HIWORD(name)) k = 0;
        if (!g_sysicon[k]) g_sysicon[k] = make_sysicon(k);
        return g_sysicon[k];
    }
    return load_group(hinst, name, FALSE);
}

HCURSOR API LoadCursor(WORD hinst, LPCSTR name)
{
    user_init();
    if (!hinst) {
        WORD id = LOWORD(name);
        int k;
        switch (id) {
        case 32513: k = 1; break;                         /* IDC_IBEAM */
        case 32514: k = 2; break;                         /* IDC_WAIT */
        case 32515: k = 3; break;                         /* IDC_CROSS */
        case 32642: k = 4; break;                         /* SIZENWSE */
        case 32644: k = 5; break;                         /* SIZEWE */
        case 32640: case 32646: k = 6; break;             /* SIZE */
        case 32645: case 32516: k = 7; break;             /* SIZENS / UPARROW */
        case 32643: k = 8; break;                         /* SIZENESW */
        default: k = 0;
        }
        if (!g_syscur[k]) g_syscur[k] = make_cursor(k);
        return g_syscur[k];
    }
    return load_group(hinst, name, TRUE);
}

static HCURSOR g_curcur;

HCURSOR API SetCursor(HCURSOR h)
{
    HCURSOR old = g_curcur;
    ICONOBJ __far *o;
    if (h == g_curcur) return old;
    g_curcur = h;
    o = icon_ptr(h);
    if (!o) { GdiSetCursor(0, 0, 0, 0, 0, 0); return old; }
    GdiSetCursor((BYTE __far *)o->andb, (BYTE __far *)o->xorb, o->w > 32 ? 32 : o->w, o->h > 32 ? 32 : o->h, o->hx, o->hy);
    return old;
}

HCURSOR API GetCursor(void)              { return g_curcur; }
int  API ShowCursor(BOOL s)              { return GdiShowCursor(s); }
void API ClipCursor(LPRECT r)            { (void)r; }
void API GetClipCursor(LPRECT r)         { r->left = 0; r->top = 0; r->right = SCR_W; r->bottom = SCR_H; }

BOOL API DrawIcon(HDC dc, int x, int y, HICON h)
{
    ICONOBJ __far *o = icon_ptr(h);
    HDC m;
    WORD ob;
    if (!o) return FALSE;
    m = CreateCompatibleDC(dc);
    ob = SelectObject(m, o->andbm);
    BitBlt(dc, x, y, o->w, o->h, m, 0, 0, 0x008800C6UL);   /* SRCAND */
    if (o->xorbm) {
        SelectObject(m, o->xorbm);
        BitBlt(dc, x, y, o->w, o->h, m, 0, 0, 0x00660046UL);   /* SRCINVERT */
    }
    SelectObject(m, ob);
    DeleteDC(m);
    return TRUE;
}

HICON API CreateIcon(WORD hinst, int w, int h, BYTE planes, BYTE bpp, BYTE __far *andm, BYTE __far *xorm)
{
    WORD xbm = CreateBitmap(w, h, planes, bpp, xorm);
    (void)hinst;
    return icon_make(w, h, w / 2, h / 2, andm, (WORD)(((w + 15) >> 4) << 1), xbm,
                     (planes * bpp == 1) ? xorm : 0, FALSE);
}

HCURSOR API CreateCursor(WORD hinst, int hx, int hy, int w, int h, BYTE __far *andm, BYTE __far *xorm)
{
    HCURSOR c;
    (void)hinst;
    c = icon_make(w, h, hx, hy, andm, (WORD)(((w + 15) >> 4) << 1), CreateBitmap(w, h, 1, 1, xorm), xorm, TRUE);
    return c;
}

/* Prywatne: ikona z danych zasobu RT_ICON (dla SHELL.ExtractIcon) - ordinal 950 */
HICON API UserIconFromDIB(BYTE __far *bi, DWORD size) { return icon_from_dib(bi, size, 16, 16, FALSE); }

BOOL API DestroyIcon(HICON h)
{
    ICONOBJ __far *o = icon_ptr(h);
    int i;
    if (!o) return FALSE;
    for (i = 0; i < 8; i++) if (g_sysicon[i] == h) return TRUE;
    for (i = 0; i < 16; i++) if (g_syscur[i] == h) return TRUE;
    DeleteObject(o->andbm);
    if (o->xorbm) DeleteObject(o->xorbm);
    o->magic = 0;
    GlobalFree(h);
    return TRUE;
}
BOOL API DestroyCursor(HCURSOR h)  { return DestroyIcon(h); }
HICON API CopyIcon(WORD hinst, HICON h)    { (void)hinst; return h; }
HCURSOR API CopyCursor(WORD hinst, HCURSOR h) { (void)hinst; return h; }

/* Rozmiar ikony (dla SHELL/USER) - ordinal 951 */
DWORD API UserIconSize(HICON h) { ICONOBJ __far *o = icon_ptr(h); return o ? MAKELONG(o->w, o->h) : 0; }

/* ============================================================
 * Stringi, bitmapy, akceleratory
 * ============================================================ */
int API LoadString(WORD hinst, WORD id, LPSTR buf, int n)
{
    WORD hr = FindResource(hinst, (LPCSTR)MAKELONG((id >> 4) + 1, 0), (LPCSTR)MAKELONG(6, 0));
    BYTE __far *p;
    WORD i;
    int len;
    if (n <= 0) return 0;
    buf[0] = 0;
    if (!hr) return 0;
    p = (BYTE __far *)LockResource(LoadResource(hinst, hr));
    if (!p) return 0;
    for (i = 0; i < (id & 15); i++) p += 1 + *p;
    len = *p < n - 1 ? *p : n - 1;
    umemcpy(buf, p + 1, len);
    buf[len] = 0;
    return len;
}

static WORD obm_bitmap(WORD id)
{
    static BYTE check[32] = { 0,0, 0,0, 0,2, 0,6, 0,14, 64,28, 96,56, 112,112, 56,224, 29,192,
                              15,128, 7,0, 2,0, 0,0, 0,0, 0,0 };
    if (id == 32760) return CreateBitmap(16, 16, 1, 1, (LPVOID)check);   /* OBM_CHECK */
    {
        WORD bm = CreateBitmap(16, 16, 1, 1, 0);
        return bm;
    }
}

HBITMAP API LoadBitmap(WORD hinst, LPCSTR name)
{
    WORD hr;
    BYTE __far *bi;
    HDC dc;
    WORD bm, ncol, bpp;
    DWORD hs;
    user_init();
    if (!hinst) return obm_bitmap(LOWORD(name));
    hr = FindResource(hinst, name, (LPCSTR)MAKELONG(2, 0));
    if (!hr) return 0;
    bi = (BYTE __far *)LockResource(LoadResource(hinst, hr));
    if (!bi) return 0;
    hs = *(DWORD __far *)bi;
    if (hs == 12) {
        bpp = *(WORD __far *)(bi + 10);
        ncol = bpp <= 8 ? (1 << bpp) : 0;
        ncol *= 3;
    } else {
        DWORD used = *(DWORD __far *)(bi + 32);
        bpp = *(WORD __far *)(bi + 14);
        ncol = used ? (WORD)used : (bpp <= 8 ? (1 << bpp) : 0);
        ncol *= 4;
    }
    dc = GetDC(0);
    bm = CreateDIBitmap(dc, bi, 4, bi + (WORD)hs + ncol, bi, 0);
    ReleaseDC(0, dc);
    return bm;
}

HANDLE API LoadAccelerators(WORD hinst, LPCSTR name)
{
    WORD hr = FindResource(hinst, name, (LPCSTR)MAKELONG(9, 0));
    return hr ? LoadResource(hinst, hr) : 0;
}

int API TranslateAccelerator(HWND h, HANDLE acc, LPMSG m)
{
    BYTE __far *p;
    BOOL sh, ct, al;
    if (!acc || (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN)) return 0;
    p = (BYTE __far *)LockResource(acc);
    if (!p) return 0;
    sh = (GetKeyState(0x10) & 0x8000) != 0;
    ct = (GetKeyState(0x11) & 0x8000) != 0;
    al = (GetKeyState(0x12) & 0x8000) != 0;
    for (;; p += 5) {
        BYTE f = p[0];
        WORD key = *(WORD __far *)(p + 1), id = *(WORD __far *)(p + 3);
        if (f & 1) {                                     /* FVIRTKEY */
            if (key == m->wParam && !(f & 4) == !sh && !(f & 8) == !ct && !(f & 0x10) == !al) {
                send_msg(h, WM_COMMAND, id, MAKELONG(0, 1));
                return 1;
            }
        } else if (ct && key >= 1 && key <= 26 && key + 'A' - 1 == m->wParam) {
            send_msg(h, WM_COMMAND, id, MAKELONG(0, 1));
            return 1;
        }
        if (f & 0x80) break;
    }
    return 0;
}
