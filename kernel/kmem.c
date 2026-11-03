/* kmem.c - KERNEL: pamiec globalna (XMS), selektory, lokalny heap
 *
 * Sterta globalna: obszar XMS [KCB.heap_phys, KCB.heap_end), first-fit
 * z lista wolnych zakresow (laczenie sasiadow przy zwalnianiu).
 * HGLOBAL = selektor (pierwszy z ciagu dla blokow > 64KB, kolejne co __AHINCR=8).
 * Selektory dynamiczne: [KCB.next_dyn_sel, KCB[512]] - opis kazdego w g_sel[].
 */
#include "kernel.h"

#define GMEM_MOVEABLE   0x0002
#define GMEM_ZEROINIT   0x0040
#define GMEM_MODIFY     0x0080
#define GMEM_DISCARDABLE 0x0100

#define ST_FREE   0     /* selektor wolny */
#define ST_HEAD   1     /* pierwszy selektor bloku sterty */
#define ST_TAIL   2     /* kolejny selektor bloku > 64KB */
#define ST_ALIAS  3     /* AllocSelector/alias (bez pamieci sterty) */
#define ST_RES    4     /* zasob (pamiec w XMS zaladowana przez loader) */
#define ST_DOS    5     /* GlobalDOSAlloc (pamiec konwencjonalna) */

#define MAX_SELS  2048
#define MAX_FREE  512

typedef struct {
    DWORD base;
    DWORD size;
    BYTE  type;
    BYTE  nsel;
    BYTE  lock;
    BYTE  flags;      /* GMEM_* >> 8 */
} SELINFO;

static SELINFO g_sel[MAX_SELS];
static WORD    g_sel_base, g_sel_max, g_sel_hint;
static struct { DWORD base, size; } g_free[MAX_FREE];
static WORD    g_nfree;
static WORD    g_scratch1, g_scratch2;
static BOOL    g_kmem_ok;

#define SIDX(s)   ((WORD)(((s) - g_sel_base) >> 3))
#define SVALID(s) ((s) >= g_sel_base && (s) <= g_sel_max && !((s) & 7) == !((g_sel_base) & 7))

/* ============================================================
 * Deskryptory GDT (przez SEL_GDT_ACCESS)
 * ============================================================ */
void desc_set(WORD sel, DWORD base, DWORD limit, BYTE access)
{
    BYTE __far *p = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel & ~7);
    BYTE b6 = 0;
    if (limit > 0xFFFFFUL) { limit >>= 12; b6 = 0x80; }
    p[0] = (BYTE)limit;
    p[1] = (BYTE)(limit >> 8);
    p[2] = (BYTE)base;
    p[3] = (BYTE)(base >> 8);
    p[4] = (BYTE)(base >> 16);
    p[5] = access;
    p[6] = (BYTE)(b6 | ((limit >> 16) & 0x0F));
    p[7] = (BYTE)(base >> 24);
}

DWORD desc_base(WORD sel)
{
    BYTE __far *p = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel & ~7);
    return (DWORD)p[2] | ((DWORD)p[3] << 8) | ((DWORD)p[4] << 16) | ((DWORD)p[7] << 24);
}

DWORD desc_limit(WORD sel)
{
    BYTE __far *p = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel & ~7);
    DWORD l = (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)(p[6] & 0x0F) << 16);
    if (p[6] & 0x80) l = (l << 12) | 0xFFF;
    return l;
}

static BYTE desc_access(WORD sel)
{
    return ((BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel & ~7))[5];
}

/* Ustaw deskryptory ciagu selektorow (okna po 64KB) dla bloku base/size */
static void desc_run(WORD sel, BYTE n, DWORD base, DWORD size, BYTE access)
{
    BYTE i;
    for (i = 0; i < n; i++) {
        DWORD rest = size - ((DWORD)i << 16);
        desc_set(sel + i * 8, base + ((DWORD)i << 16),
                 (rest > 0x10000UL ? 0x10000UL : (rest ? rest : 1)) - 1, access);
    }
}

/* ============================================================
 * Inicjalizacja (leniwa - pierwsze uzycie: LibEntry DLL albo InitTask)
 * ============================================================ */
