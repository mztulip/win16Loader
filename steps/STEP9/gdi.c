/*
 * gdi.c - GDI.EXE (STEP9c)
 *
 * Eksportowane funkcje:
 *   ordinal 1: GetDC
 *   ordinal 2: ReleaseDC
 *   ordinal 3: TextOut
 *   ordinal 4: LibMain
 *
 * TextOut rysuje tekst na VESA LFB przez far pointery:
 *   SEL_VESA (0x90): Linear Frame Buffer (base=g_lfb_phys, limit=1MB)
 *   SEL_FONT (0x98): BIOS 8x16 font ROM (base=g_font_phys, limit=4095)
 *
 * Ograniczenie: offset w SEL_VESA jest 16-bit (max 65535 B).
 * Przy pitch=1920: dziala dla y*1920 + (x+len*8)*3 < 65536
 * -> bezpiecznie dla y < 33.
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s gdi.c -fo=gdi.obj
 *   wlink system windows_dll name gdi.exe file gdi.obj,libstubs.obj
 *         export GETDC.1 export RELEASEDC.2 export TEXTOUT.3
 *         export LIBMAIN.4 option nodefaultlibs option quiet
 */

/* MK_FP - Watcom macro niedostepne bez dos.h; reimplementacja */
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
 * GDT selektory (znane globalnie przez architekture systemu)
 * ============================================================ */
#define SEL_VESA  0x90   /* 32-bit data, base=g_lfb_phys, limit=0xFFFFF (1MB) */
#define SEL_FONT  0x98   /* 16-bit data, base=g_font_phys, limit=4095          */

/* Parametry trybu graficznego */
#define VESA_PITCH  1920   /* 640 * 3 bajty/piksel */
#define VESA_BPP    3
#define FONT_W      8
#define FONT_H      16

/* ============================================================
 * Typy Win16
 * ============================================================ */
typedef unsigned short HWND;
typedef unsigned short BOOL;

/* ============================================================
 * draw_char_gdi - rysuje jeden znak 8x16 na VESA LFB
 *
 * ch:  kod ASCII
 * x:   piksel poziomy
 * y:   piksel pionowy (musi byc < 33 dla 16-bit offset)
 * br,bg,bb: skladowe RGB koloru pierwszoplanowego
 * ============================================================ */
static void draw_char_gdi(unsigned char ch, unsigned x, unsigned y,
                           unsigned char br, unsigned char bg, unsigned char bb)
{
    unsigned row, bit;
    unsigned base;

    /* Sprawdz czy caly znak miesci sie w 16-bit offsetcie LFB.
     * Przy pitch=1920 i FONT_H=16: max y = floor((65535-28824)/1920) = 19 */
    if (y >= 20)
        return;
    base = y * VESA_PITCH + x * VESA_BPP;

    for (row = 0; row < FONT_H; row++) {
        /* Odczyt bajtu wzoru znaku z fontu (SEL_FONT = 0x98) */
        unsigned char fbyte = *((unsigned char __far *)
            MK_FP(SEL_FONT, (unsigned)ch * FONT_H + row));

        unsigned edi = base + row * VESA_PITCH;

        /* Iteracja po 8 bitach: bit 7 = lewy piksel, bit 0 = prawy */
        for (bit = 8; bit-- > 0; ) {
            if (fbyte & (1 << bit)) {
                /* Zapis piksela BGR do LFB (SEL_VESA = 0x90) */
                unsigned char __far *p = (unsigned char __far *)
                    MK_FP(SEL_VESA, edi);
                p[0] = bb;
                p[1] = bg;
                p[2] = br;
            }
            edi += VESA_BPP;
        }
    }
}

/* ============================================================
 * TextOut - ordinal 3
 *
 * Rysuje tekst na VESA LFB + loguje na serial.
 * hdc: ignorowany (placeholder, zawsze = 1)
 * x, y: wspolrzedne poczatku tekstu w pikselach
 * s:    far pointer do stringa (niekoniecznie null-terminated)
 * len:  liczba znakow do narysowania
 *
 * Kolor: bialy (0xFF, 0xFF, 0xFF) na czarnym tle.
 * ============================================================ */
BOOL __far __pascal TextOut(unsigned hdc, int x, int y,
                             const char __far *s, int len)
{
    int i;
    (void)hdc;

    serial_puts("GDI: TextOut \"");
    for (i = 0; i < len; i++) serial_putc(s[i]);
    serial_puts("\"\n");

    /* Rysuj kazdy znak na LFB */
    for (i = 0; i < len; i++) {
        draw_char_gdi((unsigned char)s[i],
                      (unsigned)(x + i * FONT_W),
                      (unsigned)y,
                      0x00, 0x00, 0x00);   /* czarny (widoczny na bialym pasku) */
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
    return 1;   /* HDC = 1 (placeholder) */
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
