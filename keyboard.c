/*
 * keyboard.c - IRQ1 (klawiatura PS/2, scancode set 1 po translacji 8042)
 *
 * irq1_c: czyta scancode z portu 0x60 i wpisuje go do kolejki w KCB:
 *   wpis WORD: mlodszy bajt = scancode (bit 7 = zwolnienie klawisza),
 *              starszy bajt  = 1 jesli poprzedzony prefiksem 0xE0 (klawisz rozszerzony).
 * Translacje na kody VK, znaki, stan Shift/Ctrl/Alt robi USER.EXE.
 *
 * Wywolywana z pm_call.asm (irq1_handler) z dowolnym DS - nie uzywa DGROUP.
 * Kompilacja: wcc -ms -q -zl -s keyboard.c -fo=keyboard.obj
 */

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];

#define SEL_KCB      ((unsigned short)0x98)
#define KCB_KB_EXT   291    /* BYTE: 1 = poprzedni bajt byl prefiksem 0xE0 */
#define KCB_KQ_HEAD  1024   /* BYTE: indeks odczytu */
#define KCB_KQ_TAIL  1025   /* BYTE: indeks zapisu */
#define KCB_KQ_BUF   1026   /* WORD[64] */
#define KCB_KQ_N     64

/* Nazwa bez trailing '_': Watcom near func 'irq1_c' -> symbol 'irq1_c_' */
void irq1_c(void)
{
    unsigned char __far *kcb = (unsigned char __far *)MK_FP(SEL_KCB, 0);
    unsigned char sc, t, nt;

    /* bit 5 statusu (AUXDATA): bajt w 0x60 pochodzi od myszy, nie klawiatury */
    if (io_inb(0x64) & 0x20) return;
    sc = io_inb(0x60);

    if (sc == 0xE0) { kcb[KCB_KB_EXT] = 1; return; }
    if (sc == 0xE1) return;                       /* Pause - pomijamy */

    t = kcb[KCB_KQ_TAIL];
    nt = (unsigned char)((t + 1) % KCB_KQ_N);
    if (nt != kcb[KCB_KQ_HEAD]) {
        unsigned short __far *e = (unsigned short __far *)MK_FP(SEL_KCB, KCB_KQ_BUF + t * 2);
        *e = (unsigned short)sc | ((unsigned short)kcb[KCB_KB_EXT] << 8);
        kcb[KCB_KQ_TAIL] = nt;
    }
    kcb[KCB_KB_EXT] = 0;
}
