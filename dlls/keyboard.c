/* keyboard.c - KEYBOARD.DRV: konwersje ANSI (cp1252) <-> OEM (cp437), uklad US
 *
 * Przerwania klawiatury obsluguje loader/USER - tu tylko API zestawu znakow
 * i mapowania klawiszy uzywane przez aplikacje.
 */
#include "w16.h"

/* OEM cp437 0x80..0xFF -> ANSI cp1252 ('_' = brak odpowiednika) */
static const BYTE oem2ansi[128] = {
    0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5,
    0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x5F,0x83,
    0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x5F,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB,
    0x5F,0x5F,0x5F,0xA6,0x2B,0x2B,0x2B,0x2B,0x2B,0x2B,0xA6,0x2B,0x2B,0x2B,0x2B,0x2B,
    0x2B,0x2D,0x2D,0x2B,0x2D,0x2B,0x2B,0x2B,0x2B,0x2B,0x2D,0x2D,0x2B,0x2D,0x2B,0x2D,
    0x2D,0x2D,0x2D,0x2B,0x2B,0x2B,0x2B,0x2B,0x2B,0x2B,0x2B,0x5F,0x5F,0xA6,0x5F,0x5F,
    0x5F,0xDF,0x5F,0x5F,0x5F,0x5F,0xB5,0x5F,0x5F,0x5F,0x5F,0x5F,0x5F,0xF8,0x5F,0x5F,
    0x5F,0xB1,0x5F,0x5F,0x5F,0x5F,0xF7,0x5F,0xB0,0xB7,0xB7,0x5F,0x5F,0xB2,0x5F,0xA0
};

static BYTE ansi2oem[128];
static BOOL g_init;

static void init_tables(void)
{
    int i;
    if (g_init) return;
    g_init = TRUE;
    for (i = 0; i < 128; i++) ansi2oem[i] = '_';
    for (i = 127; i >= 0; i--)                     /* pierwsze dopasowanie wygrywa */
        if (oem2ansi[i] >= 0x80) ansi2oem[oem2ansi[i] - 0x80] = (BYTE)(0x80 + i);
    ansi2oem[0xA0 - 0x80] = 0xFF;                  /* NBSP */
}

static BYTE a2o(BYTE c) { init_tables(); return c < 0x80 ? c : ansi2oem[c - 0x80]; }
static BYTE o2a(BYTE c) { return c < 0x80 ? c : oem2ansi[c - 0x80]; }

/* ordinal 5 */
int API AnsiToOem(LPCSTR s, LPSTR d)
{
    while (*s) *d++ = (char)a2o((BYTE)*s++);
    *d = 0;
    return -1;
}

/* ordinal 6 */
BOOL API OemToAnsi(LPCSTR s, LPSTR d)
{
    while (*s) *d++ = (char)o2a((BYTE)*s++);
    *d = 0;
    return -1;
}

/* ordinal 134 */
void API AnsiToOemBuff(LPCSTR s, LPSTR d, WORD n)
{
    while (n--) *d++ = (char)a2o((BYTE)*s++);
}

/* ordinal 135 */
void API OemToAnsiBuff(LPCSTR s, LPSTR d, WORD n)
{
    while (n--) *d++ = (char)o2a((BYTE)*s++);
}

/* ordinal 132: strona kodowa */
int API GetKBCodePage(void) { return 437; }

/* ordinal 130: typ 4 = klawiatura rozszerzona 101/102, 12 klawiszy F */
int API GetKeyboardType(int what)
{
    switch (what) {
    case 0: return 4;
    case 1: return 0;
    case 2: return 12;
    }
    return 0;
}

/* VK <-> scancode (zestaw 1, US) */
static const BYTE sc_vk[0x59] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0', 0xBD, 0xBB, 0x08, 0x09,
    'Q','W','E','R','T','Y','U','I','O','P', 0xDB, 0xDD, 0x0D, 0x11, 'A','S',
    'D','F','G','H','J','K','L', 0xBA, 0xDE, 0xC0, 0x10, 0xDC, 'Z','X','C','V',
    'B','N','M', 0xBC, 0xBE, 0xBF, 0x10, 0x6A, 0x12, 0x20, 0x14, 0x70,0x71,0x72,0x73,0x74,
    0x75,0x76,0x77,0x78,0x79, 0x90, 0x91, 0x24, 0x26, 0x21, 0x6D, 0x25, 0x0C, 0x27, 0x6B, 0x23,
    0x28, 0x22, 0x2D, 0x2E, 0, 0, 0, 0x7A, 0x7B
};

