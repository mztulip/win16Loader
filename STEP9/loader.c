/*
 * loader.c - STEP9a: VESA mode set w real mode + LFB fill w PM
 *
 * Rozszerzenia vs STEP8:
 *  - vesa_init(): INT 10h 4F01h/4F02h, zapisuje g_lfb_phys + g_vesa_pitch
 *  - g_lfb_phys eksportowane do pm_call.asm (patch GDT_VESA base)
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
 * Kazdy thunk = 5 bajtow: CD 3F dll_idx off_lo off_hi
 * ============================================================ */
#define MAX_THUNKS  64
#define THUNK_SIZE   5

static unsigned       g_thunk_seg  = 0;  /* segment DOS dla thunkow */
static unsigned short g_thunk_off  = 0;  /* nastepny wolny offset w segmencie */

unsigned long  g_thunk_phys = 0;   /* eksport do pm_call.asm */
unsigned short g_thunk_size = 0;

/* emit_thunk: generuje thunk dla dll_idx / func_off, zwraca offset w SEG_THUNK */
static unsigned short emit_thunk(unsigned char dll_idx, unsigned short func_off)
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

    g_thunk_off += THUNK_SIZE;
    return off;
}

/* ============================================================
 * IDT: 64 wpisow (wektory 0x00-0x3F)
 * Wpis 0x3F = 16-bit interrupt gate -> int3f_handler w SEL_CODE16
 * ============================================================ */
#define IDT_ENTRIES  64
#define SEL_CODE16   0x20   /* musi zgadzac sie z pm_call.asm */

unsigned long g_idt_phys = 0;   /* eksport do pm_call.asm */

static void init_int3f(unsigned short handler_off)
{
    unsigned       idt_seg;
    unsigned char __far *p;
    int            i;
    unsigned short paragraphs;

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

    /* Wpis 0x3F: 16-bit interrupt gate
     * offset[0-1] = handler_off
     * selector[2-3] = SEL_CODE16
     * reserved[4] = 0
     * access[5] = 0x86  (P=1, DPL=0, type=0x6 = 16-bit int gate)
     * offset_hi[6-7] = 0 (tylko 16-bit) */
    p = MK_FP(idt_seg, 0x3F * 8);
    p[0] = (unsigned char)(handler_off & 0xFF);
    p[1] = (unsigned char)(handler_off >> 8);
    p[2] = (unsigned char)(SEL_CODE16 & 0xFF);
    p[3] = (unsigned char)(SEL_CODE16 >> 8);
    p[4] = 0;
    p[5] = 0x86;   /* P=1, DPL=0, type=6 (16-bit interrupt gate) */
    p[6] = 0;
    p[7] = 0;

    kprintf("IDT: phys=0x%05lX, int3f handler_off=0x%04X sel=0x%02X\n",
            g_idt_phys, handler_off, SEL_CODE16);
}

/* ============================================================
 * Tabela zaladowanych DLL
 * ============================================================ */
#define MAX_EXPORTS  32
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
unsigned short g_orig_ss;
unsigned short g_orig_sp;
unsigned long  g_app_data_phys;
unsigned short g_data_size;
unsigned short g_has_data;

extern void __far pm_call_app(void);
extern unsigned short get_int3f_off(void);  /* zwraca offset int3f_handler w SEL_CODE16 */

unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];
unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];
unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * parse_entry_table
 * ============================================================ */
static void parse_entry_table(FILE *f, long ne_off, NE_HEADER *ne,
                               unsigned short *exports,
                               unsigned short *num_exports)
{
    long           et_off = ne_off + ne->ne_enttab;
    unsigned short ordinal = 0;
    unsigned char  count, seg, flags;
    unsigned char  junk[3];
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
            /* moveable entries: flags(1) + INT3F(2) + seg(1) + offset(2) = 6B */
            for (j = 0; j < count; j++) {
                if (fread(&flags,  1, 1, f) != 1) break;
                if (fread(junk,    3, 1, f) != 1) break;  /* CD 3F seg */
                if (fread(&off,    2, 1, f) != 1) break;
                if (ordinal < MAX_EXPORTS) {
                    exports[ordinal] = off;
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
                    if (ordinal + 1 > *num_exports)
                        *num_exports = ordinal + 1;
                }
                ordinal++;
            }
        }
    }
}

/* ============================================================
 * apply_fixups: patchuje tablice relokacji jednego segmentu
 * ============================================================ */
