/*
 * module.c - ogolny loader modulow NE (aplikacja + DLL) do pamieci rozszerzonej.
 *
 * Dla kazdego modulu:
 *   - segmenty czytane do bufora DOS, fixupy, kopia do XMS (INT 15h AH=87h),
 *     kolejne selektory GDT (segment n -> first_sel + (n-1)*8),
 *   - kopia naglowka NE (+ nonresident names) w XMS = hModule (selektor),
 *     w tablicy zasobow pola rn_handle:rn_usage = adres fizyczny danych zasobu w XMS,
 *   - wpis w MODTAB (SEL_MODTAB) czytany w PM przez KERNEL i int3f_handler.
 * Importy:
 *   - funkcje modulow -> thunk INT 3F (DS = DGROUP modulu docelowego),
 *   - stale KERNEL (__AHINCR, __WINFLAGS, __0040H ...) -> wartosci/selektory,
 *   - brakujace moduly/ordinale -> stub (log "U!mm:oooo", AX=DX=0, zdejmuje
 *     argumenty; rozmiar argumentow z WIN16API.DB generowanego z .spec Wine).
 *
 * Kompilacja: wcc -ml -q module.c (czesc loader.exe, tryb rzeczywisty)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <io.h>
#include <conio.h>
#include "serial.h"
#include "ne.h"
#include "module.h"

#define MAX_ORD        1024     /* max ordinal w tablicy wejsc modulu */
#define MAX_GDTP       256
#define MAX_MODREFS    32
#define MAX_SEGS       64
#define THUNK_SIZE     8
#define HASH_SIZE      4096     /* > max liczby thunkow */
#define MAX_STUBS      1024
#define CHUNK          0x8000u  /* bufor do strumieniowania plik -> XMS */

/* Wartosci dla pm_call.asm / pm_helpers.c */
GDT_PATCH      g_gdtp[MAX_GDTP];
unsigned short g_ngdtp;
unsigned short g_entry_ip, g_entry_cs, g_app_ds, g_init_sp;
unsigned short g_ndllinit;
unsigned short g_dllinit[MAX_DLLINIT * 4];
unsigned short g_rmbuf_seg;
unsigned short g_app_stack, g_app_heap, g_app_heapstart;

extern unsigned long g_ext_mem_kb;

/* Przydzialy programu w kolejnosci ich wykonania. Ponowne uruchomienie zakonczonego programu
 * wykonuje te same kroki, wiec dostaje te same adresy: XMS i selektory nie uciekaja. */
#define MAX_ALLOCLOG 700
struct ALLOCLOG {
    unsigned nx, ns;
    unsigned char bad;
    struct { unsigned long phys, size; } x[MAX_ALLOCLOG];
    unsigned s[64 + 8];
};
static struct ALLOCLOG __far *g_rec, __far *g_replay;
static unsigned g_rp_x, g_rp_s, g_replay_idx, g_log_dll;

typedef struct {
    char           name[10];
    unsigned       flags;
    unsigned       nsegs;
    unsigned       first_sel;
    unsigned       dgroup_sel;
    unsigned short __far *ent_off;   /* [MAX_ORD] */
    unsigned char  __far *ent_seg;   /* [MAX_ORD]: 0=brak, 0xFE=stala */
    unsigned char  __far *ent_flg;   /* [MAX_ORD]: flagi wejscia (bit1 = shared data) */
    struct ALLOCLOG __far *log;      /* programy: przydzialy do ponownego uruchomienia */
} MOD;

static MOD            g_mods[MAX_MODS];
static unsigned       g_nmods;
static unsigned       g_next_sel;
static unsigned long  g_xms_next;
static unsigned long  g_xms_limit;

static unsigned       g_modtab_seg;     /* DOS: MODTAB (MAX_MODS * MT_SIZE) */
static unsigned       g_tmp_seg;        /* DOS: 64KB bufor segmentu */
static unsigned       g_chunk_seg;      /* DOS: 32KB bufor strumieniowy */

static unsigned       g_thunk_seg;
static unsigned long  g_thunk_phys;
static unsigned       g_thunk_max;
static unsigned       g_nthunks;
static unsigned long  __far *g_hash_key;   /* (mod<<16)|ord, 0xFFFFFFFF = wolny */
static unsigned short __far *g_hash_val;   /* offset thunka */

static struct { unsigned short thunk; unsigned char mod; unsigned short ord; } __far *g_stubs;
static unsigned       g_nstubs;

static char           g_sysdir[80];
static char           g_appdir[80];

/* ============================================================ */
static void mlog(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    serial_puts(buf);
}

static void upcase(char *s)
{
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
}

