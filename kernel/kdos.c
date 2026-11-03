/* kdos.c - KERNEL: DOS przez tryb rzeczywisty (rm_int_service w pm_call.asm)
 *
 * Bufor transferowy SEL_RMBUF (segment RM w KCB): RMREGS na poczatku,
 * dane od RMBUF_DATA. Wskazniki PM przekazywane do DOS-a sa kopiowane przez bufor.
 */
#include "kernel.h"

/* far ptr do rm_int_service (SEL_CODE16:KCB[516]); AL = numer przerwania */
DWORD g_rmint_ptr;
static void rm_call_far(BYTE intno, DWORD __far *pfn);
#pragma aux rm_call_far = "call dword ptr es:[bx]" parm [al] [es bx] modify [ax bx cx dx si di es];
#define rm_call(n) rm_call_far((n), (DWORD __far *)&g_rmint_ptr)

#define RB          ((BYTE __far *)MK_FP(SEL_RMBUF, 0))
#define RBUF(off)   ((BYTE __far *)MK_FP(SEL_RMBUF, (off)))

static WORD   g_rmseg;
static LPVOID g_pm_dta;         /* DTA ustawione przez apke (AH=1Ah) */
static BOOL   g_dos_ok;

void kdos_init(void)
{
    if (g_dos_ok) return;
    g_dos_ok = TRUE;
    g_rmseg = KCBW(KCB_RMBUF_SEG);
    g_rmint_ptr = MAKELONG(KCBW(KCB_RMINT_OFF), SEL_CODE16);
}

RMREGS __far *rmregs(void) { return (RMREGS __far *)MK_FP(SEL_RMBUF, 0); }

/* Wywolanie przerwania z RMREGS w RMBUF. Zwraca 1 gdy CF. */
int rm_int21(void)
{
    kdos_init();
    rm_call(0x21);
    return (rmregs()->flags & CF_FLAG) ? 1 : 0;
}

int rm_int(BYTE intno, RMREGS __far *r)
{
    kdos_init();
    if (r != rmregs()) kmemcpy(rmregs(), r, sizeof(RMREGS));
    rm_call(intno);
    if (r != rmregs()) kmemcpy(r, rmregs(), sizeof(RMREGS));
    return (r->flags & CF_FLAG) ? 1 : 0;
}

static RMREGS __far *rr(WORD ax)
{
    RMREGS __far *r = rmregs();
    kdos_init();
    kmemset(r, 0, sizeof(RMREGS));
    r->ax = ax;
    return r;
}

/* Kopiuj ASCIIZ z PM do RMBUF[off], zwraca off */
static WORD put_str(WORD off, LPCSTR s)
{
    LPSTR d = (LPSTR)RBUF(off);
    int n = 0;
    while (s[n] && n < 255) { d[n] = s[n]; n++; }
    d[n] = 0;
    return off;
}

/* ============================================================
 * Pliki: _lopen/_lcreat/_lclose/_lread/_lwrite/_hread/_hwrite/_llseek
 * ============================================================ */
/* ordinal 85 */
HFILE API _lopen(LPCSTR path, int mode)
{
    RMREGS __far *r = rr(0x3D00 | (mode & 0xFF));
    r->dx = put_str(RMBUF_DATA, path);
    if (rm_int21()) return HFILE_ERROR;
    return (HFILE)r->ax;
}

/* ordinal 83 */
HFILE API _lcreat(LPCSTR path, int attr)
{
    RMREGS __far *r = rr(0x3C00);
    r->cx = (WORD)attr;
    r->dx = put_str(RMBUF_DATA, path);
    if (rm_int21()) return HFILE_ERROR;
    return (HFILE)r->ax;
}

/* ordinal 81 */
HFILE API _lclose(HFILE h)
{
    RMREGS __far *r = rr(0x3E00);
    r->bx = (WORD)h;
    return rm_int21() ? HFILE_ERROR : 0;
}

