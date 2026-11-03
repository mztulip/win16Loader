/* gsurf.c - GDI: powierzchnie (ekran 24bpp LFB, bitmapy 32bpp), piksele,
 * ROP2/ROP3, kursor myszy (sprite na ekranie, jak w sterowniku DISPLAY). */
#include "gdi.h"

/* ============================================================
 * Adresowanie: offset liniowy -> far ptr (ciag selektorow co 8 = 64KB)
 * ============================================================ */
static BYTE __far *addr(SURF __far *s, DWORD off)
{
    return (BYTE __far *)MK_FP(s->sel + (WORD)(off >> 16) * 8, (WORD)off);
}

DWORD rgb2px(COLORREF c)
{
    /* COLORREF 0x00BBGGRR -> piksel 0x00RRGGBB (w pamieci: B G R 0) */
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

COLORREF px2rgb(DWORD p)
{
    return ((p >> 16) & 0xFF) | (p & 0xFF00) | ((p & 0xFF) << 16);
}

DWORD mono_of(DWORD p)
{
    WORD l = (WORD)(((p >> 16) & 0xFF) * 30 + ((p >> 8) & 0xFF) * 59 + (p & 0xFF) * 11);
    return l >= 12800 ? 0xFFFFFFUL : 0;
}

DWORD px_get(SURF __far *s, int x, int y)
{
    DWORD off;
    if (s->bpp == 32) {
        off = (DWORD)y * s->pitch + ((DWORD)x << 2);
        return *(DWORD __far *)addr(s, off) & 0xFFFFFFUL;
    }
    off = (DWORD)y * SCR_PITCH + (DWORD)x * 3;
    if ((WORD)off <= 0xFFFD) {
        BYTE __far *p = addr(s, off);
        return p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16);
    }
    return *addr(s, off) | ((DWORD)*addr(s, off + 1) << 8) | ((DWORD)*addr(s, off + 2) << 16);
}

void px_put(SURF __far *s, int x, int y, DWORD c)
{
    DWORD off;
    if (s->bpp == 32) {
        if (s->mono) c = mono_of(c);
        off = (DWORD)y * s->pitch + ((DWORD)x << 2);
        *(DWORD __far *)addr(s, off) = c;
        return;
    }
    off = (DWORD)y * SCR_PITCH + (DWORD)x * 3;
    if ((WORD)off <= 0xFFFD) {
        BYTE __far *p = addr(s, off);
        p[0] = (BYTE)c; p[1] = (BYTE)(c >> 8); p[2] = (BYTE)(c >> 16);
    } else {
        *addr(s, off) = (BYTE)c;
        *addr(s, off + 1) = (BYTE)(c >> 8);
        *addr(s, off + 2) = (BYTE)(c >> 16);
    }
}

/* ROP2 (R2_*: 1..16): tabela prawdy = rop2-1, indeks (P<<1)|D */
static DWORD rop2_apply(WORD rop2, DWORD p, DWORD d)
{
    switch (rop2) {
    case 1:  return 0;                     /* R2_BLACK */
    case 2:  return ~(p | d);              /* R2_NOTMERGEPEN */
    case 3:  return ~p & d;                /* R2_MASKNOTPEN */
    case 4:  return ~p;                    /* R2_NOTCOPYPEN */
    case 5:  return p & ~d;                /* R2_MASKPENNOT */
    case 6:  return ~d;                    /* R2_NOT */
    case 7:  return p ^ d;                 /* R2_XORPEN */
    case 8:  return ~(p & d);              /* R2_NOTMASKPEN */
    case 9:  return p & d;                 /* R2_MASKPEN */
    case 10: return ~(p ^ d);              /* R2_NOTXORPEN */
    case 11: return d;                     /* R2_NOP */
    case 12: return ~p | d;                /* R2_MERGENOTPEN */
    case 13: return p;                     /* R2_COPYPEN */
    case 14: return p | ~d;                /* R2_MERGEPENNOT */
    case 15: return p | d;                 /* R2_MERGEPEN */
    case 16: return 0xFFFFFFUL;            /* R2_WHITE */
    }
    return p;
}

/* Pozioma linia [x0,x1) na wierszu y (wsp. powierzchni, juz przyciete) */
void span_rop2(SURF __far *s, int y, int x0, int x1, DWORD c, WORD rop2)
{
    int x;
    if (x0 >= x1) return;
    if (s->mono) c = mono_of(c);
    if (rop2 == 13 && s->bpp == 32) {
        DWORD off = (DWORD)y * s->pitch + ((DWORD)x0 << 2);
        int n = x1 - x0;
        while (n > 0) {
            WORD lo = (WORD)off;
            DWORD room = (0x10000UL - lo) >> 2;
            DWORD __far *p = (DWORD __far *)addr(s, off);
            int k = (DWORD)n < room ? n : (int)room, i;
            for (i = 0; i < k; i++) p[i] = c;
            n -= k;
            off += (DWORD)k << 2;
        }
        return;
    }
    for (x = x0; x < x1; x++) {
        DWORD v = rop2 == 13 ? c : rop2_apply(rop2, c, px_get(s, x, y)) & 0xFFFFFFUL;
        px_put(s, x, y, v);
    }
}