void kmem_init(void)
{
    extern void kint_init(void);
    if (g_kmem_ok) return;
    g_kmem_ok = TRUE;
    kint_init();
    g_sel_base = KCBW(KCB_NEXT_DYN_SEL);
    g_sel_max  = KCBW(KCB_DYN_MAX_OFF);
    if ((WORD)((g_sel_max - g_sel_base) >> 3) >= MAX_SELS)
        g_sel_max = g_sel_base + (MAX_SELS - 1) * 8;
    g_sel_hint = 0;
    g_nfree = 1;
    g_free[0].base = KCBD(KCB_HEAP_PHYS);
    g_free[0].size = KCBD(KCB_HEAP_END) - g_free[0].base;
    g_scratch1 = sel_alloc_run(1);
    g_scratch2 = sel_alloc_run(1);
    g_sel[SIDX(g_scratch1)].type = ST_ALIAS;
    g_sel[SIDX(g_scratch2)].type = ST_ALIAS;
    dbg_puts("KERNEL: heap XMS "); dbg_hex(g_free[0].base, 6); dbg_puts("..");
    dbg_hex(g_free[0].base + g_free[0].size, 6);
    dbg_puts(" sel "); dbg_hex(g_sel_base, 4); dbg_puts(".."); dbg_hex(g_sel_max, 4);
    dbg_puts("\n");
}

/* ============================================================
 * Selektory
 * ============================================================ */
WORD sel_alloc_run(WORD n)
{
    WORD total, i, j, start;
    kmem_init();
    total = (WORD)((g_sel_max - g_sel_base) >> 3) + 1;
    for (i = 0; i < total; i++) {
        start = (WORD)((g_sel_hint + i) % total);
        if (start + n > total) continue;
        for (j = 0; j < n; j++)
            if (g_sel[start + j].type != ST_FREE) break;
        if (j == n) {
            for (j = 0; j < n; j++) {
                kmemset(&g_sel[start + j], 0, sizeof(SELINFO));
                g_sel[start + j].type = ST_ALIAS;
            }
            g_sel_hint = start + n;
            return g_sel_base + start * 8;
        }
    }
    dbg_puts("KERNEL: brak selektorow!\n");
    return 0;
}

static BOOL sel_ours(WORD sel)
{
    sel &= ~7;
    return sel >= g_sel_base && sel <= g_sel_max;
}

void sel_free(WORD sel)
{
    BYTE __far *p;
    sel &= ~7;
    if (!sel_ours(sel)) return;
    g_sel[SIDX(sel)].type = ST_FREE;
    p = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel);
    kmemset(p, 0, 8);
}

WORD alias_selector(DWORD base, DWORD size, BYTE access)
{
    BYTE n = (BYTE)((size + 0xFFFFUL) >> 16);
    WORD s;
    if (!n) n = 1;
    s = sel_alloc_run(n);
    if (!s) return 0;
    desc_run(s, n, base, size, access);
    g_sel[SIDX(s)].base = base;
    g_sel[SIDX(s)].size = size;
    g_sel[SIDX(s)].nsel = n;
    return s;
}

/* ============================================================
 * Fizyczne kopiowanie / zerowanie (przez dwa selektory robocze)
 * ============================================================ */
static void phys_copy(DWORD dst, DWORD src, DWORD n)
{
    BOOL back = dst > src && dst < src + n;   /* zachodza na siebie -> od konca */
    while (n) {
        WORD c = n > 0x8000UL ? 0x8000u : (WORD)n;
        DWORD so = back ? src + n - c : src;
        DWORD dO = back ? dst + n - c : dst;
        BYTE __far *s, __far *d;
        WORD i;
        desc_set(g_scratch1, so, 0xFFFF, 0x92);
        desc_set(g_scratch2, dO, 0xFFFF, 0x92);
        s = (BYTE __far *)MK_FP(g_scratch1, 0);
        d = (BYTE __far *)MK_FP(g_scratch2, 0);
        if (back) { for (i = c; i-- > 0;) d[i] = s[i]; }
        else      { for (i = 0; i < c; i++) d[i] = s[i]; }
        n -= c;
        if (!back) { src += c; dst += c; }
    }
}

static void phys_zero(DWORD dst, DWORD n)
{
    while (n) {
        WORD c = n > 0x8000UL ? 0x8000u : (WORD)n;
        desc_set(g_scratch2, dst, 0xFFFF, 0x92);
        kmemset(MK_FP(g_scratch2, 0), 0, c);
        n -= c; dst += c;
    }
}

