/*
 * loader.c - STEP5: NE Loader z obsuga importow
 *
 * Rozszerzenia vs STEP4:
 *  - load_ne_dll(): laduje NE DLL, parsuje Entry Table, buduje tabele exportow
 *  - apply_fixups(): parsuje tablice relokacji segmentu, resolwuje IMPORTORDINAL
 *  - g_kernel_phys/g_kernel_size: przekazywane do pm_call.asm (SEL_KERNEL_CODE)
 *
 * Kompilacja:
 *   wcl -ml -l=dos -q loader.c pm_call.obj serial.obj -fe=loader.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
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
    unsigned char  reloc_type;  /* 0=INTERNALREF 1=IMPORTORDINAL 2=IMPORTNAME */
    unsigned short offset;      /* offset w segmencie do patchowania */
    unsigned short target1;     /* indeks modulu (1-based) lub nr segmentu */
    unsigned short target2;     /* numer ordinal lub offset do nazwy */
} NE_RELOC;

#pragma pack(pop)

#define RELOC_FAR_PTR       3
#define RELOC_IMPORTORDINAL 1

/* ns_flags bits */
#define NSRELOC  0x0100   /* segment ma tablice relokacji (bit 8) */

/* ============================================================
 * Tabela exportow zaladowanych DLL
 * ============================================================ */
#define MAX_EXPORTS 32

typedef struct {
    char           name[16];              /* nazwa modulu (uppercase) */
    unsigned long  code_phys;             /* fizyczny adres segmentu kodu */
    unsigned short code_size;
    unsigned short num_exports;
    unsigned short exports[MAX_EXPORTS];  /* exports[ordinal-1] = offset w code seg */
} DLL_MODULE;

#define MAX_MODULES 4
static DLL_MODULE g_dll[MAX_MODULES];
static unsigned short g_ndll = 0;

/* ============================================================
 * Globalne zmienne wspoldzielone z pm_call.asm
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

/* Nowe w STEP5: fizyczny adres kodu KERNEL.EXE */
unsigned long  g_kernel_phys;
unsigned short g_kernel_size;

extern void __far pm_call_app(void);

unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];
unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];
unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * parse_entry_table: buduje tablice ordinal->offset z Entry Table NE
 * ============================================================ */
