/*
 * gdi.c - GDI.EXE (STEP15)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1:
 *   29  = PatBlt             (WHITENESS/BLACKNESS -> wypelnienie)
 *   33  = TextOut            (rysuje 8x16 na LFB przez huge selectors)
 *   34  = BitBlt             (4bpp sprite z SEL_BITMAPS -> LFB, przezroczystosc)
 *   45  = SelectObject       (zapamietuje HBITMAP w g_dc_bitmap[hdc])
 *   48  = CreateBitmap       (stub)
 *   51  = CreateCompatibleBitmap (fake handle)
 *   52  = CreateCompatibleDC     (alokuje bufor pikseli 128x128x4 z XMS)
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
 *
 * ETAP 15: pixel bufory BGRA per memDC (640x480x4)
 *   CreateCompatibleDC alokuje bufor 640x480x4 (BGRA) z XMS (19 okien GDT).
 *   Przypadki BitBlt: 1 (sprite->memDC), 2B (memDC->memDC), A (screen->memDC),
 *   B (SRCAND/SRCPAINT), C (memDC->screen z auto-clear alpha).
 *   Atlas tracking calkowicie usuniety (ETAP 15f): Windows 3.1 mial prawdziwe bufory.
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

static void serial_puthex8(unsigned char v)
{
    serial_putc("0123456789ABCDEF"[v >> 4]);
    serial_putc("0123456789ABCDEF"[v & 0xF]);
}

static void serial_putdec(int v)
{
    static char buf[8]; int i = 6;
    buf[7] = 0;
    if (v < 0) { serial_putc('-'); v = -v; }
    if (v == 0) { serial_putc('0'); return; }
    buf[i] = 0;
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    serial_puts(buf + i);
}

/* Trace GDI: ustawic TC_MAX>0 zeby wlaczyc (domyslnie wylaczone) */
static unsigned g_tc = 0;
#define TC_MAX 0

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
#define KCB_WND_W_OFF   240   /* short wnd_w[8]:  szerokosc child window (0=root) */
#define KCB_WND_H_OFF   256   /* short wnd_h[8]:  wysokosc child window (0=root) */
#define KCB_MAX_HWNDS   8

/* ETAP 15: pixel bufory memDC z dynamicznymi wymiarami (z CreateCompatibleBitmap) */
#define SEL_GDT_ACCESS  0x120   /* samoodniesienie GDT */
#define GDYN_MAX_SEL    0x528   /* max selektor dynamiczny */


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

/* Selektor GDT bufora pikseli per DC (0 = brak bufora) */
static unsigned g_dc_buf_sel[16] = {0};
/* Wymiary DC (z wybranej bitmapy przez SelectObject) */
static int g_dc_buf_w[16] = {0};
static int g_dc_buf_h[16] = {0};
/* 1 gdy bufor zawiera piksele ze screen (po screen->memDC BitBlt) */
static unsigned char g_dc_has_bg[16] = {0};

/* FAKE_HBM_BASE: fake handles z CreateCompatibleBitmap */
#define FAKE_HBM_BASE  (MAX_BITMAPS + 1)   /* 87 */
#define FAKE_HBM_MAX   16
static unsigned g_next_fake_hbm = FAKE_HBM_BASE;
/* Per-HBM dane dla fake compatible bitmap (indeks = hbm - FAKE_HBM_BASE) */
static unsigned g_hbm_buf_sel[FAKE_HBM_MAX] = {0};
static int      g_hbm_w[FAKE_HBM_MAX]       = {0};
static int      g_hbm_h[FAKE_HBM_MAX]       = {0};

/* ============================================================
 * mini_alloc - alokuje selektor GDT z XMS heapu (KCB bezposrednio)
 * Bezpieczne: KCB __far * lokalny -> Watcom uzywa ES, nie DS.
 * ============================================================ */
#pragma pack(push,1)
typedef struct {
    unsigned short app_hinstance;  /* 0 */
    unsigned short next_dyn_sel;   /* 2 */
    unsigned long  heap_phys;      /* 4 */
    unsigned long  heap_next;      /* 8 */
    unsigned long  heap_end;       /* 12 */
} KCB_MINI;
#pragma pack(pop)

