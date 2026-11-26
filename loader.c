/*
 * loader.c - STEP18: generyczny loader zasobow NE (rc_loader.c)
 *
 * Zmiany vs STEP17:
 *   - load_ski_strings / load_ski_bitmaps zastapione przez rc_init + rc_load_all
 *     (logika parsowania tablicy zasobow NE przeniesiona do rc_loader.c)
 *   - #include "rc_loader.h"
 *
 * Kompilacja:
 *   wcl -ml -l=dos -q loader.c pm_call.obj serial.obj rc_loader.obj -fe=loader.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include "serial.h"
#include "rc_loader.h"

/* Wlacz 1 aby wypisywac kazdy rekord fixupa (duzo outputu, wolno) */
#define DEBUG_FIXUPS 0

static void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    fputs(buf, stdout);
    serial_puts(buf);
}

#include "ne.h"
#include "module.h"

/* ============================================================
 * Wykrywanie RAM (INT 15h E801h / AH=88h)
 * ============================================================ */
unsigned long g_ext_mem_kb = 0;   /* KB pamieci rozszerzonej (>1MB); eksport do pm_call.asm */
char          g_mem_str[64];      /* sformatowany string wyswietlany w pm32_entry */

static void mem_detect(void)
{
    union REGS    r;
    unsigned long ext_kb = 0;
    int           method = 0;

    /* Metoda 1: INT 15h AX=E801h - rozszerzona 1..15MB (CX) + >16MB (DX bloki 64KB) */
    r.x.ax = 0xE801;
    int86(0x15, &r, &r);
    if (!r.x.cflag) {
        unsigned short cx = r.x.cx ? r.x.cx : r.x.ax;
        unsigned short dx = r.x.dx ? r.x.dx : r.x.bx;
        if (cx > 0 || dx > 0) {
            ext_kb = (unsigned long)cx + (unsigned long)dx * 64UL;
            method = 801;
            goto done;
        }
    }

    /* Metoda 2: INT 15h AH=88h - fallback (max ~64MB) */
    r.h.ah = 0x88;
    int86(0x15, &r, &r);
    if (!r.x.cflag && r.x.ax > 0) {
        ext_kb = (unsigned long)r.x.ax;
        method = 88;
        goto done;
    }

done:
    g_ext_mem_kb = ext_kb;
    if (ext_kb == 0) {
        strcpy(g_mem_str, "Memory: 640 KB conv  (extended: not detected)");
    } else if (ext_kb < 1024UL) {
        sprintf(g_mem_str, "Memory: 640 KB conv + %lu KB extended  [INT15 E%lu]",
                ext_kb, (unsigned long)method);
    } else {
        unsigned long mb  = ext_kb / 1024UL;
        unsigned long rem = (ext_kb % 1024UL) * 10UL / 1024UL;  /* 1 cyfra po kropce */
        if (rem > 0)
            sprintf(g_mem_str, "Memory: 640 KB conv + %lu.%lu MB extended  [INT15 E%lu]",
                    mb, rem, (unsigned long)method);
        else
            sprintf(g_mem_str, "Memory: 640 KB conv + %lu MB extended  [INT15 E%lu]",
                    mb, (unsigned long)method);
    }
    kprintf("%s\n", g_mem_str);
}

/* ============================================================
 * Grafika 640x480: VBE (LFB albo okno 0xA000 z bankami) lub natywny WD/Paradise
 *
 * Kolejnosc wyboru (WIN.INI [display]):
 *   bpp=8      -> 640x480x8 (paleta sprzetowa), fallback: truecolor
 *   bpp=24     -> 640x480 truecolor (24bpp, albo 32bpp gdy karta nie ma 24bpp), fallback: 8bpp
 *   banked=1   -> wymus okno 0xA000 nawet gdy jest LFB (test sciezki z 1992 r.)
 *   bank=auto|vbe|wd -> sposob przelaczania bankow (auto: porty WD gdy karta Paradise/WD)
 *
 * Tryb ze stalym numerem (0x101/0x112) nie wystarcza: od VBE 2.0 numery trybow
 * sa dowolne (Intel: 0x112 = 32bpp), wiec szukamy po liscie trybow i MIB.
 * VBE 1.2 (WD90C24 z 1994): okno 64KB z granularnoscia 4KB -> pozycja okna
 * w jednostkach granularnosci, nie w 64KB.
 * ============================================================ */
unsigned long  g_lfb_phys       = 0;   /* eksport do pm_call.asm (0 = brak LFB) */
unsigned short g_vesa_pitch     = 0;
unsigned short g_vesa_mode      = 0x0112;
unsigned short g_vesa_bpp       = 24;  /* 8, 24 albo 32 */
unsigned short g_vesa_banked    = 0;
unsigned short g_vesa_bankmode  = 0;   /* VBANK_* */
unsigned short g_vesa_granshift = 0;   /* pozycja okna = (offset >> 16) << granshift */
unsigned short g_vesa_winmask   = 1;   /* okna przelaczane przy zmianie banku: bit0=A, bit1=B */
unsigned short g_vesa_dacvbe    = 0;   /* 1: DAC nie-VGA, paleta tylko przez 4F09h */
unsigned long  g_font_phys      = 0;   /* adres fizyczny tablicy fontow 8x16 (256*16 B) */

static unsigned short g_vbe_ver = 0;   /* 0 = brak VBE */
static unsigned long  g_vbe_caps = 0;
static unsigned short g_vbe_vram_kb = 0;
static unsigned short g_vbe_modes[96];
static int            g_vbe_nmodes = 0;
static int            g_is_paradise = 0;

typedef struct {
    unsigned short attr, pitch, xres, yres, bpp, model;
    unsigned char  win_a, win_b;
    unsigned short gran_kb, size_kb, seg_a, seg_b;
    unsigned long  lfb;
} VMODEINFO;

static void read_display_ini(int *want_bpp, int *force_banked, int *bank_pref)
{
    FILE *f;
    char line[128];
    int in_disp = 0;
    *want_bpp = 24;
    *force_banked = 0;
    *bank_pref = 0;            /* 0=auto 1=vbe 2=wd */
    f = fopen("C:\\WINDOWS\\WIN.INI", "r");
    if (!f) f = fopen("WIN.INI", "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            in_disp = (strnicmp(p, "[display]", 9) == 0);
            continue;
        }
        if (!in_disp) continue;
        if (strnicmp(p, "bpp=", 4) == 0) {
            int v = atoi(p + 4);
            if (v == 8 || v == 24 || v == 32) *want_bpp = v == 32 ? 24 : v;
        } else if (strnicmp(p, "banked=", 7) == 0) {
            *force_banked = atoi(p + 7) != 0;
        } else if (strnicmp(p, "bank=", 5) == 0) {
            if (strnicmp(p + 5, "vbe", 3) == 0) *bank_pref = 1;
            else if (strnicmp(p + 5, "wd", 2) == 0) *bank_pref = 2;
        }
    }
    fclose(f);
}

