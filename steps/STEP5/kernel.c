/*
 * kernel.c - KERNEL.EXE stub (STEP5)
 *
 * Eksportuje:
 *   Ordinal 1: OutputDebugString(const char __far *s)
 *              Wysyla napis do COM1 (widoczny w terminalu przy -serial stdio)
 *
 * Kompilacja (Makefile):
 *   wcc -ms -q -zl -s kernel.c -fo=kernel_code.obj
 *   wlink format dos com name kernel_code.com file kernel_code.obj
 *         option nodefaultlibs option start=OutputDebugString_ option quiet
 *
 * Wywolanie (Watcom register convention, far pointer):
 *   AX = offset stringa, DX = segment stringa
 *   Funkcja musi byc pierwsza w pliku (offset 0 w kernel_code.com)
 */

/* Port I/O przez inline asm (bez conio.h, dziala z -zl) */
unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];

void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

#define COM1 0x3F8

/* OutputDebugString - ordinal 1
 * Parametr s: far pointer DX:AX (segment:offset) - Watcom register conv.
 * Brak DGROUP: funkcja uzywa tylko stosu i rejestrow. */
void __far OutputDebugString(const char __far *s)
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
