/* kmain.c - KERNEL: InitTask, zadania, wersja, lancuchy, debug */
#include "kernel.h"

#define COM1 0x3F8

/* ============================================================
 * Debug na COM1
 * ============================================================ */
static void ser_putc(char c)
{
    while (!(io_inb(COM1 + 5) & 0x20));
    io_outb(COM1, (BYTE)c);
}

void dbg_puts(LPCSTR s)
{
    while (*s) {
        if (*s == '\n') ser_putc('\r');
        ser_putc(*s++);
    }
}

void dbg_hex(DWORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        ser_putc("0123456789ABCDEF"[(WORD)(v >> (i * 4)) & 0xF]);
}

void dbg_dec(DWORD v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do { b[--i] = (char)('0' + (WORD)(v % 10)); v /= 10; } while (v && i);
    dbg_puts((LPCSTR)(char __far *)&b[i]);
}

void dbg_str(LPCSTR label, LPCSTR s)
{
    dbg_puts(label);
    ser_putc('\'');
    if (HIWORD(s)) dbg_puts(s);
    else { ser_putc('#'); dbg_hex(LOWORD(s), 4); }
    dbg_puts("'\n");
}

/* ============================================================
 * Lancuchy
 * ============================================================ */
int kstrlen(LPCSTR s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

void kstrcpy(LPSTR d, LPCSTR s)
{
    while ((*d++ = *s++) != 0);
}

void kstrncpy(LPSTR d, LPCSTR s, int n)
{
    if (n <= 0) return;
    while (--n > 0 && *s) *d++ = *s++;
    *d = 0;
}

char kupper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

int kstricmp(LPCSTR a, LPCSTR b)
{
    while (*a && kupper(*a) == kupper(*b)) { a++; b++; }
    return (BYTE)kupper(*a) - (BYTE)kupper(*b);
}

int kstrnicmp(LPCSTR a, LPCSTR b, int n)
{
    while (n-- > 0) {
        char x = kupper(*a++), y = kupper(*b++);
        if (x != y) return (BYTE)x - (BYTE)y;
        if (!x) break;
    }
    return 0;
}

void kmemcpy(LPVOID d, const void __far *s, WORD n)
{
    BYTE __far *dd = (BYTE __far *)d;
    const BYTE __far *ss = (const BYTE __far *)s;
    while (n--) *dd++ = *ss++;
}

void kmemset(LPVOID d, BYTE v, WORD n)
{
    BYTE __far *dd = (BYTE __far *)d;
    while (n--) *dd++ = v;
}

/* ordinal 88 */
LPSTR API lstrcpy(LPSTR d, LPCSTR s)   { kstrcpy(d, s); return d; }
/* ordinal 89 */
LPSTR API lstrcat(LPSTR d, LPCSTR s)   { kstrcpy(d + kstrlen(d), s); return d; }
/* ordinal 90 */
int   API lstrlen(LPCSTR s)            { return s ? kstrlen(s) : 0; }
/* ordinal 353 */
LPSTR API lstrcpyn(LPSTR d, LPCSTR s, int n) { kstrncpy(d, s, n); return d; }

/* ordinal 115 */
void API OutputDebugString(LPCSTR s)   { dbg_puts(s); }

/* ============================================================
 * InitTask (ordinal 91) - wejscie w asm (kasm.asm) wola inittask_c(),
 * potem laduje rejestry z g_it[]:
 *   AX=1, BX=offset linii polecen w PSP (0x81), CX=rozmiar stosu, DX=nCmdShow,
 *   SI=hPrevInstance, DI=hInstance, ES=PSP.
 * Wypelnia tez obszar instancji DS:0..0x0F (RTL Borland/Delphi czyta
 * pStackTop/pStackMin/pStackBottom) i inicjalizuje local heap apki.
 * ============================================================ */
WORD g_it_ax, g_it_bx, g_it_cx, g_it_dx, g_it_si, g_it_di, g_it_es;
static WORD g_task;

static void task_init(void)
{
    WORD hinst = KCBW(KCB_APP_HINST);
    BYTE __far *tdb;
    int mi;
    if (g_task) return;
    g_task = (WORD)GlobalAlloc(0x40, 256);
    if (!g_task) return;
    tdb = (BYTE __far *)MK_FP(g_task, 0);
    *(WORD __far *)(tdb + 0x1C) = hinst;              /* TDB_hInstance */
    mi = mod_from_handle(hinst);
    if (mi >= 0) {
        *(WORD __far *)(tdb + 0x1E) = MTW(mi, MT_HMOD);  /* TDB_hModule */
        kstrncpy((LPSTR)(tdb + 0xF2), (LPCSTR)(MTE(mi) + MT_NAME), 8);
    }
    tdb[0xFA] = 'T'; tdb[0xFB] = 'D';
}

void inittask_c(void)
{
    WORD hinst = KCBW(KCB_APP_HINST);
    WORD sp    = KCBW(KCB_APP_SP);
    WORD stk   = KCBW(KCB_APP_STACK);
    WORD __far *inst = (WORD __far *)MK_FP(hinst, 0);
    BYTE __far *psp  = (BYTE __far *)MK_FP(SEL_PSP, 0);
    LPCSTR cmd = KCBS(KCB_CMDLINE);
    int n = kstrlen(cmd);

    kmem_init();
    kdos_init();
    task_init();

    /* Linia polecen w PSP: [0x80]=dlugosc, [0x81..]=tekst, 0x0D */
    if (n > 125) n = 125;
    psp[0x80] = (BYTE)(n ? n + 1 : 0);
    psp[0x81] = ' ';
    kmemcpy(psp + (n ? 0x82 : 0x81), cmd, n);
    psp[(n ? 0x82 : 0x81) + n] = 0x0D;
    psp[(n ? 0x83 : 0x82) + n] = 0;

    /* Obszar instancji DGROUP: [0x0A]=pStackTop [0x0C]=pStackMin [0x0E]=pStackBottom */
    inst[0x0A / 2] = (WORD)(sp - stk);
    inst[0x0C / 2] = sp;
    inst[0x0E / 2] = sp;

    local_init_app();

    g_it_ax = 1;
    g_it_bx = n ? 0x82 : 0x81;
    g_it_cx = stk;
    g_it_dx = 1;                /* nCmdShow = SW_SHOWNORMAL */
    g_it_si = 0;                /* hPrevInstance */
    g_it_di = hinst;
    g_it_es = SEL_PSP;
    dbg_puts("KERNEL: InitTask hInst="); dbg_hex(hinst, 4);
    dbg_puts(" SP="); dbg_hex(sp, 4);
    dbg_puts(" stack="); dbg_hex(stk, 4);
    dbg_puts(" task="); dbg_hex(g_task, 4); dbg_puts("\n");
}

/* ============================================================
 * Wersja, flagi, zadania
 * ============================================================ */
/* ordinal 3: Windows 3.10, DOS 6.22 */
DWORD API GetVersion(void)      { return MAKELONG(0x0A03, 0x0616); }
/* ordinal 132: PMODE | CPU386 | STANDARD | 80x87 (= __WINFLAGS) */
DWORD API GetWinFlags(void)     { return 0x0415; }
/* ordinal 105 */
WORD  API GetExeVersion(void)   { return 0x030A; }

/* ordinal 36 */
HTASK API GetCurrentTask(void)  { task_init(); return g_task; }
/* ordinal 37 */
WORD  API GetCurrentPDB(void)   { return SEL_PSP; }
/* ordinal 320 */
BOOL  API IsTask(HTASK h)       { return h && h == g_task; }
/* ordinal 30 */
BOOL  API WaitEvent(HTASK h)    { (void)h; return FALSE; }
/* ordinal 31 */
BOOL  API PostEvent(HTASK h)    { (void)h; return TRUE; }
/* ordinal 29 */
void  API Yield(void)           { }
/* ordinal 150 */
void  API DirectedYield(HTASK h) { (void)h; }
/* ordinal 155 */
HINSTANCE API GetTaskDS(void)   { return KCBW(KCB_APP_HINST); }
/* ordinal 122 */
BOOL  API IsTaskLocked(void)    { return FALSE; }
/* ordinal 33 */
HTASK API LockCurrentTask(BOOL f) { (void)f; return g_task; }
/* ordinal 34 */
HANDLE API SetTaskQueue(HTASK t, HANDLE q) { (void)t; (void)q; return 0; }
/* ordinal 35 */
HANDLE API GetTaskQueue(HTASK t) { (void)t; return 0; }
/* ordinal 152 */
int   API GetNumTasks(void)     { return 1; }

static WORD g_errmode;
/* ordinal 107 */
WORD  API SetErrorMode(WORD m)  { WORD o = g_errmode; g_errmode = m; return o; }
/* ordinal 199 */
WORD  API SetHandleCount(WORD n) { return n; }
/* ordinal 137 */
void  API FatalAppExit(WORD action, LPCSTR msg)
{
    (void)action;
    dbg_str("KERNEL: FatalAppExit ", msg);
    pm_int21(0x4C01);
}
/* ordinal 1 */
void  API FatalExit(int code)
{
    dbg_puts("KERNEL: FatalExit "); dbg_hex((WORD)code, 4); dbg_puts("\n");
    pm_int21(0x4C01);
}
/* ordinal 2 */
void  API ExitKernel(void)      { pm_int21(0x4C00); }
/* ordinal 203 */
void  API DebugBreak(void)      { dbg_puts("KERNEL: DebugBreak\n"); }
/* ordinal 204 */
void  API SwapRecording(WORD f) { (void)f; }
/* ordinal 138 */
DWORD API GetHeapSpaces(HMODULE h) { (void)h; return MAKELONG(0x8000, 0xFFFF); }
/* ordinal 200 (LibMain - nieuzywane, loader nie wola LibEntry dla KERNEL) */
int   API LibMain(WORD a, WORD b, WORD c, LPCSTR d) { (void)a;(void)b;(void)c;(void)d; return 1; }