static unsigned mini_alloc(unsigned long bytes)
{
    KCB_MINI __far *kcb = (KCB_MINI __far *)MK_FP(SEL_KCB, 0);
    unsigned long base, rounded;
    unsigned short sel, first_sel;
    unsigned short n_win;
    unsigned char __far *gdt;
    unsigned i;

    rounded = (bytes + 15UL) & ~15UL;
    /* Liczba okien 64KB potrzebnych dla bufora */
    n_win = (rounded <= 65535UL) ? 1u
          : (unsigned short)((rounded + 65535UL) / 65536UL);

    /* Odczyt PRZED zapisem (DS moze byc clobbered przez zapis far) */
    base = kcb->heap_next;
    sel  = kcb->next_dyn_sel;
    if (base + rounded > kcb->heap_end) return 0;
    if ((unsigned long)sel + (unsigned long)n_win * 8u > GDYN_MAX_SEL) return 0;

    kcb->heap_next    = base + rounded;
    kcb->next_dyn_sel = sel + n_win * 8u;
    first_sel = sel;

    /* Alokuj n_win kolejnych deskryptorow GDT - kazdy 64KB okno fizyczne */
    for (i = 0; i < n_win; i++) {
        unsigned long win_base  = base + (unsigned long)i * 65536UL;
        unsigned long remaining = rounded - (unsigned long)i * 65536UL;
        unsigned short limit = (remaining >= 65536UL) ? 0xFFFFu
                                                      : (unsigned short)(remaining - 1u);
        gdt = (unsigned char __far *)MK_FP(SEL_GDT_ACCESS, sel);
        gdt[0] = (unsigned char)(limit & 0xFF);
        gdt[1] = (unsigned char)(limit >> 8);
        gdt[2] = (unsigned char)(win_base         & 0xFF);
        gdt[3] = (unsigned char)((win_base >>  8) & 0xFF);
        gdt[4] = (unsigned char)((win_base >> 16) & 0xFF);
        gdt[5] = 0x92;
        gdt[6] = 0x00;
        gdt[7] = (unsigned char)((win_base >> 24) & 0xFF);
        sel += 8;
    }
    return first_sel;
}

/* ============================================================
 * decode_4bpp_pixel - dekoduje piksel 4bpp z SEL_BITMAPS
 *   Zwraca 0x01000000 jezeli przezroczysty.
 *   Inaczej: bity 16-23=R, 8-15=G, 0-7=B (zapis do rejestrow, bezpieczne DS!=SS)
 * ============================================================ */
#define DECODE_TRANSP 0x01000000UL

static unsigned long decode_4bpp_pixel(unsigned hbm, int x, int y)
{
    unsigned char __far *bmp_buf;
    unsigned short bmp_off, row_bytes, dib_row, row_off;
    BITMAPINFOHEADER __far *bi;
    short bw, bh;
    unsigned char byte_val, idx;
    unsigned char __far *rgb;

    if (hbm < 1 || hbm > MAX_BITMAPS) return DECODE_TRANSP;
    bmp_buf = (unsigned char __far *)MK_FP(SEL_BITMAPS, 0);
    bmp_off = *(unsigned short __far *)(bmp_buf + 4 + (hbm - 1) * 2);
    if (bmp_off == 0) return DECODE_TRANSP;
    bi  = (BITMAPINFOHEADER __far *)(bmp_buf + bmp_off);
    bw  = (short)bi->biWidth;
    bh  = (short)(bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    if (x < 0 || x >= (int)bw || y < 0 || y >= (int)bh) return DECODE_TRANSP;
    row_bytes = (unsigned short)(((unsigned)bw * 4u + 31u) / 32u * 4u);
    dib_row   = (unsigned short)((int)bh - 1 - y);
    row_off   = (unsigned short)(bmp_off + 40u + 64u +
                                 (unsigned long)dib_row * row_bytes);
    byte_val  = *(bmp_buf + row_off + (unsigned)(x >> 1));
    idx       = (x & 1) ? (byte_val & 0x0F) : (byte_val >> 4);
    if (idx == TRANSP_IDX) return DECODE_TRANSP;
    rgb = bmp_buf + bmp_off + 40u + (unsigned)idx * 4u;
    return ((unsigned long)rgb[2] << 16) | ((unsigned long)rgb[1] << 8) | rgb[0];
    /* R<<16 | G<<8 | B */
}

/* ============================================================
 * draw_pixel / read_pixel
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

/* Zapis/odczyt pikselu do/z bufora memDC (wielookienkowy, jak VESA) */
/* Format bufora: B,G,R,A (4 bajty/piksel). A=0: przezroczysty, A=1: nieprzezroczysty.
 * Zero-fill w CreateCompatibleDC zeruje A -> caly bufor przezroczysty na starcie. */
static void draw_dc_pixel(unsigned buf_sel, unsigned long flat_off,
                           unsigned char r, unsigned char g, unsigned char b)
{
    unsigned win = (unsigned)(flat_off >> 16);
    unsigned off = (unsigned)(flat_off & 0xFFFFUL);
    unsigned char __far *p = (unsigned char __far *)MK_FP(buf_sel + win * 8u, off);
    p[0] = b; p[1] = g; p[2] = r; p[3] = 1;
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
    { static unsigned char s_to = 0; if (!s_to) { serial_puts("TO!\n"); s_to=1; } }
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
                      0x00, 0x00, 0x00);  /* czarny tekst */
    return 1;
}