/* ROP3: wynik bitowy z P (pattern), S (zrodlo), D (cel) wg kodu rop (bajt 2 ROP) */
DWORD rop3(BYTE rop, DWORD p, DWORD s, DWORD d)
{
    DWORD r = 0;
    switch (rop) {
    case 0x00: return 0;
    case 0xFF: return 0xFFFFFFUL;
    case 0xCC: return s;                   /* SRCCOPY */
    case 0xF0: return p;                   /* PATCOPY */
    case 0x88: return s & d;               /* SRCAND */
    case 0xEE: return s | d;               /* SRCPAINT */
    case 0x66: return s ^ d;               /* SRCINVERT */
    case 0x33: return ~s;                  /* NOTSRCCOPY */
    case 0x55: return ~d;                  /* DSTINVERT */
    case 0x5A: return p ^ d;               /* PATINVERT */
    case 0xC0: return p & s;               /* MERGECOPY */
    case 0xBB: return ~s | d;              /* MERGEPAINT */
    case 0x44: return s & ~d;              /* SRCERASE */
    case 0x11: return ~(s | d);            /* NOTSRCERASE */
    case 0xFB: return p | ~s | d;          /* PATPAINT */
    case 0xAA: return d;
    }
    /* ogolnie: dla kazdej kombinacji bitow (P,S,D) bit wyniku = bit rop */
    if (rop & 0x01) r |= ~p & ~s & ~d;
    if (rop & 0x02) r |= ~p & ~s &  d;
    if (rop & 0x04) r |= ~p &  s & ~d;
    if (rop & 0x08) r |= ~p &  s &  d;
    if (rop & 0x10) r |=  p & ~s & ~d;
    if (rop & 0x20) r |=  p & ~s &  d;
    if (rop & 0x40) r |=  p &  s & ~d;
    if (rop & 0x80) r |=  p &  s &  d;
    return r;
}

/* Powierzchnia DC: ekran (DC okna) albo wybrana bitmapa (DC pamieci) */
BOOL surf_of_dc(DCOBJ __far *dc, SURF __far *s)
{
    if (dc->is_mem) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(dc->bitmap, OT_BITMAP);
        if (!b || !b->mem) return FALSE;
        s->sel = b->mem; s->bpp = 32; s->mono = b->bpp == 1;
        s->w = b->w; s->h = b->h; s->pitch = (DWORD)b->w << 2;
        return TRUE;
    }
    s->sel = SEL_VESA_BASE; s->bpp = 24; s->mono = 0;
    s->w = SCR_W; s->h = SCR_H; s->pitch = SCR_PITCH;
    return TRUE;
}

/* ============================================================
 * Kursor myszy (do 32x32, maski AND/XOR jak zasob kursora Win16)
 * ============================================================ */
#define CUR_MAX 32
static BYTE  g_cand[CUR_MAX * CUR_MAX / 8];    /* 1 = przezroczysty */
static BYTE  g_cxor[CUR_MAX * CUR_MAX / 8];    /* 1 = bialy (gdy AND=0) / odwroc (gdy AND=1) */
static int   g_cw = 16, g_ch = 16, g_chx, g_chy;
static int   g_cx = SCR_W / 2, g_cy = SCR_H / 2;
static BYTE  g_cbg[CUR_MAX * CUR_MAX * 3];
static int   g_cbx, g_cby;                     /* lewy-gorny rog zapisanego tla */
static BOOL  g_cdrawn, g_chave, g_cexcl;
static int   g_cshow = 0;                      /* >= 0 widoczny */
static int   g_cdepth;                         /* zagniezdzenie cursor_exclude */

static void cur_default(void)
{
    static const WORD arrow[16][2] = {           /* {AND, XOR} 16 bit/wiersz */
        {0x3FFF,0x0000},{0x1FFF,0x4000},{0x0FFF,0x6000},{0x07FF,0x7000},
        {0x03FF,0x7800},{0x01FF,0x7C00},{0x00FF,0x7E00},{0x007F,0x7F00},
        {0x003F,0x7F80},{0x007F,0x7C00},{0x10FF,0x4C00},{0x30FF,0x0600},
        {0xF87F,0x0600},{0xF87F,0x0300},{0xFC3F,0x0300},{0xFE7F,0x0000}};
    int y;
    g_cw = 16; g_ch = 16; g_chx = 0; g_chy = 0;
    for (y = 0; y < CUR_MAX * CUR_MAX / 8; y++) { g_cand[y] = 0xFF; g_cxor[y] = 0; }
    for (y = 0; y < 16; y++) {
        g_cand[y * 4] = (BYTE)(arrow[y][0] >> 8); g_cand[y * 4 + 1] = (BYTE)arrow[y][0];
        g_cxor[y * 4] = (BYTE)(arrow[y][1] >> 8); g_cxor[y * 4 + 1] = (BYTE)arrow[y][1];
    }
    g_chave = TRUE;
}

