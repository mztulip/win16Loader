/* gsurf.c - GDI: powierzchnie (ekran 24bpp LFB, bitmapy 32bpp), piksele,
 * ROP2/ROP3, kursor myszy (sprite na ekranie, jak w sterowniku DISPLAY). */
#include "gdi.h"

/* ============================================================
 * Adresowanie: offset liniowy -> far ptr (ciag selektorow co 8 = 64KB
 * lub okno 0xA000 z przelaczaniem bankow VBE 1.2 dla kart SVGA z 1992 r.)
 * ============================================================ */
static WORD g_cur_bank = 0xFFFF;

/* VBE 4F05h: BX = okno (0=A, 1=B; BH=0 ustaw), DX = pozycja w jednostkach granularnosci.
 * Z PM idzie przez softint -> KERNEL -> odbicie do trybu rzeczywistego. */
static void vbe_set_win(WORD win, WORD pos);
#pragma aux vbe_set_win = \
    "mov ax, 0x4F05"        \
    "int 0x10"              \
    parm [bx] [dx] modify [ax bx cx dx si di];

/* Paradise/WD90Cxx: PR0A (Graphics Controller 3CEh, indeks 09h), jednostki 4KB */
static void wd_set_pr0a(BYTE pos);
#pragma aux wd_set_pr0a = \
    "mov ah, al"            \
    "mov al, 0x09"          \
    "mov dx, 0x3CE"         \
    "out dx, ax"            \
    parm [al] modify [ax dx];

/* Okno 0xA000 pokazuje 64KB pamieci ekranu od (bank << 16) */
static void set_bank(WORD bank)
{
    WORD pos = bank << g_hw_gransh;
    if (g_hw_bankmode == VBANK_WD) {
        wd_set_pr0a((BYTE)pos);
    } else {
        if (g_hw_winmask & 1) vbe_set_win(0, pos);
        if (g_hw_winmask & 2) vbe_set_win(1, pos);
    }
    g_cur_bank = bank;
}

static BYTE __far *addr(SURF __far *s, DWORD off)
{
    if (s->sel == SEL_VESA_BASE && g_hw_banked) {
        WORD bank = (WORD)(off >> 16);
        if (bank != g_cur_bank) set_bank(bank);
        return (BYTE __far *)MK_FP(SEL_RM_A000, (WORD)off);
    }
    return (BYTE __far *)MK_FP(s->sel + (WORD)(off >> 16) * 8, (WORD)off);
}

/* Najblizszy kolor palety sprzetowej: pelne przeszukanie 256 wpisow to ~3000 instrukcji,
 * wiec wyniki trzymamy w malym cache (direct-mapped), uniewaznianym przez g_dac_gen. */
#define NC_SIZE 256
static DWORD g_nc_px[NC_SIZE];
static BYTE  g_nc_idx[NC_SIZE];
static BYTE  g_nc_ok[NC_SIZE];
static WORD  g_nc_gen = 0xFFFF;

static BYTE dac_search(DWORD px)
{
    int r = (int)((px >> 16) & 0xFF), g = (int)((px >> 8) & 0xFF), b = (int)(px & 0xFF);
    DWORD best = 0xFFFFFFFFUL;
    WORD i, bi = 0;
    for (i = 0; i < 256; i++) {
        DWORD c = g_dac_rgb[i];
        int dr, dg, db;
        DWORD d;
        if (c == px) return (BYTE)i;
        dr = r - (int)((c >> 16) & 0xFF);
        dg = g - (int)((c >> 8) & 0xFF);
        db = b - (int)(c & 0xFF);
        d = (DWORD)((long)dr * dr + (long)dg * dg + (long)db * db);
        if (d < best) { best = d; bi = i; }
    }
    return (BYTE)bi;
}

static BYTE dac_nearest(DWORD px)
{
    WORD h;
    px &= 0xFFFFFFUL;
    if (g_nc_gen != g_dac_gen) {
        WORD i;
        for (i = 0; i < NC_SIZE; i++) g_nc_ok[i] = 0;
        g_nc_gen = g_dac_gen;
    }
    h = (WORD)((px ^ (px >> 8) ^ (px >> 16)) & (NC_SIZE - 1));
    if (!g_nc_ok[h] || g_nc_px[h] != px) {
        g_nc_px[h] = px;
        g_nc_idx[h] = dac_search(px);
        g_nc_ok[h] = 1;
    }
    return g_nc_idx[h];
}

