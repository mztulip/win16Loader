/*
 * kernel.c - KERNEL.EXE (STEP11)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1:
 *   1   = FatalExit
 *   3   = stub (Watcom DLL prolog import)
 *   4   = stub (Watcom DLL prolog import)
 *   5   = LocalAlloc
 *   6   = LocalReAlloc
 *   7   = LocalFree
 *   15  = GlobalAlloc
 *   16  = GlobalReAlloc
 *   17  = GlobalFree
 *   23  = LockSegment
 *   24  = UnlockSegment
 *   30  = WaitEvent
 *   88  = lstrCpy
 *   90  = lstrLen
 *   91  = InitTask  (KRYTYCZNE - inicjalizacja tasku Win16)
 *   113 = stub (Watcom DLL prolog import)
 *   115 = OutputDebugString
 *   128 = GetPrivateProfileString
 *   129 = WritePrivateProfileString
 *   137 = FatalAppExit
 *   200 = LibMain
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s kernel.c -fo=kernel.obj
 */

/* Port I/O bez conio.h */
unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

#define COM1 0x3F8

static void serial_putc(char c)
{
    while (!(io_inb(COM1 + 5) & 0x20));
    if (c == '\n') {
        io_outb(COM1, '\r');
        while (!(io_inb(COM1 + 5) & 0x20));
    }
    io_outb(COM1, c);
}

static void serial_puts(const char *s)
{
    while (*s) serial_putc(*s++);
}

/* ============================================================
 * ordinal 115: OutputDebugString - druk na COM1
 * ============================================================ */
void __far __pascal OutputDebugString(const char __far *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s;
        while (!(io_inb(COM1 + 5) & 0x20));
        if (c == '\n') {
            io_outb(COM1, '\r');
            while (!(io_inb(COM1 + 5) & 0x20));
        }
        io_outb(COM1, c);
        s++;
    }
}

/* ============================================================
 * ordinal 91: InitTask - inicjalizacja tasku Win16
 *
 * Wywolywany przez startup code apki (przed WinMain).
 * Zwraca w AX: hInstance (= DS aplikacji, bo int3f_handler
 * zapisuje DS callera; tutaj uproszczenie - zwracamy 0).
 * Docelowo ETAP 12: czytac g_current_app_ds z pamieci wspolnej.
 * ============================================================ */
unsigned short __far __pascal InitTask(void)
{
    serial_puts("KERNEL: InitTask\n");
    return 1;   /* hInstance placeholder; ETAP 12: wlasciwy DS apki */
}

/* ============================================================
 * ordinal 5: LocalAlloc
 * ordinal 6: LocalReAlloc
 * ordinal 7: LocalFree
 * Lokalny heap wewnatrz DGROUP apki. Stub dla ETAP 11.
 * ============================================================ */
unsigned __far __pascal LocalAlloc(unsigned wFlags, unsigned wBytes)
{
    (void)wFlags; (void)wBytes;
    return 0;   /* NULL - brak lokalnego heapa (ETAP 12) */
}

unsigned __far __pascal LocalReAlloc(unsigned hMem, unsigned wBytes, unsigned wFlags)
{
    (void)hMem; (void)wBytes; (void)wFlags;
    return 0;
}

unsigned __far __pascal LocalFree(unsigned hMem)
{
    (void)hMem;
    return 0;   /* 0 = sukces */
}

/* ============================================================
 * ordinal 15: GlobalAlloc
 * ordinal 16: GlobalReAlloc
 * ordinal 17: GlobalFree
 * Globalny heap. Stub dla ETAP 11.
 * ============================================================ */
unsigned __far __pascal GlobalAlloc(unsigned wFlags, unsigned long dwBytes)
{
    (void)wFlags; (void)dwBytes;
    return 0;   /* NULL - ETAP 12 */
}

unsigned __far __pascal GlobalReAlloc(unsigned hMem, unsigned long dwBytes, unsigned wFlags)
{
    (void)hMem; (void)dwBytes; (void)wFlags;
    return 0;
}

unsigned __far __pascal GlobalFree(unsigned hMem)
{
    (void)hMem;
    return 0;
}

/* ============================================================
 * ordinal 23: LockSegment
 * ordinal 24: UnlockSegment
 * ============================================================ */
unsigned __far __pascal LockSegment(unsigned wSeg)
{
    return wSeg;    /* zwroc ten sam segment */
}

unsigned __far __pascal UnlockSegment(unsigned wSeg)
{
    (void)wSeg;
    return 1;
}

/* ============================================================
 * ordinal 30: WaitEvent
 * ============================================================ */
unsigned __far __pascal WaitEvent(unsigned hTask)
{
    (void)hTask;
    return 0;
}

/* ============================================================
 * ordinal 88: lstrCpy - far string copy
 * ordinal 90: lstrLen - far string length
 * ============================================================ */
char __far * __far __pascal LstrCpy(char __far *dst, const char __far *src)
{
    char __far *d = dst;
    while ((*dst++ = *src++) != 0);
    return d;
}

int __far __pascal LstrLen(const char __far *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* ============================================================
 * ordinal 128: GetPrivateProfileString
 * Zwraca domyslna wartosc (brak obslugi plikow INI w ETAP 11)
 * ============================================================ */
int __far __pascal GetPrivateProfileString(
    const char __far *lpszSection,
    const char __far *lpszEntry,
    const char __far *lpszDefault,
    char __far *lpszReturnBuffer,
    int cbReturnBuffer,
    const char __far *lpszFilename)
{
    int n = 0;
    (void)lpszSection; (void)lpszEntry; (void)lpszFilename;
    /* Kopiuj wartosc domyslna do bufora */
    while (n < cbReturnBuffer - 1 && lpszDefault[n]) {
        lpszReturnBuffer[n] = lpszDefault[n];
        n++;
    }
    lpszReturnBuffer[n] = 0;
    return n;
}

/* ============================================================
 * ordinal 129: WritePrivateProfileString - stub
 * ============================================================ */
unsigned __far __pascal WritePrivateProfileString(
    const char __far *lpszSection,
    const char __far *lpszEntry,
    const char __far *lpszString,
    const char __far *lpszFilename)
{
    (void)lpszSection; (void)lpszEntry; (void)lpszString; (void)lpszFilename;
    return 1;
}

/* ============================================================
 * ordinal 1:   FatalExit
 * ordinal 137: FatalAppExit
 * ============================================================ */
void __far __pascal FatalExit(int nCode)
{
    (void)nCode;
    serial_puts("KERNEL: FatalExit\n");
    /* W prawdziwym Windows: crash dialog. U nas: zawisnij. */
    for (;;);
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
 * ordinal 3, 4, 113: stubs dla Watcom DLL prolog
 * Watcom system windows_dll generuje fixupy do KERNEL.3/4/113
 * w kazdej DLL (nie sa wywolywane, tylko musza istniec).
 * ============================================================ */
unsigned __far __pascal KernelStub3(unsigned a)
{
    (void)a;
    return 0;
}

unsigned __far __pascal KernelStub4(unsigned a, unsigned b, unsigned c)
{
    (void)a; (void)b; (void)c;
    return 1;
}

unsigned __far __pascal KernelStub113(void)
{
    return 0;
}

/* ============================================================
 * ordinal 200: LibMain - wymagany przez wlink system windows_dll
 * ============================================================ */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}