/* Paleta startowa 8bpp: 10 kolorow systemowych, kostka 6x6x6, czern, 10 kolorow systemowych */
static void dac_default(unsigned char *rgb /* 768 */)
{
    static const unsigned char sys20[20][3] = {
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
        {192,220,192},{166,202,240},{255,251,240},{160,160,164},{128,128,128},{255,0,0},
        {0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}
    };
    int i, r, g, b, k = 10;
    memset(rgb, 0, 768);
    for (i = 0; i < 10; i++) memcpy(rgb + i * 3, sys20[i], 3);
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++, k++) {
                rgb[k * 3 + 0] = (unsigned char)(r * 51);
                rgb[k * 3 + 1] = (unsigned char)(g * 51);
                rgb[k * 3 + 2] = (unsigned char)(b * 51);
            }
    for (i = 0; i < 10; i++) memcpy(rgb + (246 + i) * 3, sys20[10 + i], 3);
}

static void vga_init_dac8(void)
{
    static unsigned char rgb[768];
    int i;
    dac_default(rgb);
    if (g_vesa_dacvbe) {
        /* DAC nie zgodny z VGA: VBE 4F09h, wpisy B,G,R,0 (6 bit) */
        unsigned seg;
        if (_dos_allocmem(64, &seg) == 0) {
            unsigned char __far *t = MK_FP(seg, 0);
            union REGS r; struct SREGS s;
            for (i = 0; i < 256; i++) {
                t[i * 4 + 0] = rgb[i * 3 + 2] >> 2;
                t[i * 4 + 1] = rgb[i * 3 + 1] >> 2;
                t[i * 4 + 2] = rgb[i * 3 + 0] >> 2;
                t[i * 4 + 3] = 0;
            }
            r.x.ax = 0x4F09; r.h.bl = 0; r.x.cx = 256; r.x.dx = 0; r.x.di = 0;
            segread(&s); s.es = seg;
            int86x(0x10, &r, &r, &s);
            _dos_freemem(seg);
            if (r.x.ax == 0x004F) return;
            kprintf("VESA: 4F09h failed (ax=0x%04X), DAC przez porty\n", r.x.ax);
            g_vesa_dacvbe = 0;
        }
    }
    outp(0x3C8, 0);
    for (i = 0; i < 768; i++) outp(0x3C9, rgb[i] >> 2);
}

/* Karta Paradise/Western Digital: sygnatura "VGA=" pod C000:007D */
static int detect_paradise(void)
{
    unsigned char __far *p = MK_FP(0xC000, 0x007D);
    return p[0] == 'V' && p[1] == 'G' && p[2] == 'A' && p[3] == '=';
}

/* WD90Cxx: odblokuj PR0-PR4, wylacz PR0B (jedno okno 64KB @A000 sterowane PR0A), bank 0 */
static void wd_setup_bank(void)
{
    unsigned char pr1;
    outp(0x3CE, 0x0F); outp(0x3CF, 0x05);          /* PR5: unlock PR0..PR4 */
    outp(0x3CE, 0x0B); pr1 = (unsigned char)inp(0x3CF);
    outp(0x3CF, pr1 & ~0x08);                     /* PR1 bit3=0: PR0B wylaczony */
    outp(0x3CE, 0x09); outp(0x3CF, 0x00);          /* PR0A = 0 */
    outp(0x3CE, 0x0A); outp(0x3CF, 0x00);          /* PR0B = 0 */
}

static void vbe_probe(void)
{
    union REGS r; struct SREGS s;
    unsigned seg;
    unsigned char __far *ci;
    unsigned short __far *ml;
    g_vbe_ver = 0; g_vbe_nmodes = 0; g_vbe_caps = 0;
    if (_dos_allocmem(32, &seg) != 0) return;   /* 512 B */
    ci = MK_FP(seg, 0);
    _fmemset(ci, 0, 512);
    _fmemcpy(ci, "VBE2", 4);                       /* prosba o blok VBE 2.0+ */
    r.x.ax = 0x4F00; r.x.di = 0;
    segread(&s); s.es = seg;
    int86x(0x10, &r, &r, &s);
    if (r.x.ax == 0x004F && _fmemcmp(ci, "VESA", 4) == 0) {
        g_vbe_ver     = *(unsigned short __far *)(ci + 4);
        g_vbe_caps    = *(unsigned long  __far *)(ci + 0x0A);
        g_vbe_vram_kb = *(unsigned short __far *)(ci + 0x12) * 64u;
        ml = *(unsigned short __far * __far *)(ci + 0x0E);
        if (ml) {
            while (*ml != 0xFFFF && g_vbe_nmodes < (int)(sizeof(g_vbe_modes) / 2))
                g_vbe_modes[g_vbe_nmodes++] = *ml++;
        }
        kprintf("VBE %u.%u caps=%08lX VRAM=%uKB trybow=%d\n", g_vbe_ver >> 8, g_vbe_ver & 0xFF,
                g_vbe_caps, g_vbe_vram_kb, g_vbe_nmodes);
    } else {
        kprintf("VBE: brak (4F00h ax=0x%04X)\n", r.x.ax);
    }
    _dos_freemem(seg);
}

static int vbe_mode_info(unsigned short mode, VMODEINFO *mi)
{
    union REGS r; struct SREGS s;
    unsigned seg;
    unsigned char __far *m;
    if (_dos_allocmem(16, &seg) != 0) return 0;   /* 256 B */
    m = MK_FP(seg, 0);
    _fmemset(m, 0, 256);
    r.x.ax = 0x4F01; r.x.cx = mode; r.x.di = 0;
    segread(&s); s.es = seg;
    int86x(0x10, &r, &r, &s);
    if (r.x.ax != 0x004F) { _dos_freemem(seg); return 0; }
    mi->attr    = *(unsigned short __far *)(m + 0x00);
    mi->win_a   = m[0x02];
    mi->win_b   = m[0x03];
    mi->gran_kb = *(unsigned short __far *)(m + 0x04);
    mi->size_kb = *(unsigned short __far *)(m + 0x06);
    mi->seg_a   = *(unsigned short __far *)(m + 0x08);
    mi->seg_b   = *(unsigned short __far *)(m + 0x0A);
    mi->pitch   = *(unsigned short __far *)(m + 0x10);
    mi->xres    = *(unsigned short __far *)(m + 0x12);
    mi->yres    = *(unsigned short __far *)(m + 0x14);
    mi->bpp     = m[0x19];
    mi->model   = m[0x1B];
    mi->lfb     = *(unsigned long  __far *)(m + 0x28);
    _dos_freemem(seg);
    if (!(mi->attr & 0x0002)) {                    /* VBE 1.0: brak pol rozszerzonych */
        mi->xres = 640; mi->yres = 480;
        if (mode == 0x0101) { mi->bpp = 8;  mi->model = 4; }
        else if (mode == 0x0112) { mi->bpp = 24; mi->model = 6; }
        else return 0;
    }
    if (!mi->pitch) mi->pitch = mi->xres * ((mi->bpp + 7) / 8);
    return 1;
}