static void dir_of(const char *path, char *out)
{
    const char *p, *last = 0;
    for (p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') last = p;
    if (!last) { out[0] = 0; return; }
    memcpy(out, path, (size_t)(last - path + 1));
    out[last - path + 1] = 0;
}

/* ============================================================
 * XMS: bump allocator + kopia przez BIOS INT 15h AH=87h
 * ============================================================ */
static unsigned long xms_alloc(unsigned long size)
{
    /* Kazdy blok na wlasnych stronach 4KB: kod i dane (stos!) na jednej stronie to w QEMU
     * wolna sciezka przy kazdym zapisie - wykrywanie kodu samomodyfikujacego (docs/wydajnosc_gdi.md) */
    unsigned long p = (g_xms_next + 4095UL) & ~4095UL;
    if (g_replay) {
        if (g_rp_x < g_replay->nx && size <= g_replay->x[g_rp_x].size) return g_replay->x[g_rp_x++].phys;
        mlog("ERROR: inny uklad pliku przy ponownym ladowaniu\n");
        return 0;
    }
    if (p + size > g_xms_limit) {
        mlog("ERROR: brak XMS (potrzeba %lu B @%06lX, limit %06lX)\n", size, p, g_xms_limit);
        return 0;
    }
    g_xms_next = p + size;
    if (g_rec) {
        if (g_rec->nx < MAX_ALLOCLOG) {
            g_rec->x[g_rec->nx].phys = p;
            g_rec->x[g_rec->nx++].size = size;
        } else g_rec->bad = 1;
    }
    return p;
}

static int xms_copy_chunk(unsigned long dst, unsigned long src, unsigned bytes)
{
    static unsigned char gdt[48];
    union REGS r;
    struct SREGS s;
    unsigned char __far *g = (unsigned char __far *)gdt;
    int i;
    for (i = 0; i < 48; i++) g[i] = 0;
    /* deskryptor 2 (zrodlo) */
    g[16] = 0xFF; g[17] = 0xFF;
    g[18] = (unsigned char)src; g[19] = (unsigned char)(src >> 8);
    g[20] = (unsigned char)(src >> 16); g[21] = 0x93; g[23] = (unsigned char)(src >> 24);
    /* deskryptor 3 (cel) */
    g[24] = 0xFF; g[25] = 0xFF;
    g[26] = (unsigned char)dst; g[27] = (unsigned char)(dst >> 8);
    g[28] = (unsigned char)(dst >> 16); g[29] = 0x93; g[31] = (unsigned char)(dst >> 24);
    segread(&s);
    s.es   = FP_SEG(g);
    r.x.si = FP_OFF(g);
    r.h.ah = 0x87;
    r.x.cx = (bytes + 1u) / 2u;
    int86x(0x15, &r, &r, &s);
    if (r.x.cflag) { mlog("ERROR: INT15/87 ah=%02X\n", r.h.ah); return -1; }
    return 0;
}

/* Kopiuj len bajtow z pamieci DOS (far) do XMS */
static int xms_write(unsigned long dst, void __far *src, unsigned long len)
{
    unsigned long sp = ((unsigned long)FP_SEG(src) << 4) + FP_OFF(src);
    while (len) {
        unsigned n = (len > CHUNK) ? CHUNK : (unsigned)len;
        if (xms_copy_chunk(dst, sp, n)) return -1;
        dst += n; sp += n; len -= n;
    }
    return 0;
}

/* Kopiuj len bajtow z pliku (od biezacej pozycji) do XMS */
static int xms_from_file(FILE *f, unsigned long dst, unsigned long len)
{
    unsigned char __far *buf = (unsigned char __far *)MK_FP(g_chunk_seg, 0);
    while (len) {
        unsigned n = (len > CHUNK) ? CHUNK : (unsigned)len;
        unsigned got = (unsigned)fread(buf, 1, n, f);
        if (got < n) _fmemset(buf + got, 0, n - got);
        if (xms_copy_chunk(dst, ((unsigned long)g_chunk_seg << 4), n)) return -1;
        dst += n; len -= n;
    }
    return 0;
}

/* ============================================================
 * GDT / selektory
 * ============================================================ */
static void gdt_add(unsigned sel, unsigned long base, unsigned long limit,
                    unsigned char access, unsigned char big)
{
    unsigned i;
    for (i = 0; i < g_ngdtp; i++) if (g_gdtp[i].sel == sel) break;
    if (i == g_ngdtp) {
        if (g_ngdtp >= MAX_GDTP) { mlog("ERROR: GDT patch list pelna\n"); return; }
        g_ngdtp++;
    }
    g_gdtp[i].sel = sel; g_gdtp[i].base = base; g_gdtp[i].limit = limit;
    g_gdtp[i].access = access; g_gdtp[i].big = big;
}

static unsigned sel_alloc(unsigned n)
{
    unsigned s = g_next_sel;
    if (g_replay) return g_rp_s < g_replay->ns ? g_replay->s[g_rp_s++] : 0;
    g_next_sel += n * 8u;
    if (g_rec) {
        if (g_rec->ns < sizeof(g_rec->s) / sizeof(g_rec->s[0])) g_rec->s[g_rec->ns++] = s;
        else g_rec->bad = 1;
    }
    return s;
}

/* ============================================================
 * MODTAB
 * ============================================================ */
static unsigned char __far *mt_entry(unsigned idx)
{
    return (unsigned char __far *)MK_FP(g_modtab_seg, idx * MT_SIZE);
}

static void mt_set_word(unsigned idx, unsigned off, unsigned v)
{
    *(unsigned short __far *)(mt_entry(idx) + off) = (unsigned short)v;
}

static int find_mod(const char *name)
{
    unsigned i;
    for (i = 0; i < g_nmods; i++)
        if (stricmp(g_mods[i].name, name) == 0) return (int)i;
    return -1;
}

static int new_mod(const char *name, unsigned flags, const char *path)
{
    unsigned idx;
    MOD *m;
    unsigned char __far *e;
    if (g_nmods >= MAX_MODS) { mlog("ERROR: za duzo modulow\n"); return -1; }
    idx = g_nmods++;
    m = &g_mods[idx];
    memset(m, 0, sizeof(*m));
    strncpy(m->name, name, 9);
    upcase(m->name);
    m->flags = flags;
    m->ent_off = (unsigned short __far *)_fmalloc(MAX_ORD * 2);
    m->ent_seg = (unsigned char  __far *)_fmalloc(MAX_ORD);
    m->ent_flg = (unsigned char  __far *)_fmalloc(MAX_ORD);
    if (!m->ent_off || !m->ent_seg || !m->ent_flg) { mlog("ERROR: brak pamieci (entry)\n"); return -1; }
    _fmemset(m->ent_off, 0, MAX_ORD * 2);
    _fmemset(m->ent_seg, 0, MAX_ORD);
    _fmemset(m->ent_flg, 0, MAX_ORD);
    e = mt_entry(idx);
    _fmemset(e, 0, MT_SIZE);
    _fstrncpy((char __far *)e + MT_NAME, m->name, 9);
    _fstrncpy((char __far *)e + MT_PATH, path ? path : "", 79);
    mt_set_word(idx, MT_FLAGS, flags);
    return (int)idx;
}

/* ============================================================
 * Thunki
 * ============================================================ */
static unsigned hash_slot(unsigned long key)
{
    unsigned h = (unsigned)((key >> 16) * 40503u) ^ (unsigned)key;
    h = (h * 2654u) & (HASH_SIZE - 1);
    while (g_hash_key[h] != 0xFFFFFFFFUL && g_hash_key[h] != key)
        h = (h + 1) & (HASH_SIZE - 1);
    return h;
}

/* Thunk dla (mod, ord): istniejacy albo nowy. stub=1 -> thunk stuba. */
static int get_thunk(unsigned mod, unsigned ord, unsigned sel, unsigned off,
                     int stub, unsigned short *out)
{
    unsigned long key = ((unsigned long)mod << 16) | ord;
    unsigned h = hash_slot(key);
    unsigned char __far *p;
    if (g_hash_key[h] == key) { *out = g_hash_val[h]; return 0; }
    if (g_nthunks >= g_thunk_max) { mlog("ERROR: za duzo thunkow\n"); return -1; }
    *out = (unsigned short)(g_nthunks * THUNK_SIZE);
    p = (unsigned char __far *)MK_FP(g_thunk_seg, *out);
    p[0] = 0xCD; p[1] = 0x3F;
    if (stub) {
        p[2] = 0xFF;
        p[3] = (unsigned char)ord; p[4] = (unsigned char)(ord >> 8);
        p[5] = 0;                  /* bajty argumentow - uzupelnia stubs_resolve() */
        p[6] = (unsigned char)mod;
        if (g_nstubs < MAX_STUBS) {
            g_stubs[g_nstubs].thunk = *out;
            g_stubs[g_nstubs].mod   = (unsigned char)mod;
            g_stubs[g_nstubs].ord   = (unsigned short)ord;
            g_nstubs++;
        }
    } else {
        p[2] = (unsigned char)mod;
        p[3] = (unsigned char)off; p[4] = (unsigned char)(off >> 8);
        p[5] = (unsigned char)sel; p[6] = (unsigned char)(sel >> 8);
    }
    p[7] = 0;
    g_nthunks++;
    g_hash_key[h] = key;
    g_hash_val[h] = *out;
    return 0;
}

/* Uzupelnij rozmiar argumentow stubow z WIN16API.DB (linie: "MOD ORD ARGS NAME") */
static void stubs_resolve(void)
{
    char path[96], line[96], mod[16], name[48];
    unsigned ord, args, i, found = 0;
    FILE *f;
    if (!g_nstubs) return;
    sprintf(path, "%sWIN16API.DB", g_sysdir);
    f = fopen(path, "r");
    if (!f) { mlog("WARN: brak %s - stuby nie zdejmuja argumentow!\n", path); return; }
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%15s %u %u %47s", mod, &ord, &args, name) != 4) continue;
        for (i = 0; i < g_nstubs; i++) {
            if (g_stubs[i].ord != ord) continue;
            if (stricmp(g_mods[g_stubs[i].mod].name, mod) != 0) continue;
            *((unsigned char __far *)MK_FP(g_thunk_seg, g_stubs[i].thunk + 5)) =
                (unsigned char)args;
            mlog("STUB U!%04X:%04X %s.%u %s args=%u\n", g_stubs[i].mod, ord,
                 mod, ord, name, args);
            g_stubs[i].ord = 0xFFFF;   /* rozwiazany */
            found++;
        }
    }
    fclose(f);
    for (i = 0; i < g_nstubs; i++)
        if (g_stubs[i].ord != 0xFFFF)
            mlog("STUB U!%04X:%04X %s.%u ??? (brak w DB, args=0)\n",
                 g_stubs[i].mod, g_stubs[i].ord, g_mods[g_stubs[i].mod].name,
                 g_stubs[i].ord);
    mlog("Stuby: %u (opisane w DB: %u)\n", g_nstubs, found);
}

/* ============================================================
 * Rozwiazywanie importow
 * ============================================================ */
typedef struct { int is_const; unsigned sel, off; } TARGET;

/* Stale eksportowane przez KERNEL (equates) */
static int kernel_equate(unsigned ord, TARGET *t)
{
    unsigned v;
    switch (ord) {
    case 113: v = 3;           break;   /* __AHSHIFT */
    case 114: v = 8;           break;   /* __AHINCR */
    case 178: v = 0x0415;      break;   /* __WINFLAGS: PMODE|CPU386|STANDARD|80x87 */
    case 183: v = SEL_RM_0000; break;   /* __0000H */
    case 193: v = SEL_RM_0040; break;   /* __0040H */
    case 174: v = SEL_RM_A000; break;   /* __A000H */
    case 181: v = SEL_RM_B000; break;   /* __B000H */
    case 182: v = SEL_RM_B800; break;   /* __B800H */
    case 195: v = SEL_RM_C000; break;   /* __C000H */
    case 179: v = SEL_RM_D000; break;   /* __D000H */
    case 190: v = SEL_RM_E000; break;   /* __E000H */
    case 173:                           /* __ROMBIOS */
    case 194: v = SEL_RM_F000; break;   /* __F000H */
    default: return 0;
    }
    t->is_const = 1; t->sel = v; t->off = v;
    return 1;
}