/* ============================================================
 * blit_sprite_hbm - renderuje sprite HBITMAP (1..86) na LFB
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

/* Zeruje bufor XMS o rozmiarze bytes (sel = pierwszy selektor z mini_alloc) */
static void zero_buf(unsigned sel, unsigned long bytes)
{
    unsigned n_win = (bytes <= 65536UL) ? 1u : (unsigned)((bytes + 65535UL) / 65536UL);
    unsigned ww;
    for (ww = 0; ww < n_win; ww++) {
        unsigned char __far *wbuf = (unsigned char __far *)MK_FP(sel + ww * 8u, 0);
        unsigned long win_start = (unsigned long)ww * 65536UL;
        unsigned long win_end   = win_start + 65536UL;
        unsigned long win_bytes;
        unsigned j;
        if (win_end > bytes) win_end = bytes;
        win_bytes = win_end - win_start;
        if (win_bytes == 65536UL) {
            for (j = 0; j < 0x8000u; j++) { wbuf[j] = 0; wbuf[j + 0x8000u] = 0; }
        } else {
            unsigned sz = (unsigned)win_bytes;
            for (j = 0; j < sz; j++) wbuf[j] = 0;
        }
    }
}

/* ============================================================
 * ordinal 34: BitBlt
 *
 * Przypadek 1:  sprite (1..86) -> memDC, SRCCOPY
 *   -> rysuje piksele sprite do bufora BGRA hdcDst (alpha=1 dla nieprzezroczystych)
 * Przypadek 2B: memDC -> memDC, SRCCOPY
 *   -> kopiuje piksele alpha=1 z bufora hdcSrc do bufora hdcDst
 * Przypadek A:  screen(1) -> memDC, SRCCOPY: zapisuje LFB do bufora
 * Przypadek B:  SRCAND/SRCPAINT: compositing z realnego sprite lub bufora memDC
 * Przypadek C:  memDC -> screen(1), SRCCOPY: blit alpha=1 z bufora na LFB
 * Przypadek 4:  sprite (1..86) -> ekran bezposrednio -> blit_sprite_hbm
 * ============================================================ */
