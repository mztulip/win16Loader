/* rc_loader.c - generyczny loader zasobow NE
 * ETAP 18: rc_init / find_ne_resource / rc_load_all / rc_get / rc_size
 *
 * Kompilacja: wcc -ml -q rc_loader.c -fo=rc_loader.obj
 *
 * Architektura:
 *   rc_init()      - otwiera plik, czyta NE header, zapamietuje pozycje rsctab
 *   rc_load_all()  - jeden przebieg przez tablice zasobow:
 *                      RT_STRING -> KCB (format KCB_LAYOUT jak w STEP17)
 *                      RT_BITMAP -> bufor SEL_BITMAPS (format jak w STEP17)
 *                      RT_MENU / RT_DIALOG / RT_ACCEL -> RSC_HEAP (8KB DOS alloc)
 *   rc_get/size()  - dostep do zasobow wczytanych do RSC_HEAP
 *   find_ne_resource() - lookup pojedynczego zasobu (pomocnicze, eksportowane)
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "rc_loader.h"

/* ============================================================
 * Minimalne struktury NE (pelne definicje sa tez w loader.c,
 * tutaj tylko pola potrzebne do parsowania tablicy zasobow).
 * ============================================================ */
#pragma pack(push, 1)
typedef struct {
    unsigned short e_magic;
    unsigned char  e_pad[58];
    unsigned long  e_lfanew;
} RC_MZ;

typedef struct {
    unsigned short ne_magic;    /* 0x454E */
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
    unsigned short ne_rsrctab;  /* offset od poczatku NE header do tablicy zasobow */
} RC_NE;
#pragma pack(pop)

/* ============================================================
 * KCB_LAYOUT (mini-kopia z loader.c) - tylko pola uzywane przez RT_STRING
 * ============================================================ */
#pragma pack(push, 1)
typedef struct {
    unsigned short app_hinstance;
    unsigned short next_dyn_sel;
    unsigned long  heap_phys;
    unsigned long  heap_next;
    unsigned long  heap_end;
    unsigned short local_heap_off;
    unsigned char  rsc_nblocks;
    unsigned char  rsc_pad;
    unsigned short rsc_block_ids[2];
    unsigned short rsc_block_sizes[2];
    unsigned long  tick_ms;
    /* bajty 32..255: surowe dane RT_STRING */
} RC_KCB;
#pragma pack(pop)

#define KCB_RSC_DATA_OFF  32
#define RSC_STR_MAX_BLOCKS 2

/* ============================================================
 * Stale bitmap buffer (z loader.c)
 * ============================================================ */
#define MAX_BITMAPS    86
#define BMP_BUF_PARA   3584                     /* 56KB */
#define BMP_BUF_HDR    (4 + MAX_BITMAPS * 2)   /* 176 B naglowka */

/* ============================================================
 * RSC_HEAP - wewnetrzny bufor dla RT_MENU / RT_DIALOG / RT_ACCEL
 * ============================================================ */
#define RSC_DIR_MAX    16
#define RSC_HEAP_PARA  512   /* 8KB = 512 paragrafy */

static struct {
    unsigned short type;
    unsigned short id;
    unsigned short off;   /* offset w RSC_HEAP (bajty) */
    unsigned short size;
} s_dir[RSC_DIR_MAX];
static unsigned s_dir_n  = 0;
static unsigned s_rsc_seg = 0;   /* DOS segment RSC_HEAP (z _dos_allocmem) */
static unsigned s_rsc_used = 0;  /* bajty uzyte w RSC_HEAP */

/* ============================================================
 * Stan wewnetrzny parsera NE
 * ============================================================ */
static FILE          *s_f          = NULL;
static long           s_ne_off     = 0;
static long           s_rsctab_pos = 0;    /* pozycja w pliku: start tablicy zasobow */
static unsigned short s_align      = 0;    /* rscAlignShift */
static int            s_valid      = 0;    /* 1 jesli rc_init() powiodlo sie */

/* ============================================================
 * Pomocnicze: odczyt zasobu z pliku do bufora far
 * ============================================================ */
