/* shell.c - SHELL.DLL: rejestr Windows 3.1, Drag&Drop, ShellExecute, ExtractIcon
 *
 * Rejestr 3.1 to jedno drzewo (HKEY_CLASSES_ROOT = 1, 0 tez oznacza korzen),
 * kazdy klucz ma jedna wartosc tekstowa. Trzymamy go w DGROUP i zapisujemy
 * tekstowo do <windir>\REG.TXT (linie "sciezka\klucza=wartosc").
 */
#include "w16.h"

#define ERROR_SUCCESS       0L
#define ERROR_BADKEY        2L
#define ERROR_CANTOPEN      3L
#define ERROR_OUTOFMEMORY   6L
#define ERROR_NO_MORE_ITEMS 259L

#define MAX_KEYS  256
#define MAX_NAME  32
#define MAX_VAL   80

typedef struct {
    BYTE used;
    WORD parent;                  /* indeks rodzica (0 = korzen) */
    char name[MAX_NAME];
    char val[MAX_VAL];
} REGKEY;

static REGKEY g_key[MAX_KEYS];    /* g_key[0] = korzen */
static BOOL g_loaded;
static char g_file[80];

/* HKEY <-> indeks: korzen 0/1 -> 0, pozostale = indeks + 0x100 */
static int key_idx(HKEY h)
{
    if (h == 0 || h == 1) return 0;
    if (h >= 0x100 && h < 0x100 + MAX_KEYS && g_key[(WORD)h - 0x100].used) return (WORD)h - 0x100;
    return -1;
}
static HKEY key_handle(int i) { return i == 0 ? 1 : 0x100 + i; }

static int child(int parent, LPCSTR name, int len)
{
    int i;
    for (i = 1; i < MAX_KEYS; i++)
        if (g_key[i].used && g_key[i].parent == parent && w_strlen(g_key[i].name) == len &&
            !w_strnicmp(g_key[i].name, name, len)) return i;
    return -1;
}

/* Znajdz (lub utworz) klucz po sciezce "a\b\c" wzgledem 'from' */
static int key_walk(int from, LPCSTR path, BOOL create)
{
    int cur = from;
    if (!path || !HIWORD(path)) return cur;
    while (*path) {
        int len = 0, n;
        while (*path == '\\') path++;
        while (path[len] && path[len] != '\\') len++;
        if (!len) break;
        n = child(cur, path, len);
        if (n < 0) {
            if (!create) return -1;
            for (n = 1; n < MAX_KEYS; n++) if (!g_key[n].used) break;
            if (n == MAX_KEYS) return -2;
            g_key[n].used = 1;
            g_key[n].parent = cur;
            w_strncpy(g_key[n].name, path, len + 1 < MAX_NAME ? len + 1 : MAX_NAME);
            g_key[n].val[0] = 0;
        }
        cur = n;
        path += len;
    }
    return cur;
}

/* ---- zapis/odczyt REG.TXT ---- */
static int key_path(int i, LPSTR buf, int n)
{
    int l = 0;
    if (i == 0) { buf[0] = 0; return 0; }
    if (g_key[i].parent) {
        l = key_path(g_key[i].parent, buf, n);
        if (l < n - 1) buf[l++] = '\\';
    }
    w_strncpy(buf + l, g_key[i].name, n - l);
    return w_strlen(buf);
}

static void reg_save(void)
{
    static char line[200];
    int f, i;
    if (!g_file[0]) return;
    f = _lcreat(g_file, 0);
    if (f < 0) return;
    for (i = 1; i < MAX_KEYS; i++) {
        int l;
        if (!g_key[i].used) continue;
        l = key_path(i, line, 120);
        line[l++] = '=';
        w_strcpy(line + l, g_key[i].val);
        l = w_strlen(line);
        line[l++] = '\r'; line[l++] = '\n';
        _lwrite(f, line, l);
    }
    _lclose(f);
}

