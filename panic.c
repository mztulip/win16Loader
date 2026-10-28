/* panic.c - ekran wyjatku (32-bit PM, gcc -m32 -ffreestanding -O0 -fno-builtin -fno-pic)
 * UWAGA: brak rodata - wszystkie stringi jako lokalne char[] na stosie.
 * panic_screen MUSI byc pierwsza funkcja (offset 0 w panic.bin). */

#define VESA_W   640
#define VESA_H   480
#define VESA_BPP 3
#define PITCH    (VESA_W * VESA_BPP)
#define FONT_W   8
#define FONT_H   16

/* Forward declarations statycznych helperow */
static void serial_char(char c);
static void serial_strn(const char *s, int n);
static void hex4_buf(unsigned v, char *out);
static void fill_rect(unsigned char *fb, int x, int y, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b);
static void draw_char(unsigned char *fb, const unsigned char *font,
                      int x, int y, unsigned char ch,
                      unsigned char r, unsigned char g, unsigned char b);
static void draw_str(unsigned char *fb, const unsigned char *font,
                     int x, int y, const char *s,
                     unsigned char r, unsigned char g, unsigned char b);

/* ================================================================
 * panic_screen - MUSI BYC PIERWSZA (offset 0 w panic.bin)
 * Parametry (cdecl 32-bit):
 *   lfb    - fizyczny adres framebuffera (lub 0 jesli brak VESA)
 *   font_a - fizyczny adres fontu 8x16 (lub 0)
 *   exc    - numer wyjatku (0x00..0x0E)
 *   cs     - CS w chwili bledu
 *   ip     - IP w chwili bledu
 *   err    - kod bledu (0 jesli brak)
 *   sp     - SP w chwili bledu
 * DS = SEL_DATA32 (flat, base=0) gdy wywolywane z panic_entry_32.
 * ================================================================ */
void panic_screen(unsigned lfb, unsigned font_a, unsigned exc,
                  unsigned cs, unsigned ip, unsigned err, unsigned sp)
{
    unsigned char *fb   = (unsigned char *)lfb;
    const unsigned char *font = (const unsigned char *)font_a;
    char tmp[5];
    int row;

    /* --- wypisz na serial --- */
    {
        char s1[] = "\r\n*** EXCEPTION ";
        char s2[] = "  CS:IP=";
        char s3[] = ":";
        char s4[] = "  ERR=";
        char s5[] = "  SP=";
        char s6[] = " ***\r\n";
        serial_strn(s1, sizeof(s1)-1);
        hex4_buf(exc, tmp); serial_strn(tmp, 4);
        serial_strn(s2, sizeof(s2)-1);
        hex4_buf(cs,  tmp); serial_strn(tmp, 4);
        serial_strn(s3, 1);
        hex4_buf(ip,  tmp); serial_strn(tmp, 4);
        serial_strn(s4, sizeof(s4)-1);
        hex4_buf(err, tmp); serial_strn(tmp, 4);
        serial_strn(s5, sizeof(s5)-1);
        hex4_buf(sp,  tmp); serial_strn(tmp, 4);
        serial_strn(s6, sizeof(s6)-1);
    }

    if (!lfb) goto hlt;

    /* Pomaranczowe tlo: R=0xFF G=0xA0 B=0x00 (zapisane jako BGR: 0x00 0xA0 0xFF) */
    fill_rect(fb, 0, 0, VESA_W, VESA_H, 0xFF, 0xA0, 0x00);

    /* Ciemnoczerwony pasek tytulowy */
    fill_rect(fb, 0, 0, VESA_W, 34, 0xBB, 0x00, 0x00);

    if (!font_a) goto hlt;

    /* Tytul */
    {
        char title[] = "*** EXCEPTION FAULT - SYSTEM HALTED ***";
        draw_str(fb, font, 10, 9, title, 0xFF, 0xFF, 0xFF);
    }

    row = 50;

    /* Numer wyjatku */
    {
        char lbl[] = "Exception : 0x";
        hex4_buf(exc, tmp); tmp[4] = 0;
        draw_str(fb, font, 10, row, lbl,       0xFF, 0xFF, 0xFF);
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1)*FONT_W, row, tmp, 0xFF, 0xFF, 0x00);
    }

    row += 20;

    /* Nazwa wyjatku */
    {
        const char *excname = 0;
        char n00[] = "#DE Divide Error";
        char n01[] = "#DB Debug";
        char n02[] = "NMI";
        char n03[] = "#BP Breakpoint";
        char n04[] = "#OF Overflow";
        char n05[] = "#BR Bound Range";
        char n06[] = "#UD Invalid Opcode";
        char n07[] = "#NM Device N/A";
        char n08[] = "#DF Double Fault";
        char n09[] = "Coproc Seg Overrun";
        char n0A[] = "#TS Invalid TSS";
        char n0B[] = "#NP Seg Not Present";
        char n0C[] = "#SS Stack Fault";
        char n0D[] = "#GP General Protection";
        char n0E[] = "#PF Page Fault";
        char nxx[] = "Unknown Exception";
        switch (exc & 0xFFu) {
        case 0x00: excname = n00; break;
        case 0x01: excname = n01; break;
        case 0x02: excname = n02; break;
        case 0x03: excname = n03; break;
        case 0x04: excname = n04; break;
        case 0x05: excname = n05; break;
        case 0x06: excname = n06; break;
        case 0x07: excname = n07; break;
        case 0x08: excname = n08; break;
        case 0x09: excname = n09; break;
        case 0x0A: excname = n0A; break;
        case 0x0B: excname = n0B; break;
        case 0x0C: excname = n0C; break;
        case 0x0D: excname = n0D; break;
        case 0x0E: excname = n0E; break;
        default:   excname = nxx; break;
        }
        draw_str(fb, font, 10, row, excname, 0xFF, 0xE0, 0x80);
    }

    row += 24;

    /* CS:IP */
    {
        char lbl[]   = "CS : IP   =  ";
        char colon[] = " : ";
        draw_str(fb, font, 10, row, lbl, 0xFF, 0xFF, 0xFF);
        hex4_buf(cs, tmp); tmp[4] = 0;
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1)*FONT_W, row,
                 tmp, 0xFF, 0xFF, 0x00);
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1+4)*FONT_W, row,
                 colon, 0xFF, 0xFF, 0xFF);
        hex4_buf(ip, tmp);
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1+4+3)*FONT_W, row,
                 tmp, 0xFF, 0xFF, 0x00);
    }
    row += 20;

    /* Kod bledu */
    {
        char lbl[] = "Error code = 0x";
        hex4_buf(err, tmp); tmp[4] = 0;
        draw_str(fb, font, 10, row, lbl,       0xFF, 0xFF, 0xFF);
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1)*FONT_W, row, tmp, 0xFF, 0xFF, 0x00);
    }
    row += 20;

    /* SP przy bledu */
    {
        char lbl[] = "SP at fault= 0x";
        hex4_buf(sp, tmp); tmp[4] = 0;
        draw_str(fb, font, 10, row, lbl,       0xFF, 0xFF, 0xFF);
        draw_str(fb, font, 10 + (int)(sizeof(lbl)-1)*FONT_W, row, tmp, 0xFF, 0xFF, 0x00);
    }
    row += 30;

    {
        char msg[] = "System halted. Reset to restart.";
        draw_str(fb, font, 10, row, msg, 0xFF, 0xFF, 0xFF);
    }