/* ============================================================
 * Zakresy wolne XMS
 * ============================================================ */
static DWORD xms_take(DWORD size)
{
    WORD i;
    for (i = 0; i < g_nfree; i++) {
        if (g_free[i].size >= size) {
            DWORD b = g_free[i].base;
            g_free[i].base += size;
            g_free[i].size -= size;
            if (!g_free[i].size) {
                for (; i + 1 < g_nfree; i++) g_free[i] = g_free[i + 1];
                g_nfree--;
            }
            return b;
        }
    }
    return 0;
}

static void xms_give(DWORD base, DWORD size)
{
    WORD i, j;
    if (!size) return;
    for (i = 0; i < g_nfree && g_free[i].base < base; i++);
    /* polacz z poprzednim */
    if (i > 0 && g_free[i - 1].base + g_free[i - 1].size == base) {
        g_free[i - 1].size += size;
        if (i < g_nfree && base + size == g_free[i].base) {
            g_free[i - 1].size += g_free[i].size;
            for (j = i; j + 1 < g_nfree; j++) g_free[j] = g_free[j + 1];
            g_nfree--;
        }
        return;
    }
    /* polacz z nastepnym */
    if (i < g_nfree && base + size == g_free[i].base) {
        g_free[i].base = base;
        g_free[i].size += size;
        return;
    }
    if (g_nfree >= MAX_FREE) { dbg_puts("KERNEL: lista wolnych pelna\n"); return; }
    for (j = g_nfree; j > i; j--) g_free[j] = g_free[j - 1];
    g_free[i].base = base;
    g_free[i].size = size;
    g_nfree++;
}

static DWORD round16(DWORD n) { return (n + 15UL) & ~15UL; }

static SELINFO *head_of(WORD h)
{
    SELINFO *si;
    if (!h || !sel_ours(h)) return 0;
    si = &g_sel[SIDX(h & ~7)];
    return (si->type == ST_HEAD || si->type == ST_RES || si->type == ST_DOS) ? si : 0;
}

/* ============================================================
 * Global* API
 * ============================================================ */
/* ordinal 15 */
HGLOBAL API GlobalAlloc(WORD flags, DWORD size)
{
    DWORD cap, base;
    BYTE n;
    WORD s, i;
    kmem_init();
    if (size == 0) size = 1;
    cap = round16(size);
    n = (BYTE)((cap + 0xFFFFUL) >> 16);
    base = xms_take(cap);
    if (!base) {
        dbg_puts("KERNEL: GlobalAlloc brak pamieci "); dbg_hex(size, 8); dbg_puts("\n");
        return 0;
    }
    s = sel_alloc_run(n);
    if (!s) { xms_give(base, cap); return 0; }
    desc_run(s, n, base, cap, 0x92);
    g_sel[SIDX(s)].type  = ST_HEAD;
    g_sel[SIDX(s)].base  = base;
    g_sel[SIDX(s)].size  = size;
    g_sel[SIDX(s)].nsel  = n;
    g_sel[SIDX(s)].flags = (BYTE)(flags >> 8);
    for (i = 1; i < n; i++) g_sel[SIDX(s) + i].type = ST_TAIL;
    if (flags & GMEM_ZEROINIT) phys_zero(base, cap);
    return s;
}

/* ordinal 17 */
HGLOBAL API GlobalFree(HGLOBAL h)
{
    SELINFO *si = head_of(h);
    BYTE i;
    if (!si) return h ? h : 0;
    if (si->type == ST_HEAD) xms_give(si->base, round16(si->size));
    for (i = 0; i < si->nsel; i++) sel_free(h + i * 8);
    if (!si->nsel) sel_free(h);
    return 0;
}

