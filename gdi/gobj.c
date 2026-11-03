/* gobj.c - GDI: tabela obiektow, pule danych, obiekty magazynowe,
 * SelectObject / DeleteObject / GetObject, piora, pedzle, fonty */
#include "gdi.h"

GOBJ g_obj[MAX_OBJS];

/* Pule: dane obiektow w blokach GlobalAlloc (sloty o stalym rozmiarze) */
typedef struct { WORD size, count, mem; BYTE __far *used; } POOL;
static POOL g_pool[10];
static BYTE g_used_pen[256], g_used_brush[256], g_used_font[128], g_used_pal[32],
            g_used_bmp[512], g_used_rgn[96], g_used_dc[160];
static BOOL g_inited;

#define COM1 0x3F8
static BYTE inb(WORD p);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(WORD p, BYTE v);
#pragma aux outb = "out dx, al" parm [dx] [al];

void gdbg(LPCSTR s)
{
    for (; *s; s++) {
        if (*s == '\n') { while (!(inb(COM1 + 5) & 0x20)); outb(COM1, '\r'); }
        while (!(inb(COM1 + 5) & 0x20));
        outb(COM1, (BYTE)*s);
    }
}

void gdbg_hex(DWORD v, int digits)
{
    char b[9];
    int i;
    if (digits > 8) digits = 8;
    for (i = 0; i < digits; i++) b[i] = "0123456789ABCDEF"[(WORD)(v >> ((digits - 1 - i) * 4)) & 0xF];
    b[digits] = 0;
    gdbg((LPCSTR)(char __far *)b);
}

static void pool_init(BYTE type, WORD size, WORD count, BYTE *used)
{
    POOL *p = &g_pool[type];
    p->size = size;
    p->count = count;
    p->mem = GlobalAlloc(0x40, (DWORD)size * count);
    p->used = (BYTE __far *)used;
}

static WORD pool_slot(BYTE type)
{
    BYTE t = type == OT_MEMDC ? OT_DC : type;
    POOL *p = &g_pool[t];
    WORD i;
    for (i = 0; i < p->count; i++)
        if (!p->used[i]) { p->used[i] = 1; return i; }
    gdbg("GDI: pula pelna typ="); gdbg_hex(type, 2); gdbg("\n");
    return 0xFFFF;
}

void __far *obj_ptr(WORD h, BYTE type)
{
    WORD i;
    GOBJ *o;
    BYTE t;
    if (h < 0x4000) return 0;
    i = H2I(h);
    if (i >= MAX_OBJS || ((h - 0x4000) & 3)) return 0;
    o = &g_obj[i];
    if (o->type == OT_FREE) return 0;
    if (type && o->type != type && !(type == OT_DC && o->type == OT_MEMDC)) return 0;
    t = o->type == OT_MEMDC ? OT_DC : o->type;
    return (BYTE __far *)MK_FP(g_pool[t].mem, 0) + o->slot * g_pool[t].size;
}

BYTE obj_type(WORD h)
{
    WORD i;
    if (h < 0x4000 || ((h - 0x4000) & 3)) return OT_FREE;
    i = H2I(h);
    return i < MAX_OBJS ? g_obj[i].type : OT_FREE;
}

WORD obj_new(BYTE type)
{
    WORD i, slot;
    BYTE t = type == OT_MEMDC ? OT_DC : type;
    BYTE __far *d;
    gdi_init();
    for (i = 1; i < MAX_OBJS; i++) if (g_obj[i].type == OT_FREE) break;
    if (i == MAX_OBJS) { gdbg("GDI: tabela obiektow pelna\n"); return 0; }
    slot = pool_slot(type);
    if (slot == 0xFFFF) return 0;
    g_obj[i].type = type;
    g_obj[i].stock = 0;
    g_obj[i].slot = slot;
    d = (BYTE __far *)MK_FP(g_pool[t].mem, 0) + slot * g_pool[t].size;
    { WORD k; for (k = 0; k < g_pool[t].size; k++) d[k] = 0; }
    return I2H(i);
}

void obj_free(WORD h)
{
    WORD i;
    BYTE t;
    if (obj_type(h) == OT_FREE) return;
    i = H2I(h);
    t = g_obj[i].type == OT_MEMDC ? OT_DC : g_obj[i].type;
    g_pool[t].used[g_obj[i].slot] = 0;
    g_obj[i].type = OT_FREE;
}