BYTE gsurf_nearest(DWORD px) { return dac_nearest(px); }

/* y*pitch bez wolania __U4M (pitch < 64KB w praktyce zawsze) */
DWORD mul16(WORD a, WORD b);
#pragma aux mul16 = "mul dx" parm [ax] [dx] value [dx ax] modify exact [ax dx];

static DWORD row_off(SURF __far *s, int y)
{
    if (s->pitch < 0x10000UL) return mul16((WORD)y, (WORD)s->pitch);
    return (DWORD)y * s->pitch;
}

/* Ile calych pikseli miesci sie do konca okna 64KB (bez dzielenia DWORD: __U4D) */
#define ROOM32(lo) ((lo) ? (WORD)(0 - (lo)) >> 2 : 0x4000U)
#define ROOM24(lo) ((lo) ? (WORD)(0 - (lo)) / 3 : 21845U)
#define ROOM8(lo)  ((lo) ? (WORD)(0 - (lo)) : 0xFFFFU)
#define MUL3(n)    (((DWORD)(n) << 1) + (DWORD)(n))

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
        off = row_off(s, y) + ((DWORD)x << 2);
        return *(DWORD __far *)addr(s, off) & 0xFFFFFFUL;
    }
    if (s->bpp == 8) {
        off = row_off(s, y) + (DWORD)x;
        return g_dac_rgb[*addr(s, off)];
    }
    off = row_off(s, y) + MUL3(x);
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
        off = row_off(s, y) + ((DWORD)x << 2);
        *(DWORD __far *)addr(s, off) = c;
        return;
    }
    if (s->bpp == 8) {
        off = row_off(s, y) + (DWORD)x;
        *addr(s, off) = dac_nearest(c);
        return;
    }
    off = row_off(s, y) + MUL3(x);
    if ((WORD)off <= 0xFFFD) {
        BYTE __far *p = addr(s, off);
        p[0] = (BYTE)c; p[1] = (BYTE)(c >> 8); p[2] = (BYTE)(c >> 16);
    } else {
        *addr(s, off) = (BYTE)c;
        *addr(s, off + 1) = (BYTE)(c >> 8);
        *addr(s, off + 2) = (BYTE)(c >> 16);
    }
}

/* Wiersz powierzchni <-> bufor pikseli 0x00RRGGBB, z podzialem na okna 64KB.
 * Wszystkie masowe operacje ida tedy: px_get/px_put na piksel kosztuja ~100x wiecej. */
void row_get(SURF __far *s, int x, int y, int n, DWORD *buf)
{
    DWORD off;
    int i;
    if (s->bpp == 32) {
        off = row_off(s, y) + ((DWORD)x << 2);
        while (n > 0) {
            WORD room = ROOM32((WORD)off);
            DWORD __far *p = (DWORD __far *)addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++) buf[i] = p[i] & 0xFFFFFFUL;
            buf += k; n -= k; off += (DWORD)k << 2;
        }
        return;
    }
    if (s->bpp == 8) {
        off = row_off(s, y) + (DWORD)x;
        while (n > 0) {
            WORD room = ROOM8((WORD)off);
            BYTE __far *p = addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++) buf[i] = g_dac_rgb[p[i]];
            buf += k; n -= k; off += (DWORD)k;
        }
        return;
    }
    off = row_off(s, y) + MUL3(x);
    while (n > 0) {
        WORD room = ROOM24((WORD)off);
        if (room == 0) {                       /* piksel rozciety miedzy oknami */
            *buf++ = *addr(s, off) | ((DWORD)*addr(s, off + 1) << 8) | ((DWORD)*addr(s, off + 2) << 16);
            n--; off += 3;
            continue;
        }
        {
            BYTE __far *p = addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++, p += 3)
                buf[i] = p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16);
            buf += k; n -= k; off += MUL3(k);
        }
    }
}