static void reg_load(void)
{
    static char buf[4096];
    int f, n, s, i;
    if (g_loaded) return;
    g_loaded = TRUE;
    g_key[0].used = 1;
    n = GetWindowsDirectory(g_file, 60);
    if (n && g_file[n - 1] != '\\') g_file[n++] = '\\';
    w_strcpy(g_file + n, "REG.TXT");
    f = _lopen(g_file, 0);
    if (f < 0) return;
    n = _lread(f, buf, sizeof(buf) - 1);
    _lclose(f);
    if (n <= 0 || n >= (int)sizeof(buf)) return;
    buf[n] = 0;
    for (s = 0; s < n; s = i + 1) {
        int eq = -1, k;
        for (i = s; i < n && buf[i] != '\n'; i++) if (buf[i] == '=' && eq < 0) eq = i;
        if (eq < 0) continue;
        buf[eq] = 0;
        k = i;
        if (k > eq + 1 && buf[k - 1] == '\r') k--;
        buf[k] = 0;
        k = key_walk(0, buf + s, TRUE);
        if (k > 0) w_strncpy(g_key[k].val, buf + eq + 1, MAX_VAL);
    }
}

/* ---- API rejestru (ordynaly 1..7) ---- */
LONG API RegOpenKey(HKEY h, LPCSTR sub, HKEY __far *out)
{
    int i;
    reg_load();
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    i = key_walk(i, sub, FALSE);
    if (i < 0) return ERROR_BADKEY;
    *out = key_handle(i);
    return ERROR_SUCCESS;
}

LONG API RegCreateKey(HKEY h, LPCSTR sub, HKEY __far *out)
{
    int i;
    reg_load();
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    i = key_walk(i, sub, TRUE);
    if (i == -2) return ERROR_OUTOFMEMORY;
    if (i < 0) return ERROR_BADKEY;
    *out = key_handle(i);
    reg_save();
    return ERROR_SUCCESS;
}

LONG API RegCloseKey(HKEY h) { (void)h; return ERROR_SUCCESS; }

static void del_tree(int i)
{
    int k;
    for (k = 1; k < MAX_KEYS; k++) if (g_key[k].used && g_key[k].parent == i) del_tree(k);
    g_key[i].used = 0;
}

LONG API RegDeleteKey(HKEY h, LPCSTR sub)
{
    int i;
    reg_load();
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    i = key_walk(i, sub, FALSE);
    if (i <= 0) return ERROR_BADKEY;
    del_tree(i);
    reg_save();
    return ERROR_SUCCESS;
}

LONG API RegSetValue(HKEY h, LPCSTR sub, DWORD type, LPCSTR data, DWORD cb)
{
    int i;
    (void)cb;
    reg_load();
    if (type != 1) return ERROR_BADKEY;          /* tylko REG_SZ */
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    i = key_walk(i, sub, TRUE);
    if (i < 0) return i == -2 ? ERROR_OUTOFMEMORY : ERROR_BADKEY;
    w_strncpy(g_key[i].val, data ? data : "", MAX_VAL);
    reg_save();
    return ERROR_SUCCESS;
}

LONG API RegQueryValue(HKEY h, LPCSTR sub, LPSTR data, LPLONG cb)
{
    int i, len;
    reg_load();
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    i = key_walk(i, sub, FALSE);
    if (i < 0) return ERROR_BADKEY;
    len = w_strlen(g_key[i].val) + 1;
    if (data && cb && *cb > 0) w_strncpy(data, g_key[i].val, (int)(*cb < 0x7FFF ? *cb : 0x7FFF));
    if (cb) *cb = len;
    return ERROR_SUCCESS;
}

LONG API RegEnumKey(HKEY h, DWORD idx, LPSTR buf, DWORD cb)
{
    int i, k;
    DWORD n = 0;
    reg_load();
    i = key_idx(h);
    if (i < 0) return ERROR_BADKEY;
    for (k = 1; k < MAX_KEYS; k++) {
        if (!g_key[k].used || g_key[k].parent != i) continue;
        if (n++ == idx) {
            if (buf && cb) w_strncpy(buf, g_key[k].name, (int)(cb < 0x7FFF ? cb : 0x7FFF));
            return ERROR_SUCCESS;
        }
    }
    return ERROR_NO_MORE_ITEMS;
}