/* Okno do przelaczania bankow; 0 = tryb nie nadaje sie do pracy z oknem 0xA000 */
static int vbe_pick_window(const VMODEINFO *mi)
{
    unsigned short g;
    int sh = 0;
    if (mi->attr & 0x0040) return 0;               /* VBE 2.0: brak trybu okienkowego */
    if ((mi->win_a & 7) == 7 && mi->seg_a == 0xA000)      g_vesa_winmask = 1;
    else if ((mi->win_b & 7) == 7 && mi->seg_b == 0xA000) g_vesa_winmask = 2;
    else if ((mi->win_a & 1) && (mi->win_b & 1) && mi->seg_a == 0xA000 && mi->seg_b == 0xA000 &&
             ((mi->win_a | mi->win_b) & 6) == 6)  g_vesa_winmask = 3; /* osobne R i W */
    else return 0;
    if (mi->size_kb < 64 || !mi->gran_kb || mi->gran_kb > 64) return 0;
    for (g = mi->gran_kb; g < 64; g <<= 1) sh++;
    if (g != 64) return 0;                          /* granularnosc nie potega 2 */
    g_vesa_granshift = (unsigned short)sh;
    return 1;
}

/* Znajdz tryb 640x480 o glebi bpp (8 -> model 4 packed, 24/32 -> model 6 direct) */
static int vbe_find_mode(unsigned short bpp, unsigned short *mode, VMODEINFO *mi)
{
    unsigned short pref = bpp == 8 ? 0x0101 : 0x0112;
    int i;
    if (vbe_mode_info(pref, mi) && mi->xres == 640 && mi->yres == 480 && mi->bpp == bpp &&
        (mi->attr & 0x11) == 0x11) { *mode = pref; return 1; }
    for (i = 0; i < g_vbe_nmodes; i++) {
        unsigned short m = g_vbe_modes[i];
        if (!vbe_mode_info(m, mi)) continue;
        if ((mi->attr & 0x11) != 0x11) continue;   /* obslugiwany + graficzny */
        if (mi->xres != 640 || mi->yres != 480 || mi->bpp != bpp) continue;
        if (bpp == 8 ? mi->model != 4 : mi->model != 6) continue;
        *mode = m;
        return 1;
    }
    return 0;
}

static int vbe_set_mode(unsigned short bpp, int force_banked, int bank_pref)
{
    union REGS r;
    VMODEINFO mi;
    unsigned short mode;
    int lfb_ok, win_ok;
    if (!g_vbe_ver || !vbe_find_mode(bpp, &mode, &mi)) return 0;
    /* 32bpp: GDI ma 15 okien 64KB na ekran (960KB) */
    if ((unsigned long)mi.pitch * 480UL > 15UL * 65536UL) {
        kprintf("VESA: tryb 0x%04X %ubpp pitch=%u za duzy\n", mode, bpp, mi.pitch);
        return 0;
    }
    lfb_ok = g_vbe_ver >= 0x0200 && (mi.attr & 0x0080) && mi.lfb && !force_banked;
    win_ok = vbe_pick_window(&mi);
    if (!lfb_ok && !win_ok) {
        kprintf("VESA: tryb 0x%04X bez LFB i bez okna 64KB @A000 (A=%02X B=%02X gran=%u size=%u)\n",
                mode, mi.win_a, mi.win_b, mi.gran_kb, mi.size_kb);
        return 0;
    }
    r.x.ax = 0x4F02;
    r.x.bx = mode;                                 /* najpierw okienkowo (shell w RM) */
    int86(0x10, &r, &r);
    if (r.x.ax != 0x004F) {
        kprintf("VESA: 4F02h tryb 0x%04X failed (ax=0x%04X)\n", mode, r.x.ax);
        return 0;
    }
    g_vesa_mode   = mode;
    g_vesa_bpp    = bpp;
    g_vesa_pitch  = mi.pitch;
    g_lfb_phys    = lfb_ok ? mi.lfb : 0;
    g_vesa_banked = lfb_ok ? 0 : 1;
    g_vesa_dacvbe = (bpp == 8 && (g_vbe_caps & 2)) ? 1 : 0;
    if (g_vesa_banked) {
        g_vesa_bankmode = VBANK_VBE;
        if (bank_pref != 1 && g_is_paradise && mi.gran_kb == 4 && g_vesa_winmask != 2) {
            g_vesa_bankmode = VBANK_WD;            /* porty PR0A: bez wyjscia do RM */
            g_vesa_winmask  = 1;
            wd_setup_bank();
        }
    } else {
        g_vesa_bankmode = VBANK_LFB;
    }
    kprintf("VESA: tryb 0x%04X 640x480x%u pitch=%u %s LFB=0x%08lX okna A=%02X B=%02X gran=%uKB%s\n",
            mode, bpp, mi.pitch,
            lfb_ok ? "LFB" : (g_vesa_bankmode == VBANK_WD ? "banked(WD porty)" : "banked(VBE 4F05)"),
            g_lfb_phys, mi.win_a, mi.win_b, mi.gran_kb, g_vesa_dacvbe ? " DAC=4F09" : "");
    if (bpp == 8) vga_init_dac8();
    return 1;
}

/* Bez VBE w BIOS: Paradise/WD90Cxx tryb 5Fh = 640x480x256 (512KB+), banki przez PR0A */
static int wd_set_mode(void)
{
    union REGS r;
    if (!g_is_paradise) return 0;
    r.x.ax = 0x005F;
    int86(0x10, &r, &r);
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if ((r.h.al & 0x7F) != 0x5F) {
        kprintf("WD: tryb 5Fh nie ustawiony (AL=%02X)\n", r.h.al);
        return 0;
    }
    wd_setup_bank();
    g_vesa_mode      = 0x5F;
    g_vesa_bpp       = 8;
    g_vesa_pitch     = 640;
    g_lfb_phys       = 0;
    g_vesa_banked    = 1;
    g_vesa_bankmode  = VBANK_WD;
    g_vesa_granshift = 4;                           /* PR0A w jednostkach 4KB */
    g_vesa_winmask   = 1;
    g_vesa_dacvbe    = 0;
    kprintf("WD: natywny tryb 5Fh 640x480x8 (banki PR0A)\n");
    vga_init_dac8();
    return 1;
}

