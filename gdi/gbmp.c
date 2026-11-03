/* gbmp.c - GDI: bitmapy (DDB, wewnetrznie 32bpp) i DIB (1/4/8/24 bpp, RLE4/8) */
#include "gdi.h"

/* Domyslna paleta VGA (16) - kolejnosc Windows */
static const BYTE g_vga[16][3] = {
    {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
    {128,128,128},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}
};

/* Kolor palety domyslnej 256 (8bpp bez palety): 0-7 i 248-255 = VGA, srodek = kostka 6x6x6 */
static DWORD def_pal256(WORD i)
{
    if (i < 8) return ((DWORD)g_vga[i][0] << 16) | ((WORD)g_vga[i][1] << 8) | g_vga[i][2];
    if (i >= 248) { i -= 240; return ((DWORD)g_vga[i][0] << 16) | ((WORD)g_vga[i][1] << 8) | g_vga[i][2]; }
    i -= 8;
    if (i < 216) {
        BYTE r = (BYTE)((i / 36) * 51), g = (BYTE)(((i / 6) % 6) * 51), b = (BYTE)((i % 6) * 51);
        return ((DWORD)r << 16) | ((WORD)g << 8) | b;
    }
    i = (WORD)((i - 216) * 10 + 15);
    return ((DWORD)i << 16) | ((WORD)i << 8) | i;
}

static DWORD vga_px(WORD i) { i &= 15; return ((DWORD)g_vga[i][0] << 16) | ((WORD)g_vga[i][1] << 8) | g_vga[i][2]; }

static WORD nearest(DWORD p, WORD n, DWORD (*pal)(WORD))
{
    WORD i, best = 0;
    long bd = 0x7FFFFFFFL;
    for (i = 0; i < n; i++) {
        DWORD q = pal(i);
        long dr = (long)((p >> 16) & 0xFF) - (long)((q >> 16) & 0xFF);
        long dg = (long)((p >> 8) & 0xFF) - (long)((q >> 8) & 0xFF);
        long db = (long)(p & 0xFF) - (long)(q & 0xFF);
        long dd = dr * dr + dg * dg + db * db;
        if (dd < bd) { bd = dd; best = i; if (!dd) break; }
    }
    return best;
}

static DWORD mono_pal(WORD i) { return i ? 0xFFFFFFUL : 0; }

/* ============================================================
 * Tworzenie
 * ============================================================ */
static void bmp_surf(BMPOBJ __far *b, SURF __far *s)
{
    s->sel = b->mem; s->bpp = 32; s->mono = b->bpp == 1;
    s->w = b->w; s->h = b->h; s->pitch = (DWORD)b->w << 2;
}

WORD bmp_create(int w, int h, BYTE bpp)
{
    WORD hb;
    BMPOBJ __far *b;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    hb = obj_new(OT_BITMAP);
    b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    if (!b) return 0;
    b->w = w; b->h = h; b->bpp = bpp; b->planes = 1;
    b->mem = GlobalAlloc(0x40, (DWORD)w * h * 4);
    if (!b->mem) { obj_free(hb); return 0; }
    return hb;
}

/* Bity DDB (wiersze wyrownane do WORD) -> bitmapa */
static void ddb_set(BMPOBJ __far *b, BYTE __far *bits, DWORD max)
{
    SURF s;
    WORD wb = (WORD)((((DWORD)b->w * b->bpp + 15) >> 4) << 1);
    int x, y;
    bmp_surf(b, (SURF __far *)&s);
    for (y = 0; y < b->h; y++) {
        DWORD row = (DWORD)y * wb;
        if (row + wb > max) break;
        for (x = 0; x < b->w; x++) {
            DWORD p;
            BYTE __far *r = bits + row;
            switch (b->bpp) {
            case 1:  p = (r[x >> 3] >> (7 - (x & 7))) & 1 ? 0xFFFFFFUL : 0; break;
            case 4:  p = vga_px((r[x >> 1] >> ((x & 1) ? 0 : 4)) & 15); break;
            case 8:  p = def_pal256(r[x]); break;
            case 24: p = r[x * 3] | ((DWORD)r[x * 3 + 1] << 8) | ((DWORD)r[x * 3 + 2] << 16); break;
            default: p = ((DWORD __far *)r)[x] & 0xFFFFFFUL;
            }
            px_put((SURF __far *)&s, x, y, p);
        }
    }
}

