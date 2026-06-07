/*
 * keyboard.c - STEP16: IRQ1 handler (czesc C)
 *
 * irq1_c_: czyta scan code z portu 0x60, przelicza na Virtual Key,
 * zapisuje do cyklicznego bufora w KCB.
 *
 * Wywolywana z pm_call.asm (irq1_handler) z dowolnym DS.
 * Nie uzywa zmiennych w DGROUP - tylko far pointery i zmienne lokalne.
 *
 * KCB keyboard buffer (SEL_KCB=0x98):
 *   [272] BYTE kb_head  - indeks odczytu
 *   [273] BYTE kb_tail  - indeks zapisu
 *   [274..281] BYTE[8] kb_buf - cykliczny bufor kodow VK
 *
 * Kompilacja: wcc -ms -q -zl -s keyboard.c -fo=keyboard.obj
 */

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];

#define SEL_KCB      ((unsigned short)0x98)
#define KCB_KB_HEAD  272   /* BYTE: indeks odczytu */
#define KCB_KB_TAIL  273   /* BYTE: indeks zapisu */
#define KCB_KB_BUF   274   /* BYTE[8]: cykliczny bufor VK */
#define KCB_KB_SZ    8

/* Wywolywana z irq1_handler w pm_call.asm (AX, BX, CX, DX, ES zapisane przez caller).
 * Nazwa bez trailing '_': Watcom near func 'irq1_c' -> symbol 'irq1_c_' -> extern w NASM */
void irq1_c(void)
{
    unsigned char scancode;
    unsigned char vk;
    unsigned char __far *kcb;
    unsigned char head, tail, next_tail;

    /* bit 5 statusu (AUXDATA): bajt w 0x60 pochodzi od myszy, nie klawiatury */
    if (io_inb(0x64) & 0x20) return;

    scancode = io_inb(0x60);
    if (scancode & 0x80) return;   /* key release - ignoruj */

    /* scan code -> Virtual Key (tylko klawisze uzywane przez SKI.EXE i aplikacje Win16) */
    if      (scancode == 0x01) vk = 0x1B;   /* Esc      -> VK_ESCAPE */
    else if (scancode == 0x48) vk = 0x26;   /* Up       -> VK_UP */
    else if (scancode == 0x50) vk = 0x28;   /* Down     -> VK_DOWN */
    else if (scancode == 0x4B) vk = 0x25;   /* Left     -> VK_LEFT */
    else if (scancode == 0x4D) vk = 0x27;   /* Right    -> VK_RIGHT */
    else if (scancode == 0x3B) vk = 0x70;   /* F1       -> VK_F1 */
    else if (scancode == 0x3C) vk = 0x71;   /* F2       -> VK_F2 */
    else if (scancode == 0x3D) vk = 0x72;   /* F3       -> VK_F3 */
    else if (scancode == 0x3E) vk = 0x73;   /* F4       -> VK_F4 */
    else if (scancode == 0x3F) vk = 0x74;   /* F5       -> VK_F5 */
    else if (scancode == 0x40) vk = 0x75;   /* F6       -> VK_F6 */
    else if (scancode == 0x41) vk = 0x76;   /* F7       -> VK_F7 */
    else if (scancode == 0x42) vk = 0x77;   /* F8       -> VK_F8 */
    else if (scancode == 0x1C) vk = 0x0D;   /* Enter    -> VK_RETURN */
    else if (scancode == 0x39) vk = 0x20;   /* Space    -> VK_SPACE */
    else return;   /* nieznany klawisz */

    kcb = (unsigned char __far *)MK_FP(SEL_KCB, 0);
    head = kcb[KCB_KB_HEAD];
    tail = kcb[KCB_KB_TAIL];
    next_tail = (unsigned char)((tail + 1u) % KCB_KB_SZ);
    if (next_tail != head) {   /* bufor niepelny */
        kcb[KCB_KB_BUF + tail] = vk;
        kcb[KCB_KB_TAIL] = next_tail;
    }
}