/* ============================================================
 * Obiekty magazynowe
 * ============================================================ */
static WORD g_stock[17];
WORD g_default_bitmap;           /* 1x1 mono - domyslna bitmapa DC pamieci */

static WORD mk_pen(WORD style, int width, COLORREF c)
{
    WORD h = obj_new(OT_PEN);
    PENOBJ __far *p = (PENOBJ __far *)obj_ptr(h, OT_PEN);
    if (!p) return 0;
    p->style = style; p->width = width; p->color = c;
    return h;
}

static WORD mk_brush(WORD style, COLORREF c, WORD hatch)
{
    static const BYTE hpat[6][8] = {
        { 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },   /* HS_HORIZONTAL */
        { 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 },   /* HS_VERTICAL */
        { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },   /* HS_FDIAGONAL */
        { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },   /* HS_BDIAGONAL */
        { 0x08, 0x08, 0x08, 0x08, 0xFF, 0x08, 0x08, 0x08 },   /* HS_CROSS */
        { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 },   /* HS_DIAGCROSS */
    };
    WORD h = obj_new(OT_BRUSH), i;
    BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(h, OT_BRUSH);
    if (!b) return 0;
    b->style = style; b->color = c; b->hatch = hatch;
    if (style == BS_HATCHED && hatch < 6)
        for (i = 0; i < 8; i++) b->pat[i] = hpat[hatch][i];
    return h;
}

void gdi_init(void)
{
    WORD i;
    if (g_inited) return;
    g_inited = TRUE;
    pool_init(OT_PEN,    sizeof(PENOBJ),    256, g_used_pen);
    pool_init(OT_BRUSH,  sizeof(BRUSHOBJ),  256, g_used_brush);
    pool_init(OT_FONT,   sizeof(FONTOBJ),   128, g_used_font);
    pool_init(OT_PAL,    sizeof(PALOBJ),     32, g_used_pal);
    pool_init(OT_BITMAP, sizeof(BMPOBJ),    512, g_used_bmp);
    pool_init(OT_RGN,    sizeof(RGNDATA16),  96, g_used_rgn);
    pool_init(OT_DC,     sizeof(DCOBJ),     160, g_used_dc);

    g_stock[0] = mk_brush(BS_SOLID, RGB(255, 255, 255), 0);   /* WHITE_BRUSH */
    g_stock[1] = mk_brush(BS_SOLID, RGB(192, 192, 192), 0);   /* LTGRAY_BRUSH */
    g_stock[2] = mk_brush(BS_SOLID, RGB(128, 128, 128), 0);   /* GRAY_BRUSH */
    g_stock[3] = mk_brush(BS_SOLID, RGB(64, 64, 64), 0);      /* DKGRAY_BRUSH */
    g_stock[4] = mk_brush(BS_SOLID, RGB(0, 0, 0), 0);         /* BLACK_BRUSH */
    g_stock[5] = mk_brush(BS_NULL, 0, 0);                     /* NULL_BRUSH */
    g_stock[6] = mk_pen(0, 1, RGB(255, 255, 255));            /* WHITE_PEN */
    g_stock[7] = mk_pen(0, 1, RGB(0, 0, 0));                  /* BLACK_PEN */
    g_stock[8] = mk_pen(5, 1, 0);                             /* NULL_PEN */
    font_init();
    {
        static const char * const faces[7] = { "Terminal", "Courier", "MS Sans Serif",
                                                "System", "System", 0, "Fixedsys" };
        LOGFONT lf;
        LOGFONT __far *lfp = (LOGFONT __far *)&lf;
        for (i = 10; i <= 16; i++) {
            WORD h, k;
            FONTOBJ __far *f;
            if (i == 15) continue;
            for (k = 0; k < sizeof(lf); k++) ((BYTE *)&lf)[k] = 0;
            for (k = 0; faces[i - 10][k]; k++) lf.lfFaceName[k] = faces[i - 10][k];
            lf.lfHeight = (i == 12) ? 13 : 16;
            lf.lfWeight = (i == 13 || i == 14) ? 700 : 400;
            if (i == 10 || i == 11 || i == 16) lf.lfPitchAndFamily = 1;
            h = obj_new(OT_FONT);
            f = (FONTOBJ __far *)obj_ptr(h, OT_FONT);
            f->lf = lf;
            f->face = font_match(lfp);
            g_stock[i] = h;
        }
    }
    {
        WORD h = obj_new(OT_PAL);
        g_stock[15] = h;                                      /* DEFAULT_PALETTE */
    }
    for (i = 0; i < 17; i++) if (g_stock[i]) g_obj[H2I(g_stock[i])].stock = 1;
    {
        extern WORD bmp_create(int w, int h, BYTE bpp);
        g_default_bitmap = bmp_create(1, 1, 1);
        g_obj[H2I(g_default_bitmap)].stock = 1;
    }
    gdbg("GDI: init OK\n");
}

