/*
 * loader.c - STEP4: NE Loader wielosegmentowy
 *
 * Rozszerzenie STEP3: laduje WSZYSTKIE segmenty NE (nie tylko CS).
 * Segment ne_autodata jest przekazywany do pm_call.asm jako SEL_APP_DATA.
 *
 * Kompilacja:
 *   wcl -ml -l=dos -q loader.c pm_call.obj -fe=loader.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include "serial.h"

/* Wysyla do DOS console i COM1 jednoczesnie */
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
 * Struktury NE (packed)
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
    unsigned short ne_autodata;   /* 1-based index segmentu danych (0=brak) */
    unsigned short ne_heap;
    unsigned short ne_stack;
    unsigned short ne_ip;         /* entry point IP */
    unsigned short ne_cs;         /* entry point CS (1-based) */
    unsigned short ne_sp;
    unsigned short ne_ss;
    unsigned short ne_cseg;       /* liczba segmentow */
    unsigned short ne_cmod;
    unsigned short ne_cbnrestab;
    unsigned short ne_segtab;
    unsigned short ne_rsrctab;
    unsigned short ne_restab;
    unsigned short ne_modtab;
    unsigned short ne_imptab;
    unsigned long  ne_nrestab;
    unsigned short ne_cmovent;
    unsigned short ne_align;      /* shift count: sektor = 2^ne_align bajtow */
    unsigned short ne_cres;
    unsigned char  ne_exetyp;
    unsigned char  ne_addflags;
    unsigned short ne_gangstart;
    unsigned short ne_ganglength;
    unsigned short ne_swaparea;
    unsigned short ne_expver;
} NE_HEADER;

typedef struct {
    unsigned short ns_sector;     /* offset w pliku / 2^ne_align */
    unsigned short ns_cbseg;      /* rozmiar w pliku (0 = 65536) */
    unsigned short ns_flags;      /* flagi segmentu */
    unsigned short ns_minalloc;   /* min rozmiar alokacji (0 = 65536) */
} NE_SEG_ENTRY;

#pragma pack(pop)

/* ============================================================
 * Globalne zmienne wspoldzielone z pm_call.asm
 * ============================================================ */
unsigned long  g_app_phys;        /* fizyczny adres segmentu kodu (ne_cs) */
unsigned short g_code_size;       /* rozmiar segmentu kodu */
unsigned short g_entry_ip;        /* entry point IP */
unsigned long  g_cs_phys;         /* fizyczny adres segmentu kodu loadera */
unsigned short g_orig_cs;
unsigned short g_orig_ss;
unsigned short g_orig_sp;

/* Nowe w STEP4: segment danych aplikacji */
unsigned long  g_app_data_phys;   /* fizyczny adres DGROUP (ne_autodata) */
unsigned short g_data_size;       /* rozmiar DGROUP w pliku */
unsigned short g_has_data;        /* 1 = segment danych zaladowany */

extern void __far pm_call_app(void);

unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];

unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];

unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * load_ne: laduje wszystkie segmenty NE
 * Zwraca 0=ok, -1=blad
 * ============================================================ */
