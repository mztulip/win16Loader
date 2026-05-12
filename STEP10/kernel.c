/*
 * kernel.c - KERNEL.EXE (STEP9c+)
 *
 * Eksporty:
 *   ordinal 1:   OutputDebugString  - druk na COM1
 *   ordinal 2:   LibMain            - wymagany przez wlink
 *   ordinal 3:   GlobalFree         - stub (Watcom DLL runtime)
 *   ordinal 4:   LocalInit          - stub (Watcom DLL runtime)
 *   ordinal 113: LocalHeap          - stub (Watcom DLL runtime)
 *
 * Ordinale 3, 4, 113 sa importowane przez Watcom DLL runtime prolog
 * kazdej DLL (w segmencie kodu, przed LibMain). Nie sa wywolywane przez nas,
 * ale loader musi je znalezc zeby nie logowac ERROR.
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
 * LibMain - ordinal 2
 */
int __far __pascal LibMain(unsigned hInstance, unsigned wDataSeg,
                           unsigned cbHeapSize, const char __far *lpszCmdLine)
{
    (void)hInstance; (void)wDataSeg; (void)cbHeapSize; (void)lpszCmdLine;
    return 1;
}

/*
 * Stuby Watcom DLL runtime - ordinals 3, 4, 113
 *
 * Watcom linkuje do kazdej DLL prolog inicjalizacyjny ktory importuje
 * te funkcje z KERNEL. Nigdy nie sa wywolywane przez nasz loader,
 * ale fixup resolver musi znalezc ich offsety zeby nie logowac ERROR.
 *
 * Nazwy odpowiadaja prawdziwym Windows 3.1 KERNEL exports:
 *   ordinal 3:   GlobalFree
 *   ordinal 4:   LocalInit    (inicjalizuje lokalny stertos DLL)
 *   ordinal 113: LocalHeap    (zwraca handle lokalnego stertosu)
 */
unsigned short __far __pascal GlobalFree(unsigned hMem)
{
    (void)hMem;
    return 0;   /* 0 = sukces (NULL = no error) */
}

int __far __pascal LocalInit(unsigned uSegment, unsigned pStart, unsigned pEnd)
{
    (void)uSegment; (void)pStart; (void)pEnd;
    return 1;   /* 1 = sukces */
}

unsigned short __far __pascal LocalHeap(void)
{
    return 0;   /* NULL - brak lokalnego stertosu */
}