static DWORD huge_add(LPVOID p, DWORD n)   /* zwraca znormalizowany far ptr p+n (huge) */
{
    DWORD lin = (DWORD)FP_OFF(p) + n;
    return MAKELONG((WORD)lin, FP_SEG(p) + (WORD)(lin >> 16) * 8);
}

/* ordinal 349 */
DWORD API _hread(HFILE h, void __far *buf, DWORD n)
{
    DWORD done = 0;
    while (done < n) {
        RMREGS __far *r;
        WORD c = (n - done) > 0x8000UL ? 0x8000u : (WORD)(n - done), got, i;
        BYTE __far *dst;
        r = rr(0x3F00);
        r->bx = (WORD)h;
        r->cx = c;
        r->dx = RMBUF_DATA;
        if (rm_int21()) return done ? done : (DWORD)HFILE_ERROR;
        got = r->ax;
        /* kopiuj (moze przekraczac granice 64KB bufora docelowego) */
        for (i = 0; i < got; ) {
            DWORD p = huge_add(buf, done + i);
            WORD room = (WORD)(0x10000UL - LOWORD(p));
            WORD cnt = got - i;
            if (room && cnt > room) cnt = room;
            dst = (BYTE __far *)(void __far *)p;
            kmemcpy(dst, RBUF(RMBUF_DATA + i), cnt);
            i += cnt;
        }
        done += got;
        if (got < c) break;
    }
    return done;
}

/* ordinal 82 */
WORD API _lread(HFILE h, void __far *buf, WORD n)
{
    DWORD r = _hread(h, buf, n);
    return r == (DWORD)HFILE_ERROR ? (WORD)HFILE_ERROR : (WORD)r;
}

/* ordinal 350 */
DWORD API _hwrite(HFILE h, const void __far *buf, DWORD n)
{
    DWORD done = 0;
    if (n == 0) {                              /* obciecie pliku */
        RMREGS __far *r = rr(0x4000);
        r->bx = (WORD)h;
        r->dx = RMBUF_DATA;
        return rm_int21() ? (DWORD)HFILE_ERROR : 0;
    }
    while (done < n) {
        RMREGS __far *r;
        WORD c = (n - done) > 0x8000UL ? 0x8000u : (WORD)(n - done), i;
        for (i = 0; i < c; ) {
            DWORD p = huge_add((LPVOID)buf, done + i);
            WORD room = (WORD)(0x10000UL - LOWORD(p));
            WORD cnt = c - i;
            if (room && cnt > room) cnt = room;
            kmemcpy(RBUF(RMBUF_DATA + i), (void __far *)p, cnt);
            i += cnt;
        }
        r = rr(0x4000);
        r->bx = (WORD)h;
        r->cx = c;
        r->dx = RMBUF_DATA;
        if (rm_int21()) return done ? done : (DWORD)HFILE_ERROR;
        done += r->ax;
        if (r->ax < c) break;
    }
    return done;
}

/* ordinal 86 */
WORD API _lwrite(HFILE h, const void __far *buf, WORD n)
{
    DWORD r = _hwrite(h, buf, n);
    return r == (DWORD)HFILE_ERROR ? (WORD)HFILE_ERROR : (WORD)r;
}

/* ordinal 84 */
long API _llseek(HFILE h, long off, int origin)
{
    RMREGS __far *r = rr(0x4200 | (origin & 3));
    r->bx = (WORD)h;
    r->cx = HIWORD(off);
    r->dx = LOWORD(off);
    if (rm_int21()) return -1L;
    return (long)MAKELONG(r->ax, r->dx);
}

/* ============================================================
 * Sciezki
 * ============================================================ */
