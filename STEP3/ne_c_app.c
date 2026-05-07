/* ne_c_app.c - aplikacja Win16 NE w jezyku C, dla loadera STEP3
 *
 * Budowanie (Makefile: make ne_c_app.exe):
 *   wcc -ms -q -zl -s ne_c_app.c -fo=ne_c_app_code.obj
 *   wlink format dos com name ne_c_code.com file ne_c_app_code.obj
 *         option nodefaultlibs option start=app_entry_ option quiet
 *   nasm -f bin ne_c_wrap.asm -o ne_c_app.exe
 *
 * Flagi:
 *   -ms   maly model (near dane, far return dla __far)
 *   -zl   bez domyslnych bibliotek (brak CRT0)
 *   -s    bez runtime stack checking (brak __STK)
 *
 * Srodowisko (ustawione przez loader przed far call do offset 0):
 *   CS = SEL_APP_CODE (base = app_phys)
 *   ES = SEL_VGA      (0x38, base = 0xB8000, limit = 4000 B)
 *   SS = SEL_DATA16, SP = 0xFFF0
 *   DS = SEL_DATA16   (segment loadera - nie uzywamy)
 *
 * UWAGA: Nie uzywamy string literals (trafiaja do segmentu CONST,
 * ktory wlink COM format adresuje niepoprawnie). Zamiast tego budujemy
 * napis przez indywidualne przypisania do lokalnej tablicy na stosie -
 * Watcom generuje wtedy immediate stores przez SS (bez dostepu do DS).
 */

/* Zapis slowa char+attr do ES:offset przez inline asm.
 * ES = SEL_VGA ustawiony przez loader - nie dotykamy DS. */
extern void vga_putw(unsigned int offset, unsigned short chattr);
#pragma aux vga_putw = \
    "mov word ptr es:[bx], ax" \
    parm [bx] [ax] \
    modify [];

/* Glowna funkcja aplikacji.
 * Wywolanie: far call SEL_APP_CODE:0
 * Powrot:    retf (Watcom generuje automatycznie dla __far) */
void __far app_entry(void)
{
    char msg[22];
    unsigned int pos = (14 * 80 + 14) * 2;  /* wiersz 14, kolumna 14 */
    int i;

    /* Budujemy napis przez pojedyncze przypisania.
     * Watcom generuje mov byte [bp-N], imm - brak referencji do CONST/DGROUP. */
    msg[ 0]='['; msg[ 1]=' ';
    msg[ 2]='H'; msg[ 3]='e'; msg[ 4]='l'; msg[ 5]='l'; msg[ 6]='o';
    msg[ 7]=' '; msg[ 8]='f'; msg[ 9]='r'; msg[10]='o'; msg[11]='m';
    msg[12]=' '; msg[13]='C'; msg[14]='!'; msg[15]=' ';
    msg[16]='('; msg[17]='N'; msg[18]='E'; msg[19]=')';
    msg[20]=' '; msg[21]='\0';

    for (i = 0; msg[i]; i++) {
        /* 0x5E = zolty na purpurowym */
        vga_putw(pos + (unsigned int)i * 2,
                 (unsigned short)(0x5E00u | (unsigned char)msg[i]));
    }
}