/* ordinal 48 */
HBITMAP API CreateBitmap(int w, int h, WORD planes, WORD bpp, LPVOID bits)
{
    WORD hb;
    BYTE d = (BYTE)(planes * bpp);
    if (d != 1 && d != 4 && d != 8 && d != 24 && d != 32) d = 24;
    hb = bmp_create(w, h, d);
    if (hb && bits) ddb_set((BMPOBJ __far *)obj_ptr(hb, OT_BITMAP), (BYTE __far *)bits, 0xFFFFFFUL);
    return hb;
}

typedef struct { int type, w, h, wb; BYTE planes, bpp; LPVOID bits; } BITMAP16;

/* ordinal 49 */
HBITMAP API CreateBitmapIndirect(BITMAP16 __far *bm)
{
    return CreateBitmap(bm->w, bm->h, bm->planes, bm->bpp, bm->bits);
}

/* ordinal 51 */
HBITMAP API CreateCompatibleBitmap(HDC hdc, int w, int h)
{
    DCOBJ __far *dc = dc_get(hdc);
    BYTE bpp = 24;
    if (dc && dc->is_mem) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(dc->bitmap, OT_BITMAP);
        if (b && b->bpp == 1) bpp = 1;
    }
    return bmp_create(w, h, bpp);
}

/* ordinal 156 */
HBITMAP API CreateDiscardableBitmap(HDC hdc, int w, int h) { return CreateCompatibleBitmap(hdc, w, h); }

/* ordinal 106 */
LONG API SetBitmapBits(HBITMAP hb, DWORD n, LPVOID bits)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    if (!b) return 0;
    ddb_set(b, (BYTE __far *)bits, n);
    return (LONG)n;
}

/* ordinal 74 */
LONG API GetBitmapBits(HBITMAP hb, LONG n, LPVOID bits)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    SURF s;
    WORD wb;
    int x, y;
    BYTE __far *o = (BYTE __far *)bits;
    if (!b) return 0;
    wb = (WORD)((((DWORD)b->w * b->bpp + 15) >> 4) << 1);
    bmp_surf(b, (SURF __far *)&s);
    for (y = 0; y < b->h; y++) {
        BYTE __far *r;
        if ((LONG)(y + 1) * wb > n) break;
        r = o + (DWORD)y * wb;
        for (x = 0; x < wb; x++) r[x] = 0;
        for (x = 0; x < b->w; x++) {
            DWORD p = px_get((SURF __far *)&s, x, y);
            switch (b->bpp) {
            case 1:  if (p) r[x >> 3] |= (BYTE)(0x80 >> (x & 7)); break;
            case 4:  r[x >> 1] |= (BYTE)(nearest(p, 16, vga_px) << ((x & 1) ? 0 : 4)); break;
            case 8:  r[x] = (BYTE)nearest(p, 256, def_pal256); break;
            case 24: r[x * 3] = (BYTE)p; r[x * 3 + 1] = (BYTE)(p >> 8); r[x * 3 + 2] = (BYTE)(p >> 16); break;
            default: ((DWORD __far *)r)[x] = p;
            }
        }
    }
    return (LONG)b->h * wb;
}

/* ordinal 85/86/478/468 */
DWORD API SetBitmapDimension(HBITMAP hb, int x, int y)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    DWORD o;
    if (!b) return 0;
    o = MAKELONG(b->dimx, b->dimy);
    b->dimx = x; b->dimy = y;
    return o;
}
DWORD API GetBitmapDimension(HBITMAP hb)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    return b ? MAKELONG(b->dimx, b->dimy) : 0;
}
BOOL API SetBitmapDimensionEx(HBITMAP hb, int x, int y, LPPOINT p)
{
    DWORD o = SetBitmapDimension(hb, x, y);
    if (p) { p->x = (int)LOWORD(o); p->y = (int)HIWORD(o); }
    return TRUE;
}
BOOL API GetBitmapDimensionEx(HBITMAP hb, LPPOINT p)
{
    DWORD o = GetBitmapDimension(hb);
    if (p) { p->x = (int)LOWORD(o); p->y = (int)HIWORD(o); }
    return TRUE;
}

/* ============================================================
 * DIB
 * ============================================================ */
typedef struct {
    long w, h;                         /* h < 0 = top-down */
    WORD bpp, comp;
    WORD ncolors;
    BYTE __far *colors;                /* tablica kolorow (RGBQUAD/RGBTRIPLE/WORD) */
    WORD csize;                        /* 4 = RGBQUAD, 3 = RGBTRIPLE, 2 = indeksy palety */
} DIBINFO;