static BOOL has_dir(LPCSTR p)
{
    for (; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') return TRUE;
    return FALSE;
}

static void path_join(LPSTR out, LPCSTR dir, LPCSTR name)
{
    int n;
    kstrcpy(out, dir);
    n = kstrlen(out);
    if (n && out[n - 1] != '\\') { out[n++] = '\\'; out[n] = 0; }
    kstrcpy(out + n, name);
}

static BOOL file_exists(LPCSTR path)
{
    RMREGS __far *r = rr(0x4300);
    r->dx = put_str(RMBUF_DATA, path);
    return !rm_int21();
}

/* Pelna sciezka (INT 21h 60h); out min 128 B */
static void full_path(LPSTR out, LPCSTR in)
{
    RMREGS __far *r = rr(0x6000);
    r->si = put_str(RMBUF_DATA, in);
    r->di = RMBUF_DATA + 0x200;
    if (rm_int21()) { kstrncpy(out, in, 128); return; }
    kstrncpy(out, (LPCSTR)RBUF(RMBUF_DATA + 0x200), 128);
}

/* ordinal 134 */
WORD API GetWindowsDirectory(LPSTR buf, WORD size)
{
    kstrncpy(buf, KCBS(KCB_WINDIR), size);
    return (WORD)kstrlen(KCBS(KCB_WINDIR));
}

/* ordinal 135 */
WORD API GetSystemDirectory(LPSTR buf, WORD size)
{
    kstrncpy(buf, KCBS(KCB_SYSDIR), size);
    return (WORD)kstrlen(KCBS(KCB_SYSDIR));
}

/* ordinal 131: srodowisko DOS (alias selektora na segment z PSP:2C) */
LPSTR API GetDOSEnvironment(void)
{
    static WORD envsel;
    if (!envsel) {
        WORD seg = KCBW(KCB_ENV_SEG);
        if (!seg) return 0;
        envsel = alias_selector((DWORD)seg << 4, 0x8000, 0x92);
    }
    return (LPSTR)MK_FP(envsel, 0);
}

/* ordinal 136: 0 = brak, 2 = wymienny, 3 = staly, 4 = sieciowy */
WORD API GetDriveType(int drive)
{
    RMREGS __far *r = rr(0x4408);
    r->bx = (WORD)(drive + 1);
    if (rm_int21()) return 0;
    if (r->ax == 0) return 2;
    r = rr(0x4409);
    r->bx = (WORD)(drive + 1);
    if (!rm_int21() && (r->dx & 0x1000)) return 4;
    return 3;
}

/* ordinal 92 */
BYTE API GetTempDrive(BYTE c)
{
    RMREGS __far *r = rr(0x1900);
    (void)c;
    rm_int21();
    return (BYTE)('A' + (r->ax & 0xFF));
}

/* ============================================================
 * OpenFile (ordinal 74)
 * ============================================================ */
typedef struct {
    BYTE cBytes;
    BYTE fFixedDisk;
    WORD nErrCode;
    BYTE reserved[4];
    char szPathName[128];
} OFSTRUCT;

#define OF_PARSE   0x0100
#define OF_DELETE  0x0200
#define OF_CREATE  0x1000
#define OF_EXIST   0x4000
#define OF_REOPEN  0x8000

HFILE API OpenFile(LPCSTR name, OFSTRUCT __far *of, WORD style)
{
    char path[130];
    HFILE h;
    LPSTR p = (LPSTR)(char __far *)path;
    if (style & OF_REOPEN) kstrncpy(p, of->szPathName, 128);
    else kstrncpy(p, name, 128);
    /* szukaj: biezacy katalog, katalog Windows, systemowy */
    if (!(style & (OF_CREATE | OF_PARSE)) && !has_dir(p) && !file_exists(p)) {
        char t[130];
        LPSTR tp = (LPSTR)(char __far *)t;
        path_join(tp, KCBS(KCB_WINDIR), p);
        if (file_exists(tp)) kstrcpy(p, tp);
        else {
            path_join(tp, KCBS(KCB_SYSDIR), p);
            if (file_exists(tp)) kstrcpy(p, tp);
        }
    }
    of->cBytes = sizeof(OFSTRUCT);
    of->fFixedDisk = 1;
    of->nErrCode = 0;
    full_path(of->szPathName, p);
    if (style & OF_PARSE) return 0;
    if (style & OF_DELETE) {
        RMREGS __far *r = rr(0x4100);
        r->dx = put_str(RMBUF_DATA, p);
        if (rm_int21()) { of->nErrCode = r->ax; return HFILE_ERROR; }
        return 1;
    }
    if (style & OF_CREATE) h = _lcreat(p, 0);
    else h = _lopen(p, style & 0x73);
    if (h == HFILE_ERROR) { of->nErrCode = rmregs()->ax; return HFILE_ERROR; }
    if (style & OF_EXIST) { _lclose(h); return 1; }
    return h;
}

/* ordinal 97 */
WORD API GetTempFileName(BYTE drive, LPCSTR prefix, WORD unique, LPSTR out)
{
    char d[4];
    WORD n = unique ? unique : (WORD)(KCBD(KCB_TICK_MS) & 0xFFFF);
    int i, k;
    d[0] = (drive & 0x7F) ? (char)(drive & 0x7F) : 'C';
    d[1] = ':'; d[2] = '\\'; d[3] = 0;
    kstrcpy(out, (LPCSTR)(char __far *)d);
    k = kstrlen(out);
    out[k++] = '~';
    for (i = 0; i < 3 && prefix && prefix[i]; i++) out[k++] = prefix[i];
    for (i = 3; i >= 0; i--) out[k++] = "0123456789ABCDEF"[(n >> (i * 4)) & 0xF];
    kstrcpy(out + k, ".TMP");
    if (!unique) { HFILE h = _lcreat(out, 0); if (h != HFILE_ERROR) _lclose(h); }
    return n;
}

/* ============================================================
 * DOS3Call (ordinal 102) - wejscie w kasm.asm zapisuje rejestry w g_d3,
 * DS wolajacego z KCB. Tlumaczymy wskazniki PM <-> bufor RM.
 * ============================================================ */
typedef struct { WORD ax, bx, cx, dx, si, di, ds, es, flags; } D3REGS;
D3REGS g_d3;

static LPSTR pm_ptr(WORD seg, WORD off) { return (LPSTR)MK_FP(seg, off); }

static void d3_from_rm(RMREGS __far *r)
{
    g_d3.ax = r->ax; g_d3.bx = r->bx; g_d3.cx = r->cx; g_d3.dx = r->dx;
    g_d3.si = r->si; g_d3.di = r->di;
    g_d3.flags = (g_d3.flags & ~CF_FLAG) | (r->flags & CF_FLAG);
}

static RMREGS __far *rm_from_d3(void)
{
    RMREGS __far *r = rr(g_d3.ax);
    r->bx = g_d3.bx; r->cx = g_d3.cx; r->dx = g_d3.dx;
    r->si = g_d3.si; r->di = g_d3.di;
    return r;
}

static void d3_fail(WORD err)
{
    g_d3.ax = err;
    g_d3.flags |= CF_FLAG;
}

static void d3_pass(void)
{
    RMREGS __far *r = rm_from_d3();
    rm_int21();
    d3_from_rm(r);
}

static void rm_set_dta(void)
{
    RMREGS __far *r = rr(0x1A00);
    r->dx = RMBUF_DTA;
    rm_int21();
}

void dos3call_c(void)
{
    BYTE ah = (BYTE)(g_d3.ax >> 8);
    RMREGS __far *r;
    g_d3.flags &= ~CF_FLAG;
    switch (ah) {
    /* ASCIIZ w DS:DX */
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x41:
    case 0x43: case 0x5B:
        r = rm_from_d3();
        r->dx = put_str(RMBUF_DATA, pm_ptr(g_d3.ds, g_d3.dx));
        rm_int21();
        d3_from_rm(r);
        if (ah != 0x3C && ah != 0x3D && ah != 0x5B && ah != 0x43) {
            if (!(g_d3.flags & CF_FLAG)) g_d3.ax = 0;
        }
        break;
    case 0x6C:                                   /* extended open: DS:SI */
        r = rm_from_d3();
        r->si = put_str(RMBUF_DATA, pm_ptr(g_d3.ds, g_d3.si));
        rm_int21();
        d3_from_rm(r);
        break;
    case 0x56: {                                 /* rename DS:DX -> ES:DI */
        r = rm_from_d3();
        r->dx = put_str(RMBUF_DATA, pm_ptr(g_d3.ds, g_d3.dx));
        r->di = put_str(RMBUF_DATA + 0x100, pm_ptr(g_d3.es, g_d3.di));
        rm_int21();
        d3_from_rm(r);
        break;
    }
    case 0x3F: {                                 /* read DS:DX, CX */
        DWORD n = _hread((HFILE)g_d3.bx, pm_ptr(g_d3.ds, g_d3.dx), g_d3.cx);
        if (n == (DWORD)HFILE_ERROR) d3_fail(rmregs()->ax);
        else g_d3.ax = (WORD)n;
        break;
    }
    case 0x40: {                                 /* write DS:DX, CX */
        DWORD n = _hwrite((HFILE)g_d3.bx, pm_ptr(g_d3.ds, g_d3.dx), g_d3.cx);
        if (n == (DWORD)HFILE_ERROR) d3_fail(rmregs()->ax);
        else g_d3.ax = (WORD)n;
        break;
    }
    case 0x47:                                   /* getcwd -> DS:SI (64 B) */
        r = rm_from_d3();
        r->si = RMBUF_DATA;
        rm_int21();
        d3_from_rm(r);
        if (!(g_d3.flags & CF_FLAG)) kstrncpy(pm_ptr(g_d3.ds, g_d3.si), (LPCSTR)RBUF(RMBUF_DATA), 64);
        break;
    case 0x1A:                                   /* set DTA */
        g_pm_dta = MK_FP(g_d3.ds, g_d3.dx);
        break;
    case 0x2F:                                   /* get DTA -> ES:BX */
        g_d3.es = FP_SEG(g_pm_dta);
        g_d3.bx = FP_OFF(g_pm_dta);
        break;
    case 0x4E: case 0x4F:                        /* find first/next */
        rm_set_dta();
        if (g_pm_dta && ah == 0x4F) kmemcpy(RBUF(RMBUF_DTA), g_pm_dta, 43);
        r = rm_from_d3();
        if (ah == 0x4E) r->dx = put_str(RMBUF_DATA, pm_ptr(g_d3.ds, g_d3.dx));
        rm_int21();
        d3_from_rm(r);
        if (!(g_d3.flags & CF_FLAG) && g_pm_dta) kmemcpy(g_pm_dta, RBUF(RMBUF_DTA), 43);
        break;
    case 0x60:                                   /* truename DS:SI -> ES:DI */
        r = rm_from_d3();
        r->si = put_str(RMBUF_DATA, pm_ptr(g_d3.ds, g_d3.si));
        r->di = RMBUF_DATA + 0x200;
        rm_int21();
        d3_from_rm(r);
        if (!(g_d3.flags & CF_FLAG))
            kstrncpy(pm_ptr(g_d3.es, g_d3.di), (LPCSTR)RBUF(RMBUF_DATA + 0x200), 128);
        break;
    case 0x44:                                   /* IOCTL */
        switch (g_d3.ax & 0xFF) {
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x0C: case 0x0D:
            d3_fail(1);                          /* bloki parametrow - nieobslugiwane */
            break;
        default:
            d3_pass();
        }
        break;
    case 0x48: case 0x49: case 0x4A:             /* pamiec DOS - nie dla apek PM */
        d3_fail(8);
        g_d3.bx = 0;
        break;
    case 0x25:                                   /* set vector - ignoruj */
        break;
    case 0x35:                                   /* get vector */
        g_d3.es = 0; g_d3.bx = 0;
        break;
    case 0x51: case 0x62:                        /* PSP */
        g_d3.bx = SEL_PSP;
        break;
    case 0x4C:                                   /* koniec programu */
        dbg_puts("KERNEL: DOS3Call 4C - koniec apki\n");
        pm_int21(g_d3.ax);
        break;
    case 0x71:                                   /* LFN - brak */
        d3_fail(0x7100);
        break;
    case 0x0E: case 0x19: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
    case 0x30: case 0x33: case 0x36: case 0x3E: case 0x42: case 0x45:
    case 0x46: case 0x54: case 0x57: case 0x59: case 0x0D: case 0x68:
        d3_pass();
        break;
    default:
        dbg_puts("KERNEL: DOS3Call AH="); dbg_hex(ah, 2);
        dbg_puts(" - przekazane bez translacji\n");
        d3_pass();
    }
}

/* ordinal 103: NetBIOSCall */
void API NetBIOSCall(void) { }

/* ============================================================
 * Pliki INI
 * ============================================================ */
static char  g_ini_path[130];
static WORD  g_ini_sel;      /* tresc pliku (GlobalAlloc), 0 = brak */
static WORD  g_ini_len;

#define INI_MAX 0xF000u

static void ini_path(LPSTR out, LPCSTR file)
{
    if (has_dir(file)) kstrncpy(out, file, 128);
    else path_join(out, KCBS(KCB_WINDIR), file);
}

/* Wczytaj plik do cache (zwraca FALSE gdy brak pliku - cache pusty) */
static BOOL ini_load(LPCSTR file)
{
    char p[130];
    LPSTR pp = (LPSTR)(char __far *)p;
    HFILE h;
    ini_path(pp, file);
    if (g_ini_sel && !kstricmp(pp, (LPCSTR)(char __far *)g_ini_path)) return g_ini_len != 0xFFFF;
    kstrcpy((LPSTR)(char __far *)g_ini_path, pp);
    if (!g_ini_sel) g_ini_sel = GlobalAlloc(0, INI_MAX + 2);
    g_ini_len = 0xFFFF;
    h = _lopen(pp, 0);
    if (h == HFILE_ERROR) return FALSE;
    g_ini_len = (WORD)_hread(h, MK_FP(g_ini_sel, 0), INI_MAX);
    _lclose(h);
    if (g_ini_len == 0xFFFF) g_ini_len = 0;
    return TRUE;
}

static BOOL ini_save(void)
{
    HFILE h = _lcreat((LPCSTR)(char __far *)g_ini_path, 0);
    if (h == HFILE_ERROR) return FALSE;
    _hwrite(h, MK_FP(g_ini_sel, 0), g_ini_len);
    _lclose(h);
    return TRUE;
}

#define INI(o) (((LPSTR)MK_FP(g_ini_sel, 0))[o])

/* Nastepna linia od o; ustawia [ls, le) bez CR/LF i spacji z brzegow */
static WORD ini_line(WORD o, WORD __far *ls, WORD __far *le)
{
    WORD e;
    while (o < g_ini_len && (INI(o) == ' ' || INI(o) == '\t')) o++;
    *ls = o;
    while (o < g_ini_len && INI(o) != '\n' && INI(o) != '\r') o++;
    e = o;
    while (e > *ls && (INI(e - 1) == ' ' || INI(e - 1) == '\t')) e--;
    *le = e;
    while (o < g_ini_len && (INI(o) == '\n' || INI(o) == '\r')) o++;
    return o;
}

static BOOL span_eq(WORD s, WORD e, LPCSTR str)
{
    int n = kstrlen(str);
    if ((int)(e - s) != n) return FALSE;
    return kstrnicmp((LPCSTR)&INI(s), str, n) == 0;
}

/* Szukaj sekcji: zwraca offset pierwszej linii po naglowku albo 0xFFFF */
static WORD ini_find_section(LPCSTR sec, WORD __far *hdr_start)
{
    WORD o = 0, ls, le, n;
    while (o < g_ini_len) {
        n = ini_line(o, &ls, &le);
        if (le > ls && INI(ls) == '[') {
            WORD e = ls + 1;
            while (e < le && INI(e) != ']') e++;
            if (span_eq(ls + 1, e, sec)) { if (hdr_start) *hdr_start = o; return n; }
        }
        o = n;
    }
    return 0xFFFF;
}

/* W sekcji od o: znajdz klucz; zwraca poczatek linii, *vs/*ve = wartosc */
static WORD ini_find_key(WORD o, LPCSTR key, WORD __far *vs, WORD __far *ve, WORD __far *next)
{
    WORD ls, le, n;
    while (o < g_ini_len) {
        n = ini_line(o, &ls, &le);
        if (le > ls && INI(ls) == '[') break;
        if (le > ls && INI(ls) != ';') {
            WORD eq = ls;
            while (eq < le && INI(eq) != '=') eq++;
            if (eq < le) {
                WORD ke = eq;
                while (ke > ls && (INI(ke - 1) == ' ' || INI(ke - 1) == '\t')) ke--;
                if (span_eq(ls, ke, key)) {
                    WORD v = eq + 1;
                    while (v < le && (INI(v) == ' ' || INI(v) == '\t')) v++;
                    *vs = v; *ve = le; *next = n;
                    return o;
                }
            }
        }
        o = n;
    }
    *next = o;
    return 0xFFFF;
}

static int copy_out(LPSTR buf, int size, WORD s, WORD e)
{
    int n = 0;
    if (size <= 0) return 0;
    /* usun cudzyslowy */
    if (e - s >= 2 && INI(s) == '"' && INI(e - 1) == '"') { s++; e--; }
    while (s < e && n < size - 1) buf[n++] = INI(s++);
    buf[n] = 0;
    return n;
}

/* ordinal 128 */
int API GetPrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR def,
                                LPSTR buf, int size, LPCSTR file)
{
    WORD o, vs, ve, nx;
    int n = 0;
    if (size <= 0) return 0;
    if (!def) def = "";
    if (!ini_load(file) || !g_ini_len) goto deflt;
    if (!sec) {                                  /* lista sekcji */
        WORD ls, le;
        o = 0;
        while (o < g_ini_len) {
            nx = ini_line(o, &ls, &le);
            if (le > ls && INI(ls) == '[') {
                WORD e = ls + 1;
                while (e < le && INI(e) != ']') e++;
                for (vs = ls + 1; vs < e && n < size - 2; vs++) buf[n++] = INI(vs);
                buf[n++] = 0;
            }
            o = nx;
        }
        buf[n] = 0;
        return n ? n - 1 : 0;
    }
    o = ini_find_section(sec, 0);
    if (o == 0xFFFF) goto deflt;
    if (!key) {                                  /* lista kluczy */
        WORD ls, le;
        while (o < g_ini_len) {
            nx = ini_line(o, &ls, &le);
            if (le > ls && INI(ls) == '[') break;
            if (le > ls && INI(ls) != ';') {
                WORD eq = ls;
                while (eq < le && INI(eq) != '=') eq++;
                while (eq > ls && (INI(eq - 1) == ' ')) eq--;
                for (vs = ls; vs < eq && n < size - 2; vs++) buf[n++] = INI(vs);
                buf[n++] = 0;
            }
            o = nx;
        }
        buf[n] = 0;
        return n ? n - 1 : 0;
    }
    if (ini_find_key(o, key, &vs, &ve, &nx) == 0xFFFF) goto deflt;
    return copy_out(buf, size, vs, ve);
deflt:
    kstrncpy(buf, def, size);
    return kstrlen(buf);
}

