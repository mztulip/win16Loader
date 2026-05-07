/*
 * loader.c - STEP3: Minimalny NE Loader (C + asm glue)
 *
 * Kompilacja:
 *   wcl -ml -l=dos -q loader.c pm_call.obj -fe=loader.exe
 * (duzy model pamieci: far pointery, far calle)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

/* ============================================================
 * Struktury NE (packed - bez paddingu)
 * ============================================================ */
#pragma pack(push, 1)

typedef struct {
    unsigned short e_magic;         /* 'MZ' = 0x5A4D */
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
    unsigned long  e_lfanew;        /* offset do NE headera */
} MZ_HEADER;

typedef struct {
    unsigned short ne_magic;        /* 'NE' = 0x454E */
    unsigned char  ne_ver;
    unsigned char  ne_rev;
    unsigned short ne_enttab;
    unsigned short ne_cbenttab;
    unsigned long  ne_crc;
    unsigned short ne_flags;
    unsigned short ne_autodata;
    unsigned short ne_heap;
    unsigned short ne_stack;
    unsigned short ne_ip;           /* entry point IP */
    unsigned short ne_cs;           /* entry point CS (1-based index segmentu) */
    unsigned short ne_sp;
    unsigned short ne_ss;
    unsigned short ne_cseg;         /* liczba segmentow */
    unsigned short ne_cmod;
    unsigned short ne_cbnrestab;
    unsigned short ne_segtab;       /* offset tablicy segmentow od NE header */
    unsigned short ne_rsrctab;
    unsigned short ne_restab;
    unsigned short ne_modtab;
    unsigned short ne_imptab;
    unsigned long  ne_nrestab;
    unsigned short ne_cmovent;
    unsigned short ne_align;        /* shift count: sektor = 2^ne_align bajtow */
    unsigned short ne_cres;
    unsigned char  ne_exetyp;       /* 0x02 = Windows */
    unsigned char  ne_addflags;
    unsigned short ne_gangstart;
    unsigned short ne_ganglength;
    unsigned short ne_swaparea;
    unsigned short ne_expver;       /* wersja Windows: 0x030A = 3.10 */
} NE_HEADER;

typedef struct {
    unsigned short ns_sector;       /* offset w pliku / 2^ne_align */
    unsigned short ns_cbseg;        /* rozmiar w pliku (0 = 65536) */
    unsigned short ns_flags;        /* flagi segmentu */
    unsigned short ns_minalloc;     /* min rozmiar alokacji */
} NE_SEG_ENTRY;

#pragma pack(pop)

/* ============================================================
 * Globalne zmienne wspoldzielone z pm_call.asm
 * ============================================================ */
unsigned long  g_app_phys;      /* fizyczny adres zaladowanego kodu */
unsigned short g_code_size;     /* rozmiar segmentu kodu */
unsigned short g_entry_ip;      /* entry point IP */
unsigned long  g_cs_phys;       /* fizyczny adres naszego segmentu */
unsigned short g_orig_cs;       /* oryginalne CS do powrotu */
unsigned short g_orig_ss;       /* oryginalne SS */
unsigned short g_orig_sp;       /* oryginalne SP */

/* Funkcja z pm_call.asm - wchodzi w PM i wywoluje aplikacje */
extern void __far pm_call_app(void);

/* Pomocnicze - odczyt rejestrów segmentowych przez Watcom pragma aux */
unsigned short get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax] modify [ax];

unsigned short get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];

unsigned short get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * load_ne: otwiera plik NE, parsuje naglowki,
 *           laduje segment kodu do pamieci DOS
 * Zwraca 0=ok, -1=blad
 * ============================================================ */
