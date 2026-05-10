/*
 * kernel.c - KERNEL.EXE (STEP6)
 *
 * Pierwsza wersja kompilowana przez wlink system windows_dll (nie NASM wrap).
 * Posiada zmienna globalna g_call_count w DGROUP - weryfikacja fixupow INTERNALREF.
 * Prolog Watcom ustawia DS=DGROUP dzieki patchowanemu selektorowi.
 *
 * Kompilacja (Makefile):
 *   wcc -ms -q -zl -s kernel.c -fo=kernel.obj
 *   wlink system windows_dll name kernel.exe file kernel.obj
 *         export OutputDebugString_.1
 *         export LibMain_.2
 *         option nodefaultlibs option quiet
 */

/* Port I/O bez conio.h (dziala z -zl) */
unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];

void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

#define COM1 0x3F8

/* Globalna zmienna w DGROUP - test INTERNALREF fixupu */
static unsigned g_call_count = 0;

/*
 * OutputDebugString - ordinal 1
 * Parametr s: far pointer (Watcom register conv: DX:AX = seg:off)
 * Prolog Watcom DLL: push ds; mov ax, DGROUP_sel; mov ds, ax
 * Dzieki fixupowi INTERNALREF, DGROUP_sel jest prawdziwym selektorem danych.
 */
void __far __pascal OutputDebugString(const char __far *s)
{
    g_call_count++;          /* dostep przez DS=DGROUP (patchowane przez loader) */

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

/*
 * LibMain - ordinal 2 (wymagany przez wlink system windows_dll)
 * Wywolywany przy ladowaniu DLL (my go nie wywolujemy, ale linker go potrzebuje).
 * Zwraca 1 = sukces inicjalizacji.
 */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}