static void vesa_init(void)
{
    int want_bpp, force_banked, bank_pref;
    read_display_ini(&want_bpp, &force_banked, &bank_pref);
    g_is_paradise = detect_paradise();
    if (g_is_paradise) kprintf("Karta: Paradise/Western Digital (C000:007D 'VGA=')\n");
    if (bank_pref == 2 && g_is_paradise) {
        if (wd_set_mode()) return;
    }
    vbe_probe();
    if (want_bpp == 8) {
        if (vbe_set_mode(8, force_banked, bank_pref)) return;
        if (wd_set_mode()) return;
        if (vbe_set_mode(24, force_banked, bank_pref)) return;
        if (vbe_set_mode(32, force_banked, bank_pref)) return;
    } else {
        if (vbe_set_mode(24, force_banked, bank_pref)) return;
        if (vbe_set_mode(32, force_banked, bank_pref)) return;
        /* karty SVGA z 1992 r. (512KB-1MB VRAM): tylko 256 kolorow */
        if (vbe_set_mode(8, force_banked, bank_pref)) return;
        if (wd_set_mode()) return;
    }
    kprintf("GRAFIKA: brak trybu 640x480 (VBE ani WD) - ekran nie bedzie dzialal\n");
    g_lfb_phys = 0;
}

/* Przelacz VESA na LFB mode (bit14) — wywolac przed wejsciem w PM */
void vesa_set_lfb(void)
{
    union REGS r;
    if (g_vesa_banked || !g_lfb_phys) {
        kprintf("VESA: tryb 0x%04X banked (okno 0xA000, %s)\n", g_vesa_mode,
                g_vesa_bankmode == VBANK_WD ? "porty WD" : "INT 10h 4F05h");
        if (g_vesa_bankmode == VBANK_WD) wd_setup_bank();
        return;
    }
    r.x.ax = 0x4F02;
    r.x.bx = g_vesa_mode | 0x4000;   /* LFB mode */
    int86(0x10, &r, &r);
    if (r.x.ax == 0x004F) {
        kprintf("VESA: LFB mode 0x%04X (%ubpp) enabled\n", g_vesa_mode, g_vesa_bpp);
        if (g_vesa_bpp == 8) vga_init_dac8();
    } else {
        kprintf("VESA: LFB 4F02h failed (ax=0x%04X)\n", r.x.ax);
    }
}

/* ============================================================
 * Font BIOS 8x16
 * Watcom WORDREGS nie ma pola 'bp' wiec uzywamy pragma aux
 * do wywolania INT 10h i powrotu ES:BP jako DX:AX.
 * ============================================================ */

/* INT 10h AH=11h AL=30h BH=6 -> zwraca (ES << 16) | BP
 * INT 10h modyfikuje BP i moze modyfikowac SI/DI - recznie zapisujemy/przywracamy.
 * Nie kopiujemy fontu: SEL_DATA32 (flat) pokrywa ROM BIOS pod adresem fizycznym. */
static unsigned long font_bios_get_ptr(void);
#pragma aux font_bios_get_ptr = \
    "push si"         \
    "push di"         \
    "push bp"         \
    "mov ax, 0x1130"  \
    "mov bh, 6"       \
    "int 0x10"        \
    "mov bx, bp"      \
    "pop bp"          \
    "pop di"          \
    "pop si"          \
    "mov dx, es"      \
    "mov ax, bx"      \
    value [dx ax]     \
    modify [ax bx cx dx es];

static void font_init(void)
{
    unsigned long  ptr;
    unsigned short font_es, font_off;

    ptr      = font_bios_get_ptr();
    font_es  = (unsigned short)(ptr >> 16);
    font_off = (unsigned short)(ptr & 0xFFFF);

    kprintf("Font ROM: ES=0x%04X off=0x%04X\n", font_es, font_off);

    if (font_es == 0 && font_off == 0) {
        kprintf("Font: BIOS nie zwrocil wskaznika\n");
        return;
    }

    /* Uzywamy adresu ROM bezposrednio - nie kopiujemy.
     * W PM, FS=SEL_DATA32 (flat, 4GB) pokrywa ROM BIOS. */
    g_font_phys = ((unsigned long)font_es << 4) + (unsigned long)font_off;
    kprintf("Font: phys=0x%06lX (ROM BIOS, bez kopiowania)\n", g_font_phys);
}

/* ============================================================
 * Thunki INT 3F
 * Kazdy thunk = 8 bajtow (format: patrz int3f_handler w pm_call.asm, module.c)
 * ============================================================ */
#define MAX_THUNKS  2048
#define THUNK_SIZE   8

static unsigned       g_thunk_seg  = 0;  /* segment DOS dla thunkow */
static unsigned short g_thunk_off  = 0;  /* nastepny wolny offset w segmencie */

unsigned long  g_thunk_phys = 0;   /* eksport do pm_call.asm */
unsigned short g_thunk_size = 0;

/* ============================================================
 * IDT: 64 wpisow (wektory 0x00-0x3F)
 * Wpis 0x3F = 16-bit interrupt gate -> int3f_handler w SEL_CODE16
 * ============================================================ */
#define IDT_ENTRIES  256
#define SEL_CODE16   0x20   /* musi zgadzac sie z pm_call.asm */
#define PIC_BASE_M   0x50   /* IRQ0-7  -> INT 50h-57h (pm_call.asm) */
#define PIC_BASE_S   0x58   /* IRQ8-15 -> INT 58h-5Fh */
#define SOFTINT_STUB 6      /* rozmiar stubu przerwania programowego */

unsigned long g_idt_phys = 0;   /* eksport do pm_call.asm */

extern unsigned short get_int21_off(void);
extern unsigned short get_irq0_off(void);
extern unsigned short get_irq1_off(void);
extern unsigned short get_irq12_off(void);
extern unsigned short get_softint_off(void);
extern unsigned short get_iretstub_off(void);
extern unsigned short get_exc00_off(void);
extern unsigned short get_exc06_off(void);
extern unsigned short get_exc08_off(void);
extern unsigned short get_exc0B_off(void);
extern unsigned short get_exc0C_off(void);
extern unsigned short get_exc0D_off(void);
extern unsigned short get_exc0E_off(void);