/* ordinal 87 */
HGDIOBJ API GetStockObject(int i)
{
    gdi_init();
    return (i >= 0 && i < 17) ? g_stock[i] : 0;
}

/* ============================================================
 * Piora
 * ============================================================ */
typedef struct { WORD style; POINT width; COLORREF color; } LOGPEN;

/* ordinal 61 */
HPEN API CreatePen(int style, int width, COLORREF c)
{
    return mk_pen((WORD)style, width < 1 ? 1 : width, c);
}

/* ordinal 62 */
HPEN API CreatePenIndirect(LOGPEN __far *lp)
{
    return CreatePen(lp->style, lp->width.x, lp->color);
}

/* ============================================================
 * Pedzle
 * ============================================================ */
typedef struct { WORD style; COLORREF color; int hatch; } LOGBRUSH;

/* ordinal 66 */
HBRUSH API CreateSolidBrush(COLORREF c)          { return mk_brush(BS_SOLID, c, 0); }
/* ordinal 58 */
HBRUSH API CreateHatchBrush(int hs, COLORREF c)  { return mk_brush(BS_HATCHED, c, (WORD)hs); }

/* ordinal 60 */
HBRUSH API CreatePatternBrush(HBITMAP hbm)
{
    extern void bmp_get8x8(HBITMAP hbm, BRUSHOBJ __far *b);
    WORD h = mk_brush(BS_PATTERN, 0, 0);
    BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(h, OT_BRUSH);
    if (b) bmp_get8x8(hbm, b);
    return h;
}

/* ordinal 50 */
HBRUSH API CreateBrushIndirect(LOGBRUSH __far *lb)
{
    switch (lb->style) {
    case BS_SOLID:   return CreateSolidBrush(lb->color);
    case BS_NULL:    return GetStockObject(5);
    case BS_HATCHED: return CreateHatchBrush(lb->hatch, lb->color);
    case BS_PATTERN: return CreatePatternBrush((HBITMAP)lb->hatch);
    default:         return CreateSolidBrush(lb->color);
    }
}

/* ordinal 445: DIB pattern - pierwsze 8x8 pikseli DIB */
HBRUSH API CreateDIBPatternBrush(HANDLE hdib, WORD usage)
{
    extern void dib_get8x8(BYTE __far *bi, WORD usage, BRUSHOBJ __far *b);
    WORD h = mk_brush(BS_PATTERN, 0, 0);
    BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(h, OT_BRUSH);
    if (b) dib_get8x8((BYTE __far *)GlobalLock(hdib), usage, b);
    return h;
}

/* ============================================================
 * Fonty
 * ============================================================ */
/* ordinal 57 */
HFONT API CreateFontIndirect(LOGFONT __far *lf)
{
    WORD h = obj_new(OT_FONT);
    FONTOBJ __far *f = (FONTOBJ __far *)obj_ptr(h, OT_FONT);
    if (!f) return 0;
    f->lf = *lf;
    f->face = font_match(lf);
    return h;
}

/* ordinal 56 */
HFONT API CreateFont(int h, int w, int esc, int ori, int weight, BYTE it, BYTE ul,
                     BYTE so, BYTE cs, BYTE op, BYTE cp, BYTE q, BYTE pf, LPCSTR face)
{
    LOGFONT lf;
    int i;
    lf.lfHeight = h; lf.lfWidth = w; lf.lfEscapement = esc; lf.lfOrientation = ori;
    lf.lfWeight = weight; lf.lfItalic = it; lf.lfUnderline = ul; lf.lfStrikeOut = so;
    lf.lfCharSet = cs; lf.lfOutPrecision = op; lf.lfClipPrecision = cp;
    lf.lfQuality = q; lf.lfPitchAndFamily = pf;
    for (i = 0; i < 31 && face && face[i]; i++) lf.lfFaceName[i] = face[i];
    lf.lfFaceName[i] = 0;
    return CreateFontIndirect((LOGFONT __far *)&lf);
}