/* Import po nazwie: tablica nazw importow programu (offset w pliku) -> ordinal w pliku modulu */
static unsigned long g_imptab_off;

static unsigned name_in_table(FILE *f, const char *want, unsigned long max)
{
    unsigned long used = 0;
    for (;;) {
        char nm[130];
        unsigned char len;
        unsigned short ord;
        if (fread(&len, 1, 1, f) != 1 || !len) return 0;
        if (fread(nm, 1, len, f) != len || fread(&ord, 2, 1, f) != 1) return 0;
        nm[len] = 0;
        if (ord && !stricmp(nm, want)) return ord;
        used += len + 3u;
        if (max && used >= max) return 0;
    }
}

/* Stale KERNEL (kernel_equate): nie sa eksportami pliku, wiec po nazwie trzeba je znac tutaj */
static const struct { const char *name; unsigned ord; } g_equate_names[] = {
    { "__AHSHIFT", 113 }, { "__AHINCR", 114 }, { "__WINFLAGS", 178 }, { "__ROMBIOS", 173 },
    { "__0000H", 183 }, { "__0040H", 193 }, { "__A000H", 174 }, { "__B000H", 181 },
    { "__B800H", 182 }, { "__C000H", 195 }, { "__D000H", 179 }, { "__E000H", 190 }, { "__F000H", 194 },
    { 0, 0 }
};

static unsigned import_name_ord(FILE *f, unsigned tmod, unsigned name_off)
{
    static char want[130], path[80];
    unsigned char len = 0;
    long pos = ftell(f);
    unsigned ord = 0;
    FILE *m;
    MZ_HEADER mz;
    NE_HEADER ne;
    fseek(f, g_imptab_off + name_off, SEEK_SET);
    fread(&len, 1, 1, f);
    fread(want, 1, len, f);
    want[len] = 0;
    fseek(f, pos, SEEK_SET);
    if (!stricmp(g_mods[tmod].name, "KERNEL")) {
        unsigned k;
        for (k = 0; g_equate_names[k].name; k++)
            if (!stricmp(want, g_equate_names[k].name)) return g_equate_names[k].ord;
    }
    _fstrncpy((char __far *)path, (char __far *)mt_entry(tmod) + MT_PATH, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (path[0] && (m = fopen(path, "rb")) != 0) {
        if (fread(&mz, sizeof(mz), 1, m) == 1 && !fseek(m, mz.e_lfanew, SEEK_SET) &&
            fread(&ne, sizeof(ne), 1, m) == 1) {
            fseek(m, mz.e_lfanew + ne.ne_restab, SEEK_SET);
            ord = name_in_table(m, want, 0);
            if (!ord && ne.ne_cbnrestab) {
                fseek(m, ne.ne_nrestab, SEEK_SET);
                ord = name_in_table(m, want, ne.ne_cbnrestab);
            }
        }
        fclose(m);
    }
    if (!ord) {
        if (stricmp(g_mods[tmod].name, "KERNEL") == 0) {
            if (stricmp(want, "__AHINCR") == 0) ord = 114;
            else if (stricmp(want, "__AHSHIFT") == 0) ord = 113;
            else if (stricmp(want, "CATCH") == 0) ord = 55;
            else if (stricmp(want, "THROW") == 0) ord = 56;
        }
    }
    if (!ord) mlog("  WARN: import po nazwie %s.%s nie znaleziony\n", g_mods[tmod].name, want);
    return ord;
}

static int resolve_import(unsigned tmod, unsigned ord, TARGET *t)
{
    MOD *m = &g_mods[tmod];
    unsigned short th;
    t->is_const = 0;
    if (stricmp(m->name, "KERNEL") == 0 && kernel_equate(ord, t)) return 0;
    if (!(m->flags & MTF_VIRTUAL) && ord < MAX_ORD && m->ent_seg[ord]) {
        unsigned char es = m->ent_seg[ord];
        if (es == 0xFE) {           /* stala w tablicy wejsc */
            t->is_const = 1; t->sel = t->off = m->ent_off[ord];
            return 0;
        }
        if (get_thunk(tmod, ord, m->first_sel + (es - 1u) * 8u, m->ent_off[ord], 0, &th))
            return -1;
    } else {
        if (get_thunk(tmod, ord, 0, 0, 1, &th)) return -1;
    }
    t->sel = SEL_THUNK_SEL;
    t->off = th;
    return 0;
}

/* ============================================================
 * Fixupy jednego segmentu (bufor DOS g_tmp_seg)
 * ============================================================ */
static unsigned short rd16(unsigned char __far *b, unsigned long o)
{
    return (unsigned short)(b[o] | ((unsigned short)b[o + 1] << 8));
}

static void wr16(unsigned char __far *b, unsigned long o, unsigned v)
{
    b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8);
}

/* Zapis wartosci (sel:off) pod adresem wg typu adresu */
static void put_addr(unsigned char __far *b, unsigned long o, unsigned atype,
                     unsigned sel, unsigned off)
{
    switch (atype & 0x7F) {
    case ATYPE_LOBYTE:  b[o] = (unsigned char)off; break;
    case ATYPE_SEG16:   wr16(b, o, sel); break;
    case ATYPE_OFF16:   wr16(b, o, off); break;
    case ATYPE_FAR_PTR: wr16(b, o, off); wr16(b, o + 2, sel); break;
    }
}

static int apply_fixups(FILE *f, long reloc_off, unsigned char __far *seg,
                        unsigned long seg_size, unsigned midx, int *mod_map,
                        unsigned nmodref)
{
    MOD *m = &g_mods[midx];
    unsigned short count, i;
    NE_RELOC rec;
    unsigned nbad = 0;

    fseek(f, reloc_off, SEEK_SET);
    if (fread(&count, 2, 1, f) != 1) return -1;

    for (i = 0; i < count; i++) {
        unsigned rtype, sel = 0, off = 0, size;
        TARGET t;
        if (fread(&rec, sizeof(rec), 1, f) != 1) return -1;
        rtype = rec.reloc_type & 3;
        size  = ((rec.addr_type & 0x7F) == ATYPE_FAR_PTR) ? 4 :
                ((rec.addr_type & 0x7F) == ATYPE_LOBYTE) ? 1 : 2;

        if (rtype == RTYPE_INTERNALREF) {
            if (rec.target1 == 0xFF) {        /* wejscie w segmencie movable */
                unsigned ord = rec.target2;
                if (ord < MAX_ORD && m->ent_seg[ord] && m->ent_seg[ord] != 0xFE) {
                    sel = m->first_sel + (m->ent_seg[ord] - 1u) * 8u;
                    off = m->ent_off[ord];
                } else { nbad++; continue; }
            } else if (rec.target1 >= 1 && rec.target1 <= m->nsegs) {
                sel = m->first_sel + (rec.target1 - 1u) * 8u;
                off = rec.target2;
            } else { nbad++; continue; }
        } else if (rtype == RTYPE_IMPORTORDINAL) {
            int tm;
            if (rec.target1 < 1 || rec.target1 > nmodref) { nbad++; continue; }
            tm = mod_map[rec.target1 - 1];
            if (tm < 0) { nbad++; continue; }
            if (resolve_import((unsigned)tm, rec.target2, &t)) return -1;
            sel = t.sel; off = t.off;
        } else if (rtype == RTYPE_IMPORTNAME) {
            int tm;
            unsigned ord;
            if (rec.target1 < 1 || rec.target1 > nmodref) { nbad++; continue; }
            tm = mod_map[rec.target1 - 1];
            if (tm < 0) { nbad++; continue; }
            ord = import_name_ord(f, (unsigned)tm, rec.target2);
            /* nieznana nazwa: ord 0 = stub, ktory zaloguje wywolanie zamiast skoku pod 0000:FFFF */
            if (resolve_import((unsigned)tm, ord, &t)) return -1;
            sel = t.sel; off = t.off;
        } else {
            continue;   /* OSFIXUP (emulacja FPU) - z koprocesorem nic nie robimy */
        }

        if (rec.reloc_type & RTYPE_ADDITIVE) {
            if ((unsigned long)rec.offset + size > seg_size) { nbad++; continue; }
            switch (rec.addr_type & 0x7F) {
            case ATYPE_LOBYTE: seg[rec.offset] = (unsigned char)(seg[rec.offset] + off); break;
            case ATYPE_SEG16:  wr16(seg, rec.offset, rd16(seg, rec.offset) + sel); break;
            case ATYPE_OFF16:  wr16(seg, rec.offset, rd16(seg, rec.offset) + off); break;
            case ATYPE_FAR_PTR:
                wr16(seg, rec.offset, rd16(seg, rec.offset) + off);
                wr16(seg, rec.offset + 2, sel);
                break;
            }
        } else {
            /* Lancuch: kazda lokacja zawiera offset nastepnej (0xFFFF = koniec) */
            unsigned long o = rec.offset;
            unsigned n = 0;
            while (o + size <= seg_size && n++ < 8192) {
                unsigned next = rd16(seg, o);
                put_addr(seg, o, rec.addr_type, sel, off);
                if (next == 0xFFFF || next == (unsigned)o) break;
                o = next;
            }
        }
    }
    if (nbad) mlog("  WARN: %u nierozwiazanych fixupow\n", nbad);
    return 0;
}