/* ordinal 16 */
HGLOBAL API GlobalReAlloc(HGLOBAL h, DWORD size, WORD flags)
{
    SELINFO *si = head_of(h);
    DWORD cap, oldcap, nb;
    BYTE n, i;
    WORD idx;
    if (flags & GMEM_MODIFY) {
        if (si) si->flags = (BYTE)(flags >> 8);
        return h;
    }
    if (!h) return GlobalAlloc(flags, size);
    if (!si || si->type != ST_HEAD) return 0;
    if (size == 0) size = 1;
    idx = SIDX(h);
    cap = round16(size);
    oldcap = round16(si->size);
    n = (BYTE)((cap + 0xFFFFUL) >> 16);
    if (cap <= oldcap) {                       /* zmniejszenie w miejscu */
        xms_give(si->base + cap, oldcap - cap);
        for (i = n; i < si->nsel; i++) sel_free(h + i * 8);
    } else {
        /* potrzebne selektory: dodatkowe musza byc wolne za blokiem */
        for (i = si->nsel; i < n; i++)
            if (idx + i >= MAX_SELS || g_sel[idx + i].type != ST_FREE) break;
        if (i < n) {
            HGLOBAL nh;
            if (!(flags & GMEM_MOVEABLE) && !(si->flags & (GMEM_MOVEABLE >> 8))) {
                /* i tak przenosimy - aplikacje Win16 rzadko polegaja na tym */
            }
            nh = GlobalAlloc(flags & GMEM_ZEROINIT, size);
            if (!nh) return 0;
            phys_copy(g_sel[SIDX(nh)].base, si->base, si->size);
            g_sel[SIDX(nh)].flags = si->flags;
            GlobalFree(h);
            return nh;
        }
        nb = xms_take(cap);
        if (!nb) return 0;
        phys_copy(nb, si->base, si->size < cap ? si->size : cap);
        if (flags & GMEM_ZEROINIT) phys_zero(nb + si->size, cap - si->size);
        xms_give(si->base, oldcap);
        si->base = nb;
        for (i = si->nsel; i < n; i++) {
            kmemset(&g_sel[idx + i], 0, sizeof(SELINFO));
            g_sel[idx + i].type = ST_TAIL;
        }
    }
    si = &g_sel[idx];
    si->size = size;
    si->nsel = n;
    desc_run(h, n, si->base, cap, desc_access(h));
    return h;
}

/* ordinal 18 */
LPVOID API GlobalLock(HGLOBAL h)
{
    SELINFO *si = head_of(h);
    if (!h) return 0;
    if (si) { if (si->lock < 255) si->lock++; }
    else if (!desc_access(h)) return 0;       /* nieprawidlowy selektor */
    return MK_FP(h, 0);
}

/* ordinal 19 */
BOOL API GlobalUnlock(HGLOBAL h)
{
    SELINFO *si = head_of(h);
    if (si && si->lock) si->lock--;
    return si ? si->lock : 0;
}

/* ordinal 20 */
DWORD API GlobalSize(HGLOBAL h)
{
    SELINFO *si = head_of(h);
    if (si) return si->size;
    if (!h || !desc_access(h)) return 0;
    return desc_limit(h) + 1;
}

/* ordinal 21: low = handle, high = selektor */
DWORD API GlobalHandle(WORD sel)
{
    WORD s = sel & ~7;
    if (sel_ours(s)) {
        while (s > g_sel_base && g_sel[SIDX(s)].type == ST_TAIL) s -= 8;
    }
    return MAKELONG(s, s);
}

/* ordinal 22 */
WORD API GlobalFlags(HGLOBAL h)
{
    SELINFO *si = head_of(h);
    return si ? (WORD)(si->lock | ((WORD)si->flags << 8)) : 0;
}

/* ordinal 25: najwiekszy wolny blok */
DWORD API GlobalCompact(DWORD need)
{
    DWORD best = 0;
    WORD i;
    (void)need;
    kmem_init();
    for (i = 0; i < g_nfree; i++) if (g_free[i].size > best) best = g_free[i].size;
    return best;
}

/* ordinal 169 */
DWORD API GetFreeSpace(WORD flags)
{
    DWORD t = 0;
    WORD i;
    (void)flags;
    kmem_init();
    for (i = 0; i < g_nfree; i++) t += g_free[i].size;
    return t;
}