static void read_resource(long file_off, unsigned short size,
                          unsigned char __far *dst)
{
    unsigned short i;
    fseek(s_f, file_off, SEEK_SET);
    for (i = 0; i < size; i++) {
        int c = fgetc(s_f);
        if (c == EOF) break;
        dst[i] = (unsigned char)c;
    }
}

/* ============================================================
 * rc_init
 * ============================================================ */
void rc_init(const char *filename)
{
    RC_MZ mz;
    RC_NE ne;

    s_valid = 0;
    if (s_f) { fclose(s_f); s_f = NULL; }

    s_f = fopen(filename, "rb");
    if (!s_f) {
        printf("RC: nie mozna otworzyc %s\n", filename);
        return;
    }
    if (fread(&mz, sizeof(mz), 1, s_f) != 1) goto fail;
    s_ne_off = (long)mz.e_lfanew;
    fseek(s_f, s_ne_off, SEEK_SET);
    if (fread(&ne, sizeof(ne), 1, s_f) != 1) goto fail;
    if (ne.ne_magic != 0x454Eu) {
        printf("RC: %s nie jest NE\n", filename);
        goto fail;
    }
    s_rsctab_pos = s_ne_off + (long)ne.ne_rsrctab;
    fseek(s_f, s_rsctab_pos, SEEK_SET);
    if (fread(&s_align, 2, 1, s_f) != 1) goto fail;
    s_valid = 1;
    return;

fail:
    fclose(s_f);
    s_f = NULL;
}

/* ============================================================
 * find_ne_resource
 * Przeglada tablice zasobow NE i szuka type_id + res_id.
 * Zwraca 1 jesli znaleziony.
 * ============================================================ */
int find_ne_resource(unsigned short type_id, unsigned short res_id,
                     long *out_file_off, unsigned short *out_size)
{
    if (!s_valid) return 0;

    fseek(s_f, s_rsctab_pos + 2L, SEEK_SET);  /* +2: pomin rscAlignShift */

    for (;;) {
        unsigned short rt_id, rt_count;
        unsigned short j;

        if (fread(&rt_id, 2, 1, s_f) != 1) break;
        if (rt_id == 0) break;
        if (fread(&rt_count, 2, 1, s_f) != 1) break;
        fseek(s_f, 4L, SEEK_CUR);   /* pomin rt_proc (4 bajty) */

        for (j = 0; j < rt_count; j++) {
            unsigned short rn_offset, rn_length, rn_flags, rn_id;
            if (fread(&rn_offset, 2, 1, s_f) != 1) return 0;
            if (fread(&rn_length, 2, 1, s_f) != 1) return 0;
            if (fread(&rn_flags,  2, 1, s_f) != 1) return 0;
            if (fread(&rn_id,     2, 1, s_f) != 1) return 0;
            fseek(s_f, 4L, SEEK_CUR);  /* pomin rn_handle/rn_usage */

            if (rt_id == type_id && (rn_id & 0x7FFFu) == res_id) {
                *out_file_off = (long)rn_offset << s_align;
                *out_size     = (unsigned short)((unsigned long)rn_length << s_align);
                return 1;
            }
        }
    }
    return 0;
}

/* ============================================================
 * rc_load_all
 * Jeden przebieg przez tablice zasobow NE.
 * RT_STRING  -> KCB
 * RT_BITMAP  -> bufor SEL_BITMAPS
 * RT_MENU / RT_DIALOG / RT_ACCEL -> RSC_HEAP
 * ============================================================ */
