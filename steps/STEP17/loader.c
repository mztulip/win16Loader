/*
 * loader.c - STEP16: klawiatura (IRQ1 -> WM_KEYDOWN)
 *
 * Bez zmian vs STEP15.
 *
 * Laduje KERNEL.EXE, USER.EXE, GDI.EXE z poprawnymi numerami ordynalow
 * zgodnymi z Windows 3.1 SDK, nastepnie WIN16APP.EXE jako test.
 *
 * Kompilacja:
 *   wcl -ml -l=dos -q loader.c pm_call.obj serial.obj -fe=loader.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <i86.h>
#include "serial.h"

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

/* ============================================================
 * Struktury NE
 * ============================================================ */
#pragma pack(push, 1)

typedef struct {
    unsigned short e_magic;
    unsigned short e_cblp;
    unsigned short e_cp;
    unsigned short e_crlc;
    unsigned short e_cparhdr;
    unsigned short e_minalloc;
    unsigned short e_maxalloc;
    unsigned short e_ss;
    unsigned short e_sp;
    unsigned short e_csum;
    unsigned short e_ip;
    unsigned short e_cs;
    unsigned short e_lfarlc;
    unsigned short e_ovno;
    unsigned short e_res[4];
    unsigned short e_oemid;
    unsigned short e_oeminfo;
    unsigned short e_res2[10];
    unsigned long  e_lfanew;
} MZ_HEADER;

typedef struct {
    unsigned short ne_magic;
    unsigned char  ne_ver;
    unsigned char  ne_rev;
    unsigned short ne_enttab;
    unsigned short ne_cbenttab;
    unsigned long  ne_crc;
    unsigned short ne_flags;
    unsigned short ne_autodata;
    unsigned short ne_heap;
    unsigned short ne_stack;
    unsigned short ne_ip;
    unsigned short ne_cs;
    unsigned short ne_sp;
    unsigned short ne_ss;
    unsigned short ne_cseg;
    unsigned short ne_cmod;
    unsigned short ne_cbnrestab;
    unsigned short ne_segtab;
    unsigned short ne_rsrctab;
    unsigned short ne_restab;
    unsigned short ne_modtab;
    unsigned short ne_imptab;
    unsigned long  ne_nrestab;
    unsigned short ne_cmovent;
    unsigned short ne_align;
    unsigned short ne_cres;
    unsigned char  ne_exetyp;
    unsigned char  ne_addflags;
    unsigned short ne_gangstart;
    unsigned short ne_ganglength;
    unsigned short ne_swaparea;
    unsigned short ne_expver;
} NE_HEADER;

typedef struct {
    unsigned short ns_sector;
    unsigned short ns_cbseg;
    unsigned short ns_flags;
    unsigned short ns_minalloc;
} NE_SEG_ENTRY;

/* Rekord relokacji NE (8 bajtow) */
typedef struct {
    unsigned char  addr_type;   /* 0=LOBYTE 2=SEG16 3=FAR_PTR 5=OFF16 */
    unsigned char  reloc_type;  /* bity 0-1: typ, bit 2: ADDITIVE */
    unsigned short offset;
    unsigned short target1;     /* nr segmentu (INTERNALREF) lub mod_idx (IMPORTORDINAL) */
    unsigned short target2;     /* 0 (INTERNALREF) lub ordinal (IMPORTORDINAL) */
} NE_RELOC;

#pragma pack(pop)

/* Typy relokacji (bity 0-1 pola reloc_type) */
#define RTYPE_INTERNALREF    0
#define RTYPE_IMPORTORDINAL  1
#define RTYPE_ADDITIVE       0x04   /* bit 2 */

/* Typy adresow (addr_type) */
#define ATYPE_SEG16    2   /* 2 bajty selektora */
#define ATYPE_FAR_PTR  3   /* 4 bajty: offset + selector */

/* ns_flags */
#define NSRELOC  0x0100   /* segment ma tablice relokacji (bit 8) */

/* Selektory GDT dla DLL (dynamiczne) */
#define SEL_DLL_CODE(i)  ((unsigned short)(0x48 + (i) * 0x10))
#define SEL_DLL_DATA(i)  ((unsigned short)(0x50 + (i) * 0x10))
#define SEL_THUNK        ((unsigned short)0x88)  /* segment thunkow INT 3F */
#define SEL_APP_CODE     ((unsigned short)0x30)  /* selektor kodu apki */
#define SEL_APP_DATA     ((unsigned short)0x40)  /* selektor danych apki */

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

    /* INT 10h AX=4F02h BX=tryb|0x4000 (bit14=LFB) */
    r.x.ax = 0x4F02;
    r.x.bx = mode | 0x4000;
    int86(0x10, &r, &r);

    if (r.x.ax != 0x004F) {
        kprintf("VESA: 4F02h failed (ax=0x%04X)\n", r.x.ax);
        g_lfb_phys = 0;
        return;
    }
    kprintf("VESA: tryb 0x%04X ustawiony, LFB=0x%08lX pitch=%u\n",
            mode, g_lfb_phys, g_vesa_pitch);
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
 * Kazdy thunk = 7 bajtow: CD 3F dll_idx off_lo off_hi ord_lo ord_hi
 * ============================================================ */
#define MAX_THUNKS  256
#define THUNK_SIZE   7

static unsigned       g_thunk_seg  = 0;  /* segment DOS dla thunkow */
static unsigned short g_thunk_off  = 0;  /* nastepny wolny offset w segmencie */

unsigned long  g_thunk_phys = 0;   /* eksport do pm_call.asm */
unsigned short g_thunk_size = 0;

/* emit_thunk: generuje thunk dla dll_idx / func_off / ordinal, zwraca offset w SEG_THUNK */
static unsigned short emit_thunk(unsigned char dll_idx, unsigned short func_off,
                                 unsigned short ordinal)
{
    unsigned char __far *p;
    unsigned short       off = g_thunk_off;

    if (g_thunk_seg == 0) return 0;

    p    = MK_FP(g_thunk_seg, off);
    p[0] = 0xCD;               /* INT */
    p[1] = 0x3F;               /* 0x3F */
    p[2] = dll_idx;
    p[3] = (unsigned char)(func_off & 0xFF);
    p[4] = (unsigned char)(func_off >> 8);
    p[5] = (unsigned char)(ordinal & 0xFF);
    p[6] = (unsigned char)(ordinal >> 8);

    g_thunk_off += THUNK_SIZE;
    kprintf("THUNK dll=%u ord=%u func=0x%04X @0x88:%04X\n",
            dll_idx, ordinal, func_off, off);
    return off;
}

/* ============================================================
 * IDT: 64 wpisow (wektory 0x00-0x3F)
 * Wpis 0x3F = 16-bit interrupt gate -> int3f_handler w SEL_CODE16
 * ============================================================ */
#define IDT_ENTRIES  64
#define SEL_CODE16   0x20   /* musi zgadzac sie z pm_call.asm */

unsigned long g_idt_phys = 0;   /* eksport do pm_call.asm */

extern unsigned short get_int21_off(void);
extern unsigned short get_irq0_off(void);
extern unsigned short get_irq12_off(void);
extern unsigned short get_gpf_off(void);
extern unsigned short get_exc00_off(void);
extern unsigned short get_exc06_off(void);
extern unsigned short get_exc0B_off(void);
extern unsigned short get_exc0C_off(void);
extern unsigned short get_exc0D_off(void);
extern unsigned short get_exc0E_off(void);