/* ---- Drag & Drop (brak zrodla upuszczania - nic nie przychodzi) ---- */
void API DragAcceptFiles(HWND h, BOOL a)              { (void)h; (void)a; }
WORD API DragQueryFile(HANDLE d, WORD i, LPSTR b, WORD n) { (void)d; (void)i; if (b && n) b[0] = 0; return 0; }
void API DragFinish(HANDLE d)                         { (void)d; }
BOOL API DragQueryPoint(HANDLE d, LPVOID p)           { (void)d; if (p) w_memset(p, 0, 4); return FALSE; }

/* ---- ShellExecute / FindExecutable ---- */
HINSTANCE API ShellExecute(HWND h, LPCSTR op, LPCSTR file, LPCSTR par, LPCSTR dir, int show)
{
    (void)h; (void)par; (void)dir; (void)show;
    dbg("SHELL: ShellExecute (brak wielozadaniowosci) ");
    if (op && HIWORD(op)) { dbg(op); dbg(" "); }
    if (file && HIWORD(file)) dbg(file);
    dbg("\n");
    return 31;                                    /* SE_ERR_NOASSOC */
}

HINSTANCE API FindExecutable(LPCSTR file, LPCSTR dir, LPSTR res)
{
    (void)file; (void)dir;
    if (res) res[0] = 0;
    return 31;
}

int API ShellAbout(HWND h, LPCSTR app, LPCSTR other, HICON icon)
{
    (void)h; (void)app; (void)other; (void)icon;
    return 0;
}

/* ---- ExtractIcon: ikony z pliku NE (.EXE/.DLL) lub .ICO ---- */
static WORD rdw(BYTE __far *p) { return (WORD)(p[0] | ((WORD)p[1] << 8)); }
static DWORD rdd(BYTE __far *p) { return rdw(p) | ((DWORD)rdw(p + 2) << 16); }

static HICON icon_from_file(int f, DWORD off, DWORD size)
{
    WORD hm;
    BYTE __far *p;
    HICON r = 0;
    if (!size || size > 0xFFF0UL) return 0;
    hm = GlobalAlloc(0x42, size);
    if (!hm) return 0;
    p = (BYTE __far *)GlobalLock(hm);
    _llseek(f, off, 0);
    if (_lread(f, p, (WORD)size) == (WORD)size) r = UserIconFromDIB(p, size);
    GlobalFree(hm);
    return r;
}

/* Najlepszy wpis katalogu grupy: 32x32, najwiecej kolorow do 8 bpp */
static int best_entry(BYTE __far *ent, int n, int stride)
{
    int i, best = 0, score = -1;
    for (i = 0; i < n; i++) {
        BYTE __far *e = ent + i * stride;
        int w = e[0] ? e[0] : 256, bpp = rdw(e + 6), col = e[2];
        int s = (w == 32 ? 1000 : 0) + (bpp ? (bpp <= 8 ? bpp * 10 : 0) : (col == 16 ? 40 : col == 0 ? 80 : 10));
        if (s > score) { score = s; best = i; }
    }
    return best;
}

static HICON extract_ico(int f, int idx)
{
    static BYTE dir[6 + 16 * 16];
    int n, b;
    _llseek(f, 0, 0);
    if (_lread(f, dir, 6) != 6 || rdw(dir) != 0 || rdw(dir + 2) != 1) return 0;
    n = rdw(dir + 4);
    if (idx == -1) return 1;
    if (idx != 0 || n == 0) return 0;
    if (n > 16) n = 16;
    _lread(f, dir + 6, n * 16);
    b = best_entry(dir + 6, n, 16);
    return icon_from_file(f, rdd(dir + 6 + b * 16 + 12), rdd(dir + 6 + b * 16 + 8));
}

