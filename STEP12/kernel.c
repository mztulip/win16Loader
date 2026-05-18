/*
 * kernel.c - KERNEL.EXE (STEP12)
 *
 * Nowe vs STEP11:
 *   - InitTask: zwraca hInstance z KCB (SEL_KCB=0x98, pole app_hinstance)
 *   - GlobalAlloc: bump allocator z globalnego heapa (KCB.heap_*),
 *     tworzy dynamiczny deskryptor GDT przez SEL_GDT_ACCESS (0x120).
 *     Zwraca selektor GDT jako HGLOBAL (dla GMEM_FIXED: handle = segment).
 *   - LocalAlloc: statyczna tablica 4KB w DGROUP KERNEL (near offsets)
 *
 * Eksporty: 1,3,4,5,6,7,15,16,17,23,24,30,88,90,91,113,115,128,129,137,200
 *
 * Kompilacja: wcc -ms -q -zl -s kernel.c -fo=kernel.obj
 */

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

#define COM1 0x3F8

static void serial_putc(char c)
{
    while (!(io_inb(COM1 + 5) & 0x20));
    if (c == '\n') { io_outb(COM1, '\r'); while (!(io_inb(COM1 + 5) & 0x20)); }
    io_outb(COM1, c);
}

static void serial_puts(const char *s) { while (*s) serial_putc(*s++); }

static void serial_puthex16(unsigned short v)
{
    static char buf[5];  /* static: in DS, not stack (SS != DS in 16-bit PM DLL context) */
    int i;
    for (i = 3; i >= 0; i--) { buf[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
    buf[4] = 0; serial_puts(buf);
}

/* ============================================================
 * KCB - Kernel Control Block (SEL_KCB = 0x98, limit=31)
 *
 * Inicjalizowany przez loader.c przed wejsciem w PM (real mode).
 * Dostep w PM: MK_FP(0x98, 0) = wskaznik do KCB.
 *
 * SEL_GDT_ACCESS = 0x120: 16-bit data z bazą = adres fizyczny GDT.
 *   MK_FP(0x120, sel) = bajt 'sel' w GDT = 8-bajtowy deskryptor dla 'sel'.
 *   Pozwala kernel.c (w 16-bit PM) pisac nowe wpisy GDT bez powrotu do RM.
 * ============================================================ */
#define SEL_KCB        ((unsigned short)0x98)
#define SEL_GDT_ACCESS ((unsigned short)0x120)
#define GDYN_MAX_SEL   ((unsigned short)0x528)  /* 0x128 + 128*8 */
#define SEL_PSP        ((unsigned short)0x38)   /* fake PSP (256 B zeroed) */

/* Set ES to given selector - used by InitTask to return ES=PSP (Win16 ABI) */
static void set_es(unsigned sel);
#pragma aux set_es = "mov es, ax" parm [ax] modify [es];

#pragma pack(push, 1)
typedef struct {
    unsigned short app_hinstance;
    unsigned short next_dyn_sel;
    unsigned long  heap_phys;
    unsigned long  heap_next;
    unsigned long  heap_end;
    unsigned short local_heap_off;   /* near offset in app data seg where local heap starts */
} KCB;
#pragma pack(pop)

static void write_gdt_desc(unsigned short sel, unsigned long base, unsigned short limit)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(SEL_GDT_ACCESS, sel);
    p[0] = (unsigned char)(limit & 0xFF);
    p[1] = (unsigned char)(limit >> 8);
    p[2] = (unsigned char)(base & 0xFF);
    p[3] = (unsigned char)((base >> 8) & 0xFF);
    p[4] = (unsigned char)((base >> 16) & 0xFF);
    p[5] = 0x92;   /* P=1 DPL=0 S=1 type=010 data-rw */
    p[6] = 0x00;   /* G=0 16-bit */
    p[7] = (unsigned char)((base >> 24) & 0xFF);
}

/* ============================================================
 * ordinal 115: OutputDebugString
 * ============================================================ */
void __far __pascal OutputDebugString(const char __far *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s;
        while (!(io_inb(COM1 + 5) & 0x20));
        if (c == '\n') { io_outb(COM1, '\r'); while (!(io_inb(COM1 + 5) & 0x20)); }
        io_outb(COM1, c);
        s++;
    }
}

