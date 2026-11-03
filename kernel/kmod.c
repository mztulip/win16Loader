/* kmod.c - KERNEL: moduly, biblioteki, procedury, zasoby
 *
 * Dane z loadera: MODTAB (SEL_MODTAB) + kopia naglowka NE kazdego modulu w XMS
 * (selektor MT_HMOD = hModule). W tablicy zasobow kopii NE pola rn_handle:rn_usage
 * zawieraja adres fizyczny danych zasobu (loader skopiowal je do XMS).
 */
#include "kernel.h"

/* Offsety pol naglowka NE */
#define NE_ENTTAB   0x04
#define NE_RSRCTAB  0x24
#define NE_RESTAB   0x26

static BYTE __far *ne_of(int mi) { return (BYTE __far *)MK_FP(MTW(mi, MT_HMOD), 0); }
static WORD nw(BYTE __far *p, WORD o) { return (WORD)(p[o] | ((WORD)p[o + 1] << 8)); }

/* ============================================================
 * Wyszukiwanie modulow
 * ============================================================ */
int mod_from_handle(WORD h)
{
    int i;
    if (!h) return -1;
    h &= ~7;
    for (i = 0; i < MAX_MODS; i++) {
        WORD f, n;
        if (!MTE(i)[MT_NAME]) break;
        f = MTW(i, MT_FIRST_SEL); n = MTW(i, MT_NSEGS);
        if (h == MTW(i, MT_DGROUP) || h == MTW(i, MT_HMOD)) return i;
        if (n && h >= f && h < f + n * 8) return i;
    }
    return -1;
}