BOOL __far __pascal BitBlt(HDC hdcDst, int xDst, int yDst, int w, int h,
                            HDC hdcSrc, int xSrc, int ySrc, unsigned long dwRop)
{
    unsigned src_hbm;

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

    /* Trace: log BitBlt call */
    if (g_tc < TC_MAX) {
        unsigned char rop_hi = (unsigned char)(dwRop >> 16);
        g_tc++;
        serial_puts("BB d="); serial_puthex16(hdcDst);
        serial_puts(" s="); serial_puthex16(hdcSrc);
        serial_puts(" R="); serial_puthex8(rop_hi);
        serial_puts(" sh="); serial_puthex16(src_hbm);
        serial_puts(" "); serial_putdec(w); serial_putc('x'); serial_putdec(h);
        serial_puts(" D="); serial_putdec(xDst); serial_putc(','); serial_putdec(yDst);
        serial_puts(" S="); serial_putdec(xSrc); serial_putc(','); serial_putdec(ySrc);
        serial_putc('\n');
    }

    /* ---- Przypadek 1: sprite (1..86) -> memDC, SRCCOPY: zapisz do bufora ---- */
    if (src_hbm >= 1 && src_hbm <= MAX_BITMAPS &&
        hdcDst >= 2 && hdcDst < 16 &&
        dwRop == 0x00CC0020UL) {
        unsigned buf_sel1 = g_dc_buf_sel[hdcDst];
        if (buf_sel1 != 0) {
            int row1, col1;
            int dc_w1     = g_dc_buf_w[hdcDst];
            int dc_h1     = g_dc_buf_h[hdcDst];
            unsigned long dc_pitch1 = (unsigned long)dc_w1 * 4UL;
            for (row1 = 0; row1 < h; row1++) {
                int by1 = yDst + row1;
                if (by1 < 0 || by1 >= dc_h1) continue;
                for (col1 = 0; col1 < w; col1++) {
                    int bx1 = xDst + col1;
                    unsigned long pix1;
                    unsigned long bo1;
                    if (bx1 < 0 || bx1 >= dc_w1) continue;
                    pix1 = decode_4bpp_pixel(src_hbm, xSrc+col1, ySrc+row1);
                    if (!(pix1 & DECODE_TRANSP)) {
                        bo1 = (unsigned long)by1 * dc_pitch1
                            + (unsigned long)bx1 * 4u;
                        draw_dc_pixel(buf_sel1, bo1,
                            (unsigned char)(pix1 >> 16),
                            (unsigned char)(pix1 >> 8),
                            (unsigned char)pix1);
                    }
                }
            }
            g_dc_has_bg[hdcDst] = 1;
        }
        return 1;
    }

    /* ---- Przypadek 2B: memDC -> memDC, SRCCOPY: kopiuj bufor pikselowy ---- */
    if (hdcDst >= 2 && hdcDst < 16 &&
        hdcSrc >= 2 && hdcSrc < 16 &&
        dwRop == 0x00CC0020UL) {
        unsigned src_buf = g_dc_buf_sel[hdcSrc];
        unsigned dst_buf = g_dc_buf_sel[hdcDst];
        if (src_buf != 0 && dst_buf != 0) {
            int row2, col2;
            int src_w = g_dc_buf_w[hdcSrc], src_h = g_dc_buf_h[hdcSrc];
            int dst_w = g_dc_buf_w[hdcDst], dst_h = g_dc_buf_h[hdcDst];
            unsigned long src_pitch = (unsigned long)src_w * 4UL;
            unsigned long dst_pitch = (unsigned long)dst_w * 4UL;
            for (row2 = 0; row2 < h; row2++) {
                int sy2 = ySrc + row2;
                int dy2 = yDst + row2;
                if (sy2 < 0 || sy2 >= src_h) continue;
                if (dy2 < 0 || dy2 >= dst_h) continue;
                for (col2 = 0; col2 < w; col2++) {
                    int sx2 = xSrc + col2;
                    int dx2 = xDst + col2;
                    unsigned long src_off;
                    unsigned char __far *sp;
                    if (sx2 < 0 || sx2 >= src_w) continue;
                    if (dx2 < 0 || dx2 >= dst_w) continue;
                    src_off = (unsigned long)sy2 * src_pitch + (unsigned long)sx2 * 4u;
                    sp = (unsigned char __far *)MK_FP(
                            src_buf + (unsigned)(src_off >> 16) * 8u,
                            (unsigned)(src_off & 0xFFFFUL));
                    if (sp[3]) {
                        unsigned long dst_off = (unsigned long)dy2 * dst_pitch
                                              + (unsigned long)dx2 * 4u;
                        draw_dc_pixel(dst_buf, dst_off, sp[2], sp[1], sp[0]);
                    }
                }
            }
            g_dc_has_bg[hdcDst] = 1;
        }
        return 1;
    }

    /* ======================================================
     * ETAP 15: pixel-buffer cases
     * ====================================================== */

    /* ---- Przypadek A (15d): screen(1) -> memDC, SRCCOPY: zapisz tlo ---- */
    if (hdcSrc == 1 && hdcDst >= 2 && hdcDst < 16 && dwRop == 0x00CC0020UL) {
        unsigned buf_sel = g_dc_buf_sel[hdcDst];
        if (buf_sel != 0) {
            int row, col;
            int dc_wA    = g_dc_buf_w[hdcDst];
            int dc_hA    = g_dc_buf_h[hdcDst];
            unsigned long dc_pitchA = (unsigned long)dc_wA * 4UL;
            for (row = 0; row < h; row++) {
                int sy = ySrc + row;
                int by = yDst + row;
                if (sy < 0 || sy >= 480) continue;
                if (by < 0 || by >= dc_hA) continue;
                for (col = 0; col < w; col++) {
                    int sx = xSrc + col;
                    int bx = xDst + col;
                    unsigned char r, g, bv;
                    unsigned long buf_off;
                    if (sx < 0 || sx >= 640) continue;
                    if (bx < 0 || bx >= dc_wA) continue;
                    buf_off = (unsigned long)by * dc_pitchA
                            + (unsigned long)bx * 4u;
                    read_pixel((unsigned long)sy * VESA_PITCH +
                               (unsigned long)sx * VESA_BPP, &r, &g, &bv);
                    draw_dc_pixel(buf_sel, buf_off, r, g, bv);
                }
            }
            g_dc_has_bg[hdcDst] = 1;
        }
        return 1;
    }

    /* ---- Przypadek B: ANY -> memDC, SRCAND lub SRCPAINT ---- */
    /* Zrodlo: realny sprite (1..86) LUB memDC z buforem pikselowym. */
    if (hdcDst >= 2 && hdcDst < 16 &&
        (dwRop == 0x008800C6UL || dwRop == 0x00EE0086UL)) {
        unsigned dst_buf = g_dc_buf_sel[hdcDst];
        if (dst_buf != 0) {
            int dst_w = g_dc_buf_w[hdcDst];
            int dst_h = g_dc_buf_h[hdcDst];
            unsigned long dst_pitch = (unsigned long)dst_w * 4UL;
            if (src_hbm >= 1 && src_hbm <= MAX_BITMAPS) {
                /* Zrodlo: realny sprite (1..86) */
                int row, col;
                if (dwRop == 0x00EE0086UL) {  /* SRCPAINT: zapisz kolor */
                    for (row = 0; row < h; row++) {
                        int by = yDst + row;
                        if (by < 0 || by >= dst_h) continue;
                        for (col = 0; col < w; col++) {
                            int bx = xDst + col;
                            unsigned long pix, buf_off;
                            if (bx < 0 || bx >= dst_w) continue;
                            buf_off = (unsigned long)by * dst_pitch
                                    + (unsigned long)bx * 4u;
                            pix = decode_4bpp_pixel(src_hbm, xSrc + col, ySrc + row);
                            if (!(pix & DECODE_TRANSP))
                                draw_dc_pixel(dst_buf, buf_off,
                                    (unsigned char)(pix >> 16),
                                    (unsigned char)(pix >> 8),
                                    (unsigned char)pix);
                        }
                    }
                    g_dc_has_bg[hdcDst] = 1;
                }
                if (dwRop == 0x008800C6UL) {  /* SRCAND: zeruj opaque piksele */
                    for (row = 0; row < h; row++) {
                        int by = yDst + row;
                        if (by < 0 || by >= dst_h) continue;
                        for (col = 0; col < w; col++) {
                            int bx = xDst + col;
                            unsigned long pix, buf_off;
                            if (bx < 0 || bx >= dst_w) continue;
                            buf_off = (unsigned long)by * dst_pitch
                                    + (unsigned long)bx * 4u;
                            pix = decode_4bpp_pixel(src_hbm, xSrc + col, ySrc + row);
                            if (!(pix & DECODE_TRANSP))
                                draw_dc_pixel(dst_buf, buf_off, 0, 0, 0);
                        }
                    }
                }
            } else if (hdcSrc >= 2 && hdcSrc < 16 && dwRop == 0x00EE0086UL) {
                /* Zrodlo: memDC z buforem pikselowym, tylko SRCPAINT. */
                unsigned src_buf = g_dc_buf_sel[hdcSrc];
                if (src_buf != 0) {
                    int row, col;
                    int src_w = g_dc_buf_w[hdcSrc];
                    int src_h = g_dc_buf_h[hdcSrc];
                    unsigned long src_pitch = (unsigned long)src_w * 4UL;
                    for (row = 0; row < h; row++) {
                        int sy = ySrc + row;
                        int dy = yDst + row;
                        if (sy < 0 || sy >= src_h) continue;
                        if (dy < 0 || dy >= dst_h) continue;
                        for (col = 0; col < w; col++) {
                            int sx = xSrc + col;
                            int dx = xDst + col;
                            unsigned long src_off, dst_off;
                            unsigned char __far *sp;
                            if (sx < 0 || sx >= src_w) continue;
                            if (dx < 0 || dx >= dst_w) continue;
                            src_off = (unsigned long)sy * src_pitch
                                    + (unsigned long)sx * 4u;
                            sp = (unsigned char __far *)MK_FP(
                                    src_buf + (unsigned)(src_off >> 16) * 8u,
                                    (unsigned)(src_off & 0xFFFFUL));
                            if (sp[3]) {
                                dst_off = (unsigned long)dy * dst_pitch
                                        + (unsigned long)dx * 4u;
                                draw_dc_pixel(dst_buf, dst_off, sp[2], sp[1], sp[0]);
                            }
                        }
                    }
                    g_dc_has_bg[hdcDst] = 1;
                }
            }
        }
        return 1;
    }

    /* ---- Przypadek C (15c): memDC -> screen(1), SRCCOPY: blit z bufora ----
     * Piksele z alpha=1 kopiowane na ekran; alpha zerowane po odczycie (auto-clear).
     * Przezroczyste piksele bufora (alpha=0) nie nadpisuja tla ekranu. */
    if (hdcDst == 1 && hdcSrc >= 2 && hdcSrc < 16 && dwRop == 0x00CC0020UL) {
        unsigned buf_sel = g_dc_buf_sel[hdcSrc];
        if (buf_sel != 0 && g_dc_has_bg[hdcSrc]) {
            int row, col;
            int dc_wC    = g_dc_buf_w[hdcSrc];
            int dc_hC    = g_dc_buf_h[hdcSrc];
            unsigned long dc_pitchC = (unsigned long)dc_wC * 4UL;
            /* Precompute child window exclusion rect (raz przed petla, nie per piksel). */
            int excl_x1 = -1, excl_x2 = -1, excl_y1 = 0, excl_y2 = 0;
            {
                int ci;
                for (ci = 0; ci < KCB_MAX_HWNDS; ci++) {
                    short cx = *(short __far *)MK_FP(SEL_KCB, KCB_WND_OX_OFF + (unsigned)ci*2u);
                    short cw = *(short __far *)MK_FP(SEL_KCB, KCB_WND_W_OFF  + (unsigned)ci*2u);
                    short cy = *(short __far *)MK_FP(SEL_KCB, KCB_WND_OY_OFF + (unsigned)ci*2u);
                    short ch = *(short __far *)MK_FP(SEL_KCB, KCB_WND_H_OFF  + (unsigned)ci*2u);
                    if (cw > 0 && ch > 0) {
                        excl_x1 = (int)cx; excl_x2 = (int)cx + (int)cw;
                        excl_y1 = (int)cy; excl_y2 = (int)cy + (int)ch;
                        break;
                    }
                }
            }
            for (row = 0; row < h; row++) {
                int sy = ySrc + row;
                int dy = yDst + row;
                if (sy < 0 || sy >= dc_hC) continue;
                if (dy < 0 || dy >= 480) continue;
                for (col = 0; col < w; col++) {
                    int sx = xSrc + col;
                    int dx = xDst + col;
                    unsigned long buf_off;
                    unsigned char __far *bp;
                    if (sx < 0 || sx >= dc_wC) continue;
                    if (dx < 0 || dx >= 640) continue;
                    if (excl_x1 >= 0 &&
                        dx >= excl_x1 && dx < excl_x2 &&
                        dy >= excl_y1 && dy < excl_y2) continue;
                    buf_off = (unsigned long)sy * dc_pitchC + (unsigned long)sx * 4u;
                    bp = (unsigned char __far *)MK_FP(
                            buf_sel + (unsigned)(buf_off >> 16) * 8u,
                            (unsigned)(buf_off & 0xFFFFUL));
                    if (bp[3]) {
                        draw_pixel((unsigned long)dy * VESA_PITCH +
                                   (unsigned long)dx * VESA_BPP,
                                   bp[2], bp[1], bp[0]);
                        bp[3] = 0;
                    }
                }
            }
            g_dc_has_bg[hdcSrc] = 0;
        }
        return 1;
    }

    /* ---- Przypadek 4: sprite (1..86) -> ekran bezposrednio ---- */
    if (src_hbm >= 1 && src_hbm <= MAX_BITMAPS && hdcDst == 1) {
        blit_sprite_hbm(src_hbm, xDst, yDst, w, h, xSrc, ySrc);
        return 1;
    }

    return 1;
}

