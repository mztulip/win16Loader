/*
 * gdi.c - GDI.EXE (STEP12/ETAP14)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1:
 *   29  = PatBlt             (WHITENESS/BLACKNESS -> wypelnienie)
 *   33  = TextOut            (rysuje 8x16 na LFB przez huge selectors)
 *   34  = BitBlt             (4bpp sprite z SEL_BITMAPS -> LFB, przezroczystosc)
 *   45  = SelectObject       (zapamietuje HBITMAP w g_dc_bitmap[hdc])
 *   48  = CreateBitmap       (stub)
 *   51  = CreateCompatibleBitmap (stub)
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
    (void)hdc;
    serial_puts("GDI: TextOut \"");
    for (i = 0; i < len; i++) serial_putc(s[i]);
    serial_puts("\"\n");
    for (i = 0; i < len; i++)
        draw_char_gdi((unsigned char)s[i],
                      (unsigned)(x + i * FONT_W), (unsigned)y,
                      0xFF, 0xFF, 0xFF);
    return 1;
}

/* ============================================================
 * ordinal 34: BitBlt - blit sprite 4bpp z SEL_BITMAPS na LFB
 *
 * hdcSrc musi miec wybrany HBITMAP (1..86) przez SelectObject.
 * Bitmap 4bpp: BITMAPINFOHEADER(40B) + 16xRGBQUAD(64B) + piksele bottom-up.
 * Przezroczystosc: paleta[TRANSP_IDX=15] = bialy tlo (pomijamy te piksele).
 * ============================================================ */
BOOL __far __pascal BitBlt(HDC hdcDst, int xDst, int yDst, int w, int h,
                            HDC hdcSrc, int xSrc, int ySrc, unsigned long dwRop)
{
    unsigned char __far *bmp_buf;
    unsigned short bmp_off;
    unsigned hbm;
    BITMAPINFOHEADER __far *bi;
    unsigned char __far *pal_b;  /* wskaznik na bajty palety (4 bajty na wpis) */
    short bw, bh;
    unsigned short row_bytes;
    int r, c;

    (void)dwRop;

    serial_puts("BitBlt dst="); serial_puthex16(hdcDst);
    serial_puts(" src="); serial_puthex16(hdcSrc);
    serial_puts(" hbm="); serial_puthex16(g_dc_bitmap[hdcSrc < 16 ? hdcSrc : 0]);
    serial_puts(" x="); serial_puthex16((unsigned short)xDst);
    serial_puts(" y="); serial_puthex16((unsigned short)yDst);
    serial_puts(" w="); serial_puthex16((unsigned short)w);
    serial_puts(" h="); serial_puthex16((unsigned short)h);
    serial_putc('\n');

    if (hdcSrc < 1 || hdcSrc >= 16) return 1;
    hbm = g_dc_bitmap[hdcSrc];
    if (hbm < 1 || hbm > MAX_BITMAPS) return 1;

    bmp_buf = (unsigned char __far *)MK_FP(SEL_BITMAPS, 0);
    bmp_off  = (unsigned short)(*(unsigned short __far *)(bmp_buf + 4 + (hbm - 1) * 2));
    if (bmp_off == 0) return 1;

    bi  = (BITMAPINFOHEADER __far *)(bmp_buf + bmp_off);
    pal_b = bmp_buf + bmp_off + 40;  /* 16 x RGBQUAD (4B kazdy) po BITMAPINFOHEADER */

    bw = (short)bi->biWidth;
    bh = (short)(bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    /* bytes per row 4bpp, DWORD-aligned: ((width * 4 + 31) / 32) * 4 */
    row_bytes = (unsigned short)(((unsigned)bw * 4u + 31u) / 32u * 4u);

    /* Ogranicz do zakresu zrodlowej bitmapy */
    if (xSrc < 0) { xDst -= xSrc; w += xSrc; xSrc = 0; }
    if (ySrc < 0) { yDst -= ySrc; h += ySrc; ySrc = 0; }
    if (xSrc + w > bw) w = (int)bw - xSrc;
    if (ySrc + h > bh) h = (int)bh - ySrc;
    if (w <= 0 || h <= 0) return 1;

    for (r = 0; r < h; r++) {
        /* DIB bottom-up: wiersz 0 = dol obrazu */
        unsigned short dib_row = (unsigned short)((int)bh - 1 - ySrc - r);
        /* Offset wiersza pikseli w buforze */
        unsigned short row_off = (unsigned short)(bmp_off + 40u + 64u +
                                  (unsigned long)dib_row * row_bytes);
        int dst_y = yDst + r;

        for (c = 0; c < w; c++) {
            int sx = xSrc + c;
            unsigned char byte_val = *(bmp_buf + row_off + (unsigned)(sx >> 1));
            unsigned char idx = (sx & 1) ? (byte_val & 0x0F) : (byte_val >> 4);
            unsigned char __far *rgb;
            unsigned long flat_off;

            if (idx == TRANSP_IDX) continue;  /* przezroczystosc */

            rgb = pal_b + (unsigned)idx * 4u;  /* RGBQUAD: Blue, Green, Red, Reserved */
            flat_off = (unsigned long)dst_y * VESA_PITCH +
                       (unsigned long)(xDst + c) * VESA_BPP;
            draw_pixel(flat_off, rgb[2], rgb[1], rgb[0]);  /* rgb[2]=R, rgb[1]=G, rgb[0]=B */
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
    (void)hdc;

    if (w <= 0 || h <= 0) return 1;
    if (dwRop == 0x00FF0062UL) { rv = 0xFF; gv = 0xFF; bv = 0xFF; }  /* WHITENESS */
    else if (dwRop == 0x00000042UL) { rv = 0; gv = 0; bv = 0; }      /* BLACKNESS */
    else return 1;

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
    (void)w; (void)h; (void)planes; (void)bpp; (void)bits;
    return 0;
}

unsigned __far __pascal CreateCompatibleBitmap(HDC hdc, int w, int h)
{
    (void)hdc; (void)w; (void)h;
    return 0;  /* nie uzywane przez SKI.EXE (uzywA tylko LoadBitmap + SelectObject) */
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
    serial_puts("GDI: SelectObject hdc=");
    serial_puthex16(hdc);
    serial_puts(" obj=");
    serial_puthex16(hObject);
    serial_putc('\n');
    if (hdc >= 1 && hdc < 16) {
        prev = g_dc_bitmap[hdc];
        if (hObject >= 1 && hObject <= MAX_BITMAPS)
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

    serial_puts("GDI: GetObject hbm=");
    serial_puthex16(hObject);
    serial_puts(" -> ");
    serial_puthex16((unsigned short)bm->bmWidth);
    serial_putc('x');
    serial_puthex16((unsigned short)bm->bmHeight);
    serial_putc('\n');
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
        case 10:  result = 480;    break;  /* VERTRES */
        case 12:  result = 8;      break;  /* BITSPIXEL: 8bpp paletted */
        case 14:  result = 1;      break;  /* PLANES */
        case 24:  result = 256;    break;  /* NUMCOLORS */
        case 38:  result = 0x0100; break;  /* RASTERCAPS: RC_PALETTE */
        case 88:  result = 96;     break;  /* LOGPIXELSX */
        case 90:  result = 96;     break;  /* LOGPIXELSY */
        default:  result = 0;      break;
    }
    serial_puts("GDI: GetDeviceCaps nIndex=");
    serial_puthex16((unsigned short)nIndex);
    serial_puts(" -> ");
    serial_puthex16((unsigned short)result);
    serial_putc('\n');
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