void row_put(SURF __far *s, int x, int y, int n, DWORD *buf)
{
    DWORD off;
    int i;
    if (s->bpp == 32) {
        off = row_off(s, y) + ((DWORD)x << 2);
        while (n > 0) {
            WORD room = ROOM32((WORD)off);
            DWORD __far *p = (DWORD __far *)addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++) p[i] = buf[i];
            buf += k; n -= k; off += (DWORD)k << 2;
        }
        return;
    }
    if (s->bpp == 8) {
        off = row_off(s, y) + (DWORD)x;
        while (n > 0) {
            WORD room = ROOM8((WORD)off);
            BYTE __far *p = addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++) p[i] = dac_nearest(buf[i]);
            buf += k; n -= k; off += (DWORD)k;
        }
        return;
    }
    off = row_off(s, y) + MUL3(x);
    while (n > 0) {
        WORD room = ROOM24((WORD)off);
        if (room == 0) {
            *addr(s, off) = (BYTE)*buf; *addr(s, off + 1) = (BYTE)(*buf >> 8); *addr(s, off + 2) = (BYTE)(*buf >> 16);
            buf++; n--; off += 3;
            continue;
        }
        {
            BYTE __far *p = addr(s, off);
            int k = (WORD)n < room ? n : (int)room;
            for (i = 0; i < k; i++, p += 3) {
                DWORD v = buf[i];
                p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16);
            }
            buf += k; n -= k; off += MUL3(k);
        }
    }
}

/* Bezposredni zapis surowych indeksow 8bpp -> VESA 8bpp (LFB lub banked 0xA000) */
void row_put_idx(SURF __far *s, int x, int y, int n, const BYTE __far *idx)
{
    DWORD off = row_off(s, y) + (DWORD)x;
    int i;
    while (n > 0) {
        WORD room = ROOM8((WORD)off);
        BYTE __far *p = addr(s, off);
        int k = (WORD)n < room ? n : (int)room;
        for (i = 0; i < k; i++) p[i] = idx[i];
        idx += k; n -= k; off += (DWORD)k;
    }
}

/* Jednolita linia na ekranie 8bpp */
static void fill8(SURF __far *s, int x, int y, int n, DWORD c)
{
    DWORD off = row_off(s, y) + (DWORD)x;
    BYTE idx = dac_nearest(c);
    int i;
    while (n > 0) {
        WORD room = ROOM8((WORD)off);
        BYTE __far *p = addr(s, off);
        int k = (WORD)n < room ? n : (int)room;
        for (i = 0; i < k; i++) p[i] = idx;
        n -= k; off += (DWORD)k;
    }
}

/* Jednolita linia na ekranie 24bpp */
static void fill24(SURF __far *s, int x, int y, int n, DWORD c)
{
    DWORD off = row_off(s, y) + MUL3(x);
    BYTE b0 = (BYTE)c, b1 = (BYTE)(c >> 8), b2 = (BYTE)(c >> 16);
    while (n > 0) {
        WORD room = ROOM24((WORD)off);
        if (room == 0) {
            *addr(s, off) = b0; *addr(s, off + 1) = b1; *addr(s, off + 2) = b2;
            n--; off += 3;
            continue;
        }
        {
            BYTE __far *p = addr(s, off);
            int k = (WORD)n < room ? n : (int)room, i;
            for (i = 0; i < k; i++, p += 3) { p[0] = b0; p[1] = b1; p[2] = b2; }
            n -= k; off += MUL3(k);
        }
    }
}

/* Wiersz glifu: bit i (MSB pierwszy, bajt i>>3 lezy pod bits[(i>>3)*step]) = piksel
 * koloru c na x+i, ograniczone do [xa,xb) */