/* ordinal 127 */
WORD API GetPrivateProfileInt(LPCSTR sec, LPCSTR key, int def, LPCSTR file)
{
    char b[16];
    LPSTR p = (LPSTR)(char __far *)b;
    int i = 0, neg = 0;
    long v = 0;
    if (!GetPrivateProfileString(sec, key, "", p, sizeof(b), file)) return (WORD)def;
    if (p[0] == '-') { neg = 1; i = 1; }
    if (p[i] < '0' || p[i] > '9') return (WORD)def;
    for (; p[i] >= '0' && p[i] <= '9'; i++) v = v * 10 + (p[i] - '0');
    return (WORD)(neg ? -v : v);
}

static BOOL ini_splice(WORD at, WORD del, LPCSTR ins, WORD inslen)
{
    LPSTR b = (LPSTR)MK_FP(g_ini_sel, 0);
    WORD i;
    if ((DWORD)g_ini_len - del + inslen > INI_MAX) return FALSE;
    if (inslen > del) {
        for (i = g_ini_len; i-- > at + del;) b[i + inslen - del] = b[i];
    } else if (inslen < del) {
        for (i = at + del; i < g_ini_len; i++) b[i - (del - inslen)] = b[i];
    }
    for (i = 0; i < inslen; i++) b[at + i] = ins[i];
    g_ini_len = g_ini_len - del + inslen;
    return TRUE;
}