/* ============================================================
 * Ladowanie modulu
 * ============================================================ */
static FILE *open_module_file(const char *name, char *path_out, int system)
{
    static const char *ext[3] = { ".DLL", ".EXE", ".DRV" };
    const char *dirs[2];
    int d, e;
    FILE *f;
    dirs[0] = system ? g_sysdir : g_appdir;
    dirs[1] = system ? g_appdir : g_sysdir;
    for (d = 0; d < 2; d++)
        for (e = 0; e < 3; e++) {
            sprintf(path_out, "%s%s%s", dirs[d], name, ext[e]);
            if ((f = fopen(path_out, "rb")) != 0) return f;
        }
    return 0;
}

static int is_system_name(const char *n)
{
    return !stricmp(n, "KERNEL") || !stricmp(n, "USER") || !stricmp(n, "GDI");
}

static int mod_load(const char *name, const char *path_hint, int is_app);
static int mod_load_named(const char *name, const char *path) { return mod_load(name, path, 0); }

static int mod_load(const char *name, const char *path_hint, int is_app)
{
    FILE *f;
    MZ_HEADER mz;
    NE_HEADER ne;
    char path[96], full[96], mname[12];
    static char refnames[MAX_MODREFS][10];   /* tylko do uzycia przed rekurencja */
    static char reload_path[80];
    int replayed = 0;
    char myrefs[MAX_MODREFS][10];
    int mod_map[MAX_MODREFS];
    NE_SEG_ENTRY __far *segtab = 0;
    unsigned char __far *tmp = (unsigned char __far *)MK_FP(g_tmp_seg, 0);
    unsigned long blob_size, blob_phys, nres_off = 0;
    unsigned i, nref, idx, align;
    int system;
    MOD *m;

    if (name && !g_replay) {
        int k = find_mod(name);
        if (k >= 0) {
            unsigned char __far *e = mt_entry(k);
            if (!(*(unsigned short __far *)(e + MT_FLAGS) & MTF_UNLOADED)) return k;
            /* po FreeLibrary: od nowa, pod te same adresy (LibEntry zobaczy swieze dane) */
            if (!g_mods[k].log || g_mods[k].log->bad) return k;
            _fstrncpy((char __far *)reload_path, (char __far *)e + MT_PATH, sizeof(reload_path) - 1);
            *(unsigned short __far *)(e + MT_FLAGS) &= ~MTF_UNLOADED;
            path_hint = reload_path;
            g_replay = g_mods[k].log; g_replay_idx = k; g_rp_x = g_rp_s = 0;
            replayed = 1;
        }
    }
    system = name && is_system_name(name);

    if (path_hint) {
        strcpy(path, path_hint);
        f = fopen(path, "rb");
    } else {
        f = open_module_file(name, path, system);
    }
    if (!f) {
        if (is_app) { mlog("ERROR: nie mozna otworzyc %s\n", path_hint); return -1; }
        mlog("Modul %s: brak pliku -> wirtualny (importy = stuby)\n", name);
        return new_mod(name, MTF_DLL | MTF_VIRTUAL, "");
    }

    if (fread(&mz, sizeof(mz), 1, f) != 1 || mz.e_magic != 0x5A4D ||
        fseek(f, mz.e_lfanew, SEEK_SET) || fread(&ne, sizeof(ne), 1, f) != 1 ||
        ne.ne_magic != 0x454E) {
        mlog("ERROR: %s nie jest plikiem NE\n", path);
        fclose(f);
        return -1;
    }
    align = ne.ne_align ? ne.ne_align : 9;

    /* Kopia naglowka NE (do konca tablicy wejsc) + nonresident names */
    blob_size = (unsigned long)ne.ne_enttab + ne.ne_cbenttab;
    if (ne.ne_cbnrestab) {
        nres_off = blob_size;
        blob_size += ne.ne_cbnrestab;
    }
    if (blob_size > 0xFFF0UL) { mlog("ERROR: naglowek NE za duzy\n"); fclose(f); return -1; }
    fseek(f, mz.e_lfanew, SEEK_SET);
    fread(tmp, 1, (unsigned)(nres_off ? nres_off : blob_size), f);
    if (nres_off) {
        fseek(f, ne.ne_nrestab, SEEK_SET);
        fread(tmp + (unsigned)nres_off, 1, ne.ne_cbnrestab, f);
    }

    /* Nazwa modulu: z argumentu (import) albo z resident names (apka) */
    {
        unsigned char __far *rn = tmp + ne.ne_restab;
        unsigned len = rn[0] > 9 ? 9 : rn[0];
        for (i = 0; i < len; i++) mname[i] = (char)rn[1 + i];
        mname[len] = 0;
        if (name) { strncpy(mname, name, 9); mname[9] = 0; }
    }
    if (!_fullpath(full, path, sizeof(full))) strcpy(full, path);
    if (g_replay) idx = g_replay_idx;
    else {
        idx = (unsigned)new_mod(mname, (is_app ? 0 : MTF_DLL) | (system ? MTF_SYSTEM : 0), full);
        if ((int)idx < 0) { fclose(f); return -1; }
        if (is_app || g_log_dll) {
            g_mods[idx].log = (struct ALLOCLOG __far *)_fmalloc(sizeof(struct ALLOCLOG));
            if (g_mods[idx].log) _fmemset(g_mods[idx].log, 0, sizeof(struct ALLOCLOG));
            g_rec = g_mods[idx].log;
        }
    }
    m = &g_mods[idx];
    m->nsegs = ne.ne_cseg;
    if (m->nsegs > MAX_SEGS) { mlog("ERROR: za duzo segmentow\n"); fclose(f); return -1; }
    m->first_sel = sel_alloc(m->nsegs ? m->nsegs : 1);

    mlog("Modul %s [%u] z %s: segs=%u sel=%04X-%04X cs:ip=%u:%04X autodata=%u\n",
         m->name, idx, full, m->nsegs, m->first_sel,
         m->first_sel + (m->nsegs ? m->nsegs - 1 : 0) * 8, ne.ne_cs, ne.ne_ip,
         ne.ne_autodata);

    /* Tablica wejsc */
    {
        unsigned char __far *p = tmp + ne.ne_enttab;
        unsigned char __far *end = p + ne.ne_cbenttab;
        unsigned ord = 1;
        while (p < end && p[0]) {
            unsigned cnt = p[0], sg = p[1], j;
            p += 2;
            if (sg == 0) { ord += cnt; continue; }
            for (j = 0; j < cnt; j++, ord++) {
                unsigned es, eo, fl = p[0];
                if (sg == 0xFF) { es = p[3]; eo = p[4] | ((unsigned)p[5] << 8); p += 6; }
                else            { es = sg;   eo = p[1] | ((unsigned)p[2] << 8); p += 3; }
                if (ord < MAX_ORD) {
                    m->ent_seg[ord] = (unsigned char)es; m->ent_off[ord] = eo;
                    m->ent_flg[ord] = (unsigned char)fl;
                }
            }
        }
    }

    /* Zasoby: dane do XMS, adres fizyczny w rn_handle:rn_usage */
    if (ne.ne_rsrctab != ne.ne_restab) {
        unsigned char __far *r = tmp + ne.ne_rsrctab;
        unsigned shift = rd16(r, 0), nres = 0;
        unsigned long total = 0;
        r += 2;
        while (rd16(r, 0)) {
            unsigned cnt = rd16(r, 2), j;
            r += 8;
            for (j = 0; j < cnt; j++, r += 12) {
                unsigned long fo = (unsigned long)rd16(r, 0) << shift;
                unsigned long sz = (unsigned long)rd16(r, 2) << shift;
                unsigned long ph = xms_alloc(sz ? sz : 1);
                if (!ph) { fclose(f); return -1; }
                fseek(f, fo, SEEK_SET);
                if (sz && xms_from_file(f, ph, sz)) { fclose(f); return -1; }
                wr16(r, 8, (unsigned)ph);
                wr16(r, 10, (unsigned)(ph >> 16));
                total += sz; nres++;
            }
        }
        mlog("  Zasoby: %u (%lu B) -> XMS\n", nres, total);
    }

    /* Kopia NE w XMS -> hModule */
    blob_phys = xms_alloc(blob_size);
    if (!blob_phys || xms_write(blob_phys, tmp, blob_size)) { fclose(f); return -1; }
    {
        unsigned hsel = sel_alloc(1);
        gdt_add(hsel, blob_phys, blob_size - 1, 0x92, 0);
        mt_set_word(idx, MT_HMOD, hsel);
        mt_set_word(idx, MT_NRES_OFF, (unsigned)nres_off);
    }
    mt_set_word(idx, MT_FIRST_SEL, m->first_sel);
    mt_set_word(idx, MT_NSEGS, m->nsegs);

    /* Tablica segmentow (kopia far - tmp zostanie nadpisany przy rekurencji) */
    segtab = (NE_SEG_ENTRY __far *)_fmalloc(sizeof(NE_SEG_ENTRY) * (m->nsegs ? m->nsegs : 1));
    if (!segtab) { mlog("ERROR: brak pamieci\n"); fclose(f); return -1; }
    _fmemcpy(segtab, tmp + ne.ne_segtab, sizeof(NE_SEG_ENTRY) * m->nsegs);

    /* Nazwy modulow importowanych */
    nref = ne.ne_cmod > MAX_MODREFS ? MAX_MODREFS : ne.ne_cmod;
    for (i = 0; i < nref; i++) {
        unsigned char __far *nm = tmp + ne.ne_imptab + rd16(tmp, ne.ne_modtab + i * 2u);
        unsigned len = nm[0] > 9 ? 9 : nm[0], k;
        for (k = 0; k < len; k++) refnames[i][k] = (char)nm[1 + k];
        refnames[i][len] = 0;
        upcase(refnames[i]);
    }
    memcpy(myrefs, refnames, sizeof(myrefs));

    /* Zaleznosci (rekurencyjnie) - tmp jest potem nadpisywany */
    for (i = 0; i < nref; i++) {
        {   /* import zaladowany przy okazji nie nalezy do przydzialow programu */
            struct ALLOCLOG __far *sr = g_rec, __far *sp = g_replay;
            unsigned sx = g_rp_x, ss = g_rp_s, si = g_replay_idx, sl = g_log_dll;
            g_rec = 0; g_replay = 0; g_log_dll = 0;
            mod_map[i] = mod_load(myrefs[i], 0, 0);
            g_rec = sr; g_replay = sp; g_rp_x = sx; g_rp_s = ss; g_replay_idx = si; g_log_dll = sl;
        }
        if (mod_map[i] < 0) { fclose(f); return -1; }
        if (!replayed && !g_replay) {                    /* import statyczny: biblioteka zostaje */
            unsigned char __far *e = mt_entry(mod_map[i]);
            *(unsigned short __far *)(e + MT_FLAGS) |= MTF_PINNED;
            (*(unsigned short __far *)(e + MT_USAGE))++;
        }
    }

    g_imptab_off = mz.e_lfanew + (unsigned long)ne.ne_imptab;
    /* Segmenty */
    for (i = 0; i < m->nsegs; i++) {
        NE_SEG_ENTRY s = segtab[i];
        unsigned long fsz   = s.ns_sector ? (s.ns_cbseg ? s.ns_cbseg : 0x10000UL) : 0;
        unsigned long alloc = s.ns_minalloc ? s.ns_minalloc : 0x10000UL;
        unsigned long phys, fsz_rel = fsz;   /* fsz_rel: rozmiar w pliku (relokacje za nim) */
        unsigned sel = m->first_sel + i * 8u;
        int is_data = (s.ns_flags & NSDATA) != 0;
        if (fsz > alloc) alloc = fsz;
        if (ne.ne_autodata && i == ne.ne_autodata - 1u) {
            if (is_app) alloc += ne.ne_stack;
            if (is_app) g_app_heapstart = (unsigned)alloc;
            alloc += ne.ne_heap;
            if (alloc > 0x10000UL) alloc = 0x10000UL;
            m->dgroup_sel = sel;
        }
        _fmemset(tmp, 0, 0x8000u);
        _fmemset(tmp + 0x8000u, 0, 0x8000u);
        if (fsz) {
            fseek(f, (long)s.ns_sector << align, SEEK_SET);
            if (fsz > 0x8000UL) {
                fread(tmp, 1, 0x8000u, f);
                fread(tmp + 0x8000u, 1, (unsigned)(fsz - 0x8000UL), f);
            } else {
                fread(tmp, 1, (unsigned)fsz, f);
            }
            if (s.ns_flags & NSITER) {
                /* Rozwin rekordy iterowane: WORD powtorzenia, WORD dlugosc, bajty */
                unsigned char __far *src = (unsigned char __far *)MK_FP(g_chunk_seg, 0);
                unsigned long in = 0, out = 0;
                if (fsz > CHUNK) { mlog("ERROR: segment iterowany > 32KB\n"); fclose(f); return -1; }
                _fmemcpy(src, tmp, (unsigned)fsz);
                _fmemset(tmp, 0, (unsigned)fsz);
                while (in + 4 <= fsz) {
                    unsigned cnt = rd16(src, in), len = rd16(src, in + 2), r;
                    in += 4;
                    for (r = 0; r < cnt && out + len <= 0x10000UL; r++, out += len)
                        _fmemcpy(tmp + (unsigned)out, src + (unsigned)in, len);
                    in += len;
                }
                mlog("  seg %u: iterowany %lu -> %lu B\n", i + 1, fsz, out);
                fsz_rel = fsz;
                fsz = out;
            }
            if ((s.ns_flags & NSRELOC) &&
                apply_fixups(f, ((long)s.ns_sector << align) + (long)fsz_rel, tmp,
                             fsz, idx, mod_map, nref)) {
                fclose(f); return -1;
            }
        }
        /* Prologi eksportow "push ds; pop ax; nop" / "mov ax,ds; nop" (jak loader Windows,
         * por. NE_FixupSegmentPrologs w Wine): wpis z danymi wspolnymi (flaga 2, DLL)
         * -> "mov ax,DGROUP"; eksport modulu MULTIPLEDATA (aplikacja) -> "nop nop nop", zeby DS
         * bral sie z AX ustawionego przez thunk MakeProcInstance. */
        if (!system && ne.ne_autodata && fsz) {
            unsigned dg = m->first_sel + (ne.ne_autodata - 1u) * 8u, o, np = 0, nn = 0;
            for (o = 1; o < MAX_ORD; o++) {
                unsigned eo = m->ent_off[o];
                if (m->ent_seg[o] != i + 1u || eo + 3UL > fsz) continue;
                if (!((tmp[eo] == 0x1E && tmp[eo + 1] == 0x58 && tmp[eo + 2] == 0x90) ||
                      (tmp[eo] == 0x8C && tmp[eo + 1] == 0xD8 && tmp[eo + 2] == 0x90))) continue;
                if (m->ent_flg[o] & 2) {
                    tmp[eo] = 0xB8; tmp[eo + 1] = (unsigned char)dg;
                    tmp[eo + 2] = (unsigned char)(dg >> 8);
                    np++;
                } else if ((ne.ne_flags & 0x0002) && (m->ent_flg[o] & 1)) {   /* MULTIPLEDATA, eksport */
                    tmp[eo] = tmp[eo + 1] = tmp[eo + 2] = 0x90;
                    nn++;
                }
            }
            if (np) mlog("  seg %u: %u prologow eksportow -> mov ax,%04X\n", i + 1, np, dg);
            if (nn) mlog("  seg %u: %u prologow eksportow -> nop (MULTIPLEDATA)\n", i + 1, nn);
        }
        phys = xms_alloc(alloc);
        if (!phys || xms_write(phys, tmp, alloc)) { fclose(f); return -1; }
        gdt_add(sel, phys, alloc - 1, (unsigned char)(is_data ? 0x92 : 0x9A), 0);
        mlog("  seg %u: sel=%04X %s file=%lu alloc=%lu phys=%06lX%s\n", i + 1, sel,
             is_data ? "DATA" : "CODE", fsz, alloc, phys,
             (s.ns_flags & NSRELOC) ? " RELOC" : "");
    }
    _ffree(segtab);
    mt_set_word(idx, MT_DGROUP, m->dgroup_sel);
    if (is_app || g_log_dll) g_rec = 0;
    if (replayed) g_replay = 0;

    if (is_app) {
        g_entry_cs = ne.ne_cs ? m->first_sel + (ne.ne_cs - 1u) * 8u : 0;
        g_entry_ip = ne.ne_ip;
        g_app_ds   = m->dgroup_sel;
        g_app_stack = ne.ne_stack;
        g_app_heap  = ne.ne_heap;
        if (ne.ne_sp) g_init_sp = ne.ne_sp;
        else {
            NE_SEG_ENTRY s;
            unsigned long top;
            fseek(f, mz.e_lfanew + ne.ne_segtab + (long)(ne.ne_autodata - 1) * 8, SEEK_SET);
            fread(&s, sizeof(s), 1, f);
            top = (unsigned long)(s.ns_minalloc ? s.ns_minalloc : s.ns_cbseg)
                + ne.ne_stack;
            g_init_sp = top >= 0x10000UL ? 0xFFFE : (unsigned)(top & ~1UL);
        }
        mlog("  Wejscie apki: %04X:%04X DS=SS=%04X SP=%04X\n",
             g_entry_cs, g_entry_ip, g_app_ds, g_init_sp);
    } else if (ne.ne_cs && !system && g_ndllinit < MAX_DLLINIT) {
        unsigned short *d = &g_dllinit[g_ndllinit * 4];
        d[0] = m->first_sel + (ne.ne_cs - 1u) * 8u;
        d[1] = ne.ne_ip;
        d[2] = m->dgroup_sel;
        d[3] = ne.ne_heap;
        g_ndllinit++;
        mlog("  LibEntry %04X:%04X DS=%04X heap=%u\n", d[0], d[1], d[2], d[3]);
    }
    fclose(f);
    return (int)idx;
}

