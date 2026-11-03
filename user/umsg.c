/* umsg.c - USER: kolejka komunikatow, wejscie (klawiatura, mysz), fokus,
 * aktywacja, przechwytywanie, timery, hooki. */
#include "user.h"

extern LRESULT __far __cdecl call_proc4(WNDPROC p, WORD ds, HWND h, WORD m, WORD w, LONG l);
extern LRESULT __far __cdecl call_hook3(LPVOID p, int code, WORD w, LONG l);
WORD get_cs(void);
#pragma aux get_cs = "mov ax, cs" value [ax];
WORD get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax];
static void cpu_idle(void);
#pragma aux cpu_idle = "sti" "hlt";

/* ============================================================
 * Stan
 * ============================================================ */
#define QSIZE 128
static MSG   g_q[QSIZE];
static WORD  g_qh, g_qt;
static BOOL  g_quit;
static WORD  g_quitcode;
static HWND  g_focus, g_active, g_capture;
static BYTE  g_keys[256];                 /* 0x80 = wcisniety, 0x01 = przelaczony */
static int   g_mx = SCR_W / 2, g_my = SCR_H / 2;
static WORD  g_mbtn;
static DWORD g_lastpos, g_lasttime;
static DWORD g_clicktime;
static int   g_clickx, g_clicky;
static WORD  g_clickmsg;
static HWND  g_clickhwnd;

/* ============================================================
 * Wywolania procedur okien
 * ============================================================ */
/* Zakres selektorow kodu USER (z MODTAB: wpis, ktorego DGROUP = nasz DS) */
static WORD g_ucs_lo, g_ucs_hi;

WORD proc_ds(WNDPROC p, WORD hinst)
{
    WORD s = FP_SEG(p) & ~7;
    if (!g_ucs_lo) {
        BYTE __far *mt = (BYTE __far *)MK_FP(0x530, 0);   /* SEL_MODTAB */
        int i;
        for (i = 0; i < 32 && mt[i * 128]; i++)
            if (*(WORD __far *)(mt + i * 128 + 10) == get_ds()) {
                g_ucs_lo = *(WORD __far *)(mt + i * 128 + 12);
                g_ucs_hi = g_ucs_lo + *(WORD __far *)(mt + i * 128 + 14) * 8;
                break;
            }
        if (!g_ucs_lo) { g_ucs_lo = get_cs() & ~7; g_ucs_hi = g_ucs_lo + 8; }
    }
    if (s >= g_ucs_lo && s < g_ucs_hi) return get_ds();  /* procedura wewnetrzna USER */
    return hinst;
}

static WORD ds_for(WNDPROC p, WND __far *w) { return proc_ds(p, w ? w->hinst : 0); }

LRESULT call_proc(WNDPROC p, WORD ds, HWND h, WORD m, WORD w, LONG l)
{
    return call_proc4(p, ds, h, m, w, l);
}

/* Hooki */
#define MAX_HOOKS 32
static struct { int type; LPVOID proc; WORD hinst; } g_hook[MAX_HOOKS];

static LRESULT run_hooks(int type, int code, WORD wp, LONG lp)
{
    int i;
    for (i = MAX_HOOKS - 1; i >= 0; i--)
        if (g_hook[i].proc && g_hook[i].type == type)
            return call_hook3(g_hook[i].proc, code, wp, lp);
    return 0;
}

void shell_hook(int code, WORD w) { run_hooks(10, code, w, 0); }

LRESULT send_msg(HWND h, WORD m, WORD wp, LONG lp)
{
    WND __far *w = WP(h);
    if (!w || !w->proc) return 0;
    {
        int i;
        for (i = 0; i < MAX_HOOKS; i++) if (g_hook[i].proc && g_hook[i].type == 4) break;
        if (i < MAX_HOOKS) {                              /* WH_CALLWNDPROC */
            struct { LONG l; WORD w, m; HWND h; } cwp;
            cwp.l = lp; cwp.w = wp; cwp.m = m; cwp.h = h;
            run_hooks(4, 0, 0, (LONG)(LPVOID)&cwp);
        }
    }
    return call_proc4(w->proc, ds_for(w->proc, w), h, m, wp, lp);
}