/* ============================================================
 * clip_screen_x: klipuje zakres x/w omijajac child windows na ekranie (DC=1).
 * Klipuje tylko jezeli wiersz (y,h) zachodzi na child window (sprawdza takze Y).
 * Obsluguje tylko przypadek child window po prawej stronie rysowania.
 * ============================================================ */
static void clip_screen_x(int *px, int *pw, int y, int h)
{
    int i;
    for (i = 0; i < KCB_MAX_HWNDS; i++) {
        short cx = *(short __far *)MK_FP(SEL_KCB, KCB_WND_OX_OFF + (unsigned)i * 2u);
        short cw = *(short __far *)MK_FP(SEL_KCB, KCB_WND_W_OFF  + (unsigned)i * 2u);
        short cy = *(short __far *)MK_FP(SEL_KCB, KCB_WND_OY_OFF + (unsigned)i * 2u);
        short ch = *(short __far *)MK_FP(SEL_KCB, KCB_WND_H_OFF  + (unsigned)i * 2u);
        if (cw <= 0 || ch <= 0) continue;  /* slot pusty */
        /* Klipuj x tylko jezeli prostokat y zachodzi na child window */
        if (y + h <= (int)cy || y >= (int)cy + (int)ch) continue;
        /* child window zajmuje x w zakresie [cx, cx+cw) */
        if (*px < (int)cx && *px + *pw > (int)cx)
            *pw = (int)cx - *px;  /* obetnij prawy bok */
    }
}

