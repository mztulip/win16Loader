/*
 * serial.c - COM1 debug output dla STEP4 (Open Watcom, large model)
 *
 * Uzywamy outp/inp z <conio.h> (Watcom intrinsics, bez inline asm).
 * 115200 baud, 8N1.
 */
#include <conio.h>
#include "serial.h"

#define COM1 0x3F8

void serial_init(void)
{
    outp(COM1 + 1, 0x00);  /* wylacz przerwania         */
    outp(COM1 + 3, 0x80);  /* DLAB=1 - ustawianie baudu */
    outp(COM1 + 0, 0x01);  /* 115200 baud (divisor=1)   */
    outp(COM1 + 1, 0x00);
    outp(COM1 + 3, 0x03);  /* 8 bitow, brak parzystosci, 1 stop */
    outp(COM1 + 2, 0xC7);  /* FIFO wlaczone             */
    outp(COM1 + 4, 0x03);  /* DTR + RTS                 */
}

void serial_putchar(char c)
{
    while (!(inp(COM1 + 5) & 0x20));  /* czekaj na wolny bufor TX */
    if (c == '\n') serial_putchar('\r');
    outp(COM1, (unsigned char)c);
}

void serial_puts(const char __far *s)
{
    while (*s) serial_putchar(*s++);
}