static HICON extract_ne(int f, DWORD ne, int idx)
{
    static BYTE hdr[64];
    static BYTE rt[2048];
    static BYTE grp[6 + 14 * 16];
    WORD rsrc, restab, shift, len, gcount = 0;
    BYTE __far *p;
    DWORD goff = 0, gsize = 0;
    int n, b, i;
    WORD id;
    _llseek(f, ne, 0);
    if (_lread(f, hdr, 64) != 64) return 0;
    rsrc = rdw(hdr + 0x24); restab = rdw(hdr + 0x26);
    if (rsrc == restab) return idx == -1 ? 0 : 0;
    len = restab - rsrc;
    if (len > sizeof(rt)) len = sizeof(rt);
    _llseek(f, ne + rsrc, 0);
    if (_lread(f, rt, len) != len) return 0;
    shift = rdw(rt);
    /* RT_GROUP_ICON = 14 */
    for (p = rt + 2; p + 8 <= rt + len && rdw(p); p += 8 + rdw(p + 2) * 12) {
        if (rdw(p) != 0x800E) continue;
        gcount = rdw(p + 2);
        if (idx == -1) return gcount;
        if (idx < 0 || idx >= (int)gcount) return 0;
        goff = (DWORD)rdw(p + 8 + idx * 12) << shift;
        gsize = (DWORD)rdw(p + 8 + idx * 12 + 2) << shift;
        break;
    }
    if (idx == -1) return 0;
    if (!goff) return 0;
    if (gsize > sizeof(grp)) gsize = sizeof(grp);
    _llseek(f, goff, 0);
    if (_lread(f, grp, (WORD)gsize) < 6) return 0;
    n = rdw(grp + 4);
    if (n > 16) n = 16;
    if (!n) return 0;
    b = best_entry(grp + 6, n, 14);
    id = rdw(grp + 6 + b * 14 + 12);
    /* RT_ICON = 3 */
    for (p = rt + 2; p + 8 <= rt + len && rdw(p); p += 8 + rdw(p + 2) * 12) {
        if (rdw(p) != 0x8003) continue;
        for (i = 0; i < (int)rdw(p + 2); i++) {
            BYTE __far *e = p + 8 + i * 12;
            if (rdw(e + 6) == (id | 0x8000))
                return icon_from_file(f, (DWORD)rdw(e) << shift, (DWORD)rdw(e + 2) << shift);
        }
    }
    return 0;
}

HICON API ExtractIcon(HINSTANCE hi, LPCSTR file, int idx)
{
    static BYTE mz[64];
    int f;
    HICON r = 0;
    (void)hi;
    if (!file || !HIWORD(file)) return 0;
    f = _lopen(file, 0);
    if (f < 0) return 0;
    if (_lread(f, mz, 64) == 64) {
        if (mz[0] == 'M' && mz[1] == 'Z') {
            DWORD ne = rdd(mz + 0x3C);
            BYTE sig[2];
            _llseek(f, ne, 0);
            if (_lread(f, (LPVOID)(BYTE __far *)sig, 2) == 2 && sig[0] == 'N' && sig[1] == 'E')
                r = extract_ne(f, ne, idx);
            else r = idx == -1 ? 0 : 1;           /* plik nie jest NE (PE/DOS): 1 = nie ma ikon */
        } else r = extract_ico(f, idx);
    }
    _lclose(f);
    return r;
}

HICON API ExtractAssociatedIcon(HINSTANCE hi, LPSTR file, WORD __far *idx)
{
    HICON r = ExtractIcon(hi, file, idx ? (int)*idx : 0);
    return r > 1 ? r : 0;
}

/* ---- Rozne ---- */
DWORD API DoEnvironmentSubst(LPSTR s, WORD n) { (void)n; return MAKELONG(1, s ? w_strlen(s) : 0); }
WORD API DriveType(DWORD d) { return GetDriveType((int)d); }
BOOL API RegisterShellHook(HWND h, WORD f) { (void)h; (void)f; return FALSE; }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