/* ============================================================
 * API
 * ============================================================ */
static unsigned dos_get_strategy(void)
{
    union REGS r;
    r.x.ax = 0x5800;
    int86(0x21, &r, &r);
    return r.x.ax;
}

static void dos_set_strategy(unsigned strat)
{
    union REGS r;
    r.x.ax = 0x5801;
    r.x.bx = strat;
    int86(0x21, &r, &r);
}

static int alloc_top_buffers(void)
{
    unsigned old_strat = dos_get_strategy();
    int err = 0;
    dos_set_strategy(2); /* Last fit: przydziel z samej gory pamieci konwencjonalnej DOS */
    if (_dos_allocmem(0x1000, &g_tmp_seg) != 0 ||
        _dos_allocmem(CHUNK / 16, &g_chunk_seg) != 0) {
        err = -1;
    }
    dos_set_strategy(old_strat);
    return err;
}

int mod_init(const char *argv0, unsigned long thunk_phys, unsigned thunk_seg,
             unsigned thunk_size)
{
    dir_of(argv0, g_sysdir);
    g_thunk_seg = thunk_seg;
    g_thunk_max = thunk_size / THUNK_SIZE;
    g_thunk_phys = thunk_phys;
    if (_dos_allocmem((MAX_MODS * MT_SIZE + 15) / 16, &g_modtab_seg) ||
        alloc_top_buffers() != 0) {
        mlog("ERROR: mod_init - brak pamieci DOS\n");
        return -1;
    }
    g_rmbuf_seg = g_tmp_seg;     /* bufor ladowania = bufor transferowy DOS w PM */
    g_hash_key = (unsigned long __far *)_fmalloc(HASH_SIZE * 4UL);
    g_hash_val = (unsigned short __far *)_fmalloc(HASH_SIZE * 2UL);
    g_stubs = (void __far *)_fmalloc(MAX_STUBS * sizeof(*g_stubs));
    if (!g_hash_key || !g_hash_val || !g_stubs) { mlog("ERROR: mod_init - brak pamieci\n"); return -1; }
    mlog("mod_init: sysdir='%s' MODTAB seg=%04X tmp=%04X chunk=%04X\n",
         g_sysdir, g_modtab_seg, g_tmp_seg, g_chunk_seg);
    return 0;
}