/* ============================================================
 * SelectObject / DeleteObject / GetObject
 * ============================================================ */
extern WORD dc_select_bitmap(HDC hdc, HBITMAP hbm);
extern int  dc_select_cliprgn(HDC hdc, HRGN h);

/* ordinal 45 */
HGDIOBJ API SelectObject(HDC hdc, HGDIOBJ h)
{
    DCOBJ __far *dc = dc_get(hdc);
    WORD old;
    if (!dc) return 0;
    switch (obj_type(h)) {
    case OT_PEN:    old = dc->pen;   dc->pen = h;   return old;
    case OT_BRUSH:  old = dc->brush; dc->brush = h; return old;
    case OT_FONT:   old = dc->font;  dc->font = h;  return old;
    case OT_BITMAP: return dc_select_bitmap(hdc, h);
    case OT_RGN:    return (HGDIOBJ)dc_select_cliprgn(hdc, h);
    }
    return 0;
}

/* ordinal 69 */
BOOL API DeleteObject(HGDIOBJ h)
{
    BYTE t = obj_type(h);
    if (t == OT_FREE || t == OT_DC || t == OT_MEMDC) return FALSE;
    if (g_obj[H2I(h)].stock) return TRUE;
    if (t == OT_BITMAP) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(h, OT_BITMAP);
        if (b->selected) return FALSE;
        if (b->mem) GlobalFree(b->mem);
    } else if (t == OT_BRUSH) {
        BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(h, OT_BRUSH);
        if (b->patmem) GlobalFree(b->patmem);
    } else if (t == OT_PAL) {
        PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
        if (p->mem) GlobalFree(p->mem);
    }
    obj_free(h);
    return TRUE;
}

static int put_bytes(LPVOID dst, const void __far *src, int n, int max)
{
    int i;
    if (!dst) return n;
    if (n > max) n = max;
    for (i = 0; i < n; i++) ((BYTE __far *)dst)[i] = ((const BYTE __far *)src)[i];
    return n;
}

/* ordinal 82 */
int API GetObject(HGDIOBJ h, int size, LPVOID buf)
{
    switch (obj_type(h)) {
    case OT_PEN: {
        PENOBJ __far *p = (PENOBJ __far *)obj_ptr(h, OT_PEN);
        LOGPEN lp;
        lp.style = p->style; lp.width.x = p->width; lp.width.y = 0; lp.color = p->color;
        return put_bytes(buf, (void __far *)&lp, sizeof(lp), size);
    }
    case OT_BRUSH: {
        BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(h, OT_BRUSH);
        LOGBRUSH lb;
        lb.style = b->style; lb.color = b->color; lb.hatch = b->hatch;
        return put_bytes(buf, (void __far *)&lb, sizeof(lb), size);
    }
    case OT_FONT: {
        FONTOBJ __far *f = (FONTOBJ __far *)obj_ptr(h, OT_FONT);
        return put_bytes(buf, (void __far *)&f->lf, sizeof(LOGFONT), size);
    }
    case OT_BITMAP: {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(h, OT_BITMAP);
        struct { int type, w, h, wb; BYTE planes, bpp; LPVOID bits; } bm;
        bm.type = 0; bm.w = b->w; bm.h = b->h;
        bm.wb = (int)((((DWORD)b->w * b->bpp + 15) >> 4) << 1);
        bm.planes = 1; bm.bpp = b->bpp; bm.bits = 0;
        return put_bytes(buf, (void __far *)&bm, sizeof(bm), size);
    }
    case OT_PAL: {
        PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
        WORD n = p->n;
        return put_bytes(buf, (void __far *)&n, 2, size);
    }
    }
    return 0;
}

/* ordinal 150 */
BOOL API UnrealizeObject(HGDIOBJ h)   { (void)h; return TRUE; }

/* ordinal 462: IsGDIObject */
BOOL API IsGDIObject(HGDIOBJ h)       { return obj_type(h) != OT_FREE; }

/* ordinal 128: MulDiv (zaokraglenie jak Windows) */
int API MulDiv(int a, int b, int c)
{
    long r;
    if (!c) return -32768;
    r = (long)a * b;
    if ((r < 0) != (c < 0)) r -= c / 2; else r += c / 2;
    return (int)(r / c);
}