/* Nazwa modulu z nazwy pliku/sciezki ("C:\X\SHELL.DLL" -> "SHELL") */
int mod_from_name(LPCSTR name)
{
    char b[10];
    LPCSTR p = name, s = name;
    int i, n = 0;
    for (; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') s = p + 1;
    while (s[n] && s[n] != '.' && n < 9) { b[n] = kupper(s[n]); n++; }
    b[n] = 0;
    for (i = 0; i < MAX_MODS; i++) {
        if (!MTE(i)[MT_NAME]) break;
        if (!kstricmp((LPCSTR)(MTE(i) + MT_NAME), (LPCSTR)(char __far *)b)) return i;
    }
    return -1;
}

static int app_mod(void) { return mod_from_handle(KCBW(KCB_APP_HINST)); }

static int mod_of_arg(WORD h) { return h ? mod_from_handle(h) : app_mod(); }

/* ordinal 47 */
HMODULE API GetModuleHandle(LPCSTR name)
{
    int mi = HIWORD(name) ? mod_from_name(name) : mod_from_handle(LOWORD(name));
    if (mi < 0 || (MTW(mi, MT_FLAGS) & MTF_VIRTUAL)) return 0;
    return MTW(mi, MT_HMOD);
}

/* ordinal 48 */
int API GetModuleUsage(HMODULE h)   { return mod_from_handle(h) >= 0 ? 1 : 0; }

/* ordinal 49 */
int API GetModuleFileName(HINSTANCE h, LPSTR buf, int size)
{
    int mi = mod_of_arg(h);
    if (mi < 0 || size <= 0) return 0;
    kstrncpy(buf, (LPCSTR)(MTE(mi) + MT_PATH), size);
    return kstrlen(buf);
}

/* ordinal 27 */
int API GetModuleName(HINSTANCE h, LPSTR buf, int size)
{
    int mi = mod_of_arg(h);
    if (mi < 0) return 0;
    kstrncpy(buf, (LPCSTR)(MTE(mi) + MT_NAME), size);
    return 1;
}

/* ordinal 133 */
HMODULE API GetExePtr(WORD h)
{
    int mi = mod_of_arg(h);
    return mi < 0 ? 0 : MTW(mi, MT_HMOD);
}

/* ordinal 54 */
int API GetInstanceData(HINSTANCE h, BYTE __near *dst, int n)
{
    WORD ds = KCBW(KCB_CALLER_DS);
    kmemcpy(MK_FP(ds, (WORD)dst), MK_FP(h, (WORD)dst), (WORD)n);
    return n;
}

/* ordinal 95 */
HINSTANCE API LoadLibrary(LPCSTR name)
{
    int mi = mod_from_name(name);
    dbg_str("KERNEL: LoadLibrary ", name);
    if (mi < 0 || (MTW(mi, MT_FLAGS) & MTF_VIRTUAL)) return 2;   /* plik nie znaleziony */
    return MTW(mi, MT_DGROUP) ? MTW(mi, MT_DGROUP) : MTW(mi, MT_HMOD);
}

/* ordinal 96 */
void API FreeLibrary(HINSTANCE h)   { (void)h; }

/* ordinal 45 */
HINSTANCE API LoadModule(LPCSTR name, LPVOID params)
{
    (void)params;
    return LoadLibrary(name);
}

/* ordinal 166 */
WORD API WinExec(LPCSTR cmd, WORD show)
{
    (void)show;
    dbg_str("KERNEL: WinExec (nieobslugiwane) ", cmd);
    return 2;
}

/* ============================================================
 * Thunki w locie (segment thunkow loadera, format jak w module.c)
 * ============================================================ */
static FARPROC make_thunk(BYTE b2, WORD w3, WORD w5, BYTE b7, BYTE first)
{
    WORD next = KCBW(KCB_THUNK_NEXT), lim = KCBW(KCB_THUNK_LIMIT), o;
    BYTE __far *t = (BYTE __far *)MK_FP(SEL_THUNK_DATA, 0);
    /* juz istnieje? */
    for (o = 0; o + 8 <= next; o += 8)
        if (t[o] == first && t[o + 2] == b2 && nw(t, o + 3) == w3 &&
            nw(t, o + 5) == w5 && t[o + 7] == b7 && (first != 0xCD || t[o + 1] == 0x3F))
            return (FARPROC)MK_FP(SEL_THUNK, o);
    if (next + 8 > lim) { dbg_puts("KERNEL: brak miejsca na thunki\n"); return 0; }
    o = next;
    t[o] = first;
    t[o + 1] = (first == 0xCD) ? 0x3F : (BYTE)w3;
    t[o + 2] = b2;
    t[o + 3] = (BYTE)w3; t[o + 4] = (BYTE)(w3 >> 8);
    t[o + 5] = (BYTE)w5; t[o + 6] = (BYTE)(w5 >> 8);
    t[o + 7] = b7;
    KCBW(KCB_THUNK_NEXT) = next + 8;
    return (FARPROC)MK_FP(SEL_THUNK, o);
}

/* ordinal 51: "mov ax, DS_instancji; jmp far proc" (8 bajtow) */
FARPROC API MakeProcInstance(FARPROC proc, HINSTANCE inst)
{
    WORD __far *pp;
    BYTE __far *t;
    WORD next, lim, o;
    if (!inst || !proc) return proc;
    next = KCBW(KCB_THUNK_NEXT); lim = KCBW(KCB_THUNK_LIMIT);
    t = (BYTE __far *)MK_FP(SEL_THUNK_DATA, 0);
    for (o = 0; o + 8 <= next; o += 8) {
        pp = (WORD __far *)(t + o);
        if (t[o] == 0xB8 && pp[0] >> 8 == (inst & 0xFF) && t[o + 2] == (BYTE)(inst >> 8) &&
            t[o + 3] == 0xEA && nw(t, o + 4) == FP_OFF(proc) && nw(t, o + 6) == FP_SEG(proc))
            return (FARPROC)MK_FP(SEL_THUNK, o);
    }
    if (next + 8 > lim) return proc;
    o = next;
    t[o] = 0xB8; t[o + 1] = (BYTE)inst; t[o + 2] = (BYTE)(inst >> 8);
    t[o + 3] = 0xEA;
    t[o + 4] = (BYTE)FP_OFF(proc); t[o + 5] = (BYTE)(FP_OFF(proc) >> 8);
    t[o + 6] = (BYTE)FP_SEG(proc); t[o + 7] = (BYTE)(FP_SEG(proc) >> 8);
    KCBW(KCB_THUNK_NEXT) = next + 8;
    return (FARPROC)MK_FP(SEL_THUNK, o);
}

/* ordinal 52 */
void API FreeProcInstance(FARPROC p) { (void)p; }

/* ============================================================
 * GetProcAddress (ordinal 50)
 * ============================================================ */
static WORD find_name(BYTE __far *tab, WORD end_guard, LPCSTR name)
{
    WORD o = 0;
    int first = 1;
    while (tab[o]) {
        BYTE len = tab[o];
        if (!first && len == kstrlen(name) &&
            !kstrnicmp((LPCSTR)(tab + o + 1), name, len))
            return nw(tab, o + 1 + len);
        first = 0;
        o += 1 + len + 2;
        if (end_guard && o >= end_guard) break;
    }
    return 0;
}

/* Wejscie z tablicy wejsc: seg (0xFE = stala) i offset */
static BOOL entry_of(BYTE __far *ne, WORD ord, BYTE __far *seg, WORD __far *off)
{
    BYTE __far *p = ne + nw(ne, NE_ENTTAB);
    WORD cur = 1;
    while (p[0]) {
        BYTE cnt = p[0], sg = p[1];
        p += 2;
        if (sg == 0) { cur += cnt; continue; }
        if (ord < cur + cnt) {
            p += (ord - cur) * (sg == 0xFF ? 6 : 3);
            if (sg == 0xFF) { *seg = p[3]; *off = nw(p, 4); }
            else            { *seg = sg;   *off = nw(p, 1); }
            return TRUE;
        }
        p += cnt * (sg == 0xFF ? 6 : 3);
        cur += cnt;
    }
    return FALSE;
}

FARPROC API GetProcAddress(HMODULE h, LPCSTR name)
{
    int mi = mod_of_arg(h);
    BYTE __far *ne;
    WORD ord, off, sel;
    BYTE seg;
    if (mi < 0 || (MTW(mi, MT_FLAGS) & MTF_VIRTUAL)) goto fail;
    ne = ne_of(mi);
    if (HIWORD(name)) {
        ord = find_name(ne + nw(ne, NE_RESTAB), 0, name);
        if (!ord && MTW(mi, MT_NRES_OFF)) ord = find_name(ne + MTW(mi, MT_NRES_OFF), 0, name);
    } else ord = LOWORD(name);
    if (!ord || !entry_of(ne, ord, &seg, &off) || !seg) goto fail;
    if (seg == 0xFE) return (FARPROC)MK_FP(0, off);
    sel = MTW(mi, MT_FIRST_SEL) + (seg - 1) * 8;
    if (MTW(mi, MT_FLAGS) & MTF_SYSTEM)       /* nasze DLL: przez thunk INT 3F (DS) */
        return make_thunk((BYTE)mi, off, sel, 0, 0xCD);
    return (FARPROC)MK_FP(sel, off);
fail:
    dbg_puts("KERNEL: GetProcAddress brak "); dbg_hex(h, 4); dbg_str(" ", name);
    return 0;
}

/* ============================================================
 * Zasoby
 * hRsrc = offset 12-bajtowego wpisu NAMEINFO w kopii naglowka NE.
 * ============================================================ */
#define MAX_RCACHE 256
static struct { DWORD phys; WORD sel; } g_rc[MAX_RCACHE];
static WORD g_nrc;

/* Porownanie id/nazwy zasobu z argumentem (int, "#123" albo nazwa) */
static BOOL res_match(BYTE __far *rtab, WORD id, LPCSTR want)
{
    if (!HIWORD(want)) return (id & 0x8000) && (id & 0x7FFF) == LOWORD(want);
    if (want[0] == '#') {
        WORD v = 0;
        LPCSTR p = want + 1;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        return (id & 0x8000) && (id & 0x7FFF) == v;
    }
    if (id & 0x8000) return FALSE;
    {
        BYTE __far *s = rtab + id;
        return s[0] == kstrlen(want) && !kstrnicmp((LPCSTR)(s + 1), want, s[0]);
    }
}

/* ordinal 60 */
HRSRC API FindResource(HINSTANCE h, LPCSTR name, LPCSTR type)
{
    int mi = mod_of_arg(h);
    BYTE __far *ne, __far *rt, __far *p;
    if (mi < 0 || (MTW(mi, MT_FLAGS) & MTF_VIRTUAL)) return 0;
    ne = ne_of(mi);
    if (nw(ne, NE_RSRCTAB) == nw(ne, NE_RESTAB)) return 0;
    rt = ne + nw(ne, NE_RSRCTAB);
    p = rt + 2;
    while (nw(p, 0)) {
        WORD tid = nw(p, 0), cnt = nw(p, 2), j;
        BYTE __far *e = p + 8;
        if (res_match(rt, tid, type)) {
            for (j = 0; j < cnt; j++, e += 12)
                if (res_match(rt, nw(e, 6), name)) return (HRSRC)(e - ne);
        }
        p += 8 + cnt * 12;
    }
    return 0;
}

DWORD res_phys(int mi, HRSRC hr, DWORD __far *size)
{
    BYTE __far *ne = ne_of(mi);
    BYTE __far *e = ne + hr;
    WORD shift = nw(ne + nw(ne, NE_RSRCTAB), 0);
    if (size) *size = (DWORD)nw(e, 2) << shift;
    return (DWORD)nw(e, 8) | ((DWORD)nw(e, 10) << 16);
}

/* ordinal 61 */
HGLOBAL API LoadResource(HINSTANCE h, HRSRC hr)
{
    int mi = mod_of_arg(h);
    DWORD phys, size;
    WORD i, s;
    if (mi < 0 || !hr) return 0;
    phys = res_phys(mi, hr, (DWORD __far *)&size);
    for (i = 0; i < g_nrc; i++) if (g_rc[i].phys == phys) return g_rc[i].sel;
    s = alias_selector(phys, size ? size : 1, 0x92);
    if (s && g_nrc < MAX_RCACHE) { g_rc[g_nrc].phys = phys; g_rc[g_nrc].sel = s; g_nrc++; }
    return s;
}

/* ordinal 62 */
LPVOID API LockResource(HGLOBAL h)  { return h ? MK_FP(h, 0) : 0; }
/* ordinal 63 */
BOOL   API FreeResource(HGLOBAL h)  { (void)h; return FALSE; }

/* ordinal 65 */
DWORD API SizeofResource(HINSTANCE h, HRSRC hr)
{
    int mi = mod_of_arg(h);
    DWORD size;
    if (mi < 0 || !hr) return 0;
    res_phys(mi, hr, (DWORD __far *)&size);
    return size;
}

/* ordinal 64: plik modulu ustawiony na poczatku zasobu */
HFILE API AccessResource(HINSTANCE h, HRSRC hr)
{
    int mi = mod_of_arg(h);
    BYTE __far *ne;
    HFILE f;
    WORD shift;
    if (mi < 0 || !hr) return HFILE_ERROR;
    ne = ne_of(mi);
    shift = nw(ne + nw(ne, NE_RSRCTAB), 0);
    f = _lopen((LPCSTR)(MTE(mi) + MT_PATH), 0);
    if (f == HFILE_ERROR) return f;
    _llseek(f, (long)nw(ne + hr, 0) << shift, 0);
    return f;
}

/* ordinal 66 */
HGLOBAL API AllocResource(HINSTANCE h, HRSRC hr, DWORD size)
{
    (void)h; (void)hr;
    return GlobalAlloc(0, size);
}

/* ordinal 67 */
FARPROC API SetResourceHandler(HINSTANCE h, LPCSTR type, FARPROC p)
{
    (void)h; (void)type; (void)p;
    return 0;
}
