/* ne.h - struktury formatu NE (New Executable, Win16) */
#ifndef NE_H
#define NE_H

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
    unsigned short target1;
    unsigned short target2;
} NE_RELOC;

#pragma pack(pop)

/* Typy relokacji (bity 0-1 pola reloc_type) */
#define RTYPE_INTERNALREF    0
#define RTYPE_IMPORTORDINAL  1
#define RTYPE_IMPORTNAME     2
#define RTYPE_OSFIXUP        3
#define RTYPE_ADDITIVE       0x04

/* Typy adresow (addr_type) */
#define ATYPE_LOBYTE   0
#define ATYPE_SEG16    2
#define ATYPE_FAR_PTR  3
#define ATYPE_OFF16    5

/* ns_flags */
#define NSDATA   0x0001
#define NSITER   0x0008   /* dane w rekordach iterowanych (powtorzenia) */
#define NSRELOC  0x0100

#endif /* NE_H */