/* 16-bit interrupt gate -> SEL_CODE16:off */
static void idt_set(unsigned idt_seg, unsigned vec, unsigned short off)
{
    unsigned char __far *p = MK_FP(idt_seg, vec * 8);
    p[0] = (unsigned char)(off & 0xFF);
    p[1] = (unsigned char)(off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;
}

static void init_int3f(unsigned short handler_off)
{
    unsigned       idt_seg;
    unsigned       v;
    unsigned short paragraphs, soft;

    /* Alokuj blok na thunki */
    paragraphs = (MAX_THUNKS * THUNK_SIZE + 15) / 16;
    if (_dos_allocmem(paragraphs, &g_thunk_seg) != 0) {
        kprintf("ERROR: alloc thunk buf\n"); return;
    }
    g_thunk_phys = (unsigned long)g_thunk_seg << 4;
    g_thunk_size = (unsigned short)(paragraphs * 16);
    g_thunk_off  = 0;
    kprintf("Thunk buf: phys=0x%05lX size=%u\n", g_thunk_phys, g_thunk_size);

    paragraphs = (IDT_ENTRIES * 8 + 15) / 16;
    if (_dos_allocmem(paragraphs, &idt_seg) != 0) {
        kprintf("ERROR: alloc IDT\n"); return;
    }
    g_idt_phys = (unsigned long)idt_seg << 4;

    /* Domyslnie: przerwanie programowe -> dyspozytor KERNEL (softint_common) */
    soft = get_softint_off();
    for (v = 0; v < IDT_ENTRIES; v++)
        idt_set(idt_seg, v, (unsigned short)(soft + v * SOFTINT_STUB));

    /* Wyjatki CPU (panic screen) */
    idt_set(idt_seg, 0x00, get_exc00_off());
    idt_set(idt_seg, 0x06, get_exc06_off());
    idt_set(idt_seg, 0x08, get_exc08_off());
    idt_set(idt_seg, 0x0B, get_exc0B_off());
    idt_set(idt_seg, 0x0C, get_exc0C_off());
    idt_set(idt_seg, 0x0D, get_exc0D_off());
    idt_set(idt_seg, 0x0E, get_exc0E_off());

    /* IRQ (PIC przemapowany na 50h/58h); nieuzywane/falszywe -> IRET */
    for (v = 0; v < 16; v++)
        idt_set(idt_seg, (v < 8 ? PIC_BASE_M : PIC_BASE_S - 8) + v, get_iretstub_off());
    idt_set(idt_seg, PIC_BASE_M + 0, get_irq0_off());
    idt_set(idt_seg, PIC_BASE_M + 1, get_irq1_off());
    idt_set(idt_seg, PIC_BASE_S + 4, get_irq12_off());

    /* INT 3Fh: thunki API */
    idt_set(idt_seg, 0x3F, handler_off);

    kprintf("IDT: phys=0x%05lX (256) int3f=0x%04X softint=0x%04X irq=%02Xh/%02Xh\n",
            g_idt_phys, handler_off, soft, PIC_BASE_M, PIC_BASE_S);
}

/* ============================================================
 * Globalne dla pm_call.asm (NE app)
 * ============================================================ */
unsigned long  g_app_phys;
unsigned short g_code_size;
unsigned long  g_cs_phys;
unsigned short g_orig_cs;
unsigned short g_gdt_off_c;     /* offset tablicy GDT w segmencie kodu (ustawiany z asm) */
unsigned short g_orig_ss;
unsigned short g_orig_sp;
unsigned long  g_app_data_phys;
unsigned short g_data_size;
unsigned short g_has_data;

/* ============================================================
 * KCB (Kernel Control Block) i globalny heap - STEP15
 *
 * KCB jest inicjalizowany w real mode przed wejsciem w PM.
 * W PM dostepny przez SEL_KCB=0x98 (patchowany przez patch_gdt).
 *
 * Layout KCB (16 bajtow, packed):
 *   [0]  WORD  app_hinstance = SEL_APP_DATA (0x40)
 *   [2]  WORD  next_dyn_sel  = GDYN_FIRST (0x130)
 *   [4]  DWORD heap_phys     = g_global_heap_phys
 *   [8]  DWORD heap_next     = g_global_heap_phys (inicjalnie)
 *   [12] DWORD heap_end      = g_global_heap_phys + HEAP_SIZE
 * ============================================================ */
#define GLOBAL_HEAP_SIZE  (64UL * 1024)    /* 64KB dla GlobalAlloc (DOS <640KB limit) */
#define GDYN_FIRST_SEL    0x130            /* pierwszy slot GDT dla GlobalAlloc */

#define RSC_STR_MAX_BLOCKS 2   /* max RT_STRING blocks w KCB */

#pragma pack(push, 1)
typedef struct {
    unsigned short app_hinstance;        /* 0 */
    unsigned short next_dyn_sel;         /* 2 */
    unsigned long  heap_phys;            /* 4 */
    unsigned long  heap_next;            /* 8 */
    unsigned long  heap_end;             /* 12 */
    unsigned short local_heap_off;       /* 16 */
    unsigned char  rsc_nblocks;          /* 18: liczba wczytanych blokow RT_STRING */
    unsigned char  rsc_pad;              /* 19 */
    unsigned short rsc_block_ids[2];     /* 20,22: ID bloku (1=str1..16, 2=str17..32) */
    unsigned short rsc_block_sizes[2];   /* 24,26: rozmiar danych bloku w bajtach */
    unsigned long  tick_ms;              /* 28: licznik ms (inkrementowany przez IRQ0 handler) */
    /* bajty 32..255: surowe dane RT_STRING (maks 224 bajty) */
} KCB_LAYOUT;
#pragma pack(pop)

#define KCB_RSC_DATA_OFF  32  /* offset bajtow danych RT_STRING w KCB (po tick_ms) */

unsigned long g_kcb_phys      = 0;   /* eksport do pm_call.asm */
unsigned long g_psp_phys      = 0;   /* fake PSP (256 B zeroed) – ES at app startup */
unsigned long g_bitmaps_phys  = 0;   /* SEL_BITMAPS=0x128: bufor 86 sprite'ow 4bpp */

extern void __far pm_call_app(void);
unsigned char g_exit_code = 0;   /* kod wyjscia ostatniej apki (ExitWindows(EW_RESTARTWINDOWS) = 42) */
extern unsigned short get_int3f_off(void);
extern unsigned short get_i3f_stack_off(void), get_i3f_depth_off(void);
extern unsigned short get_rmint_off(void);  /* offset rm_int_service w SEL_CODE16 */  /* zwraca offset int3f_handler w SEL_CODE16 */

/* ============================================================
 * PS/2 mouse init (real mode, przed wejsciem w PM)
 * Sekwencja: reset (0xFF) -> ACK+0xAA+0x00, enable streaming (0xF4) -> ACK
 * ============================================================ */
static void ps2_wait_write(void)
{
    unsigned char s;
    long t;
    for (t = 0; t < 100000; t++) {
        s = inp(0x64);
        if (!(s & 0x02)) return;   /* bit 1 = input buffer full; czekaj az 0 */
    }
}

static unsigned char ps2_read_byte(void)
{
    long t;
    for (t = 0; t < 100000; t++) {
        if (inp(0x64) & 0x01)      /* bit 0 = output buffer full */
            return inp(0x60);
    }
    return 0xFF;   /* timeout */
}

void init_mouse_ps2(void)
{
    unsigned char resp;

    /* Oproznij bufor wyjsciowy 8042 (np. resztki po nacisnieciu Enter w DOS) */
    while (inp(0x64) & 0x01) inp(0x60);

    /* Wlacz port myszy PS/2 w kontrolerze (0xA8) */
    ps2_wait_write();
    outp(0x64, 0xA8);

    /* Oproznij ewentualne smieci */
    while (inp(0x64) & 0x01) inp(0x60);

    /* Wyslij reset do myszy przez kontroler (0xD4 = nastepny bajt idzie do myszy) */
    ps2_wait_write();
    outp(0x64, 0xD4);
    ps2_wait_write();
    outp(0x60, 0xFF);              /* reset */

    resp = ps2_read_byte();        /* ACK = 0xFA */
    if (resp != 0xFA) {
        kprintf("MOUSE: reset no ACK (0x%02X) - brak myszy PS/2?\n", (unsigned)resp);
        return;
    }
    ps2_read_byte();               /* BAT result = 0xAA */
    ps2_read_byte();               /* mouse ID   = 0x00 */

    /* Wlacz streaming (Enable Data Reporting) */
    ps2_wait_write();
    outp(0x64, 0xD4);
    ps2_wait_write();
    outp(0x60, 0xF4);

    resp = ps2_read_byte();        /* ACK = 0xFA */
    if (resp != 0xFA) {
        kprintf("MOUSE: enable no ACK (0x%02X)\n", (unsigned)resp);
        return;
    }
    kprintf("MOUSE: PS/2 init OK\n");
}

/* Po powrocie z PM: wylacz IRQ12 w kontrolerze i streaming myszy, zeby BIOS
 * (INT 74h) nie dostawal pakietow i nie zjadal odpowiedzi przy kolejnym
 * init_mouse_ps2(). pm16_call_app i tak ustawia CCB=0x47 przy wejsciu w PM. */
void shutdown_mouse_ps2(void)
{
    while (inp(0x64) & 0x01) inp(0x60);   /* oproznij bufor wyjsciowy */
    ps2_wait_write();
    outp(0x64, 0x60);                      /* Write CCB */
    ps2_wait_write();
    outp(0x60, 0x45);                      /* kbd_irq | self_test | translate, bez aux_irq */
    ps2_wait_write();
    outp(0x64, 0xD4);
    ps2_wait_write();
    outp(0x60, 0xF5);                      /* Disable Data Reporting */
    ps2_read_byte();                       /* ACK */
}

unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];
unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];
unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