/* ============================================================
 * ordinal 91: InitTask - implemented in libstubs.asm (INITTASK).
 *
 * Win16 Watcom C startup (C0W.OBJ) saves all registers after InitTask and
 * uses them to build WinMain's argument list:
 *   DI -> hInstance   (1st WinMain param, deepest on startup push stack)
 *   SI -> hPrevInst   (2nd param, [bp+6] in 0x3D35; must be 0 for RegisterClass)
 *   ES -> lpCmdLine segment (PSP selector)
 *   BX -> lpCmdLine offset  (0 = no command line)
 *   DX -> nCmdShow          (0 = SW_HIDE; SKI doesn't use it)
 *   AX -> non-zero check    (hInst)
 *
 * A pure-C InitTask causes Watcom to wrap with push/pop SI/DI, overwriting
 * the values we set.  The asm stub avoids this.
 *
 * InitTask_dbg: no-arg helper called from asm stub for serial debug output.
 * Uses Watcom register calling convention (no parameters -> just call it).
 * ============================================================ */
void InitTask_dbg(void)
{
    KCB __far *kcb = (KCB __far *)MK_FP(SEL_KCB, 0);
    serial_puts("KERNEL: InitTask hInst=0x");
    serial_puthex16(kcb->app_hinstance);
    serial_puts(" hPrevInst=0x0000\n");
}

/* ============================================================
 * ordinal 15: GlobalAlloc
 *
 * Bump allocator z KCB.heap_* (256KB, zainicjalizowany przez loader.c).
 * Dla kazdej alokacji tworzy nowy selektor GDT (przez SEL_GDT_ACCESS).
 * Zwraca selektor jako HGLOBAL (Win16 GMEM_FIXED: handle = segment).
 * Apka: void far *p = MAKELP(hGlobal, 0) lub MK_FP(hGlobal, 0).
 * ============================================================ */
unsigned __far __pascal GlobalAlloc(unsigned wFlags, unsigned long dwBytes)
{
    KCB __far *kcb = (KCB __far *)MK_FP(SEL_KCB, 0);
    unsigned long base, rounded;
    unsigned short limit, sel;

    if (dwBytes == 0) dwBytes = 16;
    rounded = (dwBytes + 15UL) & ~15UL;

    if (kcb->heap_next + rounded > kcb->heap_end) {
        serial_puts("KERNEL: GlobalAlloc HEAP FULL\n"); return 0;
    }
    if (kcb->next_dyn_sel >= GDYN_MAX_SEL) {
        serial_puts("KERNEL: GlobalAlloc GDT FULL\n"); return 0;
    }

    base = kcb->heap_next;
    kcb->heap_next += rounded;
    limit = (rounded >= 65536UL) ? 0xFFFFu : (unsigned short)(rounded - 1);
    sel = kcb->next_dyn_sel;
    kcb->next_dyn_sel += 8;

    write_gdt_desc(sel, base, limit);

    serial_puts("KERNEL: GlobalAlloc sel=0x");
    serial_puthex16(sel);
    serial_puts(" sz=0x");
    serial_puthex16((unsigned short)(dwBytes & 0xFFFF));
    serial_putc('\n');

    if (wFlags & 0x40) {   /* GMEM_ZEROINIT */
        unsigned char __far *p = (unsigned char __far *)MK_FP(sel, 0);
        unsigned short i;
        for (i = 0; i <= limit; i++) p[i] = 0;
    }
    return sel;
}

unsigned __far __pascal GlobalReAlloc(unsigned hMem, unsigned long dwBytes, unsigned wFlags)
{
    (void)dwBytes; (void)wFlags;
    return hMem;
}

unsigned __far __pascal GlobalFree(unsigned hMem)
{
    (void)hMem; return 0;
}

/* ============================================================
 * ordinal 5/6/7: LocalAlloc / LocalReAlloc / LocalFree
 *
 * Statyczny lokalny heap 4KB w DGROUP KERNEL.
 * Zwraca near offset tablicy g_local_heap (w DS=KERNEL_DGROUP).
 * ============================================================ */
/* Local heap is in the app's own data segment (SEL_APP_DATA = app_hinstance).
 * Returns a near offset within that segment so the app can use it as [DS:offset]. */
static unsigned g_local_next = 0;