void mod_reset(void)
{
    unsigned i;
    for (i = 0; i < g_nmods; i++) {
        if (g_mods[i].ent_off) _ffree(g_mods[i].ent_off);
        if (g_mods[i].ent_seg) _ffree(g_mods[i].ent_seg);
        if (g_mods[i].ent_flg) _ffree(g_mods[i].ent_flg);
    }
    memset(g_mods, 0, sizeof(g_mods));
    g_nmods = 0;
    g_ngdtp = 0;
    g_ndllinit = 0;
    g_nthunks = 0;
    g_nstubs = 0;
    g_next_sel = SEL_MOD_FIRST;
    g_xms_next = 0x100000UL;
    g_xms_limit = 0x100000UL + g_ext_mem_kb * 1024UL;
    for (i = 0; i < HASH_SIZE; i++) g_hash_key[i] = 0xFFFFFFFFUL;
    _fmemset(MK_FP(g_modtab_seg, 0), 0, MAX_MODS * MT_SIZE);

    /* Stale wpisy rozszerzenia GDT */
    gdt_add(SEL_MODTAB, (unsigned long)g_modtab_seg << 4, MAX_MODS * MT_SIZE - 1, 0x92, 0);
    gdt_add(SEL_RMBUF, (unsigned long)g_tmp_seg << 4, 0xFFFF, 0x92, 0);
    gdt_add(SEL_THUNK_DATA, g_thunk_phys, g_thunk_max * (unsigned long)THUNK_SIZE - 1, 0x92, 0);
    gdt_add(SEL_RM_0000, 0x00000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_0040, 0x00400UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_A000, 0xA0000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_B000, 0xB0000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_B800, 0xB8000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_C000, 0xC0000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_D000, 0xD0000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_E000, 0xE0000UL, 0xFFFF, 0x92, 0);
    gdt_add(SEL_RM_F000, 0xF0000UL, 0xFFFF, 0x92, 0);
}

static int preload_dir(const char *dir, const char *mask)
{
    struct find_t ft;
    char pat[96];
    sprintf(pat, "%s%s", dir, mask);
    if (_dos_findfirst(pat, _A_NORMAL | _A_RDONLY, &ft) != 0) return 0;
    do {
        char nm[13], full[96], *dot;
        strcpy(nm, ft.name);
        if ((dot = strchr(nm, '.')) != 0) *dot = 0;
        if (find_mod(nm) >= 0) continue;
        if (g_nmods >= MAX_MODS) { mlog("WARN: MAX_MODS - pomijam %s\n", ft.name); break; }
        sprintf(full, "%s%s", dir, ft.name);
        mlog("Preload %s\n", full);
        if (mod_load_named(nm, full) < 0) return -1;
    } while (_dos_findnext(&ft) == 0);
    return 0;
}

int mod_load_app(const char *path)
{
    int idx;
    dir_of(path, g_appdir);
    idx = mod_load(0, path, 1);
    if (idx < 0) return -1;
    /* DLL z katalogu apki i fonty z katalogu systemowego: wczytaj z gory
     * (LoadLibrary w PM znajdzie je w MODTAB) */
    if (preload_dir(g_appdir, "*.DLL") < 0) return -1;
    if (preload_dir(g_sysdir, "*.FON") < 0) return -1;
    stubs_resolve();
    mlog("Moduly: %u, thunki: %u, selektory do %04X, XMS do %06lX\n",
         g_nmods, g_nthunks, g_next_sel, g_xms_next);
    return 0;
}

/* Zapas dla modulow ladowanych w trakcie pracy (WinExec): sterta KERNEL i selektory
 * dynamiczne zaczynaja sie za nim */
#define LATE_XMS  0x200000UL
#define LATE_SELS 0x800u
static unsigned g_sel_limit;