static void init_int3f(unsigned short handler_off)
{
    unsigned       idt_seg;
    unsigned char __far *p;
    int            i;
    unsigned short paragraphs;
    unsigned short h21_off;
    unsigned short hirq0_off;
    unsigned short hirq12_off;
    unsigned short hgpf_off;

    /* Alokuj blok na thunki */
    paragraphs = (MAX_THUNKS * THUNK_SIZE + 15) / 16;
    if (_dos_allocmem(paragraphs, &g_thunk_seg) != 0) {
        kprintf("ERROR: alloc thunk buf\n"); return;
    }
    g_thunk_phys = (unsigned long)g_thunk_seg << 4;
    g_thunk_size = (unsigned short)(paragraphs * 16);
    g_thunk_off  = 0;
    kprintf("Thunk buf: phys=0x%05lX size=%u\n", g_thunk_phys, g_thunk_size);

    /* Alokuj blok na IDT (64 * 8 = 512 B = 32 parag) */
    paragraphs = (IDT_ENTRIES * 8 + 15) / 16;
    if (_dos_allocmem(paragraphs, &idt_seg) != 0) {
        kprintf("ERROR: alloc IDT\n"); return;
    }
    g_idt_phys = (unsigned long)idt_seg << 4;

    /* Zeruj caly IDT (null descriptors = P=0, ignorowane) */
    p = MK_FP(idt_seg, 0);
    for (i = 0; i < IDT_ENTRIES * 8; i++) p[i] = 0;

    /* Wpis 0x3F: 16-bit interrupt gate -> int3f_handler w SEL_CODE16 */
    p = MK_FP(idt_seg, 0x3F * 8);
    p[0] = (unsigned char)(handler_off & 0xFF);
    p[1] = (unsigned char)(handler_off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;

    /* Wpis 0x21: INT 21h handler (DOS calls from PM) */
    h21_off = get_int21_off();
    p = MK_FP(idt_seg, 0x21 * 8);
    p[0] = (unsigned char)(h21_off & 0xFF);
    p[1] = (unsigned char)(h21_off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;

    /* IDT[0x20]: IRQ0 po remapie PIC (Windows 3.1: IRQ0 -> INT 0x20, nie INT 0x08) */
    hirq0_off = get_irq0_off();
    p = MK_FP(idt_seg, 0x20 * 8);
    p[0] = (unsigned char)(hirq0_off & 0xFF);
    p[1] = (unsigned char)(hirq0_off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;

    /* IDT[0x2C]: IRQ12 (mysz PS/2) po remapie slave PIC (slave -> INT 0x28..0x2F) */
    hirq12_off = get_irq12_off();
    p = MK_FP(idt_seg, 0x2C * 8);
    p[0] = (unsigned char)(hirq12_off & 0xFF);
    p[1] = (unsigned char)(hirq12_off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;

    /* IDT[0x00]: #DE Divide Error */
    /* IDT[0x06]: #UD Invalid Opcode */
    /* IDT[0x0B]: #NP Segment Not Present */
    /* IDT[0x0C]: #SS Stack Fault */
    /* IDT[0x0D]: #GP General Protection (panic screen) */
    /* IDT[0x0E]: #PF Page Fault */
    {
        static const unsigned char exc_vecs[6]  = { 0x00, 0x06, 0x0B, 0x0C, 0x0D, 0x0E };
        unsigned short exc_offs[6];
        int ei;
        exc_offs[0] = get_exc00_off();
        exc_offs[1] = get_exc06_off();
        exc_offs[2] = get_exc0B_off();
        exc_offs[3] = get_exc0C_off();
        exc_offs[4] = get_exc0D_off();
        exc_offs[5] = get_exc0E_off();
        for (ei = 0; ei < 6; ei++) {
            p = MK_FP(idt_seg, (int)exc_vecs[ei] * 8);
            p[0] = (unsigned char)(exc_offs[ei] & 0xFF);
            p[1] = (unsigned char)(exc_offs[ei] >> 8);
            p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
            p[3] = (unsigned char)(SEL_CODE16 >> 8);
            p[4] = 0;
            p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
            p[6] = 0;
            p[7] = 0;
        }
    }
    hgpf_off = get_gpf_off();  /* nieuzywany juz w IDT, ale zachowany dla zgodnosci */

    kprintf("IDT: phys=0x%05lX int3f=0x%04X int21=0x%04X irq0@0x20=0x%04X irq12@0x2C=0x%04X\n",
            g_idt_phys, handler_off, h21_off, hirq0_off, hirq12_off);
}

/* ============================================================
 * Tabela zaladowanych DLL
 * ============================================================ */
#define MAX_EXPORTS  512  /* musi pomiescic ordinal 420 (USER._wsprintf) */
#define MAX_MODULES  4
#define MAX_DLL_SEGS 8

typedef struct {
    char           name[16];
    unsigned long  code_phys;
    unsigned short code_size;
    unsigned short selector_code;
    unsigned long  data_phys;
    unsigned short data_size;
    unsigned short selector_data;
    unsigned short has_data;
    unsigned short num_exports;
    unsigned short exports[MAX_EXPORTS];
} DLL_MODULE;

static DLL_MODULE g_dll[MAX_MODULES];

/* Tablice dla pm_call.asm (indeks = numer DLL, 0-based) */
unsigned long  g_dll_code_phys[MAX_MODULES];
unsigned short g_dll_code_size[MAX_MODULES];
unsigned long  g_dll_data_phys[MAX_MODULES];
unsigned short g_dll_data_size[MAX_MODULES];
unsigned short g_dll_has_data[MAX_MODULES];
unsigned short g_ndll = 0;

/* ============================================================
 * Globalne dla pm_call.asm (NE app)
 * ============================================================ */
unsigned long  g_app_phys;
unsigned short g_code_size;
unsigned short g_entry_ip;
unsigned long  g_cs_phys;
unsigned short g_orig_cs;
unsigned short g_gdt_off_c;     /* offset tablicy GDT w segmencie kodu (ustawiany z asm) */
unsigned short g_orig_ss;
unsigned short g_orig_sp;
unsigned long  g_app_data_phys;
unsigned short g_data_size;
unsigned short g_has_data;
unsigned short g_init_sp;

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
extern unsigned short get_int3f_off(void);  /* zwraca offset int3f_handler w SEL_CODE16 */

/* ============================================================
 * PS/2 mouse init (real mode, przed wejsciem w PM)
 * Sekwencja: reset (0xFF) -> ACK+0xAA+0x00, enable streaming (0xF4) -> ACK
 * ============================================================ */
static void ps2_wait_write(void)
{
    unsigned char s;
    int t;
    for (t = 0; t < 100000; t++) {
        s = inp(0x64);
        if (!(s & 0x02)) return;   /* bit 1 = input buffer full; czekaj az 0 */
    }
}

static unsigned char ps2_read_byte(void)
{
    int t;
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

unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];
unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];
unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * parse_entry_table
 * ============================================================ */
/* entry_segs[n]: segment number (1-based) for entry n+1; can be NULL if not needed */
static void parse_entry_table(FILE *f, long ne_off, NE_HEADER *ne,
                               unsigned short *exports,
                               unsigned char  *entry_segs,
                               unsigned short *num_exports)
{
    long           et_off = ne_off + ne->ne_enttab;
    unsigned short ordinal = 0;
    unsigned char  count, seg, flags, mov_seg;
    unsigned char  junk[2];
    unsigned short off;
    int j;

    *num_exports = 0;
    fseek(f, et_off, SEEK_SET);

    while (1) {
        if (fread(&count, 1, 1, f) != 1 || count == 0) break;
        if (fread(&seg, 1, 1, f) != 1) break;

        if (seg == 0x00) {
            /* empty bundle: skip ordinals */
            ordinal += count;
        } else if (seg == 0xFF) {
            /* moveable entries: flags(1) + CD3F(2) + seg(1) + offset(2) = 6B */
            for (j = 0; j < count; j++) {
                if (fread(&flags,   1, 1, f) != 1) break;
                if (fread(junk,     2, 1, f) != 1) break;  /* CD 3F */
                if (fread(&mov_seg, 1, 1, f) != 1) break;  /* segment number */
                if (fread(&off,     2, 1, f) != 1) break;
                if (ordinal < MAX_EXPORTS) {
                    exports[ordinal] = off;
                    if (entry_segs) entry_segs[ordinal] = mov_seg;
                    if (ordinal + 1 > *num_exports)
                        *num_exports = ordinal + 1;
                }
                ordinal++;
            }
        } else {
            /* fixed segment entries: flags(1) + offset(2) = 3B */
            for (j = 0; j < count; j++) {
                if (fread(&flags, 1, 1, f) != 1) break;
                if (fread(&off,   2, 1, f) != 1) break;
                if (ordinal < MAX_EXPORTS) {
                    exports[ordinal] = off;
                    if (entry_segs) entry_segs[ordinal] = seg;  /* bundle seg */
                    if (ordinal + 1 > *num_exports)
                        *num_exports = ordinal + 1;
                }
                ordinal++;
            }
        }
    }
}

/* ============================================================
 * str_nocase_eq: case-insensitive string compare (returns 1 if equal)
 * ============================================================ */
static int str_nocase_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (char)(*a >= 'a' && *a <= 'z' ? *a - 32 : *a);
        char cb = (char)(*b >= 'a' && *b <= 'z' ? *b - 32 : *b);
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

/* ============================================================
 * parse_mod_table: parsuje Module Reference Table NE.
 *
 * ne_modtab: tablica ne_cmod offsotow (WORD) do ne_imptab.
 * ne_imptab: stringa z prefixem dlugosci (1B + znaki, BEZ null).
 *
 * Wynik: mod_map[i] = indeks w g_dll[] dla i-tego modulu (0-based),
 *         lub -1 jesli nazwa nie znaleziona wsrod zaladowanych DLL.
 * ============================================================ */
#define MAX_MOD_REFS 16
static void parse_mod_table(FILE *f, long ne_off, NE_HEADER *ne,
                              int *mod_map)
{
    unsigned int i;
    for (i = 0; i < ne->ne_cmod && i < MAX_MOD_REFS; i++) {
        unsigned short name_off;
        unsigned char  name_len;
        char           mod_name[17];
        unsigned int   j;

        fseek(f, ne_off + ne->ne_modtab + (long)i * 2, SEEK_SET);
        if (fread(&name_off, 2, 1, f) != 1) { mod_map[i] = -1; continue; }

        fseek(f, ne_off + ne->ne_imptab + name_off, SEEK_SET);
        if (fread(&name_len, 1, 1, f) != 1) { mod_map[i] = -1; continue; }
        if (name_len > 16) name_len = 16;
        if (fread(mod_name, name_len, 1, f) != 1) { mod_map[i] = -1; continue; }
        mod_name[name_len] = 0;

        mod_map[i] = -1;
        for (j = 0; j < g_ndll; j++) {
            if (str_nocase_eq(g_dll[j].name, mod_name)) {
                mod_map[i] = (int)j;
                break;
            }
        }
        kprintf("  ModRef[%u] '%s' -> dll[%d]\n", i+1, mod_name, mod_map[i]);
    }
}

/* ============================================================
 * apply_fixups: patchuje tablice relokacji jednego segmentu
 *
 * mod_map[i]: mapuje 1-based mod_idx z rekordu fixupa na indeks g_dll[].
 *   Wymagany bo kolejnosc moduli w Module Reference Table apki
 *   moze sie roznic od kolejnosci ladowania DLL przez nasz loader.
 * ============================================================ */
static void apply_fixups(FILE *f, long reloc_file_off,
                          unsigned char __far *seg_data,
                          unsigned short seg_size,
                          unsigned short *seg_sels,
                          unsigned short num_segs,
                          int *mod_map,
                          unsigned short num_mods,
                          unsigned short *entry_offs,   /* from exports[], or NULL */
                          unsigned char  *entry_segs,   /* segment per entry,  or NULL */
                          unsigned short  num_entries)
{
    unsigned short count, i;
    NE_RELOC       rec;
    unsigned char  rtype, additive;
    /* Deferred stub patches: must not write until all chain reads are done,
     * because a stub at offset X may overlap chain terminator 0xFFFF bytes
     * that another fixup record still needs to read. */
    struct { unsigned short orig_off, skip, sel; } pending_stubs[64];
    int num_pending_stubs = 0;

    fseek(f, reloc_file_off, SEEK_SET);
    if (fread(&count, 2, 1, f) != 1) return;
    kprintf("  Fixups: %u rekordow\n", count);

    for (i = 0; i < count; i++) {
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;

        rtype    = rec.reloc_type & 0x03;
        additive = (rec.reloc_type & RTYPE_ADDITIVE) ? 1 : 0;

        if (rtype == RTYPE_INTERNALREF) {
            if (rec.addr_type == ATYPE_SEG16) {
                unsigned short seg_num = rec.target1;
                unsigned short sel = 0;

                if (seg_num == 0xFF) {
                    /* movable entry: look up entry table to find the segment */
                    if (entry_segs && rec.target2 >= 1 && rec.target2 <= num_entries) {
                        unsigned char eseg = entry_segs[rec.target2 - 1];
                        if (eseg >= 1 && eseg <= num_segs)
                            sel = seg_sels[eseg - 1];
                    }
                } else if (seg_num >= 1 && seg_num <= num_segs) {
                    sel = seg_sels[seg_num - 1];
                }

#if DEBUG_FIXUPS
                kprintf("  [%u] INTERNALREF SEG16 seg=%u entry=%u sel=0x%04X @ 0x%04X\n",
                        i, seg_num, rec.target2, sel, rec.offset);
#endif

                if (sel != 0 && rec.offset + 1 < seg_size) {
                    if (additive) {
                        unsigned short cur;
                        cur  = (unsigned short)seg_data[rec.offset];
                        cur |= (unsigned short)seg_data[rec.offset + 1] << 8;
                        sel += cur;
                    }
                    seg_data[rec.offset]     = (unsigned char)(sel & 0xFF);
                    seg_data[rec.offset + 1] = (unsigned char)(sel >> 8);
                }
            } else if (rec.addr_type == ATYPE_FAR_PTR) {
                /* FAR_PTR: 4 bytes = offset16 + selector16
                 * target1 = segment number (1-based) OR 0xFF -> entry table lookup
                 * target2 = offset within segment OR entry ordinal (if target1=0xFF) */
                unsigned short seg_num = rec.target1;
                unsigned short tgt_off = rec.target2;
                unsigned short sel = 0;
                if (seg_num == 0xFF) {
                    /* moveable: look up entry table */
                    if (entry_offs && entry_segs && rec.target2 >= 1 && rec.target2 <= num_entries) {
                        unsigned char eseg = entry_segs[rec.target2 - 1];
                        unsigned short orig_off = entry_offs[rec.target2 - 1];
                        tgt_off = orig_off;
                        if (eseg >= 1 && eseg <= num_segs)
                            sel = seg_sels[eseg - 1];
                        seg_num = eseg;
                        /* Skip unpatched JMP FAR stub (EA FF FF 00 00) at entry point.
                         * Open Watcom emits this 5-byte stub for movable segment entries;
                         * real code follows after the stub (+ possible alignment bytes). */
                        if (tgt_off + 4 < seg_size &&
                            seg_data[tgt_off + 0] == 0xEA &&
                            seg_data[tgt_off + 1] == 0xFF &&
                            seg_data[tgt_off + 2] == 0xFF) {
                            unsigned short skip = tgt_off + 5;
                            while (skip < seg_size && seg_data[skip] == 0x00)
                                skip++;
                            /* Defer stub patch: record for later so we don't
                             * corrupt chain terminators that other fixup records
                             * still need to read from within the stub area. */
                            if (sel != 0 && orig_off + 4 < seg_size &&
                                num_pending_stubs < 64) {
                                pending_stubs[num_pending_stubs].orig_off = orig_off;
                                pending_stubs[num_pending_stubs].skip     = skip;
                                pending_stubs[num_pending_stubs].sel      = sel;
                                num_pending_stubs++;
                            }
                            tgt_off = skip;
                        }
                    }
                } else {
                    if (seg_num >= 1 && seg_num <= num_segs)
                        sel = seg_sels[seg_num - 1];
                }
#if DEBUG_FIXUPS
                kprintf("  [%u] INTERNALREF FAR_PTR seg=%u off=0x%04X sel=0x%04X @ 0x%04X\n",
                        i, seg_num, tgt_off, sel, rec.offset);
#endif
                if (additive) {
                    /* Additive: single location, add to current offset value */
                    if (rec.offset + 3 < seg_size) {
                        unsigned short cur;
                        cur  = (unsigned short)seg_data[rec.offset];
                        cur |= (unsigned short)seg_data[rec.offset + 1] << 8;
                        tgt_off += cur;
                        seg_data[rec.offset + 0] = (unsigned char)(tgt_off & 0xFF);
                        seg_data[rec.offset + 1] = (unsigned char)(tgt_off >> 8);
                        seg_data[rec.offset + 2] = (unsigned char)(sel & 0xFF);
                        seg_data[rec.offset + 3] = (unsigned char)(sel >> 8);
                    }
                } else {
                    /* Non-additive: follow fixup chain. Each location's low 2 bytes
                     * store the next chain offset (0xFFFF = end). Must read BEFORE
                     * patching. All chain members call the same tgt_off:sel. */
                    unsigned short cur_off2 = rec.offset;
                    unsigned int   chain_n  = 0;
                    while (cur_off2 + 3 < seg_size && chain_n++ < 4096) {
                        unsigned short next_off2 =
                            (unsigned short)seg_data[cur_off2] |
                            ((unsigned short)seg_data[cur_off2 + 1] << 8);
                        seg_data[cur_off2 + 0] = (unsigned char)(tgt_off & 0xFF);
                        seg_data[cur_off2 + 1] = (unsigned char)(tgt_off >> 8);
                        seg_data[cur_off2 + 2] = (unsigned char)(sel & 0xFF);
                        seg_data[cur_off2 + 3] = (unsigned char)(sel >> 8);
                        if (next_off2 == 0xFFFF) break;
                        cur_off2 = next_off2;
                    }
                }
            } else if (rec.addr_type == 5) {  /* ATYPE_OFF16 */
                /* OFF16: 2-byte offset only
                 * target1 = segment number OR 0xFF -> entry table lookup
                 * target2 = offset OR entry ordinal */
                unsigned short seg_num = rec.target1;
                unsigned short tgt_off = rec.target2;
                if (seg_num == 0xFF) {
                    if (entry_offs && rec.target2 >= 1 && rec.target2 <= num_entries)
                        tgt_off = entry_offs[rec.target2 - 1];
                }
#if DEBUG_FIXUPS
                kprintf("  [%u] INTERNALREF OFF16 seg=%u off=0x%04X @ 0x%04X\n",
                        i, seg_num, tgt_off, rec.offset);
#endif
                if (rec.offset + 1 < seg_size) {
                    if (additive) {
                        unsigned short cur_off;
                        cur_off  = (unsigned short)seg_data[rec.offset];
                        cur_off |= (unsigned short)seg_data[rec.offset + 1] << 8;
                        tgt_off += cur_off;
                    }
                    seg_data[rec.offset + 0] = (unsigned char)(tgt_off & 0xFF);
                    seg_data[rec.offset + 1] = (unsigned char)(tgt_off >> 8);
                }
            } else {
#if DEBUG_FIXUPS
                kprintf("  [%u] INTERNALREF addr_type=%u @ 0x%04X (pomijam)\n",
                        i, rec.addr_type, rec.offset);
#endif
            }

        } else if (rtype == RTYPE_IMPORTORDINAL) {
            unsigned short mod_idx = rec.target1;   /* 1-based w Module Reference Table */
            unsigned short ordinal = rec.target2;
            unsigned short func_off, thunk_off;
            unsigned char  dll_idx_byte;
            int            dll_idx;

#if DEBUG_FIXUPS
            kprintf("  [%u] IMPORTORDINAL mod=%u ord=%u @ 0x%04X\n",
                    i, mod_idx, ordinal, rec.offset);
#endif

            /* Uzyj mod_map do znalezienia wlasciwego g_dll[] */
            if (mod_idx < 1 || mod_idx > num_mods) {
                kprintf("    ERROR: mod_idx=%u poza mod_map\n", mod_idx);
                continue;
            }
            dll_idx = mod_map[mod_idx - 1];
            if (dll_idx < 0 || dll_idx >= (int)g_ndll) {
                kprintf("    ERROR: mod %u nie znaleziony\n", mod_idx);
                continue;
            }
            {
                DLL_MODULE *dll = &g_dll[dll_idx];
                if (ordinal < 1 || ordinal > dll->num_exports) {
                    kprintf("    ERROR: ordinal %u poza zakresem\n", ordinal);
                    continue;
                }
                func_off     = dll->exports[ordinal - 1];
                dll_idx_byte = (unsigned char)dll_idx;
                thunk_off    = emit_thunk(dll_idx_byte, func_off, ordinal);
#if DEBUG_FIXUPS
                kprintf("    -> %s.%u func=0x%04X thunk=0x88:%04X\n",
                        dll->name, ordinal, func_off, thunk_off);
#endif
            }

            if (rec.addr_type == ATYPE_FAR_PTR) {
                /* Follow fixup chain: each location stores offset of next
                 * location in low 2 bytes; 0xFFFF terminates the chain. */
                unsigned short cur_off = rec.offset;
                while (cur_off + 3 < seg_size) {
                    unsigned short next_off =
                        (unsigned short)seg_data[cur_off] |
                        ((unsigned short)seg_data[cur_off + 1] << 8);
                    seg_data[cur_off + 0] = (unsigned char)(thunk_off & 0xFF);
                    seg_data[cur_off + 1] = (unsigned char)(thunk_off >> 8);
                    seg_data[cur_off + 2] = (unsigned char)(SEL_THUNK & 0xFF);
                    seg_data[cur_off + 3] = (unsigned char)(SEL_THUNK >> 8);
                    if (next_off == 0xFFFF) break;
                    cur_off = next_off;
                }
            }

        } else {
            kprintf("  [%u] nieznany rtype=%u (pomijam)\n", i, rtype);
        }
    }

    /* Apply deferred stub patches now that all fixup chains have been read. */
    for (i = 0; i < (unsigned short)num_pending_stubs; i++) {
        unsigned short o = pending_stubs[i].orig_off;
        unsigned short s = pending_stubs[i].skip;
        unsigned short l = pending_stubs[i].sel;
        seg_data[o + 0] = 0xEA;
        seg_data[o + 1] = (unsigned char)(s & 0xFF);
        seg_data[o + 2] = (unsigned char)(s >> 8);
        seg_data[o + 3] = (unsigned char)(l & 0xFF);
        seg_data[o + 4] = (unsigned char)(l >> 8);
    }
}

/* ============================================================
 * load_ne_dll: laduje NE DLL
 * ============================================================ */
int load_ne_dll(const char *filename, const char *modname)
{
    FILE         *f;
    MZ_HEADER     mz;
    NE_HEADER     ne;
    DLL_MODULE   *dll;
    unsigned int  i;
    int           dll_idx;

    long             seg_reloc_off[MAX_DLL_SEGS];
    unsigned char __far *seg_data_ptr[MAX_DLL_SEGS];
    unsigned short   seg_file_size[MAX_DLL_SEGS];
    unsigned short   seg_has_reloc[MAX_DLL_SEGS];
    unsigned short   seg_sels[MAX_DLL_SEGS];

    if (g_ndll >= MAX_MODULES) {
        kprintf("ERROR: za duzo moduli\n");
        return -1;
    }
    dll_idx = g_ndll;
    dll     = &g_dll[dll_idx];

    f = fopen(filename, "rb");
    if (!f) { kprintf("ERROR: cannot open %s\n", filename); return -1; }

    if (fread(&mz, sizeof(mz), 1, f) != 1) goto err;
    if (mz.e_magic != 0x5A4D) { kprintf("ERROR: not MZ\n"); goto err; }
    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) { kprintf("ERROR: not NE\n"); goto err; }

    kprintf("DLL %s: segs=%u cs=%u autodata=%u align=%u\n",
            filename, ne.ne_cseg, ne.ne_cs, ne.ne_autodata, ne.ne_align);

    _fstrncpy(dll->name, modname, sizeof(dll->name) - 1);
    dll->name[sizeof(dll->name) - 1] = '\0';
    dll->has_data        = 0;
    dll->selector_code   = SEL_DLL_CODE(dll_idx);
    dll->selector_data   = 0;
    dll->num_exports     = 0;

    /* ---- Przejscie 1: laduj segmenty, przydzielaj selektory ---- */
    for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
        NE_SEG_ENTRY   seg;
        long           file_off;
        unsigned short file_size, alloc_size, paragraphs;
        unsigned       seg_addr;
        unsigned char __far *dst;
        unsigned char *tmp;

        fseek(f, mz.e_lfanew + ne.ne_segtab + (long)i * sizeof(NE_SEG_ENTRY), SEEK_SET);
        if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

        file_size  = seg.ns_cbseg ? seg.ns_cbseg : 0;
        alloc_size = seg.ns_minalloc ? seg.ns_minalloc : (file_size ? file_size : 0);
        if (file_size > alloc_size) alloc_size = file_size;

        kprintf("  Seg %u: sector=%u file=%u alloc=%u flags=0x%04X%s\n",
                i+1, seg.ns_sector, file_size, alloc_size, seg.ns_flags,
                (seg.ns_flags & NSRELOC) ? " RELOC" : "");

        seg_reloc_off[i] = 0;
        seg_data_ptr[i]  = 0;
        seg_file_size[i] = 0;
        seg_has_reloc[i] = 0;
        seg_sels[i]      = 0;

        if (alloc_size == 0) continue;

        paragraphs = (alloc_size + 15) / 16;
        if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
            kprintf("ERROR: _dos_allocmem seg %u\n", i+1);
            goto err;
        }

        dst      = MK_FP(seg_addr, 0);
        file_off = (long)seg.ns_sector << ne.ne_align;

        if (file_size > 0) {
            unsigned short k;
            tmp = (unsigned char *)malloc(file_size);
            if (!tmp) { kprintf("ERROR: malloc seg %u\n", i+1); goto err; }
            fseek(f, file_off, SEEK_SET);
            fread(tmp, file_size, 1, f);
            _fmemcpy(dst, tmp, file_size);
            free(tmp);
            for (k = file_size; k < alloc_size; k++) dst[k] = 0;
        } else {
            unsigned short k;
            for (k = 0; k < alloc_size; k++) dst[k] = 0;
        }

        seg_data_ptr[i]  = dst;
        seg_file_size[i] = file_size;
        seg_reloc_off[i] = file_off + file_size;
        seg_has_reloc[i] = (seg.ns_flags & NSRELOC) && (file_size > 0) ? 1 : 0;

        if (ne.ne_cs != 0 && i == (unsigned)(ne.ne_cs - 1)) {
            seg_sels[i]      = dll->selector_code;
            dll->code_phys   = (unsigned long)seg_addr << 4;
            dll->code_size   = file_size ? file_size : alloc_size;
            kprintf("    -> CODE phys=0x%05lX sel=0x%04X\n",
                    dll->code_phys, seg_sels[i]);
        } else if (ne.ne_autodata != 0 && i == (unsigned)(ne.ne_autodata - 1)) {
            seg_sels[i]        = SEL_DLL_DATA(dll_idx);
            dll->selector_data = seg_sels[i];
            dll->data_phys     = (unsigned long)seg_addr << 4;
            dll->data_size     = alloc_size;
            dll->has_data      = 1;
            kprintf("    -> DATA phys=0x%05lX sel=0x%04X\n",
                    dll->data_phys, seg_sels[i]);
        } else {
            seg_sels[i] = 0;
            kprintf("    phys=0x%05lX (brak selektora)\n",
                    (unsigned long)seg_addr << 4);
        }
    }

    /* ---- Parsuj Entry Table (PRZED fixupami, by DLL widziala wlasne eksporty) ---- */
    parse_entry_table(f, mz.e_lfanew, &ne, dll->exports, NULL, &dll->num_exports);
    {
        unsigned short k, printed = 0;
        for (k = 0; k < dll->num_exports; k++) {
            if (dll->exports[k] != 0) printed++;
        }
        kprintf("  Eksporty: %u (ordinals: %u)\n", printed, dll->num_exports);
#if DEBUG_FIXUPS
        for (k = 0; k < dll->num_exports; k++) {
            if (dll->exports[k] != 0)
                kprintf("    ordinal %u -> 0x%04X\n", k+1, dll->exports[k]);
        }
#endif
    }

    /* ---- Wypelnij globalne tablice i zarejstruj DLL przed fixupami ----
     * Dzieki temu DLL moze rozwiazac wlasne self-imports (np. KERNEL -> KERNEL.3/4/113) */
    g_dll_code_phys[dll_idx] = dll->code_phys;
    g_dll_code_size[dll_idx] = dll->code_size;
    g_dll_data_phys[dll_idx] = dll->data_phys;
    g_dll_data_size[dll_idx] = dll->data_size;
    g_dll_has_data[dll_idx]  = dll->has_data;
    g_ndll++;

    /* ---- Parsuj Module Reference Table DLL (dla DLL self-imports) ---- */
    {
        int mod_map[MAX_MOD_REFS];
        unsigned int j;
        for (j = 0; j < MAX_MOD_REFS; j++) mod_map[j] = -1;
        if (ne.ne_cmod > 0)
            parse_mod_table(f, mz.e_lfanew, &ne, mod_map);

        /* ---- Przejscie 2: aplikuj fixupy ---- */
        for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
            if (seg_has_reloc[i] && seg_data_ptr[i] != 0) {
                kprintf("  Fixupy seg %u:\n", i+1);
                apply_fixups(f, seg_reloc_off[i],
                             seg_data_ptr[i], seg_file_size[i],
                             seg_sels, ne.ne_cseg,
                             mod_map, ne.ne_cmod,
                             NULL, NULL, 0);
            }
        }
    }

    fclose(f);
    return 0;