int load_ne(const char *filename)
{
    FILE        *f;
    MZ_HEADER    mz;
    NE_HEADER    ne;
    NE_SEG_ENTRY seg;
    unsigned int i;

    f = fopen(filename, "rb");
    if (!f) {
        kprintf("ERROR: cannot open %s\n", filename);
        return -1;
    }

    /* --- MZ header --- */
    if (fread(&mz, sizeof(mz), 1, f) != 1) goto err;
    if (mz.e_magic != 0x5A4D) { kprintf("ERROR: not MZ\n"); goto err; }
    kprintf("MZ OK, NE offset=0x%04lX\n", mz.e_lfanew);

    /* --- NE header --- */
    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) {
        kprintf("ERROR: not NE (0x%04X)\n", ne.ne_magic);
        goto err;
    }
    kprintf("NE OK: segs=%u align=%u expver=0x%04X\n",
           ne.ne_cseg, ne.ne_align, ne.ne_expver);
    kprintf("Entry: CS=seg#%u IP=0x%04X  autodata=seg#%u\n",
           ne.ne_cs, ne.ne_ip, ne.ne_autodata);

    g_entry_ip = ne.ne_ip;
    g_has_data = 0;
    g_app_phys = 0;

    /* --- Petla: zaladuj wszystkie segmenty --- */
    for (i = 0; i < ne.ne_cseg; i++) {
        long         seg_off;
        unsigned     file_size;   /* rozmiar danych w pliku */
        unsigned     alloc_size;  /* rozmiar do alokacji (moze byc wiekszy = BSS) */
        unsigned     paragraphs;
        unsigned     seg_addr;
        unsigned char __far *dst;
        unsigned char *tmp;
        long         code_off;
        unsigned     k;

        seg_off = mz.e_lfanew + ne.ne_segtab + (long)i * sizeof(NE_SEG_ENTRY);
        if (fseek(f, seg_off, SEEK_SET) != 0) goto err;
        if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

        /* ns_cbseg=0 oznacza 65536, ns_minalloc=0 oznacza 65536 */
        file_size  = seg.ns_cbseg   ? seg.ns_cbseg   : 0; /* 0 = brak danych w pliku */
        alloc_size = seg.ns_minalloc ? seg.ns_minalloc : 65535;
        if (file_size > alloc_size) alloc_size = file_size;

        kprintf("Seg %u: sector=%u file_size=%u alloc=%u flags=0x%04X\n",
               i+1, seg.ns_sector, file_size, alloc_size, seg.ns_flags);

        if (alloc_size == 0) {
            kprintf("  (pusty segment, pomijam)\n");
            continue;
        }

        /* Alokuj pamiec DOS */
        paragraphs = (alloc_size + 15) / 16;
        if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
            kprintf("ERROR: _dos_allocmem failed (seg %u, %u paras)\n", i+1, paragraphs);
            goto err;
        }

        dst = MK_FP(seg_addr, 0);

        /* Zaladuj dane z pliku */
        if (file_size > 0) {
            code_off = (long)seg.ns_sector << ne.ne_align;
            tmp = (unsigned char *)malloc(file_size);
            if (!tmp) { kprintf("ERROR: malloc\n"); goto err; }
            fseek(f, code_off, SEEK_SET);
            fread(tmp, file_size, 1, f);
            _fmemcpy(dst, tmp, file_size);
            free(tmp);
        }

        /* Wyzeruj BSS (czesc za danymi pliku) */
        for (k = file_size; k < alloc_size; k++)
            dst[k] = 0;

        kprintf("  zaladowano: DOS seg=0x%04X phys=0x%05lX\n",
               seg_addr, (unsigned long)seg_addr << 4);

        /* Zapisz pola dla odpowiednich segmentow */
        if (i == (unsigned)(ne.ne_cs - 1)) {
            g_app_phys  = (unsigned long)seg_addr << 4;
            g_code_size = file_size ? file_size : alloc_size;
            kprintf("  -> CODE (CS, entry IP=0x%04X)\n", ne.ne_ip);
        }
        if (ne.ne_autodata != 0 && i == (unsigned)(ne.ne_autodata - 1)) {
            g_app_data_phys = (unsigned long)seg_addr << 4;
            g_data_size     = alloc_size;
            g_has_data      = 1;
            kprintf("  -> DATA (DS/autodata, size=%u)\n", alloc_size);
        }
    }

    if (g_app_phys == 0) {
        kprintf("ERROR: segment kodu nie zaladowany\n");
        goto err;
    }

    kprintf("Gotowe: code_phys=0x%05lX ip=0x%04X has_data=%u",
           g_app_phys, g_entry_ip, g_has_data);
    if (g_has_data)
        kprintf(" data_phys=0x%05lX", g_app_data_phys);
    kprintf("\n");

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
    kprintf("=== NE Loader STEP4 ===\n");

    if (load_ne("NE_TEST.EXE") != 0)
        return 1;

    g_orig_cs = get_cs();
    g_orig_ss = get_ss();
    g_orig_sp = get_sp();
    g_cs_phys = (unsigned long)g_orig_cs << 4;

    kprintf("Calling NE app...\n");
    pm_call_app();
    kprintf("App returned! STEP4 done.\n");
    return 0;
}