static BOOL dib_parse(BYTE __far *bi, WORD usage, DIBINFO __far *di)
{
    DWORD hs = *(DWORD __far *)bi;
    if (hs == 12) {                    /* BITMAPCOREHEADER (OS/2) */
        di->w = *(WORD __far *)(bi + 4);
        di->h = *(WORD __far *)(bi + 6);
        di->bpp = *(WORD __far *)(bi + 10);
        di->comp = 0;
        di->csize = 3;
        di->ncolors = di->bpp <= 8 ? (1 << di->bpp) : 0;
    } else if (hs >= 40) {
        DWORD used;
        di->w = *(long __far *)(bi + 4);
        di->h = *(long __far *)(bi + 8);
        di->bpp = *(WORD __far *)(bi + 14);
        di->comp = (WORD)*(DWORD __far *)(bi + 16);
        used = *(DWORD __far *)(bi + 32);
        di->csize = 4;
        di->ncolors = di->bpp <= 8 ? (used ? (WORD)used : (1 << di->bpp)) : 0;
    } else return FALSE;
    if (usage == 1) di->csize = 2;     /* DIB_PAL_COLORS */
    di->colors = bi + (WORD)hs;
    return di->w > 0 && di->h != 0;
}

static DWORD dib_color(DIBINFO __far *di, WORD idx)
{
    BYTE __far *c;
    if (idx >= di->ncolors) idx = di->ncolors ? di->ncolors - 1 : 0;
    if (di->csize == 2) {
        WORD pi = ((WORD __far *)di->colors)[idx];
        return di->ncolors <= 16 ? vga_px(pi) : def_pal256(pi & 0xFF);
    }
    c = di->colors + idx * di->csize;
    return c[0] | ((DWORD)c[1] << 8) | ((DWORD)c[2] << 16);     /* B G R -> 0x00RRGGBB */
}

static WORD dib_stride(DIBINFO __far *di) { return (WORD)((((DWORD)di->w * di->bpp + 31) >> 5) << 2); }

/* Dekoduj linie skanowania [start, start+num) DIB do bitmapy */
static void dib_to_surf(SURF __far *s, DIBINFO __far *di, BYTE __far *bits, WORD start, WORD num)
{
    long ah = di->h < 0 ? -di->h : di->h;
    WORD stride = dib_stride(di);
    long line;
    int x;
    if (di->comp == 1 || di->comp == 2) {     /* RLE8 / RLE4 */
        int px = 0, py = 0;
        BYTE __far *p = bits;
        BOOL r4 = di->comp == 2;
        for (;;) {
            BYTE n = *p++, c = *p++;
            if (n) {
                BYTE k;
                for (k = 0; k < n; k++, px++) {
                    WORD idx = r4 ? ((k & 1) ? (c & 15) : (c >> 4)) : c;
                    long y = (long)ah - 1 - py;
                    if (px < s->w && y >= 0 && y < s->h) px_put(s, px, (int)y, dib_color(di, idx));
                }
                continue;
            }
            if (c == 0) { px = 0; py++; continue; }       /* koniec linii */
            if (c == 1) break;                            /* koniec bitmapy */
            if (c == 2) { px += *p++; py += *p++; continue; }
            {                                              /* blok bezposredni */
                BYTE k;
                for (k = 0; k < c; k++, px++) {
                    WORD idx = r4 ? ((k & 1) ? (p[k >> 1] & 15) : (p[k >> 1] >> 4)) : p[k];
                    long y = (long)ah - 1 - py;
                    if (px < s->w && y >= 0 && y < s->h) px_put(s, px, (int)y, dib_color(di, idx));
                }
                p += r4 ? ((c + 1) >> 1) : c;
                if ((r4 ? ((c + 1) >> 1) : c) & 1) p++;   /* wyrownanie do WORD */
            }
        }
        return;
    }
    for (line = start; line < (long)start + num && line < ah; line++) {
        DWORD lin = (DWORD)(line - start) * stride;
        BYTE __far *r = (BYTE __far *)MK_FP(FP_SEG(bits) + (WORD)((FP_OFF(bits) + lin) >> 16) * 8,
                                            (WORD)(FP_OFF(bits) + lin));
        long y = di->h < 0 ? line : ah - 1 - line;
        if (y < 0 || y >= s->h) continue;
        for (x = 0; x < (int)di->w && x < s->w; x++) {
            DWORD p;
            switch (di->bpp) {
            case 1:  p = dib_color(di, (r[x >> 3] >> (7 - (x & 7))) & 1); break;
            case 4:  p = dib_color(di, (r[x >> 1] >> ((x & 1) ? 0 : 4)) & 15); break;
            case 8:  p = dib_color(di, r[x]); break;
            case 24: p = r[x * 3] | ((DWORD)r[x * 3 + 1] << 8) | ((DWORD)r[x * 3 + 2] << 16); break;
            case 32: p = ((DWORD __far *)r)[x] & 0xFFFFFFUL; break;
            case 16: { WORD v = ((WORD __far *)r)[x];
                       p = ((DWORD)((v >> 10) & 31) << 19) | ((DWORD)((v >> 5) & 31) << 11) | ((v & 31) << 3); }
                     break;
            default: p = 0;
            }
            px_put(s, x, (int)y, p);
        }
    }
}

