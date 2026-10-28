/*
 * gdi.c - GDI.EXE (STEP11)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1:
 *   29  = PatBlt             (stub: ETAP 15)
 *   33  = TextOut            (rysuje 8x16 na LFB przez huge selectors)
 *   34  = BitBlt             (stub: ETAP 15)
 *   45  = SelectObject       (stub)
 *   48  = CreateBitmap       (stub)
 *   51  = CreateCompatibleBitmap (stub)
 *   52  = CreateCompatibleDC     (stub)
 *   68  = DeleteDC           (stub)
 *   69  = DeleteObject       (stub)
 *   80  = GetDeviceCaps      (640x480x24)
 *   82  = GetObject          (stub)
 *   87  = GetStockObject     (stale handlery)
 *   91  = GetTextExtent      (len * FONT_W)
 *   93  = GetTextMetrics     (TEXTMETRIC 8x16)
 *   200 = LibMain
 *
 * UWAGA: GetDC/ReleaseDC przeniesione do USER.EXE (ordinal 66/68).
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

/* ============================================================
 * GDT selektory (bez zmian vs STEP10)
 * ============================================================ */
#define SEL_VESA_BASE  0xA0
#define SEL_FONT       0x118

#define VESA_PITCH  1920
#define VESA_BPP    3
#define FONT_W      8
#define FONT_H      16

typedef unsigned short BOOL;
typedef unsigned short HDC;

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
 * ordinal 34: BitBlt - stub (ETAP 15)
 * ============================================================ */
BOOL __far __pascal BitBlt(HDC hdcDst, int xDst, int yDst, int w, int h,
                            HDC hdcSrc, int xSrc, int ySrc, unsigned long dwRop)
{
    (void)hdcDst; (void)xDst; (void)yDst; (void)w; (void)h;
    (void)hdcSrc; (void)xSrc; (void)ySrc; (void)dwRop;
    return 1;
}

/* ============================================================
 * ordinal 29: PatBlt - stub (ETAP 15)
 * ============================================================ */
BOOL __far __pascal PatBlt(HDC hdc, int x, int y, int w, int h,
                            unsigned long dwRop)
{
    (void)hdc; (void)x; (void)y; (void)w; (void)h; (void)dwRop;
    return 1;
}

/* ============================================================
 * ordinal 52: CreateCompatibleDC  ordinal 68: DeleteDC
 * ============================================================ */
HDC __far __pascal CreateCompatibleDC(HDC hdc)
{
    (void)hdc;
    return 0;
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
    return 0;
}

BOOL __far __pascal DeleteObject(unsigned hObject)
{
    (void)hObject;
    return 1;
}

unsigned __far __pascal SelectObject(HDC hdc, unsigned hObject)
{
    (void)hdc; (void)hObject;
    return 0;
}

int __far __pascal GetObject(unsigned hObject, int cbBuffer, void __far *lpObject)
{
    (void)hObject; (void)cbBuffer; (void)lpObject;
    return 0;
}

/* ============================================================
 * ordinal 80: GetDeviceCaps - parametry ekranu 640x480x24
 * ============================================================ */
int __far __pascal GetDeviceCaps(HDC hdc, int nIndex)
{
    (void)hdc;
    switch (nIndex) {
        case  8:  return 640;   /* HORZRES */
        case 10:  return 480;   /* VERTRES */
        case 12:  return 24;    /* BITSPIXEL */
        case 14:  return 1;     /* PLANES */
        case 88:  return 96;    /* LOGPIXELSX */
        case 90:  return 96;    /* LOGPIXELSY */
        default:  return 0;
    }
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