/* ordinal 111 */
LRESULT API SendMessage(HWND h, WORD m, WORD wp, LONG lp)
{
    if (h == 0xFFFF) {                                    /* HWND_BROADCAST */
        HWND c, next;
        for (c = WP(HDESKTOP)->child; c; c = next) { next = WP(c) ? WP(c)->next : 0; send_msg(c, m, wp, lp); }
        return 0;
    }
    return send_msg(h, m, wp, lp);
}

/* ordinal 122 */
LRESULT API CallWindowProc(WNDPROC p, HWND h, WORD m, WORD wp, LONG lp)
{
    if (!p) return 0;
    return call_proc4(p, ds_for(p, WP(h)), h, m, wp, lp);
}

/* ============================================================
 * Kolejka
 * ============================================================ */
BOOL post_msg(HWND h, WORD m, WORD wp, LONG lp)
{
    WORD nt = (g_qt + 1) % QSIZE;
    if (nt == g_qh) { udbg("USER: kolejka pelna\n"); return FALSE; }
    g_q[g_qt].hwnd = h; g_q[g_qt].message = m; g_q[g_qt].wParam = wp; g_q[g_qt].lParam = lp;
    g_q[g_qt].time = tick();
    g_q[g_qt].pt.x = g_mx; g_q[g_qt].pt.y = g_my;
    g_qt = nt;
    return TRUE;
}

/* ordinal 110 */
BOOL API PostMessage(HWND h, WORD m, WORD wp, LONG lp)
{
    if (h == 0xFFFF) {
        HWND c;
        for (c = WP(HDESKTOP)->child; c; c = WP(c)->next) post_msg(c, m, wp, lp);
        return TRUE;
    }
    if (h && !WP(h)) return FALSE;
    return post_msg(h, m, wp, lp);
}

BOOL API PostAppMessage(WORD task, WORD m, WORD wp, LONG lp) { (void)task; return post_msg(0, m, wp, lp); }

/* ordinal 6 */
void API PostQuitMessage(int code) { g_quit = TRUE; g_quitcode = (WORD)code; }

/* ============================================================
 * Klawiatura: scancode set 1 -> VK (uklad US)
 * ============================================================ */
static const BYTE sc2vk[0x59] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0', 0xBD, 0xBB, 0x08, 0x09,
    'Q','W','E','R','T','Y','U','I','O','P', 0xDB, 0xDD, 0x0D, 0x11, 'A','S',
    'D','F','G','H','J','K','L', 0xBA, 0xDE, 0xC0, 0x10, 0xDC, 'Z','X','C','V',
    'B','N','M', 0xBC, 0xBE, 0xBF, 0x10, 0x6A, 0x12, 0x20, 0x14, 0x70, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x67, 0x68, 0x69, 0x6D, 0x64, 0x65, 0x66, 0x6B, 0x61,
    0x62, 0x63, 0x60, 0x6E, 0, 0, 0, 0x7A, 0x7B
};

static BYTE ext_vk(BYTE sc)
{
    switch (sc) {
    case 0x1C: return 0x0D;  case 0x1D: return 0x11;  case 0x35: return 0x6F;
    case 0x38: return 0x12;  case 0x47: return 0x24;  case 0x48: return 0x26;
    case 0x49: return 0x21;  case 0x4B: return 0x25;  case 0x4D: return 0x27;
    case 0x4F: return 0x23;  case 0x50: return 0x28;  case 0x51: return 0x22;
    case 0x52: return 0x2D;  case 0x53: return 0x2E;
    }
    return 0;
}

static HWND key_target(void)
{
    if (g_focus && WP(g_focus)) return g_focus;
    if (g_active && WP(g_active)) return g_active;
    return 0;
}