/* ordinal 440 */
int API SetDIBits(HDC hdc, HBITMAP hb, WORD start, WORD num, LPVOID bits, BYTE __far *bi, WORD usage)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    DIBINFO di;
    SURF s;
    (void)hdc;
    if (!b || !dib_parse(bi, usage, &di)) return 0;
    bmp_surf(b, (SURF __far *)&s);
    dib_to_surf((SURF __far *)&s, &di, (BYTE __far *)bits, start, num);
    return num;
}

/* ordinal 442 */
HBITMAP API CreateDIBitmap(HDC hdc, BYTE __far *bih, DWORD init, LPVOID bits, BYTE __far *bi, WORD usage)
{
    DIBINFO di;
    WORD hb;
    long h;
    (void)hdc;
    if (!dib_parse(bih, 0, &di)) return 0;
    h = di.h < 0 ? -di.h : di.h;
    hb = bmp_create((int)di.w, (int)h, 24);
    if (hb && (init & 4) && bits && bi) {          /* CBM_INIT */
        DIBINFO d2;
        SURF s;
        if (dib_parse(bi, usage, &d2)) {
            bmp_surf((BMPOBJ __far *)obj_ptr(hb, OT_BITMAP), (SURF __far *)&s);
            dib_to_surf((SURF __far *)&s, &d2, (BYTE __far *)bits, 0, (WORD)h);
        }
    }
    return hb;
}

/* ordinal 441: bity bitmapy -> DIB (wg biBitCount w bi); bits = NULL -> tylko naglowek */
int API GetDIBits(HDC hdc, HBITMAP hb, WORD start, WORD num, LPVOID bits, BYTE __far *bi, WORD usage)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hb, OT_BITMAP);
    DWORD __far *hd = (DWORD __far *)bi;
    WORD bpp, stride, i, ncol;
    long line;
    SURF s;
    int x;
    (void)hdc; (void)usage;
    if (!b || *hd < 40) return 0;
    bpp = *(WORD __far *)(bi + 14);
    if (bpp == 0) bpp = b->bpp == 1 ? 1 : 24;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24) bpp = 24;
    *(long __far *)(bi + 4) = b->w;
    if (*(long __far *)(bi + 8) == 0) *(long __far *)(bi + 8) = b->h;
    *(WORD __far *)(bi + 12) = 1;
    *(WORD __far *)(bi + 14) = bpp;
    *(DWORD __far *)(bi + 16) = 0;
    stride = (WORD)((((DWORD)b->w * bpp + 31) >> 5) << 2);
    *(DWORD __far *)(bi + 20) = (DWORD)stride * b->h;
    ncol = bpp <= 8 ? (1 << bpp) : 0;
    {   /* tablica kolorow */
        BYTE __far *c = bi + 40;
        for (i = 0; i < ncol; i++) {
            DWORD p = bpp == 1 ? mono_pal(i) : bpp == 4 ? vga_px(i) : def_pal256(i);
            c[i * 4] = (BYTE)p; c[i * 4 + 1] = (BYTE)(p >> 8); c[i * 4 + 2] = (BYTE)(p >> 16); c[i * 4 + 3] = 0;
        }
    }
    if (!bits) return b->h;
    bmp_surf(b, (SURF __far *)&s);
    for (line = start; line < (long)start + num && line < b->h; line++) {
        DWORD lin = (DWORD)(line - start) * stride;
        BYTE __far *r = (BYTE __far *)MK_FP(FP_SEG(bits) + (WORD)((FP_OFF(bits) + lin) >> 16) * 8,
                                            (WORD)(FP_OFF(bits) + lin));
        int y = (int)(b->h - 1 - line);
        for (x = 0; x < (int)stride; x++) r[x] = 0;
        for (x = 0; x < b->w; x++) {
            DWORD p = px_get((SURF __far *)&s, x, y);
            switch (bpp) {
            case 1:  if (p) r[x >> 3] |= (BYTE)(0x80 >> (x & 7)); break;
            case 4:  r[x >> 1] |= (BYTE)(nearest(p, 16, vga_px) << ((x & 1) ? 0 : 4)); break;
            case 8:  r[x] = (BYTE)nearest(p, 256, def_pal256); break;
            case 24: r[x * 3] = (BYTE)p; r[x * 3 + 1] = (BYTE)(p >> 8); r[x * 3 + 2] = (BYTE)(p >> 16); break;
            }
        }
    }
    return num;
}