static SURF g_scr = { SEL_VESA_BASE, 24, 0, SCR_W, SCR_H, SCR_PITCH };

static void cur_erase(void)
{
    int x, y;
    if (!g_cdrawn) return;
    for (y = 0; y < g_ch; y++)
        for (x = 0; x < g_cw; x++) {
            int sx = g_cbx + x, sy = g_cby + y;
            BYTE *b = &g_cbg[(y * CUR_MAX + x) * 3];
            if (sx < 0 || sy < 0 || sx >= SCR_W || sy >= SCR_H) continue;
            px_put((SURF __far *)&g_scr, sx, sy, b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16));
        }
    g_cdrawn = FALSE;
}

static void cur_draw(void)
{
    int x, y;
    if (g_cdrawn || g_cshow < 0 || !g_chave) return;
    g_cbx = g_cx - g_chx; g_cby = g_cy - g_chy;
    for (y = 0; y < g_ch; y++)
        for (x = 0; x < g_cw; x++) {
            int sx = g_cbx + x, sy = g_cby + y, bit = y * CUR_MAX + x;
            BYTE a, xo;
            DWORD v;
            BYTE *b = &g_cbg[(y * CUR_MAX + x) * 3];
            if (sx < 0 || sy < 0 || sx >= SCR_W || sy >= SCR_H) continue;
            v = px_get((SURF __far *)&g_scr, sx, sy);
            b[0] = (BYTE)v; b[1] = (BYTE)(v >> 8); b[2] = (BYTE)(v >> 16);
            a  = (BYTE)((g_cand[bit >> 3] >> (7 - (bit & 7))) & 1);
            xo = (BYTE)((g_cxor[bit >> 3] >> (7 - (bit & 7))) & 1);
            if (a && !xo) continue;
            v = a ? (v ^ 0xFFFFFFUL) : (xo ? 0xFFFFFFUL : 0);
            px_put((SURF __far *)&g_scr, sx, sy, v);
        }
    g_cdrawn = TRUE;
}

void cursor_exclude(int x0, int y0, int x1, int y1)
{
    if (g_cdepth++) return;
    if (g_cdrawn && x0 < g_cbx + g_cw && x1 > g_cbx && y0 < g_cby + g_ch && y1 > g_cby) {
        cur_erase();
        g_cexcl = TRUE;
    }
}

void cursor_restore(void)
{
    if (--g_cdepth > 0) return;
    g_cdepth = 0;
    if (g_cexcl) { g_cexcl = FALSE; cur_draw(); }
}

/* ---- Prywatne eksporty dla USER ---- */
/* ordinal 900: GdiMoveCursor(x, y) */
void API GdiMoveCursor(int x, int y)
{
    if (!g_chave) cur_default();
    if (x == g_cx && y == g_cy && g_cdrawn) return;
    cur_erase();
    g_cx = x; g_cy = y;
    cur_draw();
}

/* ordinal 901: GdiShowCursor(bShow) - licznik jak ShowCursor */
int API GdiShowCursor(BOOL show)
{
    if (!g_chave) cur_default();
    g_cshow += show ? 1 : -1;
    if (g_cshow < 0) cur_erase(); else cur_draw();
    return g_cshow;
}

/* ordinal 902: GdiSetCursor(and, xor, w, h, hotx, hoty); and=NULL -> ukryj */
void API GdiSetCursor(BYTE __far *andm, BYTE __far *xorm, int w, int h, int hx, int hy)
{
    int y, x, wb = ((w + 15) >> 4) << 1;
    cur_erase();
    if (!andm) { g_chave = FALSE; return; }
    if (w > CUR_MAX) w = CUR_MAX;
    if (h > CUR_MAX) h = CUR_MAX;
    for (y = 0; y < CUR_MAX * CUR_MAX / 8; y++) { g_cand[y] = 0xFF; g_cxor[y] = 0; }
    for (y = 0; y < h; y++)
        for (x = 0; x < (w + 7) / 8; x++) {
            g_cand[y * 4 + x] = andm[y * wb + x];
            g_cxor[y * 4 + x] = xorm[y * wb + x];
        }
    g_cw = w; g_ch = h; g_chx = hx; g_chy = hy;
    g_chave = TRUE;
    cur_draw();
}

/* ordinal 903: GdiCursorDefault() */
void API GdiCursorDefault(void)
{
    cur_erase();
    cur_default();
    cur_draw();
}