/* ordinal 23/24, 197/198, 111/112: blokowanie - bez znaczenia (pamiec stala) */
HGLOBAL API LockSegment(WORD s)      { return s; }
void    API UnlockSegment(WORD s)    { (void)s; }
WORD    API GlobalFix(HGLOBAL h)     { return h; }
void    API GlobalUnfix(HGLOBAL h)   { (void)h; }
WORD    API GlobalWire(HGLOBAL h)    { return h; }
BOOL    API GlobalUnWire(HGLOBAL h)  { (void)h; return TRUE; }
HGLOBAL API GlobalLRUOldest(HGLOBAL h) { return h; }
HGLOBAL API GlobalLRUNewest(HGLOBAL h) { return h; }
void    API GlobalNotify(FARPROC p)  { (void)p; }
WORD    API GlobalPageLock(HGLOBAL h)   { (void)h; return 1; }
WORD    API GlobalPageUnlock(HGLOBAL h) { (void)h; return 0; }

/* ordinal 184: pamiec konwencjonalna przez DOS 48h. DX=segment RM, AX=selektor */
DWORD API GlobalDOSAlloc(DWORD size)
{
    RMREGS __far *r = rmregs();
    WORD s, i;
    kmem_init();
    kmemset(r, 0, sizeof(RMREGS));
    r->ax = 0x4800;
    r->bx = (WORD)((size + 15) >> 4);
    if (rm_int21() || (r->flags & CF_FLAG)) return 0;
    s = alias_selector((DWORD)r->ax << 4, size, 0x92);
    if (!s) return 0;
    i = SIDX(s);
    g_sel[i].type = ST_DOS;
    g_sel[i].flags = 0;
    return MAKELONG(s, r->ax);
}

/* ordinal 185 */
WORD API GlobalDOSFree(WORD sel)
{
    RMREGS __far *r = rmregs();
    SELINFO *si = head_of(sel);
    if (!si || si->type != ST_DOS) return sel;
    kmemset(r, 0, sizeof(RMREGS));
    r->ax = 0x4900;
    r->es = (WORD)(si->base >> 4);
    rm_int21();
    sel_free(sel);
    return 0;
}

/* ============================================================
 * Selektory - API
 * ============================================================ */
/* ordinal 175 */
WORD API AllocSelector(WORD src)
{
    WORD s = sel_alloc_run(1);
    if (s && src) {
        kmemcpy(MK_FP(SEL_GDT_ACCESS, s), MK_FP(SEL_GDT_ACCESS, src & ~7), 8);
        g_sel[SIDX(s)].base = desc_base(src);
        g_sel[SIDX(s)].size = desc_limit(src) + 1;
    }
    return s;
}

/* ordinal 176 */
WORD API FreeSelector(WORD sel)
{
    if (!sel_ours(sel)) return sel;
    sel_free(sel);
    return 0;
}

static WORD alias_of(WORD src, BYTE access)
{
    WORD s;
    if (!src || !desc_access(src)) return 0;
    s = sel_alloc_run(1);
    if (!s) return 0;
    kmemcpy(MK_FP(SEL_GDT_ACCESS, s), MK_FP(SEL_GDT_ACCESS, src & ~7), 8);
    ((BYTE __far *)MK_FP(SEL_GDT_ACCESS, s))[5] = access;
    return s;
}

/* ordinal 170 */
WORD API AllocCStoDSAlias(WORD cs) { return alias_of(cs, 0x92); }
/* ordinal 171 */
WORD API AllocDStoCSAlias(WORD ds) { return alias_of(ds, 0x9A); }

/* ordinal 177: kopiuj deskryptor src -> dst zamieniajac kod <-> dane */
WORD API PrestoChangoSelector(WORD src, WORD dst)
{
    BYTE __far *d = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, dst & ~7);
    kmemcpy(d, MK_FP(SEL_GDT_ACCESS, src & ~7), 8);
    d[5] = (BYTE)((d[5] & 0x08) ? 0x92 : 0x9A);
    return dst;
}

/* ordinal 186 */
DWORD API GetSelectorBase(WORD sel)  { return desc_base(sel); }
/* ordinal 187 */
WORD  API SetSelectorBase(WORD sel, DWORD base)
{
    desc_set(sel, base, desc_limit(sel), desc_access(sel));
    return sel;
}
/* ordinal 188 */
DWORD API GetSelectorLimit(WORD sel) { return desc_limit(sel); }
/* ordinal 189 */
WORD  API SetSelectorLimit(WORD sel, DWORD lim)
{
    desc_set(sel, desc_base(sel), lim, desc_access(sel));
    return 0;
}
/* ordinal 196: op=0 get, 1 set (rights = bajty access, bit 8.. ignorowane) */
WORD  API SelectorAccessRights(WORD sel, WORD op, WORD rights)
{
    BYTE __far *p = (BYTE __far *)MK_FP(SEL_GDT_ACCESS, sel & ~7);
    if (op) { p[5] = (BYTE)(rights | 0x90); return 0; }
    return (WORD)p[5] | ((WORD)(p[6] & 0xF0) << 8);
}