static void key_event(WORD e)
{
    BYTE sc = (BYTE)(e & 0x7F), ext = (BYTE)(e >> 8), vk;
    BOOL up = (e & 0x80) != 0, prev;
    LONG lp;
    WORD msg;
    HWND t;
    vk = ext ? ext_vk(sc) : (sc < sizeof(sc2vk) ? sc2vk[sc] : 0);
    if (!ext && !(g_keys[0x90] & 1) && sc >= 0x47 && sc <= 0x53) {   /* NumLock wyl.: strzalki */
        BYTE n = ext_vk(sc);
        if (n) vk = n;
    }
    if (!vk) return;
    prev = (g_keys[vk] & 0x80) != 0;
    if (up) g_keys[vk] &= ~0x80;
    else { if (!prev) g_keys[vk] ^= 1; g_keys[vk] |= 0x80; }
    if (vk == 0x10) { if (up) g_keys[0x10] &= ~0x80; }
    lp = 1L | ((LONG)sc << 16) | ((LONG)ext << 24);
    if (g_keys[0x12] & 0x80) lp |= 1L << 29;
    if (prev) lp |= 1L << 30;
    if (up) lp |= 1L << 31;
    if (run_hooks(2, 0, vk, lp)) return;                    /* WH_KEYBOARD */
    t = key_target();
    if ((g_keys[0x12] & 0x80 && !(g_keys[0x11] & 0x80)) || vk == 0x12 || vk == 0x79)
        msg = up ? WM_SYSKEYUP : WM_SYSKEYDOWN;
    else
        msg = up ? WM_KEYUP : WM_KEYDOWN;
    post_msg(t, msg, vk, lp);
}

/* ============================================================
 * Mysz
 * ============================================================ */
static WORD key_flags(void)
{
    WORD f = 0;
    if (g_mbtn & 1) f |= 1;
    if (g_mbtn & 2) f |= 2;
    if (g_keys[0x10] & 0x80) f |= 4;
    if (g_keys[0x11] & 0x80) f |= 8;
    if (g_mbtn & 4) f |= 0x10;
    return f;
}

static void mouse_msg(WORD cmsg, BOOL isdown)
{
    HWND h;
    int hit = HTCLIENT;
    WORD msg;
    WND __far *w;
    if (g_capture && WP(g_capture)) h = g_capture;
    else h = wnd_from_point(g_mx, g_my, (int __far *)&hit);
    w = WP(h);
    if (!w) return;
    if (isdown && !g_capture) {
        HWND top = wnd_toplevel(h);
        if (top != g_active && top != HDESKTOP) {
            LRESULT r = send_msg(h, WM_MOUSEACTIVATE, top, MAKELONG(hit, cmsg));
            if (r != 3 && r != 4) activate_window(top, TRUE);    /* MA_NOACTIVATE(ANDEAT) */
            if (r == 2 || r == 4) return;                        /* MA_ACTIVATEANDEAT */
        }
        if (w->style & WS_DISABLED) { MessageBeep(0); return; }
    }
    if (cmsg == WM_MOUSEMOVE || isdown) send_msg(h, WM_SETCURSOR, h, MAKELONG(hit, cmsg));
    if (!WP(h)) return;
    {   /* WH_MOUSE */
        struct { POINT pt; HWND h; WORD hit; DWORD ex; } mh;
        mh.pt.x = g_mx; mh.pt.y = g_my; mh.h = h; mh.hit = hit; mh.ex = 0;
        if (run_hooks(7, 0, cmsg, (LONG)(LPVOID)&mh)) return;
    }
    msg = cmsg;
    if (isdown) {                                                /* dwuklik */
        DWORD t = tick();
        /* CS_DBLCLKS potrzebne tylko w obszarze klienta (WM_NCxBUTTONDBLCLK zawsze) */
        if (((g_cls[w->cls].style & 0x0008) || (hit != HTCLIENT && !g_capture)) && g_clickmsg == cmsg && g_clickhwnd == h &&
            t - g_clicktime <= GetDoubleClickTime() &&
            g_mx - g_clickx < 4 && g_clickx - g_mx < 4 && g_my - g_clicky < 4 && g_clicky - g_my < 4) {
            msg = cmsg + 2;
            g_clickmsg = 0;
        } else {
            g_clickmsg = cmsg; g_clicktime = t; g_clickx = g_mx; g_clicky = g_my; g_clickhwnd = h;
        }
    }
    if (hit == HTCLIENT || g_capture) {
        int cx = g_mx - w->cr.left, cy = g_my - w->cr.top;
        post_msg(h, msg, key_flags(), MAKELONG(cx, cy));
    } else {
        post_msg(h, msg - WM_MOUSEMOVE + WM_NCMOUSEMOVE, hit, MAKELONG(g_mx, g_my));
    }
}

