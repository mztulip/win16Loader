/*
 * mouse.c - STEP17: IRQ12 handler (PS/2 mouse, czesc C)
 *
 * mouse_c: czyta bajt z portu 0x60, akumuluje 3-bajtowy pakiet PS/2,
 * oblicza pozycje absolutna (0..639 x 0..479) i zapisuje do KCB.
 *
 * Wywolywana z pm_call.asm (irq12_handler) z dowolnym DS.
 * Nie uzywa zmiennych w DGROUP - tylko far pointery na KCB.
 *
 * KCB mouse fields (SEL_KCB=0x98):
 *   [282] BYTE  mouse_pkt_state  (0/1/2 = ktory bajt pakietu czekamy)
 *   [283] BYTE  mouse_pkt_b0    (bajt statusu, zachowany z byte 0)
 *   [284] BYTE  mouse_pkt_b1    (dx, zachowany z byte 1)
 *   [285-286] WORD  mouse_x     (absoluta X, 0..639)
 *   [287-288] WORD  mouse_y     (absoluta Y, 0..479)
 *   [289] BYTE  mouse_btn       (bit 0 = LButton biezacy stan)
 *   [290] BYTE  mouse_changed   (1 = IRQ12 zapisal nowy stan; user.c zeruje po odczycie)
 *
 * Kompilacja: wcc -ms -q -zl -s mouse.c -fo=mouse.obj
 */

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];

#define SEL_KCB           ((unsigned short)0x98)

#define KCB_MOUSE_STATE   282   /* BYTE: stan pakietu (0/1/2) */
#define KCB_MOUSE_PKT_B0  283   /* BYTE: bajt 0 pakietu (status) */
#define KCB_MOUSE_PKT_B1  284   /* BYTE: bajt 1 pakietu (dx) */
#define KCB_MOUSE_X       285   /* WORD: abs X (0..639) */
#define KCB_MOUSE_Y       287   /* WORD: abs Y (0..479) */
#define KCB_MOUSE_BTN     289   /* BYTE: bity przyciskow (bit 0=LButton) */
#define KCB_MOUSE_CHANGED 290   /* BYTE: flaga: nowe zdarzenie myszy */

#define SCREEN_W  640
#define SCREEN_H  480

/* Wywolywana z irq12_handler w pm_call.asm.
 * Nazwa bez trailing '_': Watcom near func 'mouse_c' -> symbol 'mouse_c_' */
void mouse_c(void)
{
    unsigned char data;
    unsigned char __far *kcb;
    unsigned char state;

    data  = io_inb(0x60);
    kcb   = (unsigned char __far *)MK_FP(SEL_KCB, 0);
    state = kcb[KCB_MOUSE_STATE];

    if (state == 0) {
        /* Bajt 0 (status): bit 3 zawsze 1 (synchronizacja pakietu). */
        if (!(data & 0x08)) return;    /* brak bitu sync -> ignoruj (desync) */
        if (data & 0xC0)    return;    /* overflow X lub Y -> ignoruj pakiet */
        kcb[KCB_MOUSE_PKT_B0] = data;
        kcb[KCB_MOUSE_STATE]  = 1;
    } else if (state == 1) {
        /* Bajt 1: dx */
        kcb[KCB_MOUSE_PKT_B1] = data;
        kcb[KCB_MOUSE_STATE]  = 2;
    } else {
        /* Bajt 2: dy - kompletny pakiet, przetwarzaj */
        unsigned char b0, b1;
        int dx, dy;
        int ax, ay;
        unsigned short ux, uy;
        unsigned short __far *px;
        unsigned short __far *py;

        b0 = kcb[KCB_MOUSE_PKT_B0];
        b1 = kcb[KCB_MOUSE_PKT_B1];

        /* Sign-extend: bit 4 b0 = znak dx, bit 5 b0 = znak dy */
        dx = (int)(unsigned int)b1;
        if (b0 & 0x10) dx |= (int)0xFF00;

        dy = (int)(unsigned int)data;
        if (b0 & 0x20) dy |= (int)0xFF00;
        dy = -dy;    /* PS/2: Y rosnie w gore; ekran: Y rosnie w dol */

        /* Odczyt i aktualizacja pozycji absolutnej */
        px = (unsigned short __far *)MK_FP(SEL_KCB, KCB_MOUSE_X);
        py = (unsigned short __far *)MK_FP(SEL_KCB, KCB_MOUSE_Y);

        ax = (int)*px + dx;
        ay = (int)*py + dy;

        if (ax < 0)          ax = 0;
        if (ax >= SCREEN_W)  ax = SCREEN_W - 1;
        if (ay < 0)          ay = 0;
        if (ay >= SCREEN_H)  ay = SCREEN_H - 1;

        ux = (unsigned short)ax;
        uy = (unsigned short)ay;

        *px = ux;
        *py = uy;

        kcb[KCB_MOUSE_BTN]     = b0 & 0x01;   /* bit 0 = LButton */
        kcb[KCB_MOUSE_CHANGED] = 1;
        kcb[KCB_MOUSE_STATE]   = 0;
    }
}