unsigned long mod_xms_end(void)
{
    unsigned long e = ((g_xms_next + 0xFFFUL) & ~0xFFFUL) + LATE_XMS;
    if (e > g_xms_limit) e = (g_xms_next + 0xFFFUL) & ~0xFFFUL;     /* malo RAM: bez zapasu */
    g_xms_limit = e;
    return e;
}

unsigned short mod_next_sel(void)
{
    g_sel_limit = g_next_sel + LATE_SELS;
    return g_sel_limit;
}

/* Zapis listy GDT od razu do tablicy (w trakcie pracy, patch_gdt_c dziala tylko przy wejsciu) */
extern unsigned short g_orig_cs, g_gdt_off_c;

static void gdt_apply(void)
{
    unsigned i;
    for (i = 0; i < g_ngdtp; i++) {
        GDT_PATCH *p = &g_gdtp[i];
        unsigned char __far *e = (unsigned char __far *)MK_FP(g_orig_cs, g_gdt_off_c + p->sel);
        unsigned long lim = p->limit;
        unsigned char b6 = p->big ? 0x40 : 0x00;
        if (lim > 0xFFFFFUL) { lim >>= 12; b6 |= 0x80; }
        e[0] = (unsigned char)lim;
        e[1] = (unsigned char)(lim >> 8);
        e[2] = (unsigned char)p->base;
        e[3] = (unsigned char)(p->base >> 8);
        e[4] = (unsigned char)(p->base >> 16);
        e[5] = p->access;
        e[6] = (unsigned char)(b6 | ((lim >> 16) & 0x0F));
        e[7] = (unsigned char)(p->base >> 24);
    }
    g_ngdtp = 0;
}

/* INT E0h z rm_int_service (tryb rzeczywisty): zaladuj program (WinExec) albo DLL (LoadLibrary).
 * Wejscie w RMBUF+0x100: slowo trybu (0 = program, 1 = DLL), od +2 sciezka.
 * Wynik tamze, slowa: ok, hinst/blad, wejscie cs, ip, sp, stos, sterta, poczatek sterty,
 * liczba nowych DLL, po 4 slowa na DLL do wywolania LibEntry: cs, ip, ds, heap */
#define RMBUF_DATA_OFF 0x100
#define RES_NDLL   8
#define RES_DLLS   9
#define KCB_THUNK_NEXT_OFF 522
extern unsigned long g_kcb_phys;

static void base_name(const char *path, char *out)
{
    const char *p = path, *s;
    unsigned n = 0;
    for (s = path; *s; s++) if (*s == '\\' || *s == ':') p = s + 1;
    while (p[n] && p[n] != '.' && n < 8) { out[n] = p[n]; n++; }
    out[n] = 0;
    upcase(out);
}

static int is_ext(const char *path, const char *ext)
{
    int lp = strlen(path), le = strlen(ext);
    if (lp < le) return 0;
    return !stricmp(path + lp - le, ext);
}

static void trim_str(char *s)
{
    char *p = s;
    int len;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = 0;
    }
}

static int pif_read(const char *pif_path, char *prog, char *dir, char *args)
{
    FILE *f;
    unsigned char buf[370];
    size_t nr;
    prog[0] = dir[0] = args[0] = 0;
    f = fopen(pif_path, "rb");
    if (!f) return -1;
    nr = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (nr < 170) return -1;
    memcpy(prog, buf + 0x24, 64);
    prog[64] = 0;
    trim_str(prog);
    memcpy(dir, buf + 0x66, 64);
    dir[64] = 0;
    trim_str(dir);
    memcpy(args, buf + 0xA6, 64);
    args[64] = 0;
    trim_str(args);
    if (!prog[0]) strcpy(prog, "COMMAND.COM");
    return 0;
}

static void dos_chdir(const char *dir)
{
    union REGS r;
    struct SREGS s;
    r.h.ah = 0x3B;
    s.ds = FP_SEG((void __far *)dir);
    r.x.dx = FP_OFF((void __far *)dir);
    int86x(0x21, &r, &r, &s);
}

static void dump_dos_mcbs(void)
{
    union REGS r;
    struct SREGS s;
    unsigned mcb_seg;
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &s);
    mcb_seg = *(unsigned short __far *)MK_FP(s.es, r.x.bx - 2);
    mlog("--- DOS MCB MAP ---\n");
    while (mcb_seg) {
        unsigned char type = *(unsigned char __far *)MK_FP(mcb_seg, 0);
        unsigned short owner = *(unsigned short __far *)MK_FP(mcb_seg, 1);
        unsigned short size = *(unsigned short __far *)MK_FP(mcb_seg, 3);
        char name[9];
        int i;
        for (i = 0; i < 8; i++) {
            unsigned char c = *(unsigned char __far *)MK_FP(mcb_seg, 8 + i);
            name[i] = (c >= 32 && c < 127) ? c : ' ';
        }
        name[8] = 0;
        mlog("  MCB %04X: type=%c owner=%04X size=%5u para (%4lu KB) [%s]\n",
             mcb_seg, type, owner, size, (unsigned long)size * 16 / 1024,
             owner == 0 ? "FREE" : owner == 8 ? "DOS" : name);
        if (type == 0x5A || (type != 0x4D && type != 0x5A)) break;
        mcb_seg += size + 1;
    }
    mlog("-------------------\n");
}

static void dump_text_screen(void)
{
    unsigned char __far *v = (unsigned char __far *)MK_FP(0xB800, 0);
    int y, x;
    char line[81];
    mlog("--- DOS SCREEN CAPTURE ---\n");
    for (y = 0; y < 25; y++) {
        int nonblank = 0;
        for (x = 0; x < 80; x++) {
            unsigned char c = v[(y * 80 + x) * 2];
            line[x] = (c >= 32 && c < 127) ? c : ' ';
            if (c > 32 && c < 127) nonblank = 1;
        }
        line[80] = 0;
        x = 80;
        while (x > 0 && line[x - 1] == ' ') line[--x] = 0;
        if (nonblank && line[0]) {
            mlog("  [%02d]: %s\n", y, line);
        }
    }
    mlog("--------------------------\n");
}

static int dos_exec(const char *prog, const char *args)
{
    static unsigned char tail[128];
    static struct {
        unsigned short env;
        void __far    *cmdline;
        void __far    *fcb1;
        void __far    *fcb2;
    } pb;
    union REGS r;
    struct SREGS s;
    int len = 0;

    if (args && args[0]) {
        len = strlen(args);
        if (len > 124) len = 124;
        tail[0] = (unsigned char)(len + 1);
        tail[1] = ' ';
        memcpy(tail + 2, args, len);
        tail[2 + len] = 0x0D;
    } else {
        tail[0] = 0;
        tail[1] = 0x0D;
    }

    pb.env = 0;
    pb.cmdline = (void __far *)tail;
    pb.fcb1 = 0;
    pb.fcb2 = 0;

    r.x.ax = 0x4B00;
    s.ds = FP_SEG((void __far *)prog);
    r.x.dx = FP_OFF((void __far *)prog);
    s.es = FP_SEG((void __far *)&pb);
    r.x.bx = FP_OFF((void __far *)&pb);
    int86x(0x21, &r, &r, &s);
    return r.x.cflag ? r.x.ax : 0;
}

