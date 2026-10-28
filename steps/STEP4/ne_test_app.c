/*
 * ne_test_app.c - STEP4: apka NE z globalnymi danymi
 *
 * Kompilacja (Makefile):
 *   wcc -ms -q -zl -s ne_test_app.c -fo=ne_test_app.obj
 *   wlink system windows name ne_test.exe file ne_test_app.obj
 *         option nodefaultlibs option start=app_entry_ option quiet
 *
 * Generuje prawdziwy NE z dwoma segmentami:
 *   Segment 1 (_TEXT)  - kod  -> ne_cs=1
 *   Segment 2 (DGROUP) - dane -> ne_autodata=2
 *
 * Srodowisko ustawione przez loader przed far call:
 *   CS = SEL_APP_CODE (base = _TEXT phys)
 *   DS = SEL_APP_DATA (base = DGROUP phys)  <- NOWE w STEP4
 *   ES = SEL_VGA      (base = 0xB8000)
 *   SS = SEL_DATA16   (stos loadera)
 */

extern void vga_putw(unsigned int offset, unsigned short chattr);
#pragma aux vga_putw = \
    "mov word ptr es:[bx], ax" \
    parm [bx] [ax] \
    modify [];

/* Zmienne globalne - trafia do DGROUP (segment 2).
 * Dostepne przez DS=SEL_APP_DATA ustawione przez loader. */
static char g_msg[] = "[ STEP4: Hello from globals! ]";
static unsigned char g_attr = 0x1E;  /* zolty na niebieskim */

void __far app_entry(void)
{
    unsigned int pos = (12 * 80 + 12) * 2;  /* wiersz 12, kolumna 12 */
    int i;

    for (i = 0; g_msg[i]; i++) {
        vga_putw(pos + (unsigned int)i * 2,
                 (unsigned short)((unsigned short)g_attr << 8 |
                                  (unsigned char)g_msg[i]));
    }
}