int load_ne(const char *filename)
{
    FILE        *f;
    MZ_HEADER    mz;
    NE_HEADER    ne;
    NE_SEG_ENTRY seg;
    long         seg_file_offset;
    void __far  *buf;
    unsigned long phys;

    f = fopen(filename, "rb");
    if (!f) {
        printf("ERROR: cannot open %s\n", filename);
        return -1;
    }

    /* --- MZ header --- */
    if (fread(&mz, sizeof(mz), 1, f) != 1) goto err;
    if (mz.e_magic != 0x5A4D) {
        printf("ERROR: not an MZ file\n");
        goto err;
    }
    printf("MZ OK, NE offset = 0x%04lX\n", mz.e_lfanew);

    /* --- NE header --- */
    if (fseek(f, mz.e_lfanew, SEEK_SET) != 0) goto err;
    if (fread(&ne, sizeof(ne), 1, f) != 1) goto err;
    if (ne.ne_magic != 0x454E) {
        printf("ERROR: not an NE file (magic=0x%04X)\n", ne.ne_magic);
        goto err;
    }
    printf("NE OK: segs=%u align=%u expver=0x%04X\n",
           ne.ne_cseg, ne.ne_align, ne.ne_expver);
    printf("Entry: CS=seg#%u IP=0x%04X\n", ne.ne_cs, ne.ne_ip);

    /* --- Wpis tablicy segmentow dla segmentu CS --- */
    seg_file_offset = mz.e_lfanew
                    + ne.ne_segtab
                    + (long)(ne.ne_cs - 1) * sizeof(NE_SEG_ENTRY);

    if (fseek(f, seg_file_offset, SEEK_SET) != 0) goto err;
    if (fread(&seg, sizeof(seg), 1, f) != 1) goto err;

    printf("Code seg: sector=%u size=%u flags=0x%04X\n",
           seg.ns_sector, seg.ns_cbseg, seg.ns_flags);

    /* --- Alokuj pamiec DOS (paragrafami przez _dos_allocmem) --- */
    {
        unsigned paragraphs = (seg.ns_cbseg + 15) / 16;
        unsigned seg_addr;

        if (_dos_allocmem(paragraphs, &seg_addr) != 0) {
            printf("ERROR: _dos_allocmem failed\n");
            goto err;
        }

        /* Zaladuj kod do zaalokowanego segmentu DOS */
        {
            long code_off = (long)seg.ns_sector << ne.ne_align;
            unsigned char __far *dst = MK_FP(seg_addr, 0);
            unsigned char *tmp;

            printf("Loading code from file offset 0x%04lX...\n", code_off);

            tmp = (unsigned char *)malloc(seg.ns_cbseg);
            if (!tmp) { printf("ERROR: malloc\n"); goto err; }

            fseek(f, code_off, SEEK_SET);
            fread(tmp, seg.ns_cbseg, 1, f);

            /* Kopiuj do segmentu DOS */
            _fmemcpy(dst, tmp, seg.ns_cbseg);
            free(tmp);
        }

        /* Fizyczny adres = segment * 16 */
        g_app_phys  = (unsigned long)seg_addr << 4;
        g_code_size = seg.ns_cbseg;
        g_entry_ip  = ne.ne_ip;

        printf("App loaded: phys=0x%05lX size=%u entry_ip=0x%04X\n",
               g_app_phys, g_code_size, g_entry_ip);
    }

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
    printf("=== NE Loader STEP3 ===\n");

    /* Zaladuj aplikacje NE */
    if (load_ne("NE_APP.EXE") != 0)
        return 1;

    /* Wypelnij zmienne wspoldzielone z asm */
    g_orig_cs = get_cs();
    g_orig_ss = get_ss();
    g_orig_sp = get_sp();
    g_cs_phys = (unsigned long)g_orig_cs << 4;

    printf("CS=0x%04X cs_phys=0x%05lX\n", g_orig_cs, g_cs_phys);
    printf("Entering PM and calling NE app...\n");

    /* Wejdz w PM i wywolaj aplikacje */
    pm_call_app();

    printf("App returned! STEP3 done.\n");
    return 0;
}