int rc_load_all(unsigned long kcb_phys, unsigned long bmp_phys)
{
    /* Bufory docelowe */
    RC_KCB __far       *kcb      = (RC_KCB __far *)MK_FP((unsigned)(kcb_phys >> 4), 0);
    unsigned char __far *kcb_raw = (unsigned char __far *)MK_FP((unsigned)(kcb_phys >> 4), 0);
    unsigned char __far *bmp_buf = NULL;
    unsigned short kcb_data_off  = KCB_RSC_DATA_OFF;
    unsigned short kcb_nb        = 0;

    /* Offsety bitmap (static: w DS; SS!=DS w duzym modelu -> nie moze byc na stosie) */
    static unsigned short bmp_offsets[MAX_BITMAPS];
    unsigned short bmp_data_off = (unsigned short)BMP_BUF_HDR;
    unsigned short bmp_count    = 0;
    int i;

    if (!s_valid) return 0;

    /* Inicjalizacja bufora bitmap */
    if (bmp_phys != 0) {
        bmp_buf = (unsigned char __far *)MK_FP((unsigned)(bmp_phys >> 4), 0);
        for (i = 0; i < MAX_BITMAPS; i++) bmp_offsets[i] = 0;
        for (i = 0; i < BMP_BUF_HDR; i++) bmp_buf[i] = 0;
    }

    /* Alokacja RSC_HEAP jesli jeszcze nie zrobiono */
    if (s_rsc_seg == 0) {
        if (_dos_allocmem(RSC_HEAP_PARA, &s_rsc_seg) != 0) {
            printf("RC: brak pamieci na RSC_HEAP (%u para)\n", RSC_HEAP_PARA);
            s_rsc_seg = 0;
        } else {
            printf("RC: RSC_HEAP seg=0x%04X (%uB)\n", s_rsc_seg, RSC_HEAP_PARA * 16);
        }
    }
    s_rsc_used = 0;
    s_dir_n    = 0;

    /* Jeden przebieg przez tablice zasobow */
    fseek(s_f, s_rsctab_pos + 2L, SEEK_SET);

    for (;;) {
        unsigned short rt_id, rt_count;
        unsigned short j;

        if (fread(&rt_id, 2, 1, s_f) != 1) break;
        if (rt_id == 0) break;
        if (fread(&rt_count, 2, 1, s_f) != 1) break;
        fseek(s_f, 4L, SEEK_CUR);

        for (j = 0; j < rt_count; j++) {
            unsigned short rn_offset, rn_length, rn_flags, rn_id;
            long file_off;
            unsigned short size;
            long save_pos;

            if (fread(&rn_offset, 2, 1, s_f) != 1) goto done;
            if (fread(&rn_length, 2, 1, s_f) != 1) goto done;
            if (fread(&rn_flags,  2, 1, s_f) != 1) goto done;
            if (fread(&rn_id,     2, 1, s_f) != 1) goto done;
            fseek(s_f, 4L, SEEK_CUR);

            file_off = (long)rn_offset << s_align;
            size     = (unsigned short)((unsigned long)rn_length << s_align);
            save_pos = ftell(s_f);

            /* ---- RT_STRING -> KCB ---- */
            if (rt_id == RT_STRING && kcb_nb < RSC_STR_MAX_BLOCKS) {
                unsigned short block_id = rn_id & 0x7FFFu;
                if (size <= 228u && kcb_data_off + size <= 256u) {
                    unsigned char __far *dst = kcb_raw + kcb_data_off;
                    read_resource(file_off, size, dst);
                    kcb->rsc_block_ids[kcb_nb]   = block_id;
                    kcb->rsc_block_sizes[kcb_nb] = size;
                    kcb_data_off = (unsigned short)(kcb_data_off + size);
                    kcb_nb++;
                    printf("RC: RT_STRING blok %u (%uB)\n", block_id, size);
                }
            }

            /* ---- RT_BITMAP -> SEL_BITMAPS ---- */
            else if (rt_id == RT_BITMAP && bmp_buf != NULL) {
                unsigned short id = rn_id & 0x7FFFu;
                if (id >= 1 && id <= MAX_BITMAPS &&
                    (unsigned long)bmp_data_off + size < (unsigned long)BMP_BUF_PARA * 16u) {
                    read_resource(file_off, size, bmp_buf + bmp_data_off);
                    bmp_offsets[id - 1] = bmp_data_off;
                    bmp_data_off = (unsigned short)(bmp_data_off + size);
                    bmp_count++;
                }
            }

            /* ---- RT_MENU / RT_DIALOG / RT_ACCEL -> RSC_HEAP ---- */
            else if ((rt_id == RT_MENU || rt_id == RT_DIALOG || rt_id == RT_ACCEL)
                     && s_rsc_seg != 0 && s_dir_n < RSC_DIR_MAX
                     && (unsigned long)s_rsc_used + size <= (unsigned long)RSC_HEAP_PARA * 16u) {
                unsigned char __far *dst =
                    (unsigned char __far *)MK_FP(s_rsc_seg, s_rsc_used);
                read_resource(file_off, size, dst);
                s_dir[s_dir_n].type = rt_id;
                s_dir[s_dir_n].id   = rn_id & 0x7FFFu;
                s_dir[s_dir_n].off  = s_rsc_used;
                s_dir[s_dir_n].size = size;
                s_dir_n++;
                s_rsc_used = (unsigned short)(s_rsc_used + size);
                printf("RC: typ=0x%04X id=%u (%uB) -> RSC_HEAP\n",
                       rt_id, rn_id & 0x7FFu, size);
            }

            fseek(s_f, save_pos, SEEK_SET);
        }
    }

done:
    /* Zapisz naglowek bufora bitmap */
    if (bmp_buf != NULL) {
        bmp_buf[0] = (unsigned char)(bmp_count & 0xFF);
        bmp_buf[1] = (unsigned char)(bmp_count >> 8);
        bmp_buf[2] = 0; bmp_buf[3] = 0;
        for (i = 0; i < MAX_BITMAPS; i++) {
            bmp_buf[4 + i*2]     = (unsigned char)(bmp_offsets[i] & 0xFF);
            bmp_buf[4 + i*2 + 1] = (unsigned char)(bmp_offsets[i] >> 8);
        }
        printf("RC: %u RT_BITMAP, bufor phys=0x%05lX\n", bmp_count, bmp_phys);
    }

    kcb->rsc_nblocks = (unsigned char)kcb_nb;
    printf("RC: %u RT_STRING blokow w KCB\n", kcb_nb);

    fclose(s_f);
    s_f = NULL;
    return 1;
}