void bits_row(SURF __far *s, int y, int x, int xa, int xb, BYTE __far *bits, WORD step, DWORD c)
{
    int i = xa - x, end = xb - x;
    BYTE __far *bp;
    DWORD off0, off1;
    BYTE b, m;
    if (i >= end) return;
    if (s->mono) c = mono_of(c);
    bp = bits + (WORD)(i >> 3) * step;
    b = *bp;
    m = (BYTE)(0x80 >> (i & 7));
    if (s->bpp == 32) {
        off0 = row_off(s, y) + ((DWORD)xa << 2);
        off1 = off0 + ((DWORD)(end - i) << 2) - 1;
    } else if (s->bpp == 8) {
        off0 = row_off(s, y) + (DWORD)xa;
        off1 = off0 + (DWORD)(end - i) - 1;
    } else {
        off0 = row_off(s, y) + MUL3(xa);
        off1 = off0 + MUL3(end - i) - 1;
    }
    if ((WORD)(off0 >> 16) != (WORD)(off1 >> 16)) {     /* wiersz przez granice okna */
        for (; i < end; i++) {
            if (b & m) px_put(s, x + i, y, c);
            if (!(m >>= 1)) { m = 0x80; bp += step; if (i + 1 < end) b = *bp; }
        }
        return;
    }
    if (s->bpp == 32) {
        DWORD __far *p = (DWORD __far *)addr(s, off0);
        for (; i < end; i++, p++) {
            if (b & m) *p = c;
            if (!(m >>= 1)) { m = 0x80; bp += step; if (i + 1 < end) b = *bp; }
        }
    } else if (s->bpp == 8) {
        BYTE __far *p = addr(s, off0);
        BYTE idx = dac_nearest(c);
        for (; i < end; i++, p++) {
            if (b & m) *p = idx;
            if (!(m >>= 1)) { m = 0x80; bp += step; if (i + 1 < end) b = *bp; }
        }
    } else {
        BYTE __far *p = addr(s, off0);
        BYTE b0 = (BYTE)c, b1 = (BYTE)(c >> 8), b2 = (BYTE)(c >> 16);
        for (; i < end; i++, p += 3) {
            if (b & m) { p[0] = b0; p[1] = b1; p[2] = b2; }
            if (!(m >>= 1)) { m = 0x80; bp += step; if (i + 1 < end) b = *bp; }
        }
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
    static DWORD line[128];
    if (x0 >= x1) return;
    if (s->mono) c = mono_of(c);
    if (rop2 == 13 && s->bpp == 32) {
        DWORD off = row_off(s, y) + ((DWORD)x0 << 2);
        int n = x1 - x0;
        while (n > 0) {
            WORD room = ROOM32((WORD)off);
            DWORD __far *p = (DWORD __far *)addr(s, off);
            int k = (WORD)n < room ? n : (int)room, i;
            for (i = 0; i < k; i++) p[i] = c;
            n -= k;
            off += (DWORD)k << 2;
        }
        return;
    }
    if (rop2 == 13) {
        if (s->bpp == 8) fill8(s, x0, y, x1 - x0, c);
        else fill24(s, x0, y, x1 - x0, c);
        return;
    }
    while (x0 < x1) {
        int k = x1 - x0 > 128 ? 128 : x1 - x0, i;
        row_get(s, x0, y, k, line);
        for (i = 0; i < k; i++) {
            DWORD v = rop2_apply(rop2, c, line[i]) & 0xFFFFFFUL;
            line[i] = s->mono ? mono_of(v) : v;
        }
        row_put(s, x0, y, k, line);
        x0 += k;
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
    s->sel = SEL_VESA_BASE; s->bpp = g_hw_bpp; s->mono = 0;
    s->w = SCR_W; s->h = SCR_H; s->pitch = g_hw_pitch;
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
static DWORD g_cbg[CUR_MAX * CUR_MAX];
static BYTE  g_cbg8[CUR_MAX * CUR_MAX];
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

void gsurf_init_screen(void)
{
    g_scr.sel = SEL_VESA_BASE;
    g_scr.bpp = g_hw_bpp;
    g_scr.mono = 0;
    g_scr.w = SCR_W;
    g_scr.h = SCR_H;
    g_scr.pitch = g_hw_pitch;
    g_cur_bank = 0xFFFF;
}

/* Wiersze kursora przyciete do ekranu: [*x0,*x1) w wsp. ekranu; FALSE = poza ekranem */
static BOOL cur_clip(int __far *x0, int __far *x1)
{
    *x0 = g_cbx < 0 ? 0 : g_cbx;
    *x1 = g_cbx + g_cw > SCR_W ? SCR_W : g_cbx + g_cw;
    return *x0 < *x1;
}

static void cur_erase(void)
{
    int y, x0, x1;
    if (!g_cdrawn) return;
    if (cur_clip((int __far *)&x0, (int __far *)&x1))
        for (y = 0; y < g_ch; y++) {
            int sy = g_cby + y;
            if (sy < 0 || sy >= SCR_H) continue;
            if (g_scr.bpp == 8)
                row_put_idx((SURF __far *)&g_scr, x0, sy, x1 - x0,
                            (const BYTE __far *)&g_cbg8[y * CUR_MAX + (x0 - g_cbx)]);
            else
                row_put((SURF __far *)&g_scr, x0, sy, x1 - x0, &g_cbg[y * CUR_MAX + (x0 - g_cbx)]);
        }
    g_cdrawn = FALSE;
}

static void cur_draw(void)
{
    static DWORD line[CUR_MAX];
    static BYTE  line8[CUR_MAX];
    int x, y, x0, x1;
    if (g_cdrawn || g_cshow < 0 || !g_chave) return;
    g_cbx = g_cx - g_chx; g_cby = g_cy - g_chy;
    if (cur_clip((int __far *)&x0, (int __far *)&x1)) {
        if (g_scr.bpp == 8) {
            BYTE idx_blk = dac_nearest(0), idx_wht = dac_nearest(0xFFFFFFUL);
            for (y = 0; y < g_ch; y++) {
                int sy = g_cby + y;
                BYTE *bg8 = &g_cbg8[y * CUR_MAX + (x0 - g_cbx)];
                DWORD off;
                if (sy < 0 || sy >= SCR_H) continue;
                off = row_off((SURF __far *)&g_scr, sy) + (DWORD)x0;
                for (x = x0; x < x1; x++) {
                    int bit = y * CUR_MAX + (x - g_cbx);
                    BYTE a = (BYTE)((g_cand[bit >> 3] >> (7 - (bit & 7))) & 1);
                    BYTE xo = (BYTE)((g_cxor[bit >> 3] >> (7 - (bit & 7))) & 1);
                    BYTE orig = *addr((SURF __far *)&g_scr, off + (DWORD)(x - x0));
                    bg8[x - x0] = orig;
                    if (a) line8[x - x0] = xo ? (BYTE)(orig ^ 0xFF) : orig;
                    else   line8[x - x0] = xo ? idx_wht : idx_blk;
                }
                row_put_idx((SURF __far *)&g_scr, x0, sy, x1 - x0, (const BYTE __far *)line8);
            }
        } else {
            for (y = 0; y < g_ch; y++) {
                int sy = g_cby + y;
                DWORD *bg = &g_cbg[y * CUR_MAX + (x0 - g_cbx)];
                if (sy < 0 || sy >= SCR_H) continue;
                row_get((SURF __far *)&g_scr, x0, sy, x1 - x0, bg);
                for (x = x0; x < x1; x++) {
                    int bit = y * CUR_MAX + (x - g_cbx);
                    BYTE a = (BYTE)((g_cand[bit >> 3] >> (7 - (bit & 7))) & 1);
                    BYTE xo = (BYTE)((g_cxor[bit >> 3] >> (7 - (bit & 7))) & 1);
                    DWORD v = bg[x - x0];
                    if (a) { if (xo) v ^= 0xFFFFFFUL; }
                    else v = xo ? 0xFFFFFFUL : 0;
                    line[x - x0] = v;
                }
                row_put((SURF __far *)&g_scr, x0, sy, x1 - x0, line);
            }
        }
    }
    g_cdrawn = TRUE;
}

/* Zagniezdzone wywolania (cel + zrodlo BitBlt) kazde sprawdzaja swoj prostokat;
 * kursor wraca przy ostatnim cursor_restore */
void cursor_exclude(int x0, int y0, int x1, int y1)
{
    g_cdepth++;
    if (g_cdrawn && x0 < g_cbx + g_cw && x1 > g_cbx && y0 < g_cby + g_ch && y1 > g_cby) {
        cur_erase();
        g_cexcl = TRUE;
    }
}

/* Po przerwanym rysowaniu (koniec zadania w trakcie operacji) */
void cursor_reset(void)
{
    g_cdepth = 1;
    g_cexcl = !g_cdrawn;
    cursor_restore();
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

/* ordinal 904: GdiResetCursor() - po powrocie z pelnoekranowego DOS (ekran zresetowany przez VESA) */
void API GdiResetCursor(void)
{
    if (!g_chave) cur_default();
    g_cdrawn = FALSE;
    g_cdepth = 0;
    g_cexcl = FALSE;
    cur_draw();
}
