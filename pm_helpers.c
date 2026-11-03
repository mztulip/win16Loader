/*
 * pm_helpers.c - C implementation of pm_call.asm helpers
 *
 * patch_gdt_c: fills GDT base/limit fields at runtime (real-mode, before lgdt).
 * Called from pm_call_app_ via far call (real mode, DS=DGROUP, CS=loader CS).
 *
 * GDT table lives in the loader CODE segment (pm_call.asm _TEXT).
 * We access it via MK_FP(g_orig_cs, g_gdt_off_c + selector_value).
 *
 * WARUNEK: Watcom "mov ds, dx" inside far-pointer writes clobbers DS permanently.
 * Rozwiazanie: wszystkie zmienne globalne kopiowane na stos (SS-relative) na
 * poczatku funkcji, PRZED pierwszym zapisem do GDT. Nastepnie przekazywane
 * jawnie do pomocnikow set_base_p/set_limit_p, ktore nie potrzebuja DS.
 *
 * Selektory GDT (z pm_call.asm):
 *   SEL_DATASEG=0x18  SEL_CODE16=0x20  SEL_DATA16=0x28
 *   SEL_APP_CODE=0x30 SEL_PSP=0x38    SEL_APP_DATA=0x40
 *   DLLs: 0x48..0x80 (code/data pairs, 8 bytes apart)
 *   SEL_THUNK=0x88   SEL_VESA=0x90    SEL_KCB=0x98
 *   VESA windows: 0xA0..0x110 (15 windows)
 *   SEL_FONT=0x118   SEL_GDT_ACCESS=0x120  SEL_BITMAPS=0x128
 */

#include <dos.h>   /* MK_FP, FP_SEG, FP_OFF */

#define SEL_DATASEG     0x18
#define SEL_CODE16      0x20
#define SEL_DATA16      0x28
#define SEL_APP_CODE    0x30
#define SEL_PSP         0x38
#define SEL_APP_DATA    0x40
#define SEL_THUNK       0x88
#define SEL_VESA        0x90
#define SEL_KCB         0x98
#define SEL_VESA_BASE   0xA0
#define VESA_WIN_COUNT  15
#define SEL_FONT        0x118
#define SEL_GDT_ACCESS  0x120
#define SEL_BITMAPS     0x128
#define SEL_HEAP        0x528

/* Globals from loader.c */
extern unsigned short g_orig_cs;
extern unsigned short g_gdt_off_c;     /* offset GDT w segmencie kodu, ustawiany z asm */
extern unsigned long  g_cs_phys;
extern unsigned long  g_app_phys;
extern unsigned long  g_app_data_phys;
extern unsigned short g_has_data;
extern unsigned long  g_thunk_phys;
extern unsigned short g_thunk_size;
extern unsigned long  g_lfb_phys;
extern unsigned long  g_font_phys;
extern unsigned long  g_kcb_phys;
extern unsigned long  g_psp_phys;
extern unsigned long  g_bitmaps_phys;
extern unsigned long  g_ext_mem_kb;

/* Lista wpisow GDT od loadera modulow (module.c): segmenty, tablice, okna RM */
typedef struct {
    unsigned short sel;
    unsigned long  base;
    unsigned long  limit;    /* bajty-1; > 0xFFFFF -> granularnosc 4KB */
    unsigned char  access;   /* 0x9A kod, 0x92 dane */
    unsigned char  big;      /* 1 = D/B (32-bit) */
} GDT_PATCH;
extern GDT_PATCH      g_gdtp[];
extern unsigned short g_ngdtp;

/*
 * Pomocnicze - przyjmuja seg i off jako parametry (rejestrowe/stosowe),
 * niezalezne od DS. Mozna wywolac nawet po zmiane DS przez zapis dalekim wskaznikiem.
 */
static unsigned char __far *gdt_entry_p(unsigned short sel,
                                        unsigned short seg,
                                        unsigned short off)
{
    return (unsigned char __far *)MK_FP(seg, off + sel);
}

static void set_base_p(unsigned short sel, unsigned long base,
                       unsigned short seg, unsigned short off)
{
    unsigned char __far *e = gdt_entry_p(sel, seg, off);
    e[2] = (unsigned char)(base);
    e[3] = (unsigned char)(base >> 8);
    e[4] = (unsigned char)(base >> 16);
    e[7] = (unsigned char)(base >> 24);
}

static void set_limit_p(unsigned short sel, unsigned short lim,
                        unsigned short seg, unsigned short off)
{
    unsigned char __far *e = gdt_entry_p(sel, seg, off);
    e[0] = (unsigned char)(lim);
    e[1] = (unsigned char)(lim >> 8);
}

/*
 * patch_gdt_c - fill runtime-dependent GDT base/limit fields.
 * Called in real mode from pm_call_app_ before LGDT/CR0.
 */