static void mouse_moved(int x, int y)
{
    if (x == g_mx && y == g_my) return;
    g_mx = x; g_my = y;
    GdiMoveCursor(x, y);
    mouse_msg(WM_MOUSEMOVE, FALSE);
}

static void mouse_buttons(WORD b)
{
    static const WORD dn[3] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN };
    int i;
    for (i = 0; i < 3; i++) {
        WORD bit = 1 << i;
        if ((b & bit) == (g_mbtn & bit)) continue;
        g_mbtn ^= bit;
        mouse_msg((g_mbtn & bit) ? dn[i] : dn[i] + 1, (g_mbtn & bit) != 0);
    }
}

void input_poll(void)
{
    BYTE __far *k = (BYTE __far *)MK_FP(SEL_KCB, 0);
    while (k[1024] != k[1025]) {                              /* klawiatura */
        WORD e = *(WORD __far *)(k + 1026 + k[1024] * 2);
        k[1024] = (BYTE)((k[1024] + 1) % 64);
        key_event(e);
    }
    while (k[1160] != k[1161]) {                              /* przyciski myszy */
        WORD __far *e = (WORD __far *)(k + 1162 + k[1160] * 6);
        int x = e[0], y = e[1];
        WORD b = e[2];
        k[1160] = (BYTE)((k[1160] + 1) % 16);
        mouse_moved(x, y);
        mouse_buttons(b);
    }
    mouse_moved(*(int __far *)(k + 285), *(int __far *)(k + 287));
}

/* ============================================================
 * Fokus, aktywacja, przechwytywanie
 * ============================================================ */
void set_focus_internal(HWND h)
{
    HWND old = g_focus;
    if (h == old) return;
    g_focus = h;
    if (old && WP(old)) send_msg(old, WM_KILLFOCUS, h, 0);
    if (h && WP(h) && g_focus == h) send_msg(h, WM_SETFOCUS, old, 0);
}

HWND API SetFocus(HWND h)
{
    HWND old = g_focus;
    if (h && !WP(h)) return old;
    if (h) {
        HWND top = wnd_toplevel(h);
        if (top != g_active && top != HDESKTOP) activate_window(top, FALSE);
    }
    if (run_hooks(5, 9, h, old)) return old;                 /* HCBT_SETFOCUS */
    set_focus_internal(h);
    return old;
}

HWND API GetFocus(void) { return (g_focus && WP(g_focus)) ? g_focus : 0; }

void activate_window(HWND h, BOOL mouse)
{
    HWND old = g_active;
    WND __far *w = WP(h);
    if (!w || h == HDESKTOP) return;
    if (w->style & WS_CHILD) h = wnd_toplevel(h);
    if (h == old) return;
    if (run_hooks(5, 5, h, 0)) return;                       /* HCBT_ACTIVATE */
    g_active = h;
    if (old && WP(old)) {
        WP(old)->flags &= ~WF_ACTIVE;
        send_msg(old, WM_NCACTIVATE, FALSE, 0);
        send_msg(old, WM_ACTIVATE, 0, MAKELONG(h, IsIconic(old)));
    }
    if (!WP(h)) return;
    SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010);    /* na gore */
    if (!old) send_msg(h, WM_ACTIVATEAPP, TRUE, 0);
    WP(h)->flags |= WF_ACTIVE;
    send_msg(h, WM_NCACTIVATE, TRUE, 0);
    send_msg(h, WM_ACTIVATE, mouse ? 2 : 1, MAKELONG(old, IsIconic(h)));
    shell_hook(4, h);                                         /* HSHELL_ACTIVATESHELLWINDOW */
}

