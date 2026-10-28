/*
 * gdi.c - GDI.EXE (STEP12/ETAP14)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1:
 *   29  = PatBlt             (WHITENESS/BLACKNESS -> wypelnienie)
 *   33  = TextOut            (rysuje 8x16 na LFB przez huge selectors)
 *   34  = BitBlt             (4bpp sprite z SEL_BITMAPS -> LFB, przezroczystosc)
 *   45  = SelectObject       (zapamietuje HBITMAP w g_dc_bitmap[hdc])
 *   48  = CreateBitmap       (stub)
 *   51  = CreateCompatibleBitmap (fake handle + atlas tracking)
 *   52  = CreateCompatibleDC     (stub)
 *   68  = DeleteDC           (stub)
 *   69  = DeleteObject       (stub)
 *   80  = GetDeviceCaps      (640x480x24)
 *   82  = GetObject          (BITMAP struct z BITMAPINFOHEADER)
 *   87  = GetStockObject     (stale handlery)
 *   91  = GetTextExtent      (len * FONT_W)
 *   93  = GetTextMetrics     (TEXTMETRIC 8x16)
 *   200 = LibMain
 *
 * UWAGA: GetDC/ReleaseDC przeniesione do USER.EXE (ordinal 66/68).
 *
 * SEL_BITMAPS = 0x128: bufor 86 sprite'ow 4bpp z SKI.EXE.
 *   Format: [count(2)][pad(2)][offsets[86]*2][dane DIB]
 *   HBITMAP = id (1..86); MK_FP(SEL_BITMAPS, offsets[id-1]) = BITMAPINFOHEADER.
 */

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

#define COM1 0x3F8

static void serial_putc(char c)
{
    while (!(io_inb(COM1 + 5) & 0x20));
    if (c == '\n') {
        io_outb(COM1, '\r');
        while (!(io_inb(COM1 + 5) & 0x20));
    }
    io_outb(COM1, c);
}

static void serial_puts(const char *s)
{
    while (*s) serial_putc(*s++);
}