/* ordinal 129 */
BOOL API WritePrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR val, LPCSTR file)
{
    char line[300];
    LPSTR lp = (LPSTR)(char __far *)line;
    WORD o, hdr, vs, ve, nx, ks;
    if (!sec) return FALSE;
    ini_load(file);
    if (g_ini_len == 0xFFFF) g_ini_len = 0;
    if (!g_ini_sel) return FALSE;
    o = ini_find_section(sec, &hdr);
    if (!key) {                                  /* usun sekcje */
        if (o != 0xFFFF) {
            WORD e = o, ls, le;
            while (e < g_ini_len) {
                WORD n = ini_line(e, &ls, &le);
                if (le > ls && INI(ls) == '[') break;
                e = n;
            }
            ini_splice(hdr, e - hdr, "", 0);
        }
        return ini_save();
    }
    if (o == 0xFFFF) {                           /* nowa sekcja na koncu */
        if (!val) return TRUE;
        lp[0] = 0;
        if (g_ini_len && INI(g_ini_len - 1) != '\n') lstrcat(lp, "\r\n");
        lstrcat(lp, "[");
        lstrcat(lp, sec);
        lstrcat(lp, "]\r\n");
        ini_splice(g_ini_len, 0, lp, (WORD)kstrlen(lp));
        o = g_ini_len;
    }
    ks = ini_find_key(o, key, &vs, &ve, &nx);
    if (ks == 0xFFFF) {
        if (!val) return TRUE;
        /* wstaw na koncu sekcji (po ostatniej niepustej linii) */
        lp[0] = 0;
        lstrcat(lp, key); lstrcat(lp, "="); lstrcat(lp, val); lstrcat(lp, "\r\n");
        {
            WORD at = nx, ls, le, p = o, last = o;
            while (p < at) { WORD n2 = ini_line(p, &ls, &le); if (le > ls) last = n2; p = n2; }
            if (last && INI(last - 1) != '\n') { ini_splice(last, 0, "\r\n", 2); last += 2; }
            ini_splice(last, 0, lp, (WORD)kstrlen(lp));
        }
    } else if (!val) {
        ini_splice(ks, nx - ks, "", 0);
    } else {
        ini_splice(vs, ve - vs, val, (WORD)kstrlen(val));
    }
    return ini_save();
}

/* ordinal 58/57/59 - WIN.INI */
int  API GetProfileString(LPCSTR s, LPCSTR k, LPCSTR d, LPSTR b, int n)
{ return GetPrivateProfileString(s, k, d, b, n, "WIN.INI"); }
WORD API GetProfileInt(LPCSTR s, LPCSTR k, int d)
{ return GetPrivateProfileInt(s, k, d, "WIN.INI"); }
BOOL API WriteProfileString(LPCSTR s, LPCSTR k, LPCSTR v)
{ return WritePrivateProfileString(s, k, v, "WIN.INI"); }