HWND API SetActiveWindow(HWND h)
{
    HWND old = g_active;
    activate_window(h, FALSE);
    return old;
}

HWND API GetActiveWindow(void) { return (g_active && WP(g_active)) ? g_active : 0; }

HWND API SetCapture(HWND h) { HWND o = g_capture; g_capture = h; return o; }
void API ReleaseCapture(void) { g_capture = 0; }
HWND API GetCapture(void) { return (g_capture && WP(g_capture)) ? g_capture : 0; }

static HWND next_active(HWND except)
{
    HWND c;
    for (c = WP(HDESKTOP)->child; c; c = WP(c)->next) {
        WND __far *w = WP(c);
        if (c != except && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED) &&
            !(w->flags & (WF_DESTROYING | WF_MINIMIZED | WF_ICONTITLE)))
            return c;
    }
    return 0;
}

/* Minimalizacja: fokus wychodzi z okna; SW_MINIMIZE aktywuje nastepne okno (o ile jest) */
void wnd_minimized(HWND h, BOOL next)
{
    if (g_focus && (g_focus == h || IsChild(h, g_focus))) set_focus_internal(0);
    if (next && g_active == h) {
        HWND n = next_active(h);
        if (n) activate_window(n, FALSE);
    }
}

void wnd_hidden(HWND h)
{
    if (g_focus && (g_focus == h || IsChild(h, g_focus))) set_focus_internal(0);
    if (g_active == h) {
        HWND n = next_active(h);
        g_active = 0;
        if (n) activate_window(n, FALSE);
    }
}

extern void timers_kill_wnd(HWND h);

void wnd_destroying(HWND h)
{
    WORD i;
    if (g_focus == h) g_focus = 0;
    if (g_capture == h) g_capture = 0;
    if (g_active == h) {
        HWND n = next_active(h);
        g_active = 0;
        if (n) activate_window(n, FALSE);
    }
    timers_kill_wnd(h);
    for (i = g_qh; i != g_qt; i = (i + 1) % QSIZE) if (g_q[i].hwnd == h) g_q[i].message = WM_NULL;
}

/* ============================================================
 * Stan klawiszy
 * ============================================================ */
int API GetKeyState(int vk)
{
    BYTE s = g_keys[vk & 0xFF];
    if (vk == 1) s = (BYTE)((g_mbtn & 1) ? 0x80 : 0);
    if (vk == 2) s = (BYTE)((g_mbtn & 2) ? 0x80 : 0);
    if (vk == 4) s = (BYTE)((g_mbtn & 4) ? 0x80 : 0);
    return (s & 0x80) ? (int)(0xFF80 | (s & 1)) : (s & 1);
}

int  API GetAsyncKeyState(int vk) { input_poll(); return (GetKeyState(vk) & 0x8000) ? (int)0x8000 : 0; }
void API GetKeyboardState(BYTE __far *b) { umemcpy(b, (LPVOID)g_keys, 256); }
void API SetKeyboardState(BYTE __far *b) { umemcpy((LPVOID)g_keys, b, 256); }
BOOL API EnableHardwareInput(BOOL e) { (void)e; return TRUE; }

/* ============================================================
 * Timery
 * ============================================================ */
#define MAX_TIMERS 32
static struct { HWND h; WORD id; WORD ms; DWORD next; LPVOID proc; BOOL used; } g_tm[MAX_TIMERS];