/* ordinal 348: kopiowanie huge */
void API hmemcpy(void __huge *dst, const void __huge *src, long n)
{
    if (n <= 0) return;
    phys_copy(desc_base(FP_SEG(dst)) + FP_OFF(dst),
              desc_base(FP_SEG(src)) + FP_OFF(src), (DWORD)n);
}

/* ============================================================
 * IsBad*Ptr - weryfikacja selektora (VERR/VERW + LSL)
 * ============================================================ */
static WORD sel_readable(WORD s);
#pragma aux sel_readable = "xor ax, ax" "verr bx" "jnz short l1" "inc ax" "l1:" \
    parm [bx] value [ax] modify [ax];
static WORD sel_writable(WORD s);
#pragma aux sel_writable = "xor ax, ax" "verw bx" "jnz short l1" "inc ax" "l1:" \
    parm [bx] value [ax] modify [ax];

static BOOL bad_range(LPVOID p, DWORD n, BOOL write)
{
    WORD s = FP_SEG(p);
    if (!s) return TRUE;
    if (write ? !sel_writable(s) : !sel_readable(s)) return TRUE;
    if (n && (DWORD)FP_OFF(p) + n - 1 > desc_limit(s)) return TRUE;
    return FALSE;
}

/* ordinal 334 */
BOOL API IsBadReadPtr(LPVOID p, WORD n)       { return bad_range(p, n, FALSE); }
/* ordinal 335 */
BOOL API IsBadWritePtr(LPVOID p, WORD n)      { return bad_range(p, n, TRUE); }
/* ordinal 346 */
BOOL API IsBadHugeReadPtr(LPVOID p, DWORD n)  { return bad_range(p, n > 0x10000UL ? 1 : n, FALSE); }
/* ordinal 347 */
BOOL API IsBadHugeWritePtr(LPVOID p, DWORD n) { return bad_range(p, n > 0x10000UL ? 1 : n, TRUE); }
/* ordinal 336 */
BOOL API IsBadCodePtr(FARPROC p)              { return !FP_SEG(p) || !sel_readable(FP_SEG(p)); }
/* ordinal 337 */
BOOL API IsBadStringPtr(LPCSTR s, WORD max)
{
    WORD lim;
    if (bad_range((LPVOID)s, 1, FALSE)) return TRUE;
    lim = (WORD)desc_limit(FP_SEG(s));
    while (max-- && FP_OFF(s) <= lim) { if (!*s) return FALSE; s++; }
    return max == 0xFFFF ? FALSE : TRUE;
}

/* ============================================================
 * Lokalny heap (w segmencie DS wolajacego albo podanym)
 *
 * Format wlasny: w segmencie heapu pod [6] (pLocalHeap) offset naglowka LHDR.
 * Bloki: WORD rozmiar|1(zajety) przed danymi; uchwyt = offset danych.
 * ============================================================ */
#define LH_MAGIC 0x484C   /* 'LH' */

typedef struct { WORD magic, start, end; } LHDR;

static WORD lh_seg(void) { return KCBW(KCB_CALLER_DS); }

/* ordinal 4 */
BOOL API LocalInit(WORD seg, WORD start, WORD end)
{
    BYTE __far *b;
    LHDR __far *h;
    DWORD lim;
    if (!seg) seg = lh_seg();
    lim = desc_limit(seg);
    if (start == 0) {                 /* heap na koncu segmentu, end = rozmiar */
        WORD size = end;
        end = (WORD)(lim > 0xFFFE ? 0xFFFE : lim);
        start = (end > size) ? end - size : 0x10;
    }
    if (end > lim) end = (WORD)lim;
    start = (start + 1) & ~1;
    if (start < 0x10) start = 0x10;
    if (end <= start + 16) return FALSE;
    b = (BYTE __far *)MK_FP(seg, 0);
    h = (LHDR __far *)(b + start);
    h->magic = LH_MAGIC;
    h->start = start + sizeof(LHDR);
    h->end   = end & ~1;
    *(WORD __far *)(b + h->start) = (WORD)(h->end - h->start);   /* jeden wolny blok */
    *(WORD __far *)(b + 6) = start;
    return TRUE;
}