hlt:
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

/* ================================================================
 * Statyczne helpery
 * ================================================================ */

static inline void out8(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline unsigned char in8(unsigned short port) {
    unsigned char v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static void serial_char(char c) {
    while (!(in8(0x3FD) & 0x20)) {}
    out8(0x3F8, (unsigned char)c);
}

static void serial_strn(const char *s, int n) {
    int i;
    for (i = 0; i < n; i++) serial_char(s[i]);
}

static void hex4_buf(unsigned v, char *out) {
    int i;
    for (i = 0; i < 4; i++) {
        int n = (int)((v >> ((3-i)*4)) & 0xFu);
        out[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    }
    out[4] = 0;
}

static void fill_rect(unsigned char *fb, int x, int y, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int row, col;
    for (row = y; row < y + h; row++) {
        unsigned char *p = fb + row * PITCH + x * VESA_BPP;
        for (col = 0; col < w; col++) {
            p[0] = b; p[1] = g; p[2] = r;   /* BGR */
            p += VESA_BPP;
        }
    }
}

static void draw_char(unsigned char *fb, const unsigned char *font,
                      int x, int y, unsigned char ch,
                      unsigned char r, unsigned char g, unsigned char b)
{
    const unsigned char *glyph = font + (unsigned)ch * FONT_H;
    int row, col;
    for (row = 0; row < FONT_H; row++) {
        unsigned char bits = glyph[row];
        for (col = 0; col < FONT_W; col++) {
            if (bits & (unsigned char)(0x80u >> col)) {
                unsigned char *p = fb + (y + row) * PITCH + (x + col) * VESA_BPP;
                p[0] = b; p[1] = g; p[2] = r;
            }
        }
    }
}

static void draw_str(unsigned char *fb, const unsigned char *font,
                     int x, int y, const char *s,
                     unsigned char r, unsigned char g, unsigned char b)
{
    while (*s) {
        draw_char(fb, font, x, y, (unsigned char)*s, r, g, b);
        x += FONT_W;
        s++;
    }
}