WORD API SetTimer(HWND h, WORD id, WORD ms, LPVOID proc)
{
    int i;
    static WORD nextid = 0x100;
    if (ms < 55) ms = 55;
    for (i = 0; i < MAX_TIMERS; i++)
        if (g_tm[i].used && g_tm[i].h == h && h && g_tm[i].id == id) break;
    if (i == MAX_TIMERS) {
        for (i = 0; i < MAX_TIMERS; i++) if (!g_tm[i].used) break;
        if (i == MAX_TIMERS) return 0;
        if (!h) id = nextid++;
    }
    g_tm[i].used = TRUE; g_tm[i].h = h; g_tm[i].id = id; g_tm[i].ms = ms;
    g_tm[i].next = tick() + ms; g_tm[i].proc = proc;
    return id ? id : 1;
}

BOOL API KillTimer(HWND h, WORD id)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (g_tm[i].used && g_tm[i].h == h && g_tm[i].id == id) { g_tm[i].used = FALSE; return TRUE; }
    return FALSE;
}

void timers_kill_wnd(HWND h)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++) if (g_tm[i].used && g_tm[i].h == h) g_tm[i].used = FALSE;
}

static BOOL timer_msg(LPMSG m, HWND filt)
{
    int i;
    DWORD t = tick();
    for (i = 0; i < MAX_TIMERS; i++) {
        if (!g_tm[i].used || (long)(t - g_tm[i].next) < 0) continue;
        if (filt && g_tm[i].h != filt) continue;
        g_tm[i].next = t + g_tm[i].ms;
        m->hwnd = g_tm[i].h; m->message = WM_TIMER; m->wParam = g_tm[i].id;
        m->lParam = (LONG)g_tm[i].proc; m->time = t; m->pt.x = g_mx; m->pt.y = g_my;
        return TRUE;
    }
    return FALSE;
}

/* ============================================================
 * Hooki - API
 * ============================================================ */
LPVOID API SetWindowsHook(int type, LPVOID proc)
{
    int i;
    LPVOID prev = 0;
    for (i = 0; i < MAX_HOOKS; i++) if (g_hook[i].proc && g_hook[i].type == type) prev = g_hook[i].proc;
    for (i = 0; i < MAX_HOOKS; i++) if (!g_hook[i].proc) break;
    if (i == MAX_HOOKS) return 0;
    g_hook[i].type = type; g_hook[i].proc = proc;
    return prev;
}

BOOL API UnhookWindowsHook(int type, LPVOID proc)
{
    int i;
    for (i = 0; i < MAX_HOOKS; i++)
        if (g_hook[i].proc == proc && g_hook[i].type == type) { g_hook[i].proc = 0; return TRUE; }
    return FALSE;
}

DWORD API SetWindowsHookEx(int type, LPVOID proc, WORD hinst, WORD task)
{
    int i;
    (void)task;
    for (i = 0; i < MAX_HOOKS; i++) if (!g_hook[i].proc) break;
    if (i == MAX_HOOKS) return 0;
    g_hook[i].type = type; g_hook[i].proc = proc; g_hook[i].hinst = hinst;
    return MAKELONG(i + 1, 0x4B48);                         /* "HK" */
}

BOOL API UnhookWindowsHookEx(DWORD hh)
{
    WORD i = LOWORD(hh) - 1;
    if (i >= MAX_HOOKS || !g_hook[i].proc) return FALSE;
    g_hook[i].proc = 0;
    return TRUE;
}

/* Nastepny hook tego samego typu nizej w lancuchu */
LRESULT API CallNextHookEx(DWORD hh, int code, WORD wp, LONG lp)
{
    int i = (int)LOWORD(hh) - 1, j;
    if (i < 0 || i >= MAX_HOOKS) return 0;
    for (j = i - 1; j >= 0; j--)
        if (g_hook[j].proc && g_hook[j].type == g_hook[i].type)
            return call_hook3(g_hook[j].proc, code, wp, lp);
    return 0;
}

LRESULT API DefHookProc(int code, WORD wp, LONG lp, LPVOID __far *next)
{
    (void)code; (void)wp; (void)lp; (void)next;
    return 0;
}

BOOL API CallMsgFilter(LPMSG m, int code) { return run_hooks(-1, code, 0, (LONG)m) != 0; }

/* ============================================================
 * Pobieranie komunikatow
 * ============================================================ */