/* DIB -> tymczasowa bitmapa w DC pamieci -> StretchBlt */
static int dib_blt(HDC hdc, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
                   WORD start, WORD num, LPVOID bits, BYTE __far *bi, WORD usage, DWORD rop)
{
    extern BOOL API StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
    DIBINFO di;
    WORD hb, mdc, old;
    long ah;
    if (!dib_parse(bi, usage, &di)) return 0;
    ah = di.h < 0 ? -di.h : di.h;
    hb = bmp_create((int)di.w, (int)ah, 24);
    if (!hb) return 0;
    {
        SURF s;
        bmp_surf((BMPOBJ __far *)obj_ptr(hb, OT_BITMAP), (SURF __far *)&s);
        dib_to_surf((SURF __far *)&s, &di, (BYTE __far *)bits, start, num ? num : (WORD)ah);
    }
    mdc = CreateCompatibleDC(hdc);
    old = SelectObject(mdc, hb);
    StretchBlt(hdc, x, y, w, h, mdc, sx, sy, sw, sh, rop);
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(hb);
    return num ? num : (int)ah;
}

/* ordinal 443: SetDIBitsToDevice (sy liczone od dolu DIB) */
int API SetDIBitsToDevice(HDC hdc, int x, int y, int w, int h, int sx, int sy,
                          WORD start, WORD num, LPVOID bits, BYTE __far *bi, WORD usage)
{
    DIBINFO di;
    long ah;
    if (!dib_parse(bi, usage, &di)) return 0;
    ah = di.h < 0 ? -di.h : di.h;
    return dib_blt(hdc, x, y, w, h, sx, (int)(ah - sy - h), w, h, start, num, bits, bi, usage, 0xCC0020UL);
}

/* ordinal 439 */
int API StretchDIBits(HDC hdc, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
                      LPVOID bits, BYTE __far *bi, WORD usage, DWORD rop)
{
    DIBINFO di;
    long ah;
    if (!dib_parse(bi, usage, &di)) return 0;
    ah = di.h < 0 ? -di.h : di.h;
    return dib_blt(hdc, x, y, w, h, sx, (int)(ah - sy - sh), sw, sh, 0, 0, bits, bi, usage, rop);
}

/* ============================================================
 * Wzorce pedzli 8x8
 * ============================================================ */
void bmp_get8x8(HBITMAP hbm, BRUSHOBJ __far *br)
{
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hbm, OT_BITMAP);
    SURF s;
    int x, y;
    if (!b) return;
    bmp_surf(b, (SURF __far *)&s);
    if (b->bpp == 1) {
        for (y = 0; y < 8; y++) {
            BYTE v = 0;
            for (x = 0; x < 8; x++)
                if (px_get((SURF __far *)&s, x % b->w, y % b->h)) v |= (BYTE)(0x80 >> x);
            br->pat[y] = v;
        }
        return;
    }
    br->patmem = GlobalAlloc(0, 256);
    if (!br->patmem) return;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            ((DWORD __far *)MK_FP(br->patmem, 0))[y * 8 + x] = px_get((SURF __far *)&s, x % b->w, y % b->h);
}

void dib_get8x8(BYTE __far *bi, WORD usage, BRUSHOBJ __far *br)
{
    DIBINFO di;
    WORD hb;
    if (!bi || !dib_parse(bi, usage, &di)) return;
    hb = bmp_create(8, 8, 24);
    if (!hb) return;
    {
        SURF s;
        bmp_surf((BMPOBJ __far *)obj_ptr(hb, OT_BITMAP), (SURF __far *)&s);
        dib_to_surf((SURF __far *)&s, &di, di.colors + di.ncolors * di.csize, 0,
                    (WORD)(di.h < 0 ? -di.h : di.h));
    }
    bmp_get8x8(hb, br);
    DeleteObject(hb);
}
