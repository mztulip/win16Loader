/*
 * mouse_msg.c - STEP17: generowanie WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP
 *
 * Linkowany razem z user.obj do USER.EXE.
 * Kompilacja: wcc -ms -zu -q -zl -s mouse_msg.c -fo=mouse_msg.obj
 *
 * mouse_poll: sprawdza KCB mouse_changed, wypelnia MSG strukturę.
 * Wywolywana z PeekMessage/GetMessage w user.c.
 *
 * KCB mouse fields (SEL_KCB=0x98), ustawiane przez mouse.c (IRQ12 handler):
 *   [285-286] WORD  mouse_x     (abs X, 0..639)
 *   [287-288] WORD  mouse_y     (abs Y, 0..479)
 *   [289] BYTE      mouse_btn   (bit 0 = LButton)
 *   [290] BYTE      mouse_changed (1 = nowe zdarzenie)
 */

typedef unsigned int   UINT;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned long  LPARAM;
typedef unsigned int   WPARAM;
typedef unsigned int   HWND;
typedef unsigned int   BOOL;

#pragma pack(push,1)
typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    WORD   ptx, pty;
} MSG;
#pragma pack(pop)

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

#define SEL_KCB            ((unsigned short)0x98)
#define KCB_MK_FP(off)     ((unsigned char __far *)MK_FP(SEL_KCB, (unsigned short)(off)))

#define KCB_MOUSE_X        285
#define KCB_MOUSE_Y        287
#define KCB_MOUSE_BTN      289
#define KCB_MOUSE_CHANGED  290

#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define MK_LBUTTON      0x0001

/* Stan przycisku z poprzedniego wywolania (static = w DGROUP USER.EXE, -zu zapewnia
 * poprawny dostep przez SS przy wywolaniach z roznych kontekstow). */
static unsigned char s_prev_btn = 0;

/* Sprawdza KCB: jesli mouse_changed, wypelnia *pmsg i zwraca 1; inaczej 0.
 * hwnd: HWND okna docelowego (g_kb_hwnd z user.c). */
int __far mouse_poll(MSG __far *pmsg, HWND hwnd)
{
    unsigned char __far *kcb = KCB_MK_FP(0);
    unsigned char changed;
    unsigned char btn;
    unsigned short mx, my;

    changed = kcb[KCB_MOUSE_CHANGED];
    if (!changed) return 0;

    kcb[KCB_MOUSE_CHANGED] = 0;
    btn = kcb[KCB_MOUSE_BTN];
    mx  = *(unsigned short __far *)KCB_MK_FP(KCB_MOUSE_X);
    my  = *(unsigned short __far *)KCB_MK_FP(KCB_MOUSE_Y);

    pmsg->hwnd   = hwnd;
    pmsg->lParam = (LPARAM)((unsigned long)my << 16 | (unsigned long)mx);
    pmsg->time   = 0;
    pmsg->ptx    = mx;
    pmsg->pty    = my;

    if (btn && !s_prev_btn) {
        pmsg->message = WM_LBUTTONDOWN;
        pmsg->wParam  = MK_LBUTTON;
        s_prev_btn = 1;
    } else if (!btn && s_prev_btn) {
        pmsg->message = WM_LBUTTONUP;
        pmsg->wParam  = 0;
        s_prev_btn = 0;
    } else {
        pmsg->message = WM_MOUSEMOVE;
        pmsg->wParam  = btn ? MK_LBUTTON : 0;
    }
    return 1;
}