void patch_gdt_c(void)
{
    /*
     * Krok 1: skopiuj WSZYSTKIE zmienne globalne na stos (dostep przez SS,
     * nie DS) ZANIM pierwszy set_base_p zepsuje DS przez "mov ds, dx".
     */
    unsigned short l_seg       = g_orig_cs;
    unsigned short l_off       = g_gdt_off_c;
    unsigned long  l_cs_phys   = g_cs_phys;
    unsigned long  l_app_phys  = g_app_phys;
    unsigned long  l_adata     = g_app_data_phys;
    unsigned short l_has_data  = g_has_data;
    unsigned long  l_thunk     = g_thunk_phys;
    unsigned short l_thunk_sz  = g_thunk_size;
    unsigned long  l_lfb       = g_lfb_phys;
    unsigned long  l_font      = g_font_phys;
    unsigned long  l_kcb       = g_kcb_phys;
    unsigned long  l_psp       = g_psp_phys;
    unsigned long  l_bitmaps    = g_bitmaps_phys;
    unsigned long  l_ext_mem_kb = g_ext_mem_kb;
    unsigned short l_ngdtp      = g_ngdtp;
    GDT_PATCH __far *l_gdtp    = (GDT_PATCH __far *)g_gdtp;
    unsigned short i;

    /*
     * Krok 2: wszystkie zapisy do GDT przez set_base_p/set_limit_p
     * uzywajac l_seg i l_off (na stosie -> dostep przez SS).
     */

    /* SEL_DATASEG/CODE16/DATA16: base = loader CS physical */
    set_base_p(SEL_DATASEG, l_cs_phys, l_seg, l_off);
    set_base_p(SEL_CODE16,  l_cs_phys, l_seg, l_off);
    set_base_p(SEL_DATA16,  l_cs_phys, l_seg, l_off);

    /* SEL_PSP: fake PSP */
    set_base_p(SEL_PSP, l_psp, l_seg, l_off);

    /* SEL_THUNK */
    set_base_p(SEL_THUNK, l_thunk, l_seg, l_off);
    set_limit_p(SEL_THUNK, l_thunk_sz - 1, l_seg, l_off);

    /* SEL_VESA: 32-bit selector, base = LFB */
    set_base_p(SEL_VESA, l_lfb, l_seg, l_off);

    /* SEL_KCB */
    set_base_p(SEL_KCB, l_kcb, l_seg, l_off);

    /* SEL_GDT_ACCESS: base = physical address of GDT table */
    set_base_p(SEL_GDT_ACCESS, l_cs_phys + (unsigned long)l_off, l_seg, l_off);

    /* SEL_BITMAPS */
    if (l_bitmaps) {
        set_base_p(SEL_BITMAPS, l_bitmaps, l_seg, l_off);
        gdt_entry_p(SEL_BITMAPS, l_seg, l_off)[5] = 0x92;  /* P=1 DPL=0 S=1 data RW */
    }

    /* SEL_FONT */
    set_base_p(SEL_FONT, l_font, l_seg, l_off);

    /* 15 okien VESA: kazde 64KB LFB przesuniete o 64KB */
    for (i = 0; i < VESA_WIN_COUNT; i++) {
        unsigned long  base = l_lfb + ((unsigned long)i << 16);
        unsigned short sel  = SEL_VESA_BASE + i * 8;
        set_base_p(sel, base, l_seg, l_off);
        set_limit_p(sel, (i < VESA_WIN_COUNT - 1) ? 0xFFFF : 0x0FFF, l_seg, l_off);
    }

    /* SEL_HEAP (0x528): base=0x100000 (XMS), limit z ext_mem_kb.
     * D/B=1 w bajcie 6 = big/32-bit segment - dostep przez 16-bit lub 32-bit offset. */
    if (l_ext_mem_kb > 0) {
        unsigned long  heap_bytes = l_ext_mem_kb * 1024UL;
        unsigned short lim_lo;
        unsigned char  lim_hi, byte6;
        if (heap_bytes > 0x10000UL) {
            /* G=1 (page granularity): encoded_limit = (heap_bytes/4096) - 1 */
            unsigned long enc = (heap_bytes / 4096UL) - 1UL;
            lim_lo  = (unsigned short)(enc & 0xFFFF);
            lim_hi  = (unsigned char)((enc >> 16) & 0x0F);
            byte6   = (unsigned char)(0xC0 | lim_hi); /* G=1 D/B=1 */
        } else {
            lim_lo  = (unsigned short)(heap_bytes - 1);
            lim_hi  = 0;
            byte6   = 0x40; /* G=0 D/B=1 */
        }
        set_base_p(SEL_HEAP, 0x100000UL, l_seg, l_off);
        set_limit_p(SEL_HEAP, lim_lo, l_seg, l_off);
        gdt_entry_p(SEL_HEAP, l_seg, l_off)[5] = 0x92; /* P=1 DPL=0 S=1 data RW */
        gdt_entry_p(SEL_HEAP, l_seg, l_off)[6] = byte6;
    }

    /* Wpisy od loadera modulow (segmenty modulow, MODTAB, okna pamieci RM) */
    for (i = 0; i < l_ngdtp; i++) {
        GDT_PATCH p = l_gdtp[i];
        unsigned char __far *e = gdt_entry_p(p.sel, l_seg, l_off);
        unsigned long lim = p.limit;
        unsigned char b6 = p.big ? 0x40 : 0x00;
        if (lim > 0xFFFFFUL) { lim >>= 12; b6 |= 0x80; }
        set_base_p(p.sel, p.base, l_seg, l_off);
        e[0] = (unsigned char)lim;
        e[1] = (unsigned char)(lim >> 8);
        e[5] = p.access;
        e[6] = (unsigned char)(b6 | ((lim >> 16) & 0x0F));
    }
}