/* ============================================================
 * ordinal 29: PatBlt - wypelnienie prostokata
 * ============================================================ */
BOOL __far __pascal PatBlt(HDC hdc, int x, int y, int w, int h,
                            unsigned long dwRop)
{
    unsigned char rv, gv, bv;
    int row, col;
    if (g_tc < TC_MAX) {
        g_tc++;
        serial_puts("PB d="); serial_puthex16(hdc);
        serial_puts(" R="); serial_puthex8((unsigned char)(dwRop >> 16));
        serial_puts(" "); serial_putdec(w); serial_putc('x'); serial_putdec(h);
        serial_puts(" @"); serial_putdec(x); serial_putc(','); serial_putdec(y);
        serial_putc('\n');
    }
    if (dwRop == 0x00FF0062UL) { rv = 0xFF; gv = 0xFF; bv = 0xFF; }  /* WHITENESS */
    else if (dwRop == 0x00000042UL) { rv = 0; gv = 0; bv = 0; }      /* BLACKNESS */
    else return 1;

    /* memDC (2..15): wypelnij bufor pikseli kolorem pendzla */
    if (hdc >= 2 && hdc < 16) {
        unsigned buf_sel_p = g_dc_buf_sel[hdc];
        if (buf_sel_p) {
            int dc_wp = g_dc_buf_w[hdc];
            int dc_hp = g_dc_buf_h[hdc];
            unsigned long dc_pitchp = (unsigned long)dc_wp * 4UL;
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > dc_wp) w = dc_wp - x;
            if (y + h > dc_hp) h = dc_hp - y;
            if (w > 0 && h > 0) {
                for (row = 0; row < h; row++)
                    for (col = 0; col < w; col++) {
                        unsigned long bo = (unsigned long)(y+row) * dc_pitchp
                                         + (unsigned long)(x+col) * 4u;
                        draw_dc_pixel(buf_sel_p, bo, rv, gv, bv);
                    }
                g_dc_has_bg[hdc] = 1;
            }
        }
        return 1;
    }

    /* screen DC (1), HWND DC (bit14=1), window DC (bit15=1) */
    if (hdc != 1 && !((unsigned)hdc & 0xC000u)) return 1;

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

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 640) w = 640 - x;
    if (y + h > 480) h = 480 - y;
    /* Klipuj wokol child windows (DC=1 rysuje caly ekran bez klipowania okien) */
    if ((unsigned)hdc == 1u) clip_screen_x(&x, &w, y, h);
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
 * Tworzy pusty DC bez bufora. Bufor przydzielany przez SelectObject
 * gdy wybrana zostanie bitmapa z CreateCompatibleBitmap.
 * ============================================================ */