unsigned __far __pascal LocalAlloc(unsigned wFlags, unsigned wBytes)
{
    KCB __far *kcb = (KCB __far *)MK_FP(SEL_KCB, 0);
    unsigned off;
    if (g_local_next == 0)
        g_local_next = kcb->local_heap_off;  /* init to start of heap area */
    if (wBytes == 0) wBytes = 2;
    wBytes = (wBytes + 1) & ~1u;
    if (g_local_next + wBytes > 0xC000u) {   /* keep below 48KB (leave room for stack) */
        serial_puts("KERNEL: LocalAlloc FULL\n"); return 0;
    }
    off = g_local_next;
    g_local_next += wBytes;
    if (wFlags & 0x40) {
        unsigned char __far *p = (unsigned char __far *)MK_FP(kcb->app_hinstance, off);
        unsigned i;
        for (i = 0; i < wBytes; i++) p[i] = 0;
    }
    return off;  /* near offset in app's DGROUP */
}

unsigned __far __pascal LocalReAlloc(unsigned hMem, unsigned wBytes, unsigned wFlags)
{
    (void)wBytes; (void)wFlags; return hMem;
}

unsigned __far __pascal LocalFree(unsigned hMem)
{
    (void)hMem; return 0;
}

/* ============================================================
 * ordinal 23/24: LockSegment / UnlockSegment
 * ============================================================ */
unsigned __far __pascal LockSegment(unsigned wSeg)   { return wSeg; }
unsigned __far __pascal UnlockSegment(unsigned wSeg) { (void)wSeg; return 1; }

/* ============================================================
 * ordinal 30: WaitEvent
 * ============================================================ */
unsigned __far __pascal WaitEvent(unsigned hTask) { (void)hTask; return 0; }

/* ============================================================
 * ordinal 88/90: lstrCpy / lstrLen
 * ============================================================ */
char __far * __far __pascal LstrCpy(char __far *dst, const char __far *src)
{
    char __far *d = dst;
    while ((*dst++ = *src++) != 0);
    return d;
}

int __far __pascal LstrLen(const char __far *s)
{
    int n = 0; while (*s++) n++; return n;
}

/* ============================================================
 * ordinal 128: GetPrivateProfileString
 * ============================================================ */
int __far __pascal GetPrivateProfileString(
    const char __far *lpszSection, const char __far *lpszEntry,
    const char __far *lpszDefault, char __far *lpszReturnBuffer,
    int cbReturnBuffer, const char __far *lpszFilename)
{
    int n = 0;
    (void)lpszSection; (void)lpszEntry; (void)lpszFilename;
    while (n < cbReturnBuffer - 1 && lpszDefault[n]) {
        lpszReturnBuffer[n] = lpszDefault[n]; n++;
    }
    lpszReturnBuffer[n] = 0;
    return n;
}

/* ============================================================
 * ordinal 129: WritePrivateProfileString
 * ============================================================ */
unsigned __far __pascal WritePrivateProfileString(
    const char __far *lpszSection, const char __far *lpszEntry,
    const char __far *lpszString, const char __far *lpszFilename)
{
    (void)lpszSection; (void)lpszEntry; (void)lpszString; (void)lpszFilename;
    return 1;
}

/* ============================================================
 * ordinal 1: FatalExit / 137: FatalAppExit
 * ============================================================ */
void __far __pascal FatalExit(int nCode)
{
    (void)nCode; serial_puts("KERNEL: FatalExit\n"); for (;;);
}

void __far __pascal FatalAppExit(unsigned wAction, const char __far *lpszMsg)
{
    (void)wAction;
    serial_puts("KERNEL: FatalAppExit: ");
    while (*lpszMsg) serial_putc((char)*lpszMsg++);
    serial_putc('\n');
    for (;;);
}

/* ============================================================
 * ordinal 3, 4, 113: stubs Watcom DLL prolog
 * ============================================================ */
unsigned __far __pascal KernelStub3(unsigned a)            { (void)a; return 0; }
unsigned __far __pascal KernelStub4(unsigned a, unsigned b, unsigned c)
                                                           { (void)a;(void)b;(void)c; return 1; }
unsigned __far __pascal KernelStub113(void)                { return 0; }

/* ============================================================
 * ordinal 200: LibMain
 * ============================================================ */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}