err:
    fclose(f);
    return -1;
}

/* ============================================================
 * load_ne: laduje aplikacje NE
 * ============================================================ */
int load_ne(const char *filename)
{
    FILE        *f;
    MZ_HEADER    mz;
    NE_HEADER    ne;
    NE_SEG_ENTRY seg;
    unsigned int i;

    f = fopen(filename, "rb");
    if (!f) { kprintf("ERROR: cannot open %s\n", filename); return -1; }

    if (fread(&mz, sizeof(mz), 1, f) != 1) goto err;
    if (mz.e_magic != 0x5A4D) { kprintf("ERROR: not MZ\n"); goto err; }
    kprintf("MZ OK, NE at 0x%04lX\n", mz.e_lfanew);

    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) { kprintf("ERROR: not NE\n"); goto err; }

    kprintf("NE: segs=%u align=%u cs=%u ip=0x%04X autodata=%u mods=%u\n",
            ne.ne_cseg, ne.ne_align, ne.ne_cs, ne.ne_ip,
            ne.ne_autodata, ne.ne_cmod);

    g_entry_ip = ne.ne_ip;
    g_has_data = 0;
    g_app_phys = 0;

    {
        long             seg_reloc_off[MAX_DLL_SEGS];
        unsigned char __far *seg_data_ptr[MAX_DLL_SEGS];
        unsigned short   seg_file_size[MAX_DLL_SEGS];
        unsigned short   seg_has_reloc[MAX_DLL_SEGS];
        unsigned short   seg_sels[MAX_DLL_SEGS];

        for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
            long          file_off;
            unsigned      file_size, alloc_size, paragraphs;
            unsigned      seg_addr;
            unsigned char __far *dst;
            unsigned char *tmp;
            unsigned      k;

            fseek(f, mz.e_lfanew + ne.ne_segtab + (long)i * sizeof(NE_SEG_ENTRY),
                  SEEK_SET);
            if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

            file_size  = seg.ns_cbseg ? seg.ns_cbseg : 0;
            alloc_size = seg.ns_minalloc ? seg.ns_minalloc : 65535;
            if (file_size > alloc_size) alloc_size = file_size;
            /* Segment danych aplikacji: dodaj ne_heap + ne_stack jak Windows 3.1.
             * Przyklad SKI.EXE: ns_minalloc=3016, ne_heap=16384, ne_stack=16384
             * -> alloc=35784, wiec offset 0x404A (16458) jest w zakresie. */
            if (ne.ne_autodata != 0 && i == (unsigned)(ne.ne_autodata - 1)) {
                unsigned long total = (unsigned long)alloc_size
                                    + (unsigned long)ne.ne_heap
                                    + (unsigned long)ne.ne_stack;
                alloc_size = (total > 65535UL) ? 65535U : (unsigned)total;
            }

            kprintf("Seg %u: file=%u alloc=%u flags=0x%04X%s\n",
                    i+1, file_size, alloc_size, seg.ns_flags,
                    (seg.ns_flags & NSRELOC) ? " RELOC" : "");

            seg_reloc_off[i] = 0;
            seg_data_ptr[i]  = 0;
            seg_file_size[i] = 0;
            seg_has_reloc[i] = 0;
            seg_sels[i]      = 0;

            if (alloc_size == 0) continue;

            paragraphs = (alloc_size + 15) / 16;
            if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
                kprintf("ERROR: _dos_allocmem\n"); goto err;
            }

            dst      = MK_FP(seg_addr, 0);
            file_off = (long)seg.ns_sector << ne.ne_align;

            if (file_size > 0) {
                tmp = (unsigned char *)malloc(file_size);
                if (!tmp) { kprintf("ERROR: malloc\n"); goto err; }
                fseek(f, file_off, SEEK_SET);
                fread(tmp, file_size, 1, f);
                _fmemcpy(dst, tmp, file_size);
                free(tmp);
            }
            for (k = file_size; k < alloc_size; k++) dst[k] = 0;

            seg_data_ptr[i]  = dst;
            seg_file_size[i] = (unsigned short)file_size;
            seg_reloc_off[i] = file_off + file_size;
            seg_has_reloc[i] = (seg.ns_flags & NSRELOC) && file_size > 0 ? 1 : 0;

            if (ne.ne_cs != 0 && i == (unsigned)(ne.ne_cs - 1)) {
                seg_sels[i]  = SEL_APP_CODE;
                g_app_phys   = (unsigned long)seg_addr << 4;
                g_code_size  = file_size ? (unsigned short)file_size
                                         : (unsigned short)alloc_size;
                kprintf("  phys=0x%05lX -> CODE sel=0x%04X\n",
                        g_app_phys, seg_sels[i]);
            } else if (ne.ne_autodata != 0 && i == (unsigned)(ne.ne_autodata - 1)) {
                seg_sels[i]      = SEL_APP_DATA;
                g_app_data_phys  = (unsigned long)seg_addr << 4;
                g_data_size      = (unsigned short)alloc_size;
                g_has_data       = 1;
                kprintf("  phys=0x%05lX -> DATA sel=0x%04X\n",
                        g_app_data_phys, seg_sels[i]);
            } else {
                kprintf("  phys=0x%05lX\n", (unsigned long)seg_addr << 4);
            }
        }

        /* Oblicz poczatkowy SP z NE header.
         * alloc_size dla seg danych juz zawiera ne_heap+ne_stack (linia wyzej),
         * wiec g_data_size = data_minalloc + ne_heap + ne_stack = top stosu. */
        if (ne.ne_sp != 0)
            g_init_sp = ne.ne_sp;
        else if (g_has_data)
            g_init_sp = g_data_size;   /* g_data_size = data + heap + stack */
        else
            g_init_sp = 0xFFFE;
        kprintf("init SP=0x%04X (data_total=%u)\n", g_init_sp, g_data_size);

        /* Parsuj Entry Table apki (potrzebna do INTERNALREF seg=0xFF) */
        {
            static unsigned short app_exports[MAX_EXPORTS];
            static unsigned char  app_entry_segs[MAX_EXPORTS];
            unsigned short        app_num_exports = 0;
            unsigned int          j;
            for (j = 0; j < MAX_EXPORTS; j++) { app_exports[j] = 0; app_entry_segs[j] = 0; }
            parse_entry_table(f, mz.e_lfanew, &ne,
                              app_exports, app_entry_segs, &app_num_exports);
            kprintf("App entry table: %u entries\n", app_num_exports);

            /* Parsuj Module Reference Table apki */
            {
                int mod_map[MAX_MOD_REFS];
                for (j = 0; j < MAX_MOD_REFS; j++) mod_map[j] = -1;
                kprintf("ModRef table (%u entries):\n", ne.ne_cmod);
                if (ne.ne_cmod > 0)
                    parse_mod_table(f, mz.e_lfanew, &ne, mod_map);

                for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
                    if (seg_has_reloc[i] && seg_data_ptr[i]) {
                        kprintf("Fixupy seg %u:\n", i+1);
                        apply_fixups(f, seg_reloc_off[i],
                                     seg_data_ptr[i], seg_file_size[i],
                                     seg_sels, ne.ne_cseg,
                                     mod_map, ne.ne_cmod,
                                     app_exports, app_entry_segs, app_num_exports);
                        /* Debug: dump bytes at CALL FAR site 0x59AE (= 0x599E outer fn) */
                        if (i == 0 && seg_file_size[i] > 0x59B2) {
                            unsigned char __far *p = seg_data_ptr[i];
                            kprintf("CALLSITE 0x59AE: %02X %02X %02X %02X %02X\n",
                                    (unsigned)p[0x59AE], (unsigned)p[0x59AF],
                                    (unsigned)p[0x59B0], (unsigned)p[0x59B1],
                                    (unsigned)p[0x59B2]);
                        }
                    }
                }

            }
        }
    }

    if (g_app_phys == 0) { kprintf("ERROR: brak segmentu kodu\n"); goto err; }

    fclose(f);
    return 0;