extern HWND paint_pending(HWND filt);     /* upaint.c */
extern void caret_blink(void);            /* upaint.c */

static BOOL in_filter(WORD m, WORD mn, WORD mx)
{
    if (!mn && !mx) return TRUE;
    return m >= mn && m <= mx;
}

static BOOL get_msg(LPMSG m, HWND filt, WORD mn, WORD mx, BOOL remove)
{
    WORD i;
    input_poll();
    for (i = g_qh; i != g_qt; i = (i + 1) % QSIZE) {
        MSG *q = &g_q[i];
        if (q->message == WM_NULL && i == g_qh) { g_qh = (g_qh + 1) % QSIZE; continue; }
        if (q->message == WM_NULL) continue;
        if (filt && q->hwnd != filt && !IsChild(filt, q->hwnd)) continue;
        if (!in_filter(q->message, mn, mx)) continue;
        *m = *q;
        if (remove) {
            if (i == g_qh) g_qh = (g_qh + 1) % QSIZE;
            else q->message = WM_NULL;
        }
        g_lastpos = MAKELONG(m->pt.x, m->pt.y);
        g_lasttime = m->time;
        return TRUE;
    }
    if (g_quit && !filt) {
        m->hwnd = 0; m->message = WM_QUIT; m->wParam = g_quitcode; m->lParam = 0;
        if (remove) g_quit = FALSE;
        return TRUE;
    }
    if (in_filter(WM_PAINT, mn, mx)) {
        HWND p = paint_pending(filt);
        if (p) {
            m->hwnd = p; m->message = paint_msg(p); m->wParam = 0; m->lParam = 0;
            m->time = tick(); m->pt.x = g_mx; m->pt.y = g_my;
            return TRUE;
        }
    }
    if (in_filter(WM_TIMER, mn, mx) && timer_msg(m, filt)) return TRUE;
    return FALSE;
}

/* ordinal 108 */
BOOL API GetMessage(LPMSG m, HWND filt, WORD mn, WORD mx)
{
    user_init();
    for (;;) {
        if (get_msg(m, filt, mn, mx, TRUE)) break;
        caret_blink();
        cpu_idle();
    }
    run_hooks(3, 0, 0, (LONG)m);                              /* WH_GETMESSAGE */
    return m->message != WM_QUIT;
}

/* ordinal 109 */
BOOL API PeekMessage(LPMSG m, HWND filt, WORD mn, WORD mx, WORD fl)
{
    user_init();
    if (!get_msg(m, filt, mn, mx, (fl & 1) != 0)) { caret_blink(); return FALSE; }
    if (fl & 1) run_hooks(3, 0, 0, (LONG)m);
    return TRUE;
}

void API WaitMessage(void)
{
    MSG m;
    while (!get_msg((LPMSG)&m, 0, 0, 0, FALSE)) { caret_blink(); cpu_idle(); }
}

/* ordinal 113: WM_KEYDOWN -> WM_CHAR (uklad US) */
static char vk_char(BYTE vk, BOOL shift, BOOL caps, BOOL ctrl)
{
    static const char num_sh[] = ")!@#$%^&*(";
    if (vk >= 'A' && vk <= 'Z') {
        if (ctrl) return (char)(vk - 'A' + 1);
        return (char)((shift != caps) ? vk : vk + 32);
    }
    if (ctrl) return 0;
    if (vk >= '0' && vk <= '9') return shift ? num_sh[vk - '0'] : (char)vk;
    if (vk >= 0x60 && vk <= 0x69) return (char)('0' + vk - 0x60);
    switch (vk) {
    case 0x20: return ' ';   case 0x0D: return '\r';  case 0x08: return 8;
    case 0x09: return 9;     case 0x1B: return 27;
    case 0x6A: return '*';   case 0x6B: return '+';   case 0x6D: return '-';
    case 0x6E: return '.';   case 0x6F: return '/';
    case 0xBA: return shift ? ':' : ';';  case 0xBB: return shift ? '+' : '=';
    case 0xBC: return shift ? '<' : ',';  case 0xBD: return shift ? '_' : '-';
    case 0xBE: return shift ? '>' : '.';  case 0xBF: return shift ? '?' : '/';
    case 0xC0: return shift ? '~' : '`';  case 0xDB: return shift ? '{' : '[';
    case 0xDC: return shift ? '|' : '\\'; case 0xDD: return shift ? '}' : ']';
    case 0xDE: return shift ? '"' : '\'';
    }
    return 0;
}