static void serial_puthex16(unsigned short v)
{
    static char buf[5];
    int i;
    for (i = 3; i >= 0; i--) { buf[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
    buf[4] = 0; serial_puts(buf);
}

/* ============================================================
 * GDT selektory
 * ============================================================ */
#define SEL_VESA_BASE  0xA0
#define SEL_FONT       0x118
#define SEL_BITMAPS    0x128   /* bufor 86 sprite'ow 4bpp SKI.EXE */

#define VESA_PITCH  1920
#define VESA_BPP    3
#define FONT_W      8
#define FONT_H      16

#define MAX_BITMAPS  86
#define BMP_BUF_HDR  (4 + MAX_BITMAPS * 2)  /* 176: count(2)+pad(2)+offsets[86]*2 */
#define TRANSP_IDX   15   /* paleta 4bpp: indeks 15 = bialy = tlo (przezroczysty) */

typedef unsigned short BOOL;
typedef unsigned short HDC;

/* KCB - tablica pozycji okien (zsynchronizowana przez USER.EXE) */
#define SEL_KCB         ((unsigned short)0x98)
#define KCB_WND_OX_OFF  208   /* short wnd_ox[8]: abs x per hwnd (indeks hwnd-1) */
#define KCB_WND_OY_OFF  224   /* short wnd_oy[8]: abs y per hwnd (indeks hwnd-1) */

/* Dekodowanie origin okna z HDC:
 *   HDC = 1 lub 2..15  -> origin (0,0)
 *   HDC bit14=1        -> HWND-based DC: origin z KCB[hwnd-1]
 *   HDC bit15=1        -> (legacy) bits[14:7]=x/4, bits[6:0]=y/4 */
static void gdi_decode_dc(HDC hdc, int *ox, int *oy)
{
    if ((unsigned)hdc & 0x4000u) {
        unsigned hwnd = (unsigned)hdc & 0x3FFFu;
        unsigned off_x = KCB_WND_OX_OFF + (hwnd - 1u) * 2u;
        short __far *kox = (short __far *)MK_FP(SEL_KCB, off_x);
        short __far *koy = (short __far *)MK_FP(SEL_KCB, KCB_WND_OY_OFF + (hwnd - 1u) * 2u);
        *ox = *kox;
        *oy = *koy;
    } else if ((unsigned)hdc & 0x8000u) {
        *ox = (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        *oy = (int)((unsigned)hdc & 0x7Fu) * 4;
    } else {
        *ox = 0; *oy = 0;
    }
}

#pragma pack(push, 1)
typedef struct {
    unsigned long  biSize;
    long           biWidth;
    long           biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned long  biCompression;
    unsigned long  biSizeImage;
    long           biXPelsPerMeter;
    long           biYPelsPerMeter;
    unsigned long  biClrUsed;
    unsigned long  biClrImportant;
} BITMAPINFOHEADER;  /* 40 bajtow */

typedef struct {
    unsigned char rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;  /* 4 bajty */

typedef struct {
    short bmType;
    short bmWidth;
    short bmHeight;
    short bmWidthBytes;
    unsigned char bmPlanes;
    unsigned char bmBitsPixel;
    void __far *bmBits;
} BITMAP;  /* 14 bajtow */
#pragma pack(pop)

/* DC -> HBITMAP mapping: g_dc_bitmap[hdc] gdzie hdc = 1..15 */
static unsigned g_dc_bitmap[16] = {0};

/* ============================================================
 * Atlas tracking dla BitBlt:
 *   - fake HBITMAP z CreateCompatibleBitmap: 87..N
 *   - atlas entry: (atlasDC, yOff) -> sprite_hbm (1..86)
 *   - g_dc_sprite[hdc]: ktory sprite jest "gotowy do renderowania" w tym DC
 * ============================================================ */
#define FAKE_HBM_BASE  (MAX_BITMAPS + 1)   /* 87 */
static unsigned g_next_fake_hbm = FAKE_HBM_BASE;

typedef struct {
    HDC            hdc;
    unsigned short y_off;
    unsigned       hbm;
} AtlasEntry;
#define MAX_ATLAS_ENTRIES  90
static AtlasEntry g_atlas_tab[MAX_ATLAS_ENTRIES];
static unsigned   g_atlas_cnt = 0;

/* Per-DC: ktory sprite jest gotowy do narysowania gdy ten DC jest zrodlem do screen.
 * Fallback; zastapiony przez g_comp_slots dla dokladniejszego dopasowania. */
static unsigned g_dc_sprite[16] = {0};

/* Compositing slots: SRCAND rejestruje (hdcDst, xDst, yDst) -> sprite_hbm.
 * SRCCOPY szuka slotu po (hdcSrc, xDst, yDst) zamiast uzywac g_dc_sprite[hdc]
 * ktore jest wspoldzielone przez wszystkie sprite na tym samym DC.
 *
 * Bez tego: logo SRCAND nadpisuje gondola SRCAND w g_dc_sprite[[B46]], przez
 * co logo SRCCOPY rysuje gondole przy pozycji logo -> gondola "skacze w lewo". */
typedef struct {
    HDC      hdc;
    int      x, y;
    unsigned hbm;
} CompSlot;
#define MAX_COMP_SLOTS 32
static CompSlot g_comp_slots[MAX_COMP_SLOTS];
static unsigned g_comp_cnt = 0;

/* Save-behind: zapamiętuje piksele ekranu pod sprite'm przed jego narysowaniem.
 * Przy nastepnym ruchu: przywraca stare piksele (chroni logo, przeszkody statyczne).
 * MAX_SB_SLOTS slot na jednoczesnie aktywne sprite'y; reszta: fallback fill_white. */
#define MAX_SB_W     64
#define MAX_SB_H     64
#define MAX_SB_SLOTS  4
typedef struct {
    unsigned      hbm;                           /* 1..86, 0 = wolny slot */
    unsigned      age;                           /* zegar LRU: wyzszy = mlodszy */
    int           x, y, w, h;
    unsigned char pixels[MAX_SB_W * MAX_SB_H * 3]; /* 64*64*3=12288 < 32767 ok */
} SBSlot;
static SBSlot   g_sb[MAX_SB_SLOTS];              /* zero-init: wszystkie wolne */
static unsigned g_sb_clock;                      /* monotonicznie rosnie przy kazdym uzyciu */

/* Fallback per-sprite dla sprite'ow bez slotu save-behind */
typedef struct { int x, y, w, h; unsigned valid; } PrevPos;
static PrevPos g_prev_pos[MAX_BITMAPS + 1];

/* ============================================================
 * draw_pixel + draw_char_gdi (bez zmian vs STEP10)
 * ============================================================ */
static void draw_pixel(unsigned long flat_off,
                       unsigned char r, unsigned char g, unsigned char b)
{
    unsigned win = (unsigned)(flat_off >> 16);
    unsigned off = (unsigned)(flat_off & 0xFFFFUL);
    unsigned sel = SEL_VESA_BASE + win * 8;
    unsigned char __far *p = (unsigned char __far *)MK_FP(sel, off);
    p[0] = b; p[1] = g; p[2] = r;
}

static void read_pixel(unsigned long flat_off,
                       unsigned char *r_out, unsigned char *g_out, unsigned char *b_out)
{
    unsigned win = (unsigned)(flat_off >> 16);
    unsigned off = (unsigned)(flat_off & 0xFFFFUL);
    unsigned sel = SEL_VESA_BASE + win * 8;
    unsigned char __far *p = (unsigned char __far *)MK_FP(sel, off);
    *b_out = p[0]; *g_out = p[1]; *r_out = p[2];
}

/* Wypelnij bialym prostokat na ekranie (clip do 640x480) */
static void fill_white_rect(int x, int y, int w, int h)
{
    int row, col;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 640) w = 640 - x;
    if (y + h > 480) h = 480 - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            draw_pixel((unsigned long)(y + row) * VESA_PITCH +
                       (unsigned long)(x + col) * VESA_BPP, 0xFF, 0xFF, 0xFF);
}

/* Przywroc piksele ekranu ze slotu i zwolnij go */
static void sb_restore_and_free(SBSlot *sl)
{
    int row, col;
    unsigned char *px = sl->pixels;
    for (row = 0; row < sl->h; row++)
        for (col = 0; col < sl->w; col++) {
            unsigned long flat = (unsigned long)(sl->y + row) * VESA_PITCH
                               + (unsigned long)(sl->x + col) * VESA_BPP;
            draw_pixel(flat, px[0], px[1], px[2]);
            px += 3;
        }
    sl->hbm = 0;
}

/* Zapisz piksele ekranu do slotu (clip do ekranu i rozmiaru bufora) */
static void sb_save_to(SBSlot *sl, int x, int y, int w, int h)
{
    int row, col;
    unsigned char *px;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 640) w = 640 - x;
    if (y + h > 480) h = 480 - y;
    if (w > MAX_SB_W) w = MAX_SB_W;
    if (h > MAX_SB_H) h = MAX_SB_H;
    if (w <= 0 || h <= 0) { sl->w = 0; sl->h = 0; return; }
    sl->x = x; sl->y = y; sl->w = w; sl->h = h;
    px = sl->pixels;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++) {
            unsigned long flat = (unsigned long)(y + row) * VESA_PITCH
                               + (unsigned long)(x + col) * VESA_BPP;
            read_pixel(flat, px, px + 1, px + 2);
            px += 3;
        }
}

/* Znajdz slot dla hbm (odswierza wiek) lub NULL */
static SBSlot * sb_find(unsigned hbm)
{
    unsigned i;
    for (i = 0; i < MAX_SB_SLOTS; i++) {
        if (g_sb[i].hbm == hbm) {
            g_sb[i].age = ++g_sb_clock;
            return &g_sb[i];
        }
    }
    return 0;
}

/* Alokuj wolny slot dla hbm lub NULL jesli brak miejsca (test bez LRU) */
static SBSlot * sb_alloc(unsigned hbm)
{
    unsigned i;
    for (i = 0; i < MAX_SB_SLOTS; i++)
        if (g_sb[i].hbm == 0) { g_sb[i].hbm = hbm; g_sb[i].age = ++g_sb_clock; return &g_sb[i]; }
    return 0;
}

static void draw_char_gdi(unsigned char ch, unsigned x, unsigned y,
                           unsigned char br, unsigned char bg, unsigned char bb)
{
    unsigned row, bit;
    unsigned long row_base = (unsigned long)y * VESA_PITCH
                           + (unsigned long)x * VESA_BPP;
    for (row = 0; row < FONT_H; row++) {
        unsigned char fbyte = *((unsigned char __far *)
            MK_FP(SEL_FONT, (unsigned)ch * FONT_H + row));
        unsigned long edi = row_base + (unsigned long)row * VESA_PITCH;
        for (bit = 8; bit-- > 0; ) {
            if (fbyte & (1 << bit))
                draw_pixel(edi, br, bg, bb);
            edi += VESA_BPP;
        }
    }
}

/* ============================================================
 * ordinal 33: TextOut
 * ============================================================ */
BOOL __far __pascal TextOut(HDC hdc, int x, int y,
                             const char __far *s, int len)
{
    int i;
    /* Dekoduj origin z HDC (inline - near ptr na stos nie dziala gdy DS!=SS) */
    if ((unsigned)hdc & 0x4000u) {
        unsigned hwnd_v = (unsigned)hdc & 0x3FFFu;
        short __far *kox = (short __far *)MK_FP(SEL_KCB, KCB_WND_OX_OFF + (hwnd_v-1u)*2u);
        short __far *koy = (short __far *)MK_FP(SEL_KCB, KCB_WND_OY_OFF + (hwnd_v-1u)*2u);
        x += *kox;
        y += *koy;
    } else if ((unsigned)hdc & 0x8000u) {
        x += (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        y += (int)((unsigned)hdc & 0x7Fu) * 4;
    }
    for (i = 0; i < len; i++)
        draw_char_gdi((unsigned char)s[i],
                      (unsigned)(x + i * FONT_W), (unsigned)y,
                      0x00, 0x00, 0x00);  /* czarny tekst na bialym tle */
    return 1;
}

/* ============================================================
 * blit_sprite_hbm - renderuje sprite HBITMAP (1..86) na LFB
 * (faktyczny rendering pikseli, wywolywany z BitBlt)
 * ============================================================ */
static void blit_sprite_hbm(unsigned hbm, int xDst, int yDst, int w, int h,
                              int xSrc, int ySrc)
{
    unsigned char __far *bmp_buf;
    unsigned short bmp_off;
    BITMAPINFOHEADER __far *bi;
    unsigned char __far *pal_b;
    short bw, bh;
    unsigned short row_bytes;
    int r, c;

    if (hbm < 1 || hbm > MAX_BITMAPS) return;
    bmp_buf = (unsigned char __far *)MK_FP(SEL_BITMAPS, 0);
    bmp_off = *(unsigned short __far *)(bmp_buf + 4 + (hbm - 1) * 2);
    if (bmp_off == 0) return;

    bi        = (BITMAPINFOHEADER __far *)(bmp_buf + bmp_off);
    pal_b     = bmp_buf + bmp_off + 40;
    bw        = (short)bi->biWidth;
    bh        = (short)(bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    row_bytes = (unsigned short)(((unsigned)bw * 4u + 31u) / 32u * 4u);

    if (xSrc < 0) { xDst -= xSrc; w += xSrc; xSrc = 0; }
    if (ySrc < 0) { yDst -= ySrc; h += ySrc; ySrc = 0; }
    if (xSrc + w > bw) w = (int)bw - xSrc;
    if (ySrc + h > bh) h = (int)bh - ySrc;
    /* Clip destination do granic ekranu */
    if (xDst < 0) { xSrc -= xDst; w += xDst; xDst = 0; }
    if (yDst < 0) { ySrc -= yDst; h += yDst; yDst = 0; }
    if (xDst + w > 640) w = 640 - xDst;
    if (yDst + h > 480) h = 480 - yDst;
    if (w <= 0 || h <= 0) return;

    for (r = 0; r < h; r++) {
        unsigned short dib_row = (unsigned short)((int)bh - 1 - ySrc - r);
        unsigned short row_off = (unsigned short)(bmp_off + 40u + 64u +
                                  (unsigned long)dib_row * row_bytes);
        int dst_y = yDst + r;
        for (c = 0; c < w; c++) {
            int sx = xSrc + c;
            unsigned char byte_val = *(bmp_buf + row_off + (unsigned)(sx >> 1));
            unsigned char idx = (sx & 1) ? (byte_val & 0x0F) : (byte_val >> 4);
            unsigned char __far *rgb;
            unsigned long flat_off;
            if (idx == TRANSP_IDX) continue;
            rgb = pal_b + (unsigned)idx * 4u;
            flat_off = (unsigned long)dst_y * VESA_PITCH +
                       (unsigned long)(xDst + c) * VESA_BPP;
            draw_pixel(flat_off, rgb[2], rgb[1], rgb[0]);
        }
    }
}

/* ============================================================
 * ordinal 34: BitBlt
 *
 * Logika atlas-tracking (SKI.EXE uzywa atlasow sprite'ow):
 *
 * Faza init: SelectObject([B46], sprite_hbm); BitBlt(atlasDC, 0, y, w, h, [B46], 0, 0, SRCCOPY)
 *   -> rejestrujemy: g_atlas_tab[{atlasDC, y}] = sprite_hbm
 *
 * Faza render (per-sprite):
 *   BitBlt([B46]DC, x, y, w, h, spriteAtlasDC, 0, y_off, SRCAND)
 *   -> szukamy sprite_hbm = g_atlas_tab[{spriteAtlasDC, y_off}]
 *   -> rejestrujemy: g_dc_sprite[[B46]DC] = sprite_hbm
 *
 *   BitBlt(screenDC=1, xDst, yDst, w, h, [B46]DC, 0, 0, SRCCOPY)
 *   -> renderujemy g_dc_sprite[[B46]DC] na ekranie
 * ============================================================ */
BOOL __far __pascal BitBlt(HDC hdcDst, int xDst, int yDst, int w, int h,
                            HDC hdcSrc, int xSrc, int ySrc, unsigned long dwRop)
{
    unsigned src_hbm, dst_hbm, sprite_hbm;
    unsigned i;
    SBSlot *sl, *sl2;

    if (hdcDst < 1 || hdcDst >= 16) return 1;

    /* Pattern ROPs: WHITENESS/BLACKNESS - wypelnienie niezalezne od zrodla */
    if (dwRop == 0x00FF0062UL || dwRop == 0x00000042UL) {
        if (hdcDst == 1) {
            unsigned char rv = (dwRop == 0x00FF0062UL) ? 0xFF : 0;
            int row, col, cx = xDst, cy = yDst, cw = w, ch = h;
            if (cx < 0) { cw += cx; cx = 0; }
            if (cy < 0) { ch += cy; cy = 0; }
            if (cx + cw > 640) cw = 640 - cx;
            if (cy + ch > 480) ch = 480 - cy;
            for (row = 0; row < ch; row++)
                for (col = 0; col < cw; col++)
                    draw_pixel((unsigned long)(cy + row) * VESA_PITCH +
                               (unsigned long)(cx + col) * VESA_BPP, rv, rv, rv);
        }
        return 1;
    }

    if (hdcSrc < 1 || hdcSrc >= 16) return 1;

    src_hbm = g_dc_bitmap[hdcSrc];
    dst_hbm = g_dc_bitmap[hdcDst];

    /* ---- Przypadek 1: sprite (1..86) -> atlas DC (fake bitmap) ---- */
    /* SRCCOPY: rejestrujemy w tablicy atlasu */
    if (src_hbm >= 1 && src_hbm <= MAX_BITMAPS &&
        dst_hbm >= FAKE_HBM_BASE && dst_hbm < 0x8000u &&
        dwRop == 0x00CC0020UL) {
        if (g_atlas_cnt < MAX_ATLAS_ENTRIES) {
            g_atlas_tab[g_atlas_cnt].hdc   = hdcDst;
            g_atlas_tab[g_atlas_cnt].y_off = (unsigned short)yDst;
            g_atlas_tab[g_atlas_cnt].hbm   = src_hbm;
            g_atlas_cnt++;
        }
        return 1;
    }

    /* ---- Przypadek 2: sprite (1..86) -> screen DC (bezposredni blit) ---- */
    if (src_hbm >= 1 && src_hbm <= MAX_BITMAPS && hdcDst == 1) {
        sl = sb_find(src_hbm);
        if (sl) {
            sb_restore_and_free(sl);      /* przywroc stare tlo */
        } else if (g_prev_pos[src_hbm].valid) {
            fill_white_rect(g_prev_pos[src_hbm].x, g_prev_pos[src_hbm].y,
                            g_prev_pos[src_hbm].w, g_prev_pos[src_hbm].h);
        }
        sl = sb_alloc(src_hbm);
        if (sl) {
            sb_save_to(sl, xDst, yDst, w, h);  /* zapisz nowe tlo */
            g_prev_pos[src_hbm].valid = 0;
        } else {
            g_prev_pos[src_hbm].x = xDst; g_prev_pos[src_hbm].y = yDst;
            g_prev_pos[src_hbm].w = w;    g_prev_pos[src_hbm].h = h;
            g_prev_pos[src_hbm].valid = 1;
        }
        blit_sprite_hbm(src_hbm, xDst, yDst, w, h, xSrc, ySrc);
        return 1;
    }

    /* ---- Przypadek 3: atlas DC -> memDC lub screen DC ---- */
    /* Szukamy sprite w tablicy atlasu (po kluczu hdcSrc + ySrc) */
    sprite_hbm = 0;
    if (src_hbm >= FAKE_HBM_BASE && src_hbm < 0x8000u) {
        for (i = 0; i < g_atlas_cnt; i++) {
            if (g_atlas_tab[i].hdc == hdcSrc &&
                g_atlas_tab[i].y_off == (unsigned short)ySrc) {
                sprite_hbm = g_atlas_tab[i].hbm;
                break;
            }
        }
    }

    if (sprite_hbm >= 1 && sprite_hbm <= MAX_BITMAPS) {
        if (hdcDst == 1) {
            /* Bezposredni render atlas -> ekran (SRCCOPY) */
            if (dwRop == 0x00CC0020UL) {
                sl2 = sb_find(sprite_hbm);
                if (sl2) sb_restore_and_free(sl2);
                sl2 = sb_alloc(sprite_hbm);
                if (sl2) {
                    sb_save_to(sl2, xDst, yDst, w, h);
                    g_prev_pos[sprite_hbm].valid = 0;
                } else {
                    g_prev_pos[sprite_hbm].x = xDst; g_prev_pos[sprite_hbm].y = yDst;
                    g_prev_pos[sprite_hbm].w = w;    g_prev_pos[sprite_hbm].h = h;
                    g_prev_pos[sprite_hbm].valid = 1;
                }
                blit_sprite_hbm(sprite_hbm, xDst, yDst, w, h, xSrc, ySrc);
            }
        } else {
            /* Compositing krok: zarejstruj sprite w comp_slots po (hdcDst, xDst, yDst).
             * (xDst, yDst) = offset WEWNATRZ DC, nie pozycja na ekranie.
             * Kilka sprite'ow moze byc zlozona w DC6 przy roznych offsetach. */
            unsigned found_slot = 0;
            for (i = 0; i < g_comp_cnt; i++) {
                if (g_comp_slots[i].hdc == hdcDst &&
                    g_comp_slots[i].x == xDst &&
                    g_comp_slots[i].y == yDst) {
                    g_comp_slots[i].hbm = sprite_hbm;
                    found_slot = 1; break;
                }
            }
            if (!found_slot && g_comp_cnt < MAX_COMP_SLOTS) {
                g_comp_slots[g_comp_cnt].hdc = hdcDst;
                g_comp_slots[g_comp_cnt].x   = xDst;
                g_comp_slots[g_comp_cnt].y   = yDst;
                g_comp_slots[g_comp_cnt].hbm = sprite_hbm;
                g_comp_cnt++;
            }
            g_dc_sprite[hdcDst] = sprite_hbm;
        }
    } else if (hdcDst == 1 && dwRop == 0x00CC0020UL) {
        /* Render memDC (np. DC6 z wieloma sprite'ami) na ekran.
         * Kazdy comp_slot dla hdcSrc to oddzielny sprite z ofsetem w DC.
         * Pozycja na ekranie = (xDst + slot.x, yDst + slot.y). */
        unsigned found_any = 0;
        unsigned j;
        for (j = 0; j < g_comp_cnt; j++) {
            if (g_comp_slots[j].hdc == hdcSrc) {
                unsigned s_hbm = g_comp_slots[j].hbm;
                int s_x = xDst + g_comp_slots[j].x;
                int s_y = yDst + g_comp_slots[j].y;
                if (s_hbm >= 1 && s_hbm <= MAX_BITMAPS) {
                    sl2 = sb_find(s_hbm);
                    if (sl2) sb_restore_and_free(sl2);
                    sl2 = sb_alloc(s_hbm);
                    if (sl2) {
                        sb_save_to(sl2, s_x, s_y, w, h);
                        g_prev_pos[s_hbm].valid = 0;
                    } else {
                        g_prev_pos[s_hbm].x = s_x; g_prev_pos[s_hbm].y = s_y;
                        g_prev_pos[s_hbm].w = w;    g_prev_pos[s_hbm].h = h;
                        g_prev_pos[s_hbm].valid = 1;
                    }
                    blit_sprite_hbm(s_hbm, s_x, s_y, w, h, xSrc, ySrc);
                    found_any = 1;
                }
            }
        }
        /* Usun wszystkie sloty dla tego DC */
        j = 0;
        while (j < g_comp_cnt) {
            if (g_comp_slots[j].hdc == hdcSrc)
                g_comp_slots[j] = g_comp_slots[--g_comp_cnt];
            else
                j++;
        }
        /* Fallback: pojedynczy sprite z g_dc_sprite jesli brak comp_slots */
        if (!found_any) {
            sprite_hbm = g_dc_sprite[hdcSrc];
            if (sprite_hbm >= 1 && sprite_hbm <= MAX_BITMAPS) {
                sl2 = sb_find(sprite_hbm);
                if (sl2) sb_restore_and_free(sl2);
                sl2 = sb_alloc(sprite_hbm);
                if (sl2) {
                    sb_save_to(sl2, xDst, yDst, w, h);
                    g_prev_pos[sprite_hbm].valid = 0;
                } else {
                    g_prev_pos[sprite_hbm].x = xDst; g_prev_pos[sprite_hbm].y = yDst;
                    g_prev_pos[sprite_hbm].w = w;    g_prev_pos[sprite_hbm].h = h;
                    g_prev_pos[sprite_hbm].valid = 1;
                }
                blit_sprite_hbm(sprite_hbm, xDst, yDst, w, h, xSrc, ySrc);
            }
        }
    }

    return 1;
}

/* ============================================================
 * ordinal 29: PatBlt - wypelnienie prostokata
 *
 * Obslugiwane ROPs:
 *   WHITENESS = 0x00FF0062 -> bialy (255,255,255)
 *   BLACKNESS = 0x00000042 -> czarny (0,0,0)
 *   inne: stub (brak rysowania)
 * ============================================================ */
BOOL __far __pascal PatBlt(HDC hdc, int x, int y, int w, int h,
                            unsigned long dwRop)
{
    unsigned char rv, gv, bv;
    int row, col;
    /* Pomijaj memory DC (2..15); akceptuj screen DC (1), HWND DC (bit14=1), window DC (bit15=1) */
    if (hdc != 1 && !((unsigned)hdc & 0xC000u)) return 1;
    if (dwRop == 0x00FF0062UL) { rv = 0xFF; gv = 0xFF; bv = 0xFF; }  /* WHITENESS */
    else if (dwRop == 0x00000042UL) { rv = 0; gv = 0; bv = 0; }      /* BLACKNESS */
    else return 1;

    /* Dekoduj origin (inline - DS!=SS w DLL) */
    if ((unsigned)hdc & 0x4000u) {
        unsigned hwnd_v = (unsigned)hdc & 0x3FFFu;
        short __far *kox = (short __far *)MK_FP(SEL_KCB, KCB_WND_OX_OFF + (hwnd_v-1u)*2u);
        short __far *koy = (short __far *)MK_FP(SEL_KCB, KCB_WND_OY_OFF + (hwnd_v-1u)*2u);
        x += *kox;
        y += *koy;
    } else if ((unsigned)hdc & 0x8000u) {
        x += (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        y += (int)((unsigned)hdc & 0x7Fu) * 4;
    }

    /* Clip do granic ekranu */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 640) w = 640 - x;
    if (y + h > 480) h = 480 - y;
    if (w <= 0 || h <= 0) return 1;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            draw_pixel((unsigned long)(y + row) * VESA_PITCH +
                       (unsigned long)(x + col) * VESA_BPP, rv, gv, bv);
        }
    }
    return 1;
}

/* ============================================================
 * ordinal 52: CreateCompatibleDC  ordinal 68: DeleteDC
 * ============================================================ */
/* Fake memory DC: return HDC=2 (screen HDC=1, memory DC=2..N) */
static HDC g_next_hdc = 2;
HDC __far __pascal CreateCompatibleDC(HDC hdc)
{
    HDC result = g_next_hdc++;
    (void)hdc;
    /* Inicjalizuj DC domyslna bitmapa (sentinel 0xFFFF),
       bo SelectObject zwraca poprzedni obiekt i SKI sprawdza czy != 0 */
    if (result < 16)
        g_dc_bitmap[result] = 0xFFFFu;
    serial_puts("GDI: CreateCompatDC -> ");
    serial_puthex16(result);
    serial_putc('\n');
    return result;
}

int __far __pascal DeleteDC(HDC hdc)
{
    (void)hdc;
    return 1;
}

/* ============================================================
 * Operacje na bitmapach i obiektach GDI - stubs (ETAP 15)
 * ============================================================ */
unsigned __far __pascal CreateBitmap(int w, int h, unsigned planes,
                                      unsigned bpp, const void __far *bits)
{
    unsigned handle = g_next_fake_hbm++;
    (void)w; (void)h; (void)planes; (void)bpp; (void)bits;
    serial_puts("GDI: CreateBitmap -> ");
    serial_puthex16(handle);
    serial_putc('\n');
    return handle;
}

unsigned __far __pascal CreateCompatibleBitmap(HDC hdc, int w, int h)
{
    unsigned handle = g_next_fake_hbm++;
    (void)hdc; (void)w; (void)h;
    serial_puts("GDI: CreateCompatBitmap -> ");
    serial_puthex16(handle);
    serial_putc('\n');
    return handle;
}

BOOL __far __pascal DeleteObject(unsigned hObject)
{
    (void)hObject;
    return 1;
}

/* ============================================================
 * ordinal 45: SelectObject
 *
 * Jesli hObject to HBITMAP (1..MAX_BITMAPS): zapamietaj w g_dc_bitmap[hdc].
 * Zwraca poprzedni obiekt (lub 0 jesli brak).
 * Stock objects (0x8000|n) sa zapamietywane w osobnej tabeli TODO, na razie ignorowane.
 * ============================================================ */
unsigned __far __pascal SelectObject(HDC hdc, unsigned hObject)
{
    unsigned prev = 0;
    if (hdc >= 1 && hdc < 16) {
        prev = g_dc_bitmap[hdc];
        /* Akceptuj: prawdziwe sprite'y (1..86), fake bitmapy (87..),
           ale NIE stock objects (0x8000+) - te sa zwracane przez GetStockObject */
        if (hObject >= 1 && hObject < 0x8000u)
            g_dc_bitmap[hdc] = hObject;
    }
    return prev;
}

/* ============================================================
 * ordinal 82: GetObject
 *
 * Dla HBITMAP (1..MAX_BITMAPS): wypelnia struct BITMAP z BITMAPINFOHEADER.
 * cbBuffer musi byc >= 14 (sizeof BITMAP).
 * Zwraca liczbe bajtow zapisanych lub 0 przy bledzie.
 * ============================================================ */
int __far __pascal GetObject(unsigned hObject, int cbBuffer, void __far *lpObject)
{
    unsigned char __far *bmp_buf;
    unsigned short bmp_off;
    BITMAPINFOHEADER __far *bi;
    BITMAP __far *bm;

    if (hObject < 1 || hObject > MAX_BITMAPS || cbBuffer < 14 || !lpObject)
        return 0;

    bmp_buf = (unsigned char __far *)MK_FP(SEL_BITMAPS, 0);
    bmp_off  = (unsigned short)(*(unsigned short __far *)(bmp_buf + 4 + (hObject - 1) * 2));
    if (bmp_off == 0) return 0;

    bi = (BITMAPINFOHEADER __far *)(bmp_buf + bmp_off);
    bm = (BITMAP __far *)lpObject;

    bm->bmType       = 0;
    bm->bmWidth      = (short)bi->biWidth;
    bm->bmHeight     = (short)(bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    bm->bmWidthBytes = (short)(((unsigned)bm->bmWidth * (unsigned)bi->biBitCount + 31u) / 32u * 4u);
    bm->bmPlanes     = 1;
    bm->bmBitsPixel  = (unsigned char)bi->biBitCount;
    bm->bmBits       = 0;

    return 14;
}

/* ============================================================
 * ordinal 80: GetDeviceCaps - parametry ekranu 640x480x24
 * ============================================================ */
int __far __pascal GetDeviceCaps(HDC hdc, int nIndex)
{
    int result;
    (void)hdc;
    switch (nIndex) {
        case  8:  result = 640;    break;  /* HORZRES */
        case 10:  result = 640;    break;  /* VERTRES = HORZRES: SKI.EXE uzywa min(H,V) jako rozmiar okna */
        case 12:  result = 8;      break;  /* BITSPIXEL: 8bpp paletted */
        case 14:  result = 1;      break;  /* PLANES */
        case 24:  result = 256;    break;  /* NUMCOLORS */
        case 38:  result = 0x0100; break;  /* RASTERCAPS: RC_PALETTE */
        case 88:  result = 96;     break;  /* LOGPIXELSX */
        case 90:  result = 96;     break;  /* LOGPIXELSY */
        default:  result = 0;      break;
    }
    return result;
}

/* ============================================================
 * ordinal 87: GetStockObject
 * Zwraca stale handlery (0x8000 | nObject)
 * ============================================================ */
unsigned __far __pascal GetStockObject(int nObject)
{
    return (unsigned)(0x8000 | (unsigned)nObject);
}

/* ============================================================
 * ordinal 91: GetTextExtent
 * ordinal 93: GetTextMetrics
 * ============================================================ */
unsigned long __far __pascal GetTextExtent(HDC hdc, const char __far *s, int len)
{
    (void)hdc; (void)s;
    return ((unsigned long)FONT_H << 16) | (unsigned long)((unsigned)len * FONT_W);
}

typedef struct {
    int  tmHeight;
    int  tmAscent;
    int  tmDescent;
    int  tmInternalLeading;
    int  tmExternalLeading;
    int  tmAveCharWidth;
    int  tmMaxCharWidth;
    int  tmWeight;
    unsigned char tmItalic, tmUnderlined, tmStruckOut;
    unsigned char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    unsigned char tmPitchAndFamily, tmCharSet;
    int  tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
} TEXTMETRIC;

BOOL __far __pascal GetTextMetrics(HDC hdc, TEXTMETRIC __far *lptm)
{
    (void)hdc;
    lptm->tmHeight          = FONT_H;
    lptm->tmAscent          = FONT_H - 2;
    lptm->tmDescent         = 2;
    lptm->tmInternalLeading = 0;
    lptm->tmExternalLeading = 0;
    lptm->tmAveCharWidth    = FONT_W;
    lptm->tmMaxCharWidth    = FONT_W;
    lptm->tmWeight          = 400;
    lptm->tmItalic          = 0;
    lptm->tmUnderlined      = 0;
    lptm->tmStruckOut       = 0;
    lptm->tmFirstChar       = 0x20;
    lptm->tmLastChar        = 0xFF;
    lptm->tmDefaultChar     = '?';
    lptm->tmBreakChar       = ' ';
    lptm->tmPitchAndFamily  = 0x01;
    lptm->tmCharSet         = 0;
    lptm->tmOverhang        = 0;
    lptm->tmDigitizedAspectX = 96;
    lptm->tmDigitizedAspectY = 96;
    return 1;
}

/* ============================================================
 * ordinal 200: LibMain
 * ============================================================ */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}