err:
    fclose(f);
    return -1;
}

/* ============================================================
 * load_ski_strings: wczytaj bloki RT_STRING z SKI.EXE do KCB
 * ============================================================ */
static void load_ski_strings(const char *filename)
{
    FILE *f;
    MZ_HEADER mz;
    NE_HEADER ne;
    long ne_off;
    unsigned short rsctab_abs, rscAlignShift;
    unsigned short kcb_seg;
    KCB_LAYOUT __far *kcb;
    unsigned char  __far *kdata;   /* wskaznik na bajty KCB */
    unsigned short data_off;       /* offset zapisu surowych danych w KCB */
    unsigned short nb;

    f = fopen(filename, "rb");
    if (!f) { kprintf("WARN: nie mozna otworzyc %s dla zasobow\n", filename); return; }

    if (fread(&mz, sizeof(mz), 1, f) != 1) { fclose(f); return; }
    ne_off = (long)mz.e_lfanew;
    fseek(f, ne_off, SEEK_SET);
    if (fread(&ne, sizeof(ne), 1, f) != 1) { fclose(f); return; }
    if (ne.ne_magic != 0x454E) { fclose(f); return; }

    rsctab_abs   = (unsigned short)(ne_off + ne.ne_rsrctab);
    fseek(f, rsctab_abs, SEEK_SET);
    if (fread(&rscAlignShift, 2, 1, f) != 1) { fclose(f); return; }

    kcb_seg = (unsigned short)(g_kcb_phys >> 4);
    kcb     = (KCB_LAYOUT __far *)MK_FP(kcb_seg, 0);
    kdata   = (unsigned char __far *)MK_FP(kcb_seg, 0);
    nb      = 0;
    data_off = KCB_RSC_DATA_OFF;

    /* Przeszukaj tabele zasobow w poszukiwaniu RT_STRING (0x8006) */
    for (;;) {
        unsigned short rt_id, rt_count;
        unsigned short j;

        if (fread(&rt_id, 2, 1, f) != 1) break;
        if (rt_id == 0) break;
        if (fread(&rt_count, 2, 1, f) != 1) break;
        fseek(f, 4, SEEK_CUR);  /* pomin rt_reserved */

        for (j = 0; j < rt_count; j++) {
            unsigned short rn_offset, rn_length, rn_flags, rn_id;
            long file_off;
            unsigned long block_size;

            if (fread(&rn_offset, 2, 1, f) != 1) break;
            if (fread(&rn_length, 2, 1, f) != 1) break;
            if (fread(&rn_flags,  2, 1, f) != 1) break;
            if (fread(&rn_id,     2, 1, f) != 1) break;
            fseek(f, 4, SEEK_CUR);  /* pomin rn_handle/rn_usage */

            if (rt_id == 0x8006 && nb < RSC_STR_MAX_BLOCKS) {
                long save_pos = ftell(f);
                unsigned short block_id = rn_id & 0x7FFF;
                file_off   = (long)rn_offset << rscAlignShift;
                block_size = (unsigned long)rn_length << rscAlignShift;

                if (block_size > 228U || data_off + (unsigned short)block_size > 256U) {
                    kprintf("RSC: blok %u za duzy (%lu B), pomijam\n",
                            block_id, block_size);
                } else {
                    unsigned short i;
                    unsigned char __far *dst = kdata + data_off;
                    fseek(f, file_off, SEEK_SET);
                    for (i = 0; i < (unsigned short)block_size; i++) {
                        int c = fgetc(f);
                        if (c == EOF) break;
                        dst[i] = (unsigned char)c;
                    }
                    kcb->rsc_block_ids[nb]   = block_id;
                    kcb->rsc_block_sizes[nb] = (unsigned short)block_size;
                    data_off += (unsigned short)block_size;
                    nb++;
                    kprintf("RSC: blok %u (str%u..%u) %u B wczytany\n",
                            block_id, (block_id-1)*16+1, block_id*16,
                            (unsigned short)block_size);
                    fseek(f, save_pos, SEEK_SET);
                }
            }
        }
    }

    kcb->rsc_nblocks = (unsigned char)nb;
    kprintf("RSC: %u blokow RT_STRING wczytanych do KCB\n", nb);
    fclose(f);
}