#define MAX_BITMAPS     86
#define BMP_BUF_PARA    3584           /* 56KB = 3584 * 16 */

/* 20b-20e: shell tekstowy - skanowanie katalogu i wybor apki */
extern const char *shell_run(void);

/* Pola KCB >= 512 dla KERNEL (patrz module.h, KCB_*) */
static unsigned short g_trace;

/* BLASTER=A220 I5 D1 H5 T6 -> KCB (port, DMA 8-bit, IRQ) dla MMSYSTEM */
static void blaster_to_kcb(unsigned short __far *w)
{
    const char *b = getenv("BLASTER");
    unsigned short port = 0, dma = 0xFFFF, irq = 0xFFFF;
    if (b) {
        while (*b) {
            char c = (char)(*b & 0xDF);
            if (c == 'A') port = (unsigned short)strtoul(b + 1, NULL, 16);
            else if (c == 'D') dma = (unsigned short)atoi(b + 1);
            else if (c == 'I') irq = (unsigned short)atoi(b + 1);
            while (*b && *b != ' ') b++;
            while (*b == ' ') b++;
        }
        kprintf("BLASTER: port=%X dma=%d irq=%d\n", port, (int)(short)dma, (int)(short)irq);
    }
    w[KCB_SB_PORT / 2] = port;
    w[KCB_SB_DMA / 2]  = dma;
    w[KCB_SB_IRQ / 2]  = irq;
}

static void kcb_set_ext(unsigned kcb_rm, int argc, char *argv[])
{
    unsigned short __far *w = (unsigned short __far *)MK_FP(kcb_rm, 0);
    char __far *c = (char __far *)MK_FP(kcb_rm, 0);
    struct find_t ft;
    int i;
    w[KCB_RMINT_OFF / 2]     = get_rmint_off();
    w[590 / 2]               = get_i3f_stack_off();
    w[592 / 2]               = get_i3f_depth_off();
    w[KCB_RMBUF_SEG / 2]     = g_rmbuf_seg;
    w[KCB_THUNK_NEXT / 2]    = mod_thunk_next();
    w[KCB_THUNK_LIMIT / 2]   = g_thunk_size;
    w[KCB_ENV_SEG / 2]       = *(unsigned short __far *)MK_FP(_psp, 0x2C);
    w[KCB_APP_STACK / 2]     = g_app_stack;
    w[KCB_APP_SP / 2]        = g_init_sp;
    w[KCB_APP_HEAP / 2]      = g_app_heap;
    w[KCB_APP_HEAPSTART / 2] = g_app_heapstart;
    w[KCB_TRACE / 2]         = g_trace;
    w[KCB_VESA_BPP / 2]      = g_vesa_bpp;
    w[KCB_VESA_PITCH / 2]    = g_vesa_pitch ? g_vesa_pitch : (g_vesa_bpp == 8 ? 640 : 1920);
    w[KCB_VESA_BANKED / 2]   = g_vesa_banked;
    w[KCB_VESA_BANKMODE / 2] = g_vesa_bankmode;
    w[KCB_VESA_GRANSH / 2]   = g_vesa_granshift;
    w[KCB_VESA_WINMASK / 2]  = g_vesa_winmask;
    w[KCB_VESA_DACVBE / 2]   = g_vesa_dacvbe;
    blaster_to_kcb(w);
    /* Katalog Windows: C:\WINDOWS jesli istnieje, inaczej katalog loadera */
    if (_dos_findfirst("C:\\WINDOWS", _A_SUBDIR, &ft) == 0 && (ft.attrib & _A_SUBDIR))
        _fstrcpy(c + KCB_WINDIR, "C:\\WINDOWS");
    else {
        _fstrcpy(c + KCB_WINDIR, mod_sysdir());
        i = _fstrlen(c + KCB_WINDIR);
        if (i > 3 && c[KCB_WINDIR + i - 1] == '\\') c[KCB_WINDIR + i - 1] = 0;
    }
    _fstrcpy(c + KCB_SYSDIR, mod_sysdir());
    i = _fstrlen(c + KCB_SYSDIR);
    if (i > 3 && c[KCB_SYSDIR + i - 1] == '\\') c[KCB_SYSDIR + i - 1] = 0;
    c[KCB_CMDLINE] = 0;
    for (i = 2; i < argc; i++) {
        if (_fstrlen(c + KCB_CMDLINE) + strlen(argv[i]) + 2 > 127) break;
        if (i > 2) _fstrcat(c + KCB_CMDLINE, " ");
        _fstrcat(c + KCB_CMDLINE, argv[i]);
    }
    kprintf("KCB ext: rmint=%04X rmbuf=%04X thunks=%04X/%04X env=%04X windir=%Fs\n",
            w[KCB_RMINT_OFF / 2], w[KCB_RMBUF_SEG / 2], w[KCB_THUNK_NEXT / 2],
            w[KCB_THUNK_LIMIT / 2], w[KCB_ENV_SEG / 2], c + KCB_WINDIR);
}