static HDC g_next_hdc = 2;
HDC __far __pascal CreateCompatibleDC(HDC hdc)
{
    HDC result = g_next_hdc++;
    (void)hdc;
    if (result < 16) {
        g_dc_bitmap[result] = 0xFFFFu;
        g_dc_buf_sel[result] = 0;
        g_dc_buf_w[result]   = 0;
        g_dc_buf_h[result]   = 0;
        g_dc_has_bg[result]  = 0;
        serial_puts("GDI: CreateCompatDC DC=");
        serial_puthex16(result);
        serial_putc('\n');
    }
    return result;
}

int __far __pascal DeleteDC(HDC hdc)
{
    if (hdc >= 2 && hdc < 16)
        g_dc_has_bg[hdc] = 0;
    return 1;
}

/* ============================================================
 * Operacje na bitmapach i obiektach GDI
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
    unsigned handle = g_next_fake_hbm;
    unsigned idx    = handle - FAKE_HBM_BASE;
    unsigned sel    = 0;
    (void)hdc;
    if (idx < FAKE_HBM_MAX && w > 0 && h > 0) {
        unsigned long bytes = (unsigned long)w * (unsigned long)h * 4UL;
        sel = mini_alloc(bytes);
        if (sel) {
            zero_buf(sel, bytes);
            g_hbm_buf_sel[idx] = sel;
            g_hbm_w[idx]       = w;
            g_hbm_h[idx]       = h;
            g_next_fake_hbm++;
        }
    }
    serial_puts("GDI: CreateCompatBitmap ");
    serial_putdec(w); serial_putc('x'); serial_putdec(h);
    serial_puts(" -> "); serial_puthex16(handle);
    if (!sel) serial_puts(" FAIL");
    serial_putc('\n');
    return sel ? handle : 0;
}

BOOL __far __pascal DeleteObject(unsigned hObject)
{
    (void)hObject;
    return 1;
}

/* ============================================================
 * ordinal 45: SelectObject
 * ============================================================ */