void mod_exec_rm(void)
{
    static char path[96], base[10];
    unsigned short __far *res = (unsigned short __far *)MK_FP(g_rmbuf_seg, RMBUF_DATA_OFF);
    unsigned short __far *kcb = (unsigned short __far *)MK_FP((unsigned)(g_kcb_phys >> 4), 0);
    int idx, mode = res[0];
    unsigned i;
    _fstrncpy((char __far *)path, (char __far *)(res + 1), sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    g_nthunks = kcb[KCB_THUNK_NEXT_OFF / 2] / THUNK_SIZE;
    g_ndllinit = 0;
    if (g_next_sel + (MAX_SEGS + 4u) * 8u > g_sel_limit) { res[0] = 0; res[1] = 8; return; }
    if (access(path, 0)) {
        static char cand[96];
        int found = 0;
        char __far *wdir = (char __far *)kcb + KCB_WINDIR;
        char __far *sdir = (char __far *)kcb + KCB_SYSDIR;
        if (wdir[0]) {
            _fstrncpy((char __far *)cand, wdir, 64);
            cand[64] = 0;
            if (cand[strlen(cand) - 1] != '\\') strcat(cand, "\\");
            strcat(cand, path);
            if (!access(cand, 0)) { strcpy(path, cand); found = 1; }
        }
        if (!found && sdir[0]) {
            _fstrncpy((char __far *)cand, sdir, 64);
            cand[64] = 0;
            if (cand[strlen(cand) - 1] != '\\') strcat(cand, "\\");
            strcat(cand, path);
            if (!access(cand, 0)) { strcpy(path, cand); found = 1; }
        }
        if (!found && g_appdir[0]) {
            strcpy(cand, g_appdir);
            if (cand[strlen(cand) - 1] != '\\') strcat(cand, "\\");
            strcat(cand, path);
            if (!access(cand, 0)) { strcpy(path, cand); found = 1; }
        }
        if (!found) {
            static const char *dos_exts[] = { ".PIF", ".COM", ".BAT", 0 };
            int e;
            char noext[96], cand2[96];
            char *dot;
            strcpy(noext, path);
            dot = strrchr(noext, '.');
            if (dot) *dot = 0;
            for (e = 0; dos_exts[e] && !found; e++) {
                sprintf(cand2, "%s%s", noext, dos_exts[e]);
                if (!access(cand2, 0)) { strcpy(path, cand2); found = 1; break; }
                if (wdir[0]) {
                    sprintf(cand, "%s\\%s", wdir, cand2);
                    if (!access(cand, 0)) { strcpy(path, cand); found = 1; break; }
                }
                if (sdir[0]) {
                    sprintf(cand, "%s\\%s", sdir, cand2);
                    if (!access(cand, 0)) { strcpy(path, cand); found = 1; break; }
                }
                if (g_appdir[0]) {
                    sprintf(cand, "%s\\%s", g_appdir, cand2);
                    if (!access(cand, 0)) { strcpy(path, cand); found = 1; break; }
                }
            }
        }
        if (!found) { res[0] = 0; res[1] = 2; return; }
    }
    if (mode == 0) {
        int is_pif = is_ext(path, ".PIF");
        int is_dos = is_pif || is_ext(path, ".COM") || is_ext(path, ".BAT");
        if (!is_dos) {
            FILE *fc = fopen(path, "rb");
            if (fc) {
                MZ_HEADER mz;
                if (fread(&mz, sizeof(mz), 1, fc) == 1 && mz.e_magic == 0x5A4D) {
                    NE_HEADER ne;
                    if (fseek(fc, mz.e_lfanew, SEEK_SET) != 0 ||
                        fread(&ne, sizeof(ne), 1, fc) != 1 ||
                        ne.ne_magic != 0x454E) {
                        is_dos = 1;
                    }
                }
                fclose(fc);
            }
        }
        if (is_dos) {
            static char cmd[96], pdir[96], pargs[96];
            strcpy(cmd, path);
            pdir[0] = pargs[0] = 0;
            if (is_pif) {
                pif_read(path, cmd, pdir, pargs);
            }
            if (!pdir[0]) {
                char *slash = strrchr(cmd, '\\');
                if (!slash) slash = strrchr(cmd, '/');
                if (slash) {
                    int dlen = (int)(slash - cmd);
                    if (dlen > 0) {
                        strncpy(pdir, cmd, dlen);
                        pdir[dlen] = 0;
                    }
                }
            }
            if (pdir[0]) {
                int dlen = strlen(pdir);
                if (dlen > 3 && (pdir[dlen - 1] == '\\' || pdir[dlen - 1] == '/')) {
                    pdir[dlen - 1] = 0;
                }
                if (pdir[1] == ':') {
                    unsigned total;
                    int drv = (pdir[0] >= 'a' && pdir[0] <= 'z') ? pdir[0] - 'a' + 1 : pdir[0] - 'A' + 1;
                    _dos_setdrive(drv, &total);
                }
                dos_chdir(pdir);
            }
            if (!stricmp(cmd, "COMMAND.COM") && access("COMMAND.COM", 0) != 0) {
                if (access("C:\\FREEDOS\\BIN\\COMMAND.COM", 0) == 0)
                    strcpy(cmd, "C:\\FREEDOS\\BIN\\COMMAND.COM");
                else if (access("A:\\FREEDOS\\BIN\\COMMAND.COM", 0) == 0)
                    strcpy(cmd, "A:\\FREEDOS\\BIN\\COMMAND.COM");
            }
            mlog("DOS EXEC (fullscreen): %s\n", cmd);
            shutdown_mouse_ps2();
            set_text_mode();
            if (strstr(cmd, "COMMAND.COM")) {
                printf("\r\n");
                printf("==================================================\r\n");
                printf("  FreeDOS Prompt (Win16 Standard Mode)\r\n");
                printf("  Wpisz EXIT aby powrocic do Calmiry.\r\n");
                printf("==================================================\r\n\r\n");
            }
            {
                unsigned maxpara = 0;
                unsigned char old_pic_mask;
                int err;
                _dos_freemem(g_tmp_seg);
                _dos_freemem(g_chunk_seg);
                _dos_allocmem(0xFFFF, &maxpara);
                mlog("DOS free mem: %u para (%lu KB)\n", maxpara, (unsigned long)maxpara * 16 / 1024);
                dump_dos_mcbs();
                old_pic_mask = (unsigned char)inp(0x21);
                outp(0x21, old_pic_mask & ~0x02);
                err = dos_exec(cmd, pargs);
                outp(0x21, old_pic_mask);
                mlog("DOS EXEC ret=%d\n", err);
                {
                    union REGS r;
                    r.h.ah = 0x4D;
                    int86(0x21, &r, &r);
                    mlog("DOS exit code: type=%02X code=%u\n", r.h.ah, r.h.al);
                }
                dump_text_screen();
                alloc_top_buffers();
                g_rmbuf_seg = g_tmp_seg;
                kcb[KCB_RMBUF_SEG / 2] = g_rmbuf_seg;
                gdt_add(SEL_RMBUF, (unsigned long)g_tmp_seg << 4, 0xFFFF, 0x92, 0);
                gdt_apply();
            }
            vesa_set_lfb();
            init_mouse_ps2();
            res[0] = 2;
            res[1] = 0x21;
            return;
        }
        dir_of(path, g_appdir);
        {   /* zakonczony program z tego samego pliku: te same adresy */
            static char full[96];
            g_replay = 0;
            if (!_fullpath(full, path, sizeof(full))) strcpy(full, path);
            for (i = 0; i < g_nmods; i++) {
                unsigned char __far *e = mt_entry(i);
                if (g_mods[i].log && !g_mods[i].log->bad && !(*(unsigned short __far *)(e + MT_FLAGS) & MTF_RUNNING) &&
                    !_fstricmp((char __far *)e + MT_PATH, (char __far *)full)) {
                    g_replay = g_mods[i].log; g_replay_idx = i; g_rp_x = g_rp_s = 0;
                    break;
                }
            }
        }
        idx = mod_load(0, path, 1);
        g_replay = 0;
    } else {
        base_name(path, base);
        g_log_dll = 1;                                 /* FreeLibrary pozwoli zaladowac ja ponownie */
        idx = mod_load_named(base, path);
        g_log_dll = 0;
        g_replay = 0;
    }
    if (idx >= 0) stubs_resolve();
    gdt_apply();
    kcb[KCB_THUNK_NEXT_OFF / 2] = g_nthunks * THUNK_SIZE;
    if (idx < 0) { res[0] = 0; res[1] = 11; return; }          /* zly format EXE */
    res[0] = 1;
    res[RES_NDLL] = g_ndllinit;
    for (i = 0; i < g_ndllinit * 4u; i++) res[RES_DLLS + i] = g_dllinit[i];
    if (mode == 0) {
        mlog("WinExec: %s hInst=%04X XMS do %06lX sel do %04X\n", path, g_app_ds, g_xms_next, g_next_sel);
        res[1] = g_app_ds;
        res[2] = g_entry_cs;
        res[3] = g_entry_ip;
        res[4] = g_init_sp;
        res[5] = g_app_stack;
        res[6] = g_app_heap;
        res[7] = g_app_heapstart;
    } else {
        mlog("LoadLibrary: %s [%d] DLL do inicjalizacji: %u\n", path, idx, g_ndllinit);
        res[1] = (unsigned short)idx;
    }
}

unsigned short mod_thunk_next(void)
{
    return (unsigned short)(g_nthunks * THUNK_SIZE);
}

const char *mod_sysdir(void)
{
    return g_sysdir;
}