void set_text_mode(void)
{
    union REGS r;
    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

/* ============================================================
 * main
 * ============================================================ */
/* Przy jednej stacji DOS udaje B: jako druga dyskietke w tej samej stacji i przy przejsciu
 * miedzy literami prosi o zamiane. Dla aplikacji Win16 B: nie istnieje (jak w Windows NT):
 * na czas ich dzialania wpis B: w tablicy CDS DOS-a jest nieprawidlowy (flagi = 0),
 * wiec sciezki, IOCTL i zmiana dysku dostaja blad od samego DOS-a. */
#define CDS_FLAGS 0x43
static unsigned __far *g_cds_b;
static unsigned g_cds_b_flags;

static void phantom_b_hide(void)
{
    union REGS r;
    struct SREGS s;
    unsigned drive;
    unsigned char __far *lol, __far *cds, __far *e;
    int86(0x11, &r, &r);
    if (!(r.x.ax & 1) || (r.x.ax & 0xC0)) return;      /* brak stacji albo sa co najmniej dwie */
    _dos_getdrive(&drive);
    if (drive == 2) return;                            /* uruchomiono z B: */
    segread(&s);
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &s);                          /* List of Lists -> ES:BX */
    lol = (unsigned char __far *)MK_FP(s.es, r.x.bx);
    cds = *(unsigned char __far * __far *)(lol + 0x16);
    if (lol[0x21] < 2 || !cds) return;
    /* rozmiar wpisu: 0x58 (DOS 4+) albo 0x51 (DOS 3.x); pisz tylko gdy to na pewno wpis B: */
    e = cds + 0x58;
    if (e[0] != 'B' || e[1] != ':') e = cds + 0x51;
    if (e[0] != 'B' || e[1] != ':') { kprintf("Dysk B: brak wpisu CDS\n"); return; }
    g_cds_b = (unsigned __far *)(e + CDS_FLAGS);
    g_cds_b_flags = *g_cds_b;
    *g_cds_b = 0;
    kprintf("Dysk B: ukryty (jedna stacja)\n");
}

static void phantom_b_restore(void)
{
    if (g_cds_b) *g_cds_b = g_cds_b_flags;
    g_cds_b = 0;
}

static void shrink_dos_memory(void)
{
    unsigned ss_val, sp_val;
    unsigned needed, maxsize;
    unsigned psp_val = _psp;
    unsigned err;
    __asm {
        mov ss_val, ss
        mov sp_val, sp
    }
    needed = (ss_val - psp_val) + ((sp_val + 15u) >> 4) + 0x100u;
    err = _dos_setblock(needed, psp_val, &maxsize);
    kprintf("DOS mem shrink: psp=%04X ss=%04X needed=%u para err=%u max=%u\n",
            psp_val, ss_val, needed, err, maxsize);
}

