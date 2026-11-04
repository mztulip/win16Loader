/* umisc.c - USER: debug, lancuchy, prostokaty, metryki, kolory, czas, wsprintf */
#include "user.h"

#define COM1 0x3F8
static BYTE inb(WORD p);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(WORD p, BYTE v);
#pragma aux outb = "out dx, al" parm [dx] [al];

void udbg(LPCSTR s)
{
    for (; *s; s++) {
        if (*s == '\n') { while (!(inb(COM1 + 5) & 0x20)); outb(COM1, '\r'); }
        while (!(inb(COM1 + 5) & 0x20));
        outb(COM1, (BYTE)*s);
    }
}

void udbg_hex(DWORD v, int d)
{
    char b[9];
    int i;
    if (d > 8) d = 8;
    for (i = 0; i < d; i++) b[i] = "0123456789ABCDEF"[(WORD)(v >> ((d - 1 - i) * 4)) & 0xF];
    b[d] = 0;
    udbg((LPCSTR)(char __far *)b);
}

/* ============================================================
 * Lancuchy
 * ============================================================ */
int ustrlen(LPCSTR s) { int n = 0; if (!s) return 0; while (s[n]) n++; return n; }
void ustrcpy(LPSTR d, LPCSTR s) { while ((*d++ = *s++) != 0); }
void ustrncpy(LPSTR d, LPCSTR s, int n)
{
    if (n <= 0) return;
    while (--n > 0 && s && *s) *d++ = *s++;
    *d = 0;
}
static char up(char c) { return (c >= 'a' && c <= 'z') || ((BYTE)c >= 0xE0 && (BYTE)c != 0xF7) ? (char)(c - 32) : c; }
static char lo(char c) { return (c >= 'A' && c <= 'Z') || ((BYTE)c >= 0xC0 && (BYTE)c <= 0xDE && (BYTE)c != 0xD7) ? (char)(c + 32) : c; }
int ustricmp(LPCSTR a, LPCSTR b)
{
    while (*a && up(*a) == up(*b)) { a++; b++; }
    return (int)(BYTE)up(*a) - (int)(BYTE)up(*b);
}
void umemset(LPVOID d, BYTE v, WORD n) { BYTE __far *p = (BYTE __far *)d; while (n--) *p++ = v; }
void umemcpy(LPVOID d, const void __far *s, WORD n)
{
    BYTE __far *p = (BYTE __far *)d;
    const BYTE __far *q = (const BYTE __far *)s;
    while (n--) *p++ = *q++;
}

/* ordinal 430 */
int API lstrcmp(LPCSTR a, LPCSTR b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(BYTE)*a - (int)(BYTE)*b;
}
/* ordinal 471 */
int API lstrcmpi(LPCSTR a, LPCSTR b) { return ustricmp(a, b); }

/* ordinal 431/432: znak (HIWORD=0) albo lancuch w miejscu */
LPSTR API AnsiUpper(LPSTR s)
{
    LPSTR p;
    if (!HIWORD(s)) return (LPSTR)MAKELONG(up((char)LOWORD(s)), 0);
    for (p = s; *p; p++) *p = up(*p);
    return s;
}
LPSTR API AnsiLower(LPSTR s)
{
    LPSTR p;
    if (!HIWORD(s)) return (LPSTR)MAKELONG(lo((char)LOWORD(s)), 0);
    for (p = s; *p; p++) *p = lo(*p);
    return s;
}
WORD API AnsiUpperBuff(LPSTR s, WORD n) { WORD i; for (i = 0; i < n; i++) s[i] = up(s[i]); return n; }
WORD API AnsiLowerBuff(LPSTR s, WORD n) { WORD i; for (i = 0; i < n; i++) s[i] = lo(s[i]); return n; }
LPSTR API AnsiNext(LPCSTR s) { return (LPSTR)(*s ? s + 1 : s); }
LPSTR API AnsiPrev(LPCSTR start, LPCSTR s) { return (LPSTR)(s > start ? s - 1 : start); }
BOOL API IsCharAlpha(char c) { return up(c) != lo(c); }
BOOL API IsCharAlphaNumeric(char c) { return IsCharAlpha(c) || (c >= '0' && c <= '9'); }
BOOL API IsCharUpper(char c) { return IsCharAlpha(c) && up(c) == c; }
BOOL API IsCharLower(char c) { return IsCharAlpha(c) && lo(c) == c; }

