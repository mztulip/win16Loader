/*
 * gdi.c - GDI.EXE (STEP10)
 *
 * Eksportowane funkcje:
 *   ordinal 1: GetDC
 *   ordinal 2: ReleaseDC
 *   ordinal 3: TextOut
 *   ordinal 4: LibMain
 *
 * STEP10 - huge selectors:
 *   15 okien GDT (SEL_VESA_BASE=0xA0..0x110), kazde 64KB LFB
 *   draw_pixel(flat_off) wybiera okno: win=flat_off>>16, sel=0xA0+win*8
 *   Brak ograniczenia y<20 - pelna rozdzielczosc 640x480.
 *
 *   SEL_FONT = 0x118 (BIOS 8x16 ROM font, limit=4095)
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s gdi.c -fo=gdi.obj
 *   wlink system windows_dll name gdi.exe file gdi.obj,libstubs.obj
 *         export GETDC.1 export RELEASEDC.2 export TEXTOUT.3
 *         export LIBMAIN.4 option nodefaultlibs option quiet
 */

/* MK_FP - bez dos.h */
#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

/* Port I/O (bez conio.h) */
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
 * GDT selektory STEP10
 * ============================================================ */
#define SEL_VESA_BASE  0xA0   /* okno 0: 0xA0, okno 1: 0xA8, ..., okno 14: 0x110 */
#define SEL_FONT       0x118  /* BIOS 8x16 ROM font, limit=4095 */

/* Parametry trybu graficznego: 640x480x24bpp */
#define VESA_PITCH  1920   /* 640 * 3 bajty/piksel */
#define VESA_BPP    3
#define FONT_W      8
#define FONT_H      16

typedef unsigned short HWND;
typedef unsigned short BOOL;

/* ============================================================
 * draw_pixel - zapis piksela RGB do LFB przez huge selectors
 *
 * flat_off: liniowy offset w LFB (0..921599 dla 640x480)
 * Wybiera okno GDT: win = flat_off >> 16, sel = SEL_VESA_BASE + win*8
 * ============================================================ */
static void draw_pixel(unsigned long flat_off,
                       unsigned char r, unsigned char g, unsigned char b)
{
    unsigned win = (unsigned)(flat_off >> 16);
    unsigned off = (unsigned)(flat_off & 0xFFFFUL);
    unsigned sel = SEL_VESA_BASE + win * 8;
    unsigned char __far *p = (unsigned char __far *)MK_FP(sel, off);
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

/* ============================================================
 * draw_char_gdi - rysuje jeden znak 8x16 na VESA LFB
 *
 * Pelna rozdzielczosc 640x480 - brak ograniczenia y.
 * br,bg,bb: skladowe RGB koloru pierwszoplanowego (BGR w LFB)
 * ============================================================ */
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
            if (fbyte & (1 << bit)) {
                draw_pixel(edi, br, bg, bb);
            }
            edi += VESA_BPP;
        }
    }
}

/* ============================================================
 * TextOut - ordinal 3
 *
 * Rysuje bialy tekst (255,255,255) na LFB + loguje na serial.
 * hdc: ignorowany
 * x, y: wspolrzedne w pikselach (pelna rozdzielczosc 640x480)
 * ============================================================ */
BOOL __far __pascal TextOut(unsigned hdc, int x, int y,
                             const char __far *s, int len)
{
    int i;
    (void)hdc;

    serial_puts("GDI: TextOut \"");
    for (i = 0; i < len; i++) serial_putc(s[i]);
    serial_puts("\"\n");

    for (i = 0; i < len; i++) {
        draw_char_gdi((unsigned char)s[i],
                      (unsigned)(x + i * FONT_W),
                      (unsigned)y,
                      0xFF, 0xFF, 0xFF);   /* bialy */
    }

    return 1;
}

/* ============================================================
 * GetDC - ordinal 1
 * ============================================================ */
unsigned __far __pascal GetDC(HWND hwnd)
{
    (void)hwnd;
    serial_puts("GDI: GetDC\n");
    return 1;
}

/* ============================================================
 * ReleaseDC - ordinal 2
 * ============================================================ */
int __far __pascal ReleaseDC(HWND hwnd, unsigned hdc)
{
    (void)hwnd; (void)hdc;
    return 1;
}

/* ============================================================
 * LibMain - ordinal 4
 * ============================================================ */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}