/* ============================================================
 * rc_get / rc_size
 * ============================================================ */
unsigned char __far *rc_get(unsigned short type, unsigned short id)
{
    unsigned i;
    if (s_rsc_seg == 0) return NULL;
    for (i = 0; i < s_dir_n; i++) {
        if (s_dir[i].type == type && s_dir[i].id == id)
            return (unsigned char __far *)MK_FP(s_rsc_seg, s_dir[i].off);
    }
    return NULL;
}

unsigned short rc_size(unsigned short type, unsigned short id)
{
    unsigned i;
    for (i = 0; i < s_dir_n; i++) {
        if (s_dir[i].type == type && s_dir[i].id == id)
            return s_dir[i].size;
    }
    return 0;
}

/* ============================================================
 * rc_copy_menu_to_kcb
 * Kopiuje pierwszy RT_MENU z RSC_HEAP do KCB[292..511].
 * Format: [292]=n_menus, [293-294]=menu_id, [295-296]=menu_size, [297+]=data
 * ============================================================ */
int rc_copy_menu_to_kcb(unsigned long kcb_phys)
{
    unsigned char __far *kcb_raw;
    unsigned char __far *src;
    unsigned short menu_id, menu_size, j;
    unsigned i;

    kcb_raw = (unsigned char __far *)MK_FP((unsigned)(kcb_phys >> 4), 0);
    kcb_raw[292] = 0;   /* domyslnie brak menu */

    if (s_rsc_seg == 0 || s_dir_n == 0) return 0;

    for (i = 0; i < s_dir_n; i++) {
        if (s_dir[i].type == RT_MENU) break;
    }
    if (i >= s_dir_n) return 0;

    menu_id   = s_dir[i].id;
    menu_size = s_dir[i].size;
    if (menu_size > 215u) menu_size = 215u;  /* max: 512-297=215 */

    src = (unsigned char __far *)MK_FP(s_rsc_seg, s_dir[i].off);

    kcb_raw[292] = 1;
    kcb_raw[293] = (unsigned char)(menu_id & 0xFF);
    kcb_raw[294] = (unsigned char)(menu_id >> 8);
    kcb_raw[295] = (unsigned char)(menu_size & 0xFF);
    kcb_raw[296] = (unsigned char)(menu_size >> 8);
    for (j = 0; j < menu_size; j++)
        kcb_raw[297 + j] = src[j];

    printf("RC: RT_MENU id=%u size=%u -> KCB[297]\n", menu_id, menu_size);
    return 1;
}