/* ============================================================
 * load_ski_bitmaps: wczytaj wszystkie RT_BITMAP z SKI.EXE do bufora DOS
 *
 * Format bufora (SEL_BITMAPS, 56KB):
 *   [0..1]   WORD  count      - liczba wczytanych bitmap (max 86)
 *   [2..3]   WORD  pad        - 0
 *   [4..175] WORD  offsets[86] - offsets[id-1] = offset w buforze do danych DIB
 *                               0 = brak (nie zaladowana)
 *   [176..]  surowe dane DIB (BITMAPINFOHEADER + 16xRGBQUAD + piksele 4bpp)
 *
 * Bitmap: HBITMAP = id (1..86); MK_FP(SEL_BITMAPS, offsets[id-1]) = DIB.
 * ============================================================ */
#define MAX_BITMAPS     86
#define BMP_BUF_PARA    3584           /* 56KB = 3584 * 16 */
#define BMP_BUF_HDR     (4 + MAX_BITMAPS * 2)  /* 176 bajtow naglowka */

/* load_ski_bitmaps: wypelnia pre-alokowany bufor DOS danymi RT_BITMAP.
 * g_bitmaps_phys musi byc juz ustawiony przed wywolaniem tej funkcji. */
static void load_ski_bitmaps(const char *filename)
{
    FILE *f;
    MZ_HEADER mz;
    NE_HEADER ne;
    long ne_off;
    unsigned short rsctab_abs, rscAlignShift;
    unsigned bmp_seg;
    unsigned char __far *buf;
    static unsigned short offsets[MAX_BITMAPS];  /* static: w DS (SS != DS w duzym modelu) */
    unsigned short data_off = (unsigned short)BMP_BUF_HDR;
    unsigned short count = 0;
    int i;

    if (g_bitmaps_phys == 0) {
        kprintf("ERROR: g_bitmaps_phys not set (alloc first)\n"); return;
    }
    for (i = 0; i < MAX_BITMAPS; i++) offsets[i] = 0;

    bmp_seg = (unsigned)(g_bitmaps_phys >> 4);
    buf = (unsigned char __far *)MK_FP(bmp_seg, 0);
    /* Wyzeruj naglowek */
    for (i = 0; i < BMP_BUF_HDR; i++) buf[i] = 0;

    f = fopen(filename, "rb");
    if (!f) { kprintf("WARN: nie mozna otworzyc %s dla bitmap\n", filename); return; }

    if (fread(&mz, sizeof(mz), 1, f) != 1) { fclose(f); return; }
    ne_off = (long)mz.e_lfanew;
    fseek(f, ne_off, SEEK_SET);
    if (fread(&ne, sizeof(ne), 1, f) != 1) { fclose(f); return; }
    if (ne.ne_magic != 0x454E) { fclose(f); return; }

    rsctab_abs = (unsigned short)(ne_off + ne.ne_rsrctab);
    fseek(f, rsctab_abs, SEEK_SET);
    if (fread(&rscAlignShift, 2, 1, f) != 1) { fclose(f); return; }

    /* Przeszukaj tablice zasobow w poszukiwaniu RT_BITMAP (0x8002) */
    for (;;) {
        unsigned short rt_id, rt_count;
        unsigned short j;

        if (fread(&rt_id,    2, 1, f) != 1) break;
        if (rt_id == 0) break;
        if (fread(&rt_count, 2, 1, f) != 1) break;
        fseek(f, 4, SEEK_CUR);  /* pomin rt_reserved */

        for (j = 0; j < rt_count; j++) {
            unsigned short rn_offset, rn_length, rn_flags, rn_id;
            if (fread(&rn_offset, 2, 1, f) != 1) break;
            if (fread(&rn_length, 2, 1, f) != 1) break;
            if (fread(&rn_flags,  2, 1, f) != 1) break;
            if (fread(&rn_id,     2, 1, f) != 1) break;
            fseek(f, 4, SEEK_CUR);  /* pomin rn_handle/rn_usage */

            if (rt_id == 0x8002) {  /* RT_BITMAP */
                long file_off     = (long)rn_offset << rscAlignShift;
                unsigned long bmp_sz = (unsigned long)rn_length << rscAlignShift;
                unsigned short id = rn_id & 0x7FFF;  /* zdejmij bit nazwy */

                if (id >= 1 && id <= MAX_BITMAPS &&
                    (unsigned long)data_off + bmp_sz < (unsigned long)BMP_BUF_PARA * 16U) {
                    long save_pos = ftell(f);
                    unsigned short sz16 = (unsigned short)bmp_sz;
                    unsigned short i2;

                    fseek(f, file_off, SEEK_SET);
                    for (i2 = 0; i2 < sz16; i2++) {
                        int c = fgetc(f);
                        if (c == EOF) break;
                        buf[data_off + i2] = (unsigned char)c;
                    }
                    offsets[id - 1] = data_off;
                    data_off = (unsigned short)(data_off + sz16);
                    count++;
                    fseek(f, save_pos, SEEK_SET);
                }
            }
        }
    }
    fclose(f);

    /* Zapisz naglowek bufora */
    buf[0] = (unsigned char)(count & 0xFF);
    buf[1] = (unsigned char)(count >> 8);
    buf[2] = 0; buf[3] = 0;
    for (i = 0; i < MAX_BITMAPS; i++) {
        buf[4 + i*2]     = (unsigned char)(offsets[i] & 0xFF);
        buf[4 + i*2 + 1] = (unsigned char)(offsets[i] >> 8);
    }
    kprintf("BITMAPS: %u RT_BITMAP -> bufor phys=0x%05lX size=%uB\n",
            count, g_bitmaps_phys, data_off);
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char *argv[])
{
    const char *app_name = (argc > 1) ? argv[1] : "SKI.EXE";
    serial_init();
    kprintf("=== NE Loader STEP15 - GlobalHeap XMS + RAM detection ===\n");
    kprintf("App: %s\n", app_name);

    /* 0. Wykryj dostepna pamiec RAM (przed PM - INT 15h dostepny tylko w real mode) */
    kprintf("--- RAM detection ---\n");
    mem_detect();

    /* 0a. Inicjalizacja VESA (przed wejsciem w PM!) */
    kprintf("--- VESA init ---\n");
    vesa_init();

    /* 0b. Pobierz font BIOS 8x16 */
    kprintf("--- Font init ---\n");
    font_init();

    /* 1. Inicjalizacja bufora thunkow i IDT */
    kprintf("--- Init INT 3F ---\n");
    init_int3f(get_int3f_off());

    /* 2. Zaladuj KERNEL.EXE jako DLL (dll_idx=0) */
    kprintf("--- Ladowanie KERNEL.EXE ---\n");
    if (load_ne_dll("KERNEL.EXE", "KERNEL") != 0)
        return 1;
    kprintf("KERNEL zaladowany: code=0x%05lX data=0x%05lX\n",
            g_dll[0].code_phys, g_dll[0].data_phys);

    /* 3. Zaladuj USER.EXE jako DLL (dll_idx=1) */
    kprintf("--- Ladowanie USER.EXE ---\n");
    if (load_ne_dll("USER.EXE", "USER") != 0)
        return 1;
    kprintf("USER zaladowany: code=0x%05lX data=0x%05lX\n",
            g_dll[1].code_phys, g_dll[1].data_phys);

    /* 4. Zaladuj GDI.EXE jako DLL (dll_idx=2) */
    kprintf("--- Ladowanie GDI.EXE ---\n");
    if (load_ne_dll("GDI.EXE", "GDI") != 0)
        return 1;
    kprintf("GDI zaladowany: code=0x%05lX data=0x%05lX\n",
            g_dll[2].code_phys, g_dll[2].data_phys);

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

        if (_dos_allocmem(32, &kcb_seg) != 0) {  /* 32 paragrafy = 512 B: KCB (256B) + wnd_h (16B) + RT_STRING */
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
        kcb->app_hinstance  = SEL_APP_DATA;      /* 0x40 */
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
        }
        kprintf("KCB init: hInst=0x%04X dyn_sel=0x%04X heap=0x%06lX..0x%06lX\n",
                kcb->app_hinstance, kcb->next_dyn_sel,
                kcb->heap_phys, kcb->heap_end);
    }

    /* 6. Zaladuj aplikacje NE */
    kprintf("--- Ladowanie %s ---\n", app_name);
    if (load_ne(app_name) != 0)
        return 1;

    /* 6b: RT_STRING - tylko dla SKI.EXE */
    if (strcmp(app_name, "SKI.EXE") == 0) {
        load_ski_strings("SKI.EXE");
    }
    /* 6c: RT_BITMAP - zawsze laduj z SKI.EXE (potrzebne rowniez dla test appow) */
    kprintf("--- Ladowanie bitmap SKI.EXE ---\n");
    load_ski_bitmaps("SKI.EXE");

    g_orig_cs = get_cs();
    g_orig_ss = get_ss();
    g_orig_sp = get_sp();
    g_cs_phys = (unsigned long)g_orig_cs << 4;

    /* Reset tick_ms do 0 - IRQ0 incrementowal go podczas ladowania bitmap w PM */
    {
        unsigned kcb_rm = (unsigned)(g_kcb_phys >> 4);
        KCB_LAYOUT __far *kcb_ptr = (KCB_LAYOUT __far *)MK_FP(kcb_rm, 0);
        kcb_ptr->tick_ms = 0UL;
    }
    /* Inicjalizacja myszy PS/2 (real mode, przed PM) */
    init_mouse_ps2();

    kprintf("LFB=0x%08lX pitch=%u font=0x%06lX -> wchodze w PM\n",
            g_lfb_phys, g_vesa_pitch, g_font_phys);
    pm_call_app();
    kprintf("STEP17 done.\n");
    return 0;
}