unsigned __far __pascal SelectObject(HDC hdc, unsigned hObject)
{
    unsigned prev = 0;
    if (hdc >= 1 && hdc < 16) {
        prev = g_dc_bitmap[hdc];
        if (hObject >= 1 && hObject < 0x8000u) {
            g_dc_bitmap[hdc] = hObject;
            /* Jesli to fake compatible bitmap (z CreateCompatibleBitmap):
             * przypisz jego bufor i wymiary do DC */
            if (hObject >= FAKE_HBM_BASE) {
                unsigned idx = hObject - FAKE_HBM_BASE;
                if (idx < FAKE_HBM_MAX && g_hbm_buf_sel[idx] != 0) {
                    g_dc_buf_sel[hdc] = g_hbm_buf_sel[idx];
                    g_dc_buf_w[hdc]   = g_hbm_w[idx];
                    g_dc_buf_h[hdc]   = g_hbm_h[idx];
                }
            }
        }
    }
    if (g_tc < TC_MAX) {
        g_tc++;
        serial_puts("SO d="); serial_puthex16(hdc);
        serial_puts(" h="); serial_puthex16(hObject);
        serial_puts(" was="); serial_puthex16(prev);
        serial_putc('\n');
    }
    return prev;
}

/* ============================================================
 * ordinal 82: GetObject
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
    if (g_tc < TC_MAX) {
        g_tc++;
        serial_puts("GDC idx="); serial_putdec(nIndex);
        serial_putc('\n');
    }
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