BOOL API TranslateMessage(LPMSG m)
{
    char c;
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return FALSE;
    c = vk_char((BYTE)m->wParam, (g_keys[0x10] & 0x80) != 0, (g_keys[0x14] & 1) != 0,
                (g_keys[0x11] & 0x80) != 0);
    if (!c) return FALSE;
    post_msg(m->hwnd, m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, (BYTE)c, m->lParam);
    return TRUE;
}

/* ordinal 114 */
LONG API DispatchMessage(LPMSG m)
{
    if (m->message == WM_TIMER && m->lParam) {
        typedef void (__far __pascal *TIMERPROC)(HWND, WORD, WORD, DWORD);
        ((TIMERPROC)m->lParam)(m->hwnd, WM_TIMER, m->wParam, tick());
        return 0;
    }
    if (!m->hwnd) return 0;
    if (m->message == WM_PAINT || m->message == WM_PAINTICON) {
        LONG r = send_msg(m->hwnd, m->message, m->wParam, m->lParam);
        {   /* aplikacja nie wywolala BeginPaint - uznaj obszar za odswiezony */
            WND __far *w = WP(m->hwnd);
            if (w && w->upd) { DeleteObject(w->upd); w->upd = 0; w->flags &= ~WF_ERASE; }
        }
        return r;
    }
    return send_msg(m->hwnd, m->message, m->wParam, m->lParam);
}

DWORD API GetMessagePos(void)        { return g_lastpos; }
LONG  API GetMessageTime(void)       { return (LONG)g_lasttime; }
LONG  API GetMessageExtraInfo(void)  { return 0; }
BOOL  API InSendMessage(void)        { return FALSE; }
void  API ReplyMessage(LONG r)       { (void)r; }
BOOL  API SetMessageQueue(int n)     { (void)n; return TRUE; }
BOOL  API GetInputState(void)        { input_poll(); return g_qh != g_qt; }
DWORD API GetQueueStatus(WORD f)     { (void)f; input_poll(); return g_qh != g_qt ? 0x00040004UL : 0; }

/* ordinal 17/70: pozycja kursora */
void API GetCursorPos(LPPOINT p)     { input_poll(); p->x = g_mx; p->y = g_my; }
void API SetCursorPos(int x, int y)
{
    *(int __far *)MK_FP(SEL_KCB, 285) = x;
    *(int __far *)MK_FP(SEL_KCB, 287) = y;
    g_mx = x; g_my = y;
    GdiMoveCursor(x, y);
}

/* Petla modalna (przesuwanie okien, menu): nastepne zdarzenie myszy/klawiatury */
BOOL modal_next(LPMSG m)
{
    for (;;) {
        if (get_msg(m, 0, 0, 0, TRUE)) {
            if ((m->message >= WM_MOUSEMOVE && m->message <= WM_MBUTTONUP) ||
                (m->message >= WM_NCMOUSEMOVE && m->message <= WM_NCRBUTTONUP) ||
                (m->message >= WM_KEYDOWN && m->message <= WM_SYSCHAR)) return TRUE;
            if (m->message == WM_PAINT || m->message == WM_PAINTICON) { DispatchMessage(m); continue; }
            if (m->message == WM_QUIT) { post_msg(0, WM_QUIT, m->wParam, 0); g_quit = TRUE; return FALSE; }
            DispatchMessage(m);
            continue;
        }
        caret_blink();
        cpu_idle();
    }
}

int mouse_x(void) { return g_mx; }
int mouse_y(void) { return g_my; }
WORD mouse_btn(void) { return g_mbtn; }