int main(int argc, char *argv[])
{
    const char *app_name;
    serial_init();
    shrink_dos_memory();
    kprintf("=== NE Loader STEP20 - shell / menu bar ===\n");

    /* 0. Wykryj dostepna pamiec RAM (przed PM - INT 15h dostepny tylko w real mode) */
    kprintf("--- RAM detection ---\n");
    mem_detect();

    /* 0a. Inicjalizacja VESA (przed shell i PM — potrzebne do graficznego shella) */
    kprintf("--- VESA init ---\n");
    vesa_init();

    /* 20b-20f: jezeli brak argumentu -> uruchom shell graficzny (VESA)
     * Jezeli argument podany -> bezposrednie uruchomienie (jak dotychczas) */
    /* Opcje loadera przed nazwa apki: -t = trace wywolan API na COM1 */
    while (argc > 1 && argv[1][0] == '-') {
        int k;
        if (argv[1][1] == 't' || argv[1][1] == 'T') g_trace = 1;
        for (k = 1; k < argc - 1; k++) argv[k] = argv[k + 1];   /* argv[0] zostaje */
        argc--;
    }
    if (argc <= 1) {
        kprintf("--- Shell mode ---\n");
        app_name = shell_run();  /* skanuje *.EXE, wyswietla UI + ikony, zwraca wybrany plik */
        if (!app_name) { set_text_mode(); return 0; }   /* Esc */
    } else {
        app_name = argv[1];
    }
    kprintf("App: %s\n", app_name);

    /* 0b. Pobierz font BIOS 8x16 */
    kprintf("--- Font init ---\n");
    font_init();

    /* 1. Inicjalizacja bufora thunkow i IDT */
    kprintf("--- Init INT 3F ---\n");
    init_int3f(get_int3f_off());

    /* 2. Loader modulow: katalog systemowy = katalog LOADER.EXE (KERNEL/USER/GDI,
     *    WIN16API.DB), bufory DOS alokowane z wyprzedzeniem */
    if (mod_init(argv[0], g_thunk_phys, g_thunk_seg, g_thunk_size) != 0)
        return 1;

    /* 5. Alokuj wszystkie bufory DOS z wyprzedzeniem (przed file I/O z DLL/SKI).
     *    Kolejnosc: PSP, KCB, bufor bitmap (SEL_BITMAPS), globalny heap.
     *    C runtime podczas fopen/fread moze rosic arene DOS, wiec alokujemy wczesnie. */
    {
        unsigned      kcb_seg, heap_seg, psp_seg, bmp_seg;
        unsigned long heap_phys;
        KCB_LAYOUT __far *kcb;

        /* Fake PSP: 16 paragrafow = 256 B, wyzerowane; [PSP:0x2C]=0 (no env) */
        if (_dos_allocmem(16, &psp_seg) != 0) {
            kprintf("ERROR: alloc PSP\n"); return 1;
        }
        {
            unsigned char __far *p = MK_FP(psp_seg, 0);
            unsigned i;
            for (i = 0; i < 256; i++) p[i] = 0;
        }
        g_psp_phys = (unsigned long)psp_seg << 4;
        kprintf("PSP: seg=0x%04X phys=0x%05lX\n", psp_seg, g_psp_phys);

        if (_dos_allocmem(128, &kcb_seg) != 0) {  /* 128 paragrafow = 2KB (kolejki wejscia od 1024) */
            kprintf("ERROR: alloc KCB\n"); return 1;
        }
        g_kcb_phys = (unsigned long)kcb_seg << 4;
        kprintf("KCB: seg=0x%04X phys=0x%05lX\n", kcb_seg, g_kcb_phys);

        /* Bufor bitmap: 56KB alokowany tylko dla standalone SKI.EXE (niepotrzebny w Calmira) */
        if (strstr(app_name, "SKI") != NULL || strstr(app_name, "ski") != NULL) {
            if (_dos_allocmem(BMP_BUF_PARA, &bmp_seg) != 0) {
                kprintf("ERROR: alloc bitmap buffer (%u para)\n", BMP_BUF_PARA); return 1;
            }
            g_bitmaps_phys = (unsigned long)bmp_seg << 4;
            kprintf("BmpBuf: seg=0x%04X phys=0x%05lX size=%uB\n",
                    bmp_seg, g_bitmaps_phys, (unsigned)BMP_BUF_PARA * 16u);
        } else {
            g_bitmaps_phys = 0;
        }

        /* 14b/14c: GlobalHeap z pamieci rozszerzonej (XMS >1MB) jezeli dostepna.
         * Fallback do konwencjonalnej pamieci DOS gdy ext_mem_kb < 64. */
        if (g_ext_mem_kb >= 64) {
            /* XMS: bazowy adres = 0x100000 (1MB), koniec = 0x100000 + ext_mem_kb*1024 */
            heap_phys = 0x100000UL;
            kprintf("GlobalHeap: XMS phys=0x%06lX..0x%06lX size=%luKB\n",
                    heap_phys, heap_phys + g_ext_mem_kb * 1024UL, g_ext_mem_kb);
        } else {
            /* Fallback: konwencjonalna pamiec DOS */
            if (_dos_allocmem((unsigned)(GLOBAL_HEAP_SIZE / 16), &heap_seg) != 0) {
                kprintf("ERROR: alloc global heap\n"); return 1;
            }
            heap_phys = (unsigned long)heap_seg << 4;
            kprintf("GlobalHeap: DOS seg=0x%04X phys=0x%05lX size=%luKB\n",
                    heap_seg, heap_phys, GLOBAL_HEAP_SIZE / 1024UL);
        }

        kcb = (KCB_LAYOUT __far *)MK_FP(kcb_seg, 0);
        kcb->app_hinstance  = 0;                 /* ustawiane po zaladowaniu apki */
        kcb->next_dyn_sel   = GDYN_FIRST_SEL;    /* 0x130 */
        kcb->heap_phys      = heap_phys;
        kcb->heap_next      = heap_phys;
        kcb->heap_end       = (g_ext_mem_kb >= 64)
                              ? (0x100000UL + g_ext_mem_kb * 1024UL)
                              : (heap_phys + GLOBAL_HEAP_SIZE);
        kcb->local_heap_off = 0x1000;            /* 4KB after start of data seg = past BSS */
        kcb->rsc_nblocks    = 0;
        /* Zeruj bufor klawiatury (head=tail=0, buf=0) i pola myszy */
        {
            unsigned char __far *kb = (unsigned char __far *)MK_FP(kcb_seg, 272);
            int ki;
            for (ki = 0; ki < 19; ki++) kb[ki] = 0;  /* 272..290 (klaw 10B + mysz 9B) */
            for (ki = 1024 - 272; ki < 2048 - 272; ki++) kb[ki] = 0;   /* kolejki wejscia */
        }
        kprintf("KCB init: hInst=0x%04X dyn_sel=0x%04X heap=0x%06lX..0x%06lX\n",
                kcb->app_hinstance, kcb->next_dyn_sel,
                kcb->heap_phys, kcb->heap_end);
    }

    /* 6. Petla shell: laduj i uruchamiaj aplikacje; wraca do shella po zamknieciu
     *    (20e). Przy argc>1: jeden przebieg, potem exit. */
    for (;;) {
        unsigned kcb_rm = (unsigned)(g_kcb_phys >> 4);
        KCB_LAYOUT __far *kcb_ptr = (KCB_LAYOUT __far *)MK_FP(kcb_rm, 0);

        kprintf("--- Ladowanie %s ---\n", app_name);
        mod_reset();
        if (mod_load_app(app_name) != 0) return 1;
        g_has_data = g_app_ds != 0;

        kprintf("--- Ladowanie zasobow NE: %s ---\n", app_name);
        rc_init(app_name);
        rc_load_all(g_kcb_phys, g_bitmaps_phys);
        {
            int rc_ok = rc_copy_menu_to_kcb(g_kcb_phys);
            unsigned char __far *kd = (unsigned char __far *)MK_FP((unsigned)(g_kcb_phys >> 4), 0);
            kprintf("rc_copy_menu=%d KCB[292]=%u\n", rc_ok, kd[292]);
        }

        g_orig_cs = get_cs();
        g_orig_ss = get_ss();
        g_orig_sp = get_sp();
        g_cs_phys = (unsigned long)g_orig_cs << 4;

        /* Reset KCB pomiedzy uruchomieniami: tick, keyboard, mouse,
         * dynamiczne selektory GlobalAlloc i wskazniki heapow */
        kcb_ptr->tick_ms        = 55UL;    /* GetTickCount > 0 juz przed pierwszym IRQ0 */
        kcb_ptr->app_hinstance  = g_app_ds;
        kcb_ptr->next_dyn_sel   = mod_next_sel();      /* GlobalAlloc: za segmentami modulow */
        kcb_ptr->heap_phys      = mod_xms_end();       /* i za ich danymi w XMS */
        kcb_ptr->heap_next      = kcb_ptr->heap_phys;
        kcb_ptr->local_heap_off = 0x1000;
        *(unsigned short __far *)MK_FP(kcb_rm, KCB_DYN_MAX_OFF) = GDT_TOTAL_SIZE - 8;
        kcb_set_ext(kcb_rm, argc, argv);
        kprintf("KCB: hInst=%04X dyn_sel=%04X..%04X heap=%06lX..%06lX\n",
                kcb_ptr->app_hinstance, kcb_ptr->next_dyn_sel, GDT_TOTAL_SIZE - 8,
                kcb_ptr->heap_phys, kcb_ptr->heap_end);
        {
            unsigned char __far *kb = (unsigned char __far *)MK_FP(kcb_rm, 272);
            int ki;
            for (ki = 0; ki < 19; ki++) kb[ki] = 0;
            for (ki = 1024 - 272; ki < 2048 - 272; ki++) kb[ki] = 0;
        }

        init_mouse_ps2();

        vesa_set_lfb();  /* przelacz VESA z banked na LFB mode przed PM */
        kprintf("LFB=0x%08lX pitch=%u font=0x%06lX -> wchodze w PM\n",
                g_lfb_phys, g_vesa_pitch, g_font_phys);
        phantom_b_hide();
        pm_call_app();
        phantom_b_restore();
        shutdown_mouse_ps2();
        {
            unsigned maxpara = 0;
            _dos_allocmem(0xFFFF, &maxpara);   /* zawodzi, zwraca najwiekszy wolny blok */
            kprintf("App zakonczyla dzialanie. DOS free=%luB\n", (unsigned long)maxpara * 16UL);
        }

        /* Tryb bezposredni: jeden przebieg, wroc do DOS w trybie tekstowym */
        if (argc > 1) {
            set_text_mode();
            return g_exit_code;
        }

        /* Tryb shell: przywroc tryb banked dla shella, wyczysc stan DLL,
         * pokaz liste ponownie i wybierz nastepna apke */
        vesa_init();
        app_name = shell_run();
        if (!app_name) { set_text_mode(); break; }   /* Esc */
    }
    return 0;
}