static void parse_entry_table(FILE *f, long ne_off, NE_HEADER *ne,
                               unsigned short *exports,
                               unsigned short *num_exports)
{
    long     et_off = ne_off + ne->ne_enttab;
    unsigned short ordinal = 0;
    unsigned char  count, seg, flags;
    unsigned short off;
    int j;

    *num_exports = 0;
    fseek(f, et_off, SEEK_SET);

    while (1) {
        if (fread(&count, 1, 1, f) != 1 || count == 0) break;
        if (fread(&seg, 1, 1, f) != 1) break;

        if (seg == 0x00) {
            /* unused entries - brak danych, tylko przesun licznik */
            ordinal += count;
        } else if (seg == 0xFF) {
            /* moveable entries: 6 bajtow kazda (nie obslugujemy) */
            fseek(f, (long)count * 6, SEEK_CUR);
            ordinal += count;
        } else {
            /* fixed segment entries: flags(1) + offset(2) = 3 bajty kazda */
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
 * apply_fixups: resolvuje IMPORTORDINAL dla jednego segmentu
 *
 * sel_kernel - selektor SEL_KERNEL_CODE (z pm_call.asm)
 * reloc_file_off - offset w pliku gdzie zaczyna sie tablica relokacji
 *                  (= fileoff_seg + ns_cbseg)
 * seg_data - zaladowane dane segmentu w pamieci DOS (do patchowania)
 * seg_size - rozmiar segmentu (do weryfikacji offsetow)
 * ============================================================ */
#define SEL_KERNEL_CODE 0x48

static void apply_fixups(FILE *f, long reloc_file_off,
                          unsigned char __far *seg_data, unsigned short seg_size)
{
    unsigned short count, i;
    NE_RELOC       rec;
    unsigned short mod_idx, ordinal;
    unsigned short far_off, far_sel;

    fseek(f, reloc_file_off, SEEK_SET);
    if (fread(&count, 2, 1, f) != 1) return;

    kprintf("  Fixups: %u rekordow\n", count);

    for (i = 0; i < count; i++) {
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;

        if (rec.reloc_type != RELOC_IMPORTORDINAL) {
            kprintf("  [%u] pomijam typ reloc=%u\n", i, rec.reloc_type);
            continue;
        }

        mod_idx = rec.target1;   /* 1-based indeks modulu */
        ordinal = rec.target2;   /* numer ordinal */

        kprintf("  [%u] IMPORTORDINAL mod=%u ordinal=%u offset=0x%04X\n",
                i, mod_idx, ordinal, rec.offset);

        /* Znajdz modul (mod_idx 1-based) */
        if (mod_idx < 1 || mod_idx > g_ndll) {
            kprintf("    ERROR: nieznany modul %u\n", mod_idx);
            continue;
        }

        {
            DLL_MODULE *dll = &g_dll[mod_idx - 1];

            if (ordinal < 1 || ordinal > dll->num_exports) {
                kprintf("    ERROR: ordinal %u poza zakresem (max %u)\n",
                        ordinal, dll->num_exports);
                continue;
            }

            far_off = dll->exports[ordinal - 1];
            far_sel = SEL_KERNEL_CODE;  /* na razie tylko KERNEL */

            kprintf("    -> %s ordinal %u = 0x%04X:0x%04X\n",
                    dll->name, ordinal, far_sel, far_off);

            /* Patchuj segment w pamieci DOS */
            if (rec.addr_type == RELOC_FAR_PTR && rec.offset + 3 < seg_size) {
                seg_data[rec.offset + 0] = (unsigned char)(far_off & 0xFF);
                seg_data[rec.offset + 1] = (unsigned char)(far_off >> 8);
                seg_data[rec.offset + 2] = (unsigned char)(far_sel & 0xFF);
                seg_data[rec.offset + 3] = (unsigned char)(far_sel >> 8);
                kprintf("    patchowano offset 0x%04X\n", rec.offset);
            }
        }
    }
}

/* ============================================================
 * load_ne_dll: laduje NE DLL i rejestruje jej eksporty
 * Zwraca 0=ok, -1=blad
 * ============================================================ */
int load_ne_dll(const char *filename, const char *modname)
{
    FILE         *f;
    MZ_HEADER     mz;
    NE_HEADER     ne;
    NE_SEG_ENTRY  seg;
    DLL_MODULE   *dll;
    unsigned      seg_addr, paragraphs, file_size, alloc_size;
    unsigned char *tmp;
    long          code_off;
    unsigned char __far *dst;

    if (g_ndll >= MAX_MODULES) {
        kprintf("ERROR: za duzo moduli\n");
        return -1;
    }
    dll = &g_dll[g_ndll];

    f = fopen(filename, "rb");
    if (!f) { kprintf("ERROR: cannot open %s\n", filename); return -1; }

    if (fread(&mz, sizeof(mz), 1, f) != 1) goto err;
    if (mz.e_magic != 0x5A4D) { kprintf("ERROR: not MZ\n"); goto err; }

    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) { kprintf("ERROR: not NE\n"); goto err; }

    kprintf("DLL %s: segs=%u enttab_off=%u enttab_len=%u\n",
            filename, ne.ne_cseg, ne.ne_enttab, ne.ne_cbenttab);

    /* Zaladuj segment kodu (segment 1) */
    if (fseek(f, mz.e_lfanew + ne.ne_segtab, SEEK_SET) != 0) goto err;
    if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

    file_size  = seg.ns_cbseg   ? seg.ns_cbseg   : 0;
    alloc_size = seg.ns_minalloc ? seg.ns_minalloc : file_size;
    if (file_size > alloc_size) alloc_size = file_size;

    paragraphs = (alloc_size + 15) / 16;
    if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
        kprintf("ERROR: _dos_allocmem DLL\n");
        goto err;
    }

    dst = MK_FP(seg_addr, 0);
    code_off = (long)seg.ns_sector << ne.ne_align;

    tmp = (unsigned char *)malloc(file_size);
    if (!tmp) { kprintf("ERROR: malloc DLL\n"); goto err; }
    fseek(f, code_off, SEEK_SET);
    fread(tmp, file_size, 1, f);
    _fmemcpy(dst, tmp, file_size);
    free(tmp);

    dll->code_phys = (unsigned long)seg_addr << 4;
    dll->code_size = file_size;
    _fstrncpy(dll->name, modname, sizeof(dll->name) - 1);
    dll->name[sizeof(dll->name) - 1] = '\0';

    kprintf("DLL kod: phys=0x%05lX size=%u\n", dll->code_phys, dll->code_size);

    /* Parsuj Entry Table */
    parse_entry_table(f, mz.e_lfanew, &ne, dll->exports, &dll->num_exports);
    kprintf("DLL eksporty: %u\n", dll->num_exports);
    {
        unsigned short k;
        for (k = 0; k < dll->num_exports; k++)
            kprintf("  ordinal %u -> offset 0x%04X\n", k+1, dll->exports[k]);
    }

    fclose(f);
    g_ndll++;
    return 0;
err:
    fclose(f);
    return -1;
}

/* ============================================================
 * load_ne: laduje aplikacje NE (wszystkie segmenty + fixupy)
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
    kprintf("MZ OK, NE offset=0x%04lX\n", mz.e_lfanew);

    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) { kprintf("ERROR: not NE\n"); goto err; }

    kprintf("NE OK: segs=%u align=%u cs=%u ip=0x%04X autodata=%u mods=%u\n",
            ne.ne_cseg, ne.ne_align, ne.ne_cs, ne.ne_ip,
            ne.ne_autodata, ne.ne_cmod);

    g_entry_ip = ne.ne_ip;
    g_has_data = 0;
    g_app_phys = 0;

    for (i = 0; i < ne.ne_cseg; i++) {
        long          seg_off;
        long          file_off;
        unsigned      file_size, alloc_size, paragraphs;
        unsigned      seg_addr;
        unsigned char __far *dst;
        unsigned char *tmp;
        unsigned      k;

        seg_off = mz.e_lfanew + ne.ne_segtab + (long)i * sizeof(NE_SEG_ENTRY);
        if (fseek(f, seg_off, SEEK_SET) != 0) goto err;
        if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

        file_size  = seg.ns_cbseg   ? seg.ns_cbseg   : 0;
        alloc_size = seg.ns_minalloc ? seg.ns_minalloc : 65535;
        if (file_size > alloc_size) alloc_size = file_size;

        kprintf("Seg %u: sector=%u file=%u alloc=%u flags=0x%04X%s\n",
                i+1, seg.ns_sector, file_size, alloc_size, seg.ns_flags,
                (seg.ns_flags & NSRELOC) ? " RELOC" : "");

        if (alloc_size == 0) continue;

        paragraphs = (alloc_size + 15) / 16;
        if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
            kprintf("ERROR: _dos_allocmem\n"); goto err;
        }

        dst = MK_FP(seg_addr, 0);
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

        kprintf("  phys=0x%05lX\n", (unsigned long)seg_addr << 4);

        /* Resolwuj fixupy segmentu */
        if ((seg.ns_flags & NSRELOC) && file_size > 0) {
            apply_fixups(f, file_off + file_size, dst, file_size);
        }

        if (i == (unsigned)(ne.ne_cs - 1)) {
            g_app_phys  = (unsigned long)seg_addr << 4;
            g_code_size = file_size ? file_size : alloc_size;
            kprintf("  -> CODE\n");
        }
        if (ne.ne_autodata != 0 && i == (unsigned)(ne.ne_autodata - 1)) {
            g_app_data_phys = (unsigned long)seg_addr << 4;
            g_data_size     = alloc_size;
            g_has_data      = 1;
            kprintf("  -> DATA\n");
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
    kprintf("=== NE Loader STEP5 ===\n");

    /* 1. Zaladuj KERNEL.EXE jako DLL */
    kprintf("--- Ladowanie KERNEL.EXE ---\n");
    if (load_ne_dll("KERNEL.EXE", "KERNEL") != 0)
        return 1;

    /* Przekaz adres kodu KERNEL do pm_call.asm (SEL_KERNEL_CODE) */
    g_kernel_phys = g_dll[0].code_phys;
    g_kernel_size = g_dll[0].code_size;
    kprintf("KERNEL: phys=0x%05lX size=%u\n", g_kernel_phys, g_kernel_size);

    /* 2. Zaladuj aplikacje NE (z resolwowaniem fixupow) */
    kprintf("--- Ladowanie NE_TEST.EXE ---\n");
    if (load_ne("NE_TEST.EXE") != 0)
        return 1;

    g_orig_cs = get_cs();
    g_orig_ss = get_ss();
    g_orig_sp = get_sp();
    g_cs_phys = (unsigned long)g_orig_cs << 4;

    kprintf("Calling NE app...\n");
    pm_call_app();
    kprintf("App returned! STEP5 done.\n");
    return 0;
}