static char vk_ascii(BYTE vk, BOOL shift)
{
    static const char num_sh[] = ")!@#$%^&*(";
    if (vk >= 'A' && vk <= 'Z') return (char)(shift ? vk : vk + 32);
    if (vk >= '0' && vk <= '9') return shift ? num_sh[vk - '0'] : (char)vk;
    switch (vk) {
    case 0x20: return ' ';
    case 0xBA: return shift ? ':' : ';';
    case 0xBB: return shift ? '+' : '=';
    case 0xBC: return shift ? '<' : ',';
    case 0xBD: return shift ? '_' : '-';
    case 0xBE: return shift ? '>' : '.';
    case 0xBF: return shift ? '?' : '/';
    case 0xC0: return shift ? '~' : '`';
    case 0xDB: return shift ? '{' : '[';
    case 0xDC: return shift ? '|' : '\\';
    case 0xDD: return shift ? '}' : ']';
    case 0xDE: return shift ? '"' : '\'';
    }
    return 0;
}

/* ordinal 131: 0 = VK->SC, 1 = SC->VK, 2 = VK->znak */
WORD API MapVirtualKey(WORD code, WORD type)
{
    WORD i;
    switch (type) {
    case 0:
        for (i = 1; i < sizeof(sc_vk); i++) if (sc_vk[i] == code) return i;
        return 0;
    case 1: return code < sizeof(sc_vk) ? sc_vk[code] : 0;
    case 2: return (BYTE)vk_ascii((BYTE)code, FALSE);
    }
    return 0;
}

/* ordinal 129: znak -> VK (HIBYTE: 1 = Shift) */
DWORD API VkKeyScan(WORD ch)
{
    WORD vk;
    for (vk = 0x20; vk < 0xE0; vk++) {
        if (vk_ascii((BYTE)vk, FALSE) == (char)ch) return vk;
        if (vk_ascii((BYTE)vk, TRUE) == (char)ch) return vk | 0x100;
    }
    return 0xFFFFFFFFUL;
}

/* ordinal 128 */
DWORD API OemKeyScan(WORD oem)
{
    DWORD r = VkKeyScan(oem);
    WORD sc;
    if (r == 0xFFFFFFFFUL) return r;
    sc = MapVirtualKey((WORD)(r & 0xFF), 0);
    return MAKELONG(sc, (r >> 8) & 1);
}

/* ordinal 4 */
int API ToAscii(WORD vk, WORD sc, BYTE __far *state, DWORD __far *out, WORD fl)
{
    BOOL shift = state && ((state[0x10] & 0x80) != 0);
    BOOL caps = state && (state[0x14] & 1);
    char c;
    (void)sc; (void)fl;
    if (vk >= 'A' && vk <= 'Z') shift = shift != caps;
    c = vk_ascii((BYTE)vk, shift);
    if (state && (state[0x11] & 0x80) && vk >= 'A' && vk <= 'Z') c = (char)(vk - 'A' + 1);
    if (vk == 0x0D || vk == 0x08 || vk == 0x09 || vk == 0x1B) c = (char)vk;
    if (!c) return 0;
    *(BYTE __far *)out = (BYTE)c;
    return 1;
}

/* ordinal 133 */
int API GetKeyNameText(LONG lp, LPSTR buf, int n)
{
    static const char * const names[] = { "Esc", "Backspace", "Tab", "Enter", "Ctrl", "Shift", "Alt", "Space", "Caps Lock" };
    WORD sc = (WORD)((lp >> 16) & 0xFF), vk = sc < sizeof(sc_vk) ? sc_vk[sc] : 0;
    char tmp[2];
    LPCSTR s = 0;
    if (n <= 0) return 0;
    switch (vk) {
    case 0x1B: s = names[0]; break; case 0x08: s = names[1]; break; case 0x09: s = names[2]; break;
    case 0x0D: s = names[3]; break; case 0x11: s = names[4]; break; case 0x10: s = names[5]; break;
    case 0x12: s = names[6]; break; case 0x20: s = names[7]; break; case 0x14: s = names[8]; break;
    }
    if (!s) {
        tmp[0] = vk_ascii((BYTE)vk, TRUE); tmp[1] = 0;
        if (!tmp[0]) { buf[0] = 0; return 0; }
        s = (LPCSTR)(char __far *)tmp;
    }
    w_strncpy(buf, s, n);
    return w_strlen(buf);
}

/* Sterownik: Inquire/Enable/Disable - nic nie robimy (obsluga w loaderze) */
WORD API Inquire(LPVOID info)   { if (info) w_memset(info, 0, 14); return 14; }
void API Enable(LPVOID cb, LPVOID st) { (void)cb; (void)st; }
void API Disable(void)          { }
void API SetSpeed(WORD s)       { (void)s; }
void API ScreenSwitchEnable(WORD e) { (void)e; }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
