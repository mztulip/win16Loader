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
 * VESA
 * ============================================================ */
unsigned long  g_lfb_phys   = 0;   /* eksport do pm_call.asm */
unsigned short g_vesa_pitch = 0;
unsigned long  g_font_phys  = 0;   /* adres fizyczny tablicy fontow 8x16 (256*16 B) */

static void vesa_init(void)
{
    union REGS   r;
    struct SREGS s;
    unsigned     mib_seg;
    unsigned char __far *mib;
    unsigned short mode = 0x0112;   /* 640x480x24bpp */

    /* Alokuj 256B na VESA Mode Info Block */
    if (_dos_allocmem(16, &mib_seg) != 0) {
        kprintf("VESA: alloc MIB failed\n");
        return;
    }
    mib = MK_FP(mib_seg, 0);

    /* INT 10h AX=4F01h CX=tryb, ES:DI=bufor mode info */
    r.x.ax = 0x4F01;
    r.x.cx = mode;
    r.x.di = 0;
    segread(&s);
    s.es   = mib_seg;
    int86x(0x10, &r, &r, &s);

    if (r.x.ax != 0x004F) {
        kprintf("VESA: 4F01h failed (ax=0x%04X), tryb 0x%04X\n", r.x.ax, mode);
        _dos_freemem(mib_seg);
        return;
    }

    /* PhysBasePtr na offsecie 0x28 w bloku MIB */
    g_lfb_phys   = *(unsigned long  __far *)(mib + 0x28);
    g_vesa_pitch = *(unsigned short __far *)(mib + 0x10);
    kprintf("VESA MIB: LFB=0x%08lX pitch=%u\n", g_lfb_phys, g_vesa_pitch);

    _dos_freemem(mib_seg);

    /* INT 10h AX=4F02h BX=tryb (bez bit14 = banked mode dla shell)
     * LFB wlaczamy pozniej przez vesa_set_lfb() przed wejsciem w PM. */
    r.x.ax = 0x4F02;
    r.x.bx = mode;   /* banked mode — 0xA000 window dostepny */
    int86(0x10, &r, &r);

    if (r.x.ax != 0x004F) {
        kprintf("VESA: 4F02h failed (ax=0x%04X)\n", r.x.ax);
        g_lfb_phys = 0;
        return;
    }
    kprintf("VESA: tryb 0x%04X ustawiony (banked), LFB=0x%08lX pitch=%u\n",
            mode, g_lfb_phys, g_vesa_pitch);
}

/* Przelacz VESA na LFB mode (bit14) — wywolac przed wejsciem w PM */
static void vesa_set_lfb(void)
{
    union REGS r;
    r.x.ax = 0x4F02;
    r.x.bx = 0x0112 | 0x4000;   /* LFB mode */
    int86(0x10, &r, &r);
    if (r.x.ax == 0x004F)
        kprintf("VESA: LFB mode enabled\n");
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
extern unsigned short get_int3f_off(void);
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

static void init_mouse_ps2(void)
{
    unsigned char resp;

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
static void shutdown_mouse_ps2(void)
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

static void kcb_set_ext(unsigned kcb_rm, int argc, char *argv[])
{
    unsigned short __far *w = (unsigned short __far *)MK_FP(kcb_rm, 0);
    char __far *c = (char __far *)MK_FP(kcb_rm, 0);
    struct find_t ft;
    int i;
    w[KCB_RMINT_OFF / 2]     = get_rmint_off();
    w[KCB_RMBUF_SEG / 2]     = g_rmbuf_seg;
    w[KCB_THUNK_NEXT / 2]    = mod_thunk_next();
    w[KCB_THUNK_LIMIT / 2]   = g_thunk_size;
    w[KCB_ENV_SEG / 2]       = *(unsigned short __far *)MK_FP(_psp, 0x2C);
    w[KCB_APP_STACK / 2]     = g_app_stack;
    w[KCB_APP_SP / 2]        = g_init_sp;
    w[KCB_APP_HEAP / 2]      = g_app_heap;
    w[KCB_APP_HEAPSTART / 2] = g_app_heapstart;
    w[KCB_TRACE / 2]         = g_trace;
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

static void set_text_mode(void)
{
    union REGS r;
    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char *argv[])
{
    const char *app_name;
    serial_init();
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

        /* Bufor bitmap: 56KB = 3584 paragrafow; musi byc alokowany PRZED file I/O */
        if (_dos_allocmem(BMP_BUF_PARA, &bmp_seg) != 0) {
            kprintf("ERROR: alloc bitmap buffer (%u para)\n", BMP_BUF_PARA); return 1;
        }
        g_bitmaps_phys = (unsigned long)bmp_seg << 4;
        kprintf("BmpBuf: seg=0x%04X phys=0x%05lX size=%uB\n",
                bmp_seg, g_bitmaps_phys, (unsigned)BMP_BUF_PARA * 16u);

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
        kcb_ptr->tick_ms        = 0UL;
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
        pm_call_app();
        shutdown_mouse_ps2();
        {
            unsigned maxpara = 0;
            _dos_allocmem(0xFFFF, &maxpara);   /* zawodzi, zwraca najwiekszy wolny blok */
            kprintf("App zakonczyla dzialanie. DOS free=%luB\n", (unsigned long)maxpara * 16UL);
        }

        /* Tryb bezposredni: jeden przebieg, wroc do DOS w trybie tekstowym */
        if (argc > 1) {
            set_text_mode();
            break;
        }

        /* Tryb shell: przywroc tryb banked dla shella, wyczysc stan DLL,
         * pokaz liste ponownie i wybierz nastepna apke */
        vesa_init();
        app_name = shell_run();
        if (!app_name) { set_text_mode(); break; }   /* Esc */
    }
    return 0;
}