/* ============================================================
 * wsprintf / wvsprintf (formaty Win16: %d %u %i %x %X %s %c %ld %lu %lx %%)
 * ============================================================ */
static int fmt_num(LPSTR out, DWORD v, BOOL neg, int base, BOOL upper, int width, int prec, BOOL zero, BOOL left)
{
    char b[40];
    int n = 0, len, k = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { b[n++] = dig[(WORD)(v % base)]; v /= base; } while (v);
    /* precyzja = minimalna liczba cyfr, np. "%2.2u" (czas w SkiFree); wylacza flage 0 */
    if (prec >= 0) zero = FALSE;
    while (n < prec && n < 32) b[n++] = '0';
    if (neg) b[n++] = '-';
    len = n;
    if (!left) while (width > len) { out[k++] = zero ? '0' : ' '; width--; }
    if (zero && neg && !left && k) { out[0] = '-'; b[n - 1] = '0'; }
    while (n) out[k++] = b[--n];
    if (left) while (width > len) { out[k++] = ' '; width--; }
    return k;
}

int API wvsprintf(LPSTR out, LPCSTR f, WORD __far *a)
{
    int k = 0;
    while (*f) {
        BOOL left = FALSE, zero = FALSE, lng = FALSE;
        int width = 0, prec = -1;
        if (*f != '%') { out[k++] = *f++; continue; }
        f++;
        if (*f == '%') { out[k++] = *f++; continue; }
        if (*f == '-') { left = TRUE; f++; }
        if (*f == '#') f++;
        if (*f == '0') { zero = TRUE; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { prec = 0; f++; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        if (*f == 'l' || *f == 'L') { lng = TRUE; f++; }
        switch (*f) {
        case 'd': case 'i': {
            long v = lng ? *(long __far *)a : (long)(int)*a;
            a += lng ? 2 : 1;
            k += fmt_num(out + k, (DWORD)(v < 0 ? -v : v), v < 0, 10, FALSE, width, prec, zero, left);
            break;
        }
        case 'u': case 'x': case 'X': {
            DWORD v = lng ? *(DWORD __far *)a : (DWORD)*a;
            a += lng ? 2 : 1;
            k += fmt_num(out + k, v, FALSE, *f == 'u' ? 10 : 16, *f == 'X', width, prec, zero, left);
            break;
        }
        case 'c':
            out[k++] = (char)*a++;
            break;
        case 's': {
            LPCSTR s = *(LPCSTR __far *)a;
            int n;
            a += 2;
            if (!s) s = "(null)";
            n = ustrlen(s);
            if (prec >= 0 && n > prec) n = prec;
            if (!left) while (width > n) { out[k++] = ' '; width--; }
            while (n--) { out[k++] = *s++; width--; }
            if (left) while (width-- > 0) out[k++] = ' ';
            break;
        }
        default:
            out[k++] = *f;
        }
        if (*f) f++;
    }
    out[k] = 0;
    return k;
}

/* ordinal 420: cdecl, argumenty po formacie */
int __far __cdecl wsprintf(LPSTR out, LPCSTR f, ...)
{
    return wvsprintf(out, f, (WORD __far *)((char __far *)&f + sizeof(f)));
}

/* ============================================================
 * Prostokaty
 * ============================================================ */
BOOL rect_and(LPRECT o, const RECT __far *a, const RECT __far *b)
{
    o->left = a->left > b->left ? a->left : b->left;
    o->top = a->top > b->top ? a->top : b->top;
    o->right = a->right < b->right ? a->right : b->right;
    o->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (o->left >= o->right || o->top >= o->bottom) { o->left = o->top = o->right = o->bottom = 0; return FALSE; }
    return TRUE;
}

void API SetRect(LPRECT r, int l, int t, int ri, int b) { r->left = l; r->top = t; r->right = ri; r->bottom = b; }
void API SetRectEmpty(LPRECT r) { r->left = r->top = r->right = r->bottom = 0; }
int  API CopyRect(LPRECT d, const RECT __far *s) { *d = *s; return 1; }
BOOL API IsRectEmpty(const RECT __far *r) { return r->left >= r->right || r->top >= r->bottom; }
BOOL API PtInRect(const RECT __far *r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
void API OffsetRect(LPRECT r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; }
void API InflateRect(LPRECT r, int dx, int dy) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; }
BOOL API IntersectRect(LPRECT d, const RECT __far *a, const RECT __far *b) { return rect_and(d, a, b); }
BOOL API UnionRect(LPRECT d, const RECT __far *a, const RECT __far *b)
{
    if (IsRectEmpty(a)) { *d = *b; return !IsRectEmpty(b); }
    if (IsRectEmpty(b)) { *d = *a; return TRUE; }
    d->left = a->left < b->left ? a->left : b->left;
    d->top = a->top < b->top ? a->top : b->top;
    d->right = a->right > b->right ? a->right : b->right;
    d->bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    return TRUE;
}
BOOL API EqualRect(const RECT __far *a, const RECT __far *b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
BOOL API SubtractRect(LPRECT d, const RECT __far *a, const RECT __far *b)
{
    RECT i;
    *d = *a;
    if (!rect_and((LPRECT)&i, a, b)) return !IsRectEmpty(d);
    if (i.top == a->top && i.bottom == a->bottom) {
        if (i.left == a->left) d->left = i.right;
        else if (i.right == a->right) d->right = i.left;
    } else if (i.left == a->left && i.right == a->right) {
        if (i.top == a->top) d->top = i.bottom;
        else if (i.bottom == a->bottom) d->bottom = i.top;
    }
    return !IsRectEmpty(d);
}

/* ============================================================
 * Metryki i kolory systemowe
 * ============================================================ */
int sysmetric(int i)
{
    switch (i) {
    case 0:  return SCR_W;   case 1:  return SCR_H;
    case 2:  return 17;      case 3:  return 17;      /* CXVSCROLL, CYHSCROLL */
    case 4:  return 20;      /* CYCAPTION */
    case 5:  return 1;       case 6:  return 1;       /* CXBORDER, CYBORDER */
    case 7:  return 4;       case 8:  return 4;       /* CXDLGFRAME */
    case 9:  return 17;      case 10: return 17;      /* CYVTHUMB, CXHTHUMB */
    case 11: return 32;      case 12: return 32;      /* CXICON */
    case 13: return 32;      case 14: return 32;      /* CXCURSOR */
    case 15: return 18;      /* CYMENU */
    case 16: return SCR_W;   case 17: return SCR_H - 20;
    case 19: return 1;       /* MOUSEPRESENT */
    case 20: return 17;      case 21: return 17;      /* CYVSCROLL, CXHSCROLL */
    case 28: return 100;     case 29: return 27;      /* CXMIN, CYMIN */
    case 30: return 18;      case 31: return 18;      /* CXSIZE, CYSIZE */
    case 32: return 4;       case 33: return 4;       /* CXFRAME */
    case 34: return 100;     case 35: return 27;      /* MINTRACK */
    case 36: return 4;       case 37: return 4;       /* DOUBLECLK */
    case 38: return 75;      case 39: return 75;      /* ICONSPACING */
    }
    return 0;
}

int API GetSystemMetrics(int i) { return sysmetric(i); }

/* Domyslne kolory systemowe - wlasna paleta win16Loader (teal), nadpisywane przez [colors] w WIN.INI */
static COLORREF g_col[NCOLORS] = {
    RGB(0xB0, 0xD8, 0xE0), RGB(0x1C, 0x50, 0x58), RGB(0x0A, 0x6E, 0x7E), RGB(0x50, 0x88, 0x90), RGB(0xC0, 0xE4, 0xE8),
    RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0x00, 0x00), RGB(0x00, 0x00, 0x00), RGB(0x00, 0x00, 0x00), RGB(0xFF, 0xFF, 0xFF),
    RGB(0x88, 0xC8, 0xD0), RGB(0x88, 0xC8, 0xD0), RGB(0x88, 0xC0, 0xC8), RGB(0x0A, 0x6E, 0x7E), RGB(0xFF, 0xFF, 0xFF),
    RGB(0x90, 0xC8, 0xD0), RGB(0x40, 0x80, 0x88), RGB(0x50, 0x78, 0x80), RGB(0x00, 0x00, 0x00), RGB(0xC0, 0xE4, 0xE8),
    RGB(0xD0, 0xEC, 0xF0)
};
static HBRUSH g_colbr[NCOLORS];

static void load_colors(void)
{
    static const char * const names[NCOLORS] = {
        "Scrollbar", "Background", "ActiveTitle", "InactiveTitle", "Menu", "Window",
        "WindowFrame", "MenuText", "WindowText", "TitleText", "ActiveBorder", "InactiveBorder",
        "AppWorkspace", "Hilight", "HilightText", "ButtonFace", "ButtonShadow", "GrayText",
        "ButtonText", "InactiveTitleText", "ButtonHilight" };
    extern int API GetProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int);
    char b[24];
    int i;
    for (i = 0; i < NCOLORS; i++) {
        int v[3], k = 0, p = 0;
        if (!GetProfileString("colors", names[i], "", (LPSTR)(char __far *)b, sizeof(b))) continue;
        v[0] = v[1] = v[2] = 0;
        while (b[p] && k < 3) {
            while (b[p] == ' ') p++;
            while (b[p] >= '0' && b[p] <= '9') v[k] = v[k] * 10 + (b[p++] - '0');
            k++;
            while (b[p] && (b[p] < '0' || b[p] > '9')) p++;
        }
        if (k == 3) g_col[i] = RGB(v[0], v[1], v[2]);
    }
}

COLORREF syscolor(int i) { return (i >= 0 && i < NCOLORS) ? g_col[i] : 0; }
COLORREF API GetSysColor(int i) { return syscolor(i); }

HBRUSH sysbrush(int i)
{
    if (i < 0 || i >= NCOLORS) return GetStockObject(0);
    if (!g_colbr[i]) g_colbr[i] = CreateSolidBrush(g_col[i]);
    return g_colbr[i];
}

/* ordinal 181 */
void API SetSysColors(int n, int __far *idx, COLORREF __far *c)
{
    int i;
    for (i = 0; i < n; i++)
        if (idx[i] >= 0 && idx[i] < NCOLORS) {
            g_col[idx[i]] = c[i];
            if (g_colbr[idx[i]]) { DeleteObject(g_colbr[idx[i]]); g_colbr[idx[i]] = 0; }
        }
    {
        extern void sys_colors_changed(void);
        sys_colors_changed();
    }
}

void misc_init(void) { load_colors(); }

/* ============================================================
 * Czas i rozne
 * ============================================================ */
DWORD tick(void) { return *(DWORD __far *)MK_FP(SEL_KCB, 28); }
DWORD API GetTickCount(void)   { return tick(); }
DWORD API GetCurrentTime(void) { return tick(); }

static WORD g_dblclk = 500;
WORD API GetDoubleClickTime(void)    { return g_dblclk; }
void API SetDoubleClickTime(WORD t)  { g_dblclk = t ? t : 500; }

void API MessageBeep(WORD t)         { (void)t; }
BOOL API FlashWindow(HWND h, BOOL b) { (void)h; (void)b; return FALSE; }
int  API GetFreeSystemResources(WORD f) { (void)f; return 80; }
HWND API GetSysModalWindow(void)     { return 0; }
HWND API SetSysModalWindow(HWND h)   { (void)h; return 0; }
BOOL API LockWindowUpdate(HWND h)    { (void)h; return TRUE; }

/* ordinal 483: SPI_* - zwracamy rozsadne domyslne */
BOOL API SystemParametersInfo(WORD act, WORD p, LPVOID pv, WORD win)
{
    (void)p; (void)win;
    switch (act) {
    case 1:  if (pv) *(WORD __far *)pv = 0; return TRUE;          /* SPI_GETBEEP */
    case 13: if (pv) *(int __far *)pv = 75; return TRUE;          /* SPI_ICONHORIZONTALSPACING */
    case 24: if (pv) *(int __far *)pv = 75; return TRUE;          /* SPI_ICONVERTICALSPACING */
    case 25: if (pv) *(int __far *)pv = 1; return TRUE;           /* SPI_GETICONTITLEWRAP */
    case 27: if (pv) *(int __far *)pv = 0; return TRUE;           /* SPI_GETMENUDROPALIGNMENT */
    case 31: if (pv) umemset(pv, 0, 50); return TRUE;             /* SPI_GETICONTITLELOGFONT */
    }
    return TRUE;
}

/* ordinal 7: ExitWindows - koniec sesji = koniec apki */
BOOL API ExitWindows(DWORD r, WORD res)
{
    (void)r; (void)res;
    udbg("USER: ExitWindows\n");
    FatalAppExit(0, "ExitWindows");
    return FALSE;
}

/* ============================================================
 * Rozne (wymagane przez Calmire i Delphi 1)
 * ============================================================ */
BOOL API WinHelp(HWND h, LPCSTR file, WORD cmd, DWORD data)
{
    (void)h; (void)cmd; (void)data;
    udbg("USER: WinHelp (brak) "); udbg(file ? file : ""); udbg("\n");
    return FALSE;
}
DWORD API GetSystemDebugState(void)             { return 0; }   /* SDK: DWORD (DX = 0) */
BOOL API ExitWindowsExec(LPCSTR exe, LPCSTR par) { (void)exe; (void)par; return FALSE; }
WORD API WNetGetConnection(LPSTR l, LPSTR r, WORD __far *n) { (void)l; (void)r; (void)n; return 1; }  /* WN_NOT_SUPPORTED */

/* Palety: USER.282/283 przekazuja do GDI */
WORD API SelectPalette(HDC dc, WORD pal, BOOL bg) { return GDISelectPalette(dc, pal, bg); }
WORD API RealizePalette(HDC dc)                  { return GDIRealizePalette(dc); }

/* MDI (bez okna MDICLIENT - wystarcza domyslne przetwarzanie) */
LRESULT API DefFrameProc(HWND h, HWND client, WORD m, WORD wp, LONG lp) { (void)client; return DefWindowProc(h, m, wp, lp); }
LRESULT API DefMDIChildProc(HWND h, WORD m, WORD wp, LONG lp)           { return DefWindowProc(h, m, wp, lp); }
BOOL API TranslateMDISysAccel(HWND h, LPMSG m)                         { (void)h; (void)m; return FALSE; }

/* Atomy globalne (USER.268..271) - wspolna tablica z RegisterWindowMessage */
extern WORD user_atom(LPCSTR s);
extern LPCSTR user_atom_name(WORD a);

static WORD int_atom(LPCSTR s)
{
    WORD v = 0;
    if (!HIWORD(s)) return LOWORD(s);
    if (s[0] != '#') return 0;
    for (s++; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v ? v : 0xFFFF;
}

WORD API GlobalAddAtom(LPCSTR s)
{
    WORD a = int_atom(s);
    if (a) return a == 0xFFFF ? 0 : a;
    return user_atom(s);
}

WORD API GlobalFindAtom(LPCSTR s)
{
    extern WORD user_atom_find(LPCSTR s);
    WORD a = int_atom(s);
    if (a) return a == 0xFFFF ? 0 : a;
    return user_atom_find(s);
}

WORD API GlobalDeleteAtom(WORD a) { (void)a; return 0; }

int API GlobalGetAtomName(WORD a, LPSTR buf, int n)
{
    LPCSTR s;
    if (n <= 0) return 0;
    if (a < 0xC000) {
        WORD args[1];
        args[0] = a;
        return wvsprintf(buf, "#%u", (WORD __far *)args);
    }
    s = user_atom_name(a);
    if (!s || !s[0]) { buf[0] = 0; return 0; }
    ustrncpy(buf, s, n);
    return ustrlen(buf);
}