void local_init_app(void)
{
    WORD hinst = KCBW(KCB_APP_HINST);
    WORD hs = KCBW(KCB_APP_HEAPSTART), hz = KCBW(KCB_APP_HEAP);
    if (!hinst || !hz) return;
    LocalInit(hinst, hs, (WORD)(hs + hz - 1));
}

static LHDR __far *lh_get(WORD seg)
{
    WORD off = *(WORD __far *)MK_FP(seg, 6);
    LHDR __far *h = (LHDR __far *)MK_FP(seg, off);
    if (!off || h->magic != LH_MAGIC) return 0;
    return h;
}

/* ordinal 5 */
HLOCAL API LocalAlloc(WORD flags, WORD size)
{
    WORD seg = lh_seg(), o;
    BYTE __far *b = (BYTE __far *)MK_FP(seg, 0);
    LHDR __far *h = lh_get(seg);
    if (!h) {
        dbg_puts("KERNEL: LocalAlloc bez LocalInit DS="); dbg_hex(seg, 4); dbg_puts("\n");
        return 0;
    }
    size = (size + 3) & ~1;            /* + naglowek 2B, parzysty */
    if (size < 4) size = 4;
    for (o = h->start; o < h->end;) {
        WORD w = *(WORD __far *)(b + o), bs = w & ~1;
        if (!bs) break;
        if (!(w & 1)) {
            /* polacz z kolejnymi wolnymi */
            while (o + bs < h->end && !(*(WORD __far *)(b + o + bs) & 1) &&
                   *(WORD __far *)(b + o + bs))
                bs += *(WORD __far *)(b + o + bs);
            *(WORD __far *)(b + o) = bs;
            if (bs >= size) {
                if (bs - size >= 6) {
                    *(WORD __far *)(b + o + size) = bs - size;
                    bs = size;
                }
                *(WORD __far *)(b + o) = bs | 1;
                if (flags & 0x40) kmemset(b + o + 2, 0, bs - 2);
                return o + 2;
            }
        }
        o += bs;
    }
    dbg_puts("KERNEL: LocalAlloc pelny DS="); dbg_hex(seg, 4); dbg_puts("\n");
    return 0;
}

/* ordinal 7 */
HLOCAL API LocalFree(HLOCAL m)
{
    WORD seg = lh_seg();
    if (m >= 2 && lh_get(seg)) *(WORD __far *)MK_FP(seg, m - 2) &= ~1;
    return 0;
}

/* ordinal 10 */
WORD API LocalSize(HLOCAL m)
{
    WORD seg = lh_seg();
    if (m < 2 || !lh_get(seg)) return 0;
    return (*(WORD __far *)MK_FP(seg, m - 2) & ~1) - 2;
}

/* ordinal 6 */
HLOCAL API LocalReAlloc(HLOCAL m, WORD size, WORD flags)
{
    WORD seg = lh_seg(), old, n;
    if (flags & GMEM_MODIFY) return m;
    if (!m) return LocalAlloc(flags, size);
    old = LocalSize(m);
    if (size <= old) return m;
    n = LocalAlloc(flags, size);
    if (!n) return 0;
    kmemcpy(MK_FP(seg, n), MK_FP(seg, m), old);
    LocalFree(m);
    return n;
}

/* ordinal 8/9/11/12/13 */
LPSTR  API LocalLock(HLOCAL m)        { return m ? (LPSTR)MK_FP(lh_seg(), m) : 0; }
BOOL   API LocalUnlock(HLOCAL m)      { (void)m; return FALSE; }
HLOCAL API LocalHandle(WORD m)        { return m; }
WORD   API LocalFlags(HLOCAL m)       { (void)m; return 0; }
WORD   API LocalCompact(WORD n)       { (void)n; return 0x1000; }
WORD   API LocalShrink(HLOCAL h, WORD n) { (void)h; (void)n; return 0; }
DWORD  API LocalNotify(FARPROC p)     { (void)p; return 0; }