static void apply_fixups(FILE *f, long reloc_file_off,
                          unsigned char __far *seg_data,
                          unsigned short seg_size,
                          unsigned short *seg_sels,
                          unsigned short num_segs)
{
    unsigned short count, i;
    NE_RELOC       rec;
    unsigned char  rtype, additive;

    fseek(f, reloc_file_off, SEEK_SET);
    if (fread(&count, 2, 1, f) != 1) return;
    kprintf("  Fixups: %u rekordow\n", count);

    for (i = 0; i < count; i++) {
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;

        rtype    = rec.reloc_type & 0x03;
        additive = (rec.reloc_type & RTYPE_ADDITIVE) ? 1 : 0;

        if (rtype == RTYPE_INTERNALREF) {
            if (rec.addr_type == ATYPE_SEG16) {
                unsigned short seg_num = rec.target1;  /* 1-based */
                unsigned short sel = 0;

                if (seg_num >= 1 && seg_num <= num_segs)
                    sel = seg_sels[seg_num - 1];

                kprintf("  [%u] INTERNALREF SEG16 seg=%u sel=0x%04X @ 0x%04X\n",
                        i, seg_num, sel, rec.offset);

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
            } else {
                kprintf("  [%u] INTERNALREF addr_type=%u (pomijam)\n",
                        i, rec.addr_type);
            }

        } else if (rtype == RTYPE_IMPORTORDINAL) {
            unsigned short mod_idx = rec.target1;   /* 1-based */
            unsigned short ordinal = rec.target2;
            unsigned short func_off, thunk_off;
            unsigned char  dll_idx_byte;

            kprintf("  [%u] IMPORTORDINAL mod=%u ord=%u @ 0x%04X\n",
                    i, mod_idx, ordinal, rec.offset);

            if (mod_idx < 1 || mod_idx > g_ndll) {
                kprintf("    ERROR: nieznany modul %u\n", mod_idx);
                continue;
            }
            {
                DLL_MODULE *dll = &g_dll[mod_idx - 1];
                if (ordinal < 1 || ordinal > dll->num_exports) {
                    kprintf("    ERROR: ordinal %u poza zakresem\n", ordinal);
                    continue;
                }
                func_off    = dll->exports[ordinal - 1];
                dll_idx_byte = (unsigned char)(mod_idx - 1);  /* 0-based */
                thunk_off   = emit_thunk(dll_idx_byte, func_off);
                kprintf("    -> %s.%u func=0x%04X thunk=0x%88:%04X\n",
                        dll->name, ordinal, func_off, thunk_off);
            }

            if (rec.addr_type == ATYPE_FAR_PTR && rec.offset + 3 < seg_size) {
                seg_data[rec.offset + 0] = (unsigned char)(thunk_off & 0xFF);
                seg_data[rec.offset + 1] = (unsigned char)(thunk_off >> 8);
                seg_data[rec.offset + 2] = (unsigned char)(SEL_THUNK & 0xFF);
                seg_data[rec.offset + 3] = (unsigned char)(SEL_THUNK >> 8);
            }

        } else {
            kprintf("  [%u] nieznany rtype=%u (pomijam)\n", i, rtype);
        }
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

    /* ---- Przejscie 2: aplikuj fixupy ---- */
    for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
        if (seg_has_reloc[i] && seg_data_ptr[i] != 0) {
            kprintf("  Fixupy seg %u:\n", i+1);
            apply_fixups(f, seg_reloc_off[i],
                         seg_data_ptr[i], seg_file_size[i],
                         seg_sels, ne.ne_cseg);
        }
    }

    /* ---- Parsuj Entry Table ---- */
    parse_entry_table(f, mz.e_lfanew, &ne, dll->exports, &dll->num_exports);
    kprintf("  Eksporty: %u\n", dll->num_exports);
    {
        unsigned short k;
        for (k = 0; k < dll->num_exports; k++)
            kprintf("    ordinal %u -> 0x%04X\n", k+1, dll->exports[k]);
    }

    /* ---- Wypelnij globalne tablice dla pm_call.asm ---- */
    g_dll_code_phys[dll_idx] = dll->code_phys;
    g_dll_code_size[dll_idx] = dll->code_size;
    g_dll_data_phys[dll_idx] = dll->data_phys;
    g_dll_data_size[dll_idx] = dll->data_size;
    g_dll_has_data[dll_idx]  = dll->has_data;

    fclose(f);
    g_ndll++;
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

        for (i = 0; i < ne.ne_cseg && i < MAX_DLL_SEGS; i++) {
            if (seg_has_reloc[i] && seg_data_ptr[i]) {
                kprintf("Fixupy seg %u:\n", i+1);
                apply_fixups(f, seg_reloc_off[i],
                             seg_data_ptr[i], seg_file_size[i],
                             seg_sels, ne.ne_cseg);
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
 * main
 * ============================================================ */
int main(void)
{
    serial_init();
    kprintf("=== NE Loader STEP9c - VESA + font + GDI.EXE ===\n");

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

    /* 5. Zaladuj aplikacje NE */
    kprintf("--- Ladowanie WIN16APP.EXE ---\n");
    if (load_ne("WIN16APP.EXE") != 0)
        return 1;

    g_orig_cs = get_cs();
    g_orig_ss = get_ss();
    g_orig_sp = get_sp();
    g_cs_phys = (unsigned long)g_orig_cs << 4;

    kprintf("LFB=0x%08lX pitch=%u font=0x%06lX -> wchodze w PM\n",
            g_lfb_phys, g_vesa_pitch, g_font_phys);
    pm_call_app();
    kprintf("STEP9c done.\n");
    return 0;
}
