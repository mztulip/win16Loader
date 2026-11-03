/* uwnd.c - USER: okna (tworzenie, niszczenie, pozycja, kolejnosc Z), klasy,
 * wlasciwosci, regiony widoczne i uniewaznianie. */
#include "user.h"

WORD  g_wndmem;
CLASS g_cls[MAX_CLASS];

#define WNDBASE 0x1000                     /* HWND = WNDBASE + 4*indeks */

WND __far *wnd_ptr(HWND h)
{
    WORD i;
    WND __far *w;
    if (h < WNDBASE || ((h - WNDBASE) & 3) || !g_wndmem) return 0;
    i = (h - WNDBASE) >> 2;
    if (i >= MAX_WND) return 0;
    w = (WND __far *)MK_FP(g_wndmem, 0) + i;
    return w->used ? w : 0;
}

HWND wnd_handle(WORD i) { return (HWND)(WNDBASE + (i << 2)); }

/* ============================================================
 * Klasy
 * ============================================================ */
int cls_find(LPCSTR name, WORD hinst)
{
    int i, sys = -1;
    if (!HIWORD(name)) {                    /* atom klasy = indeks + 0xC000 */
        i = LOWORD(name) - 0xC000;
        return (i >= 0 && i < MAX_CLASS && g_cls[i].used) ? i : -1;
    }
    for (i = 0; i < MAX_CLASS; i++) {
        if (!g_cls[i].used || ustricmp((LPCSTR)g_cls[i].name, name)) continue;
        if (g_cls[i].system) sys = i;
        else if (!hinst || g_cls[i].hinst == hinst || (g_cls[i].style & 0x4000)) return i;  /* CS_GLOBALCLASS */
        else if (sys < 0) sys = i;
    }
    return sys;
}

typedef struct {
    WORD style; WNDPROC proc; int clsextra, wndextra; WORD hinst, icon, cursor, brush;
    LPCSTR menu, name;
} WNDCLASS;

static ATOM register_class(WNDCLASS __far *wc, BOOL system)
{
    int i = cls_find(wc->name, wc->hinst);
    CLASS *c;
    if (i >= 0 && !g_cls[i].system && g_cls[i].hinst == wc->hinst) return 0;   /* juz jest */
    for (i = 0; i < MAX_CLASS; i++) if (!g_cls[i].used) break;
    if (i == MAX_CLASS) return 0;
    c = &g_cls[i];
    umemset((LPVOID)(CLASS __far *)c, 0, sizeof(CLASS));
    c->used = 1;
    c->system = system;
    if (HIWORD(wc->name)) ustrncpy((LPSTR)(char __far *)c->name, wc->name, sizeof(c->name));
    c->style = wc->style;
    c->proc = wc->proc;
    c->clsextra = wc->clsextra > 32 ? 32 : wc->clsextra;
    c->wndextra = wc->wndextra > MAX_EXTRA ? MAX_EXTRA : wc->wndextra;
    c->hinst = wc->hinst;
    c->icon = wc->icon;
    c->cursor = wc->cursor;
    c->brush = wc->brush;
    c->menu = wc->menu;
    return (ATOM)(0xC000 + i);
}

/* ordinal 57 */
ATOM API RegisterClass(WNDCLASS __far *wc)
{
    user_init();
    return register_class(wc, FALSE);
}

ATOM register_system_class(LPCSTR name, WORD style, WNDPROC proc, int extra, WORD cursor, WORD brush)
{
    WNDCLASS wc;
    umemset((LPVOID)(WNDCLASS __far *)&wc, 0, sizeof(wc));
    wc.style = style; wc.proc = proc; wc.wndextra = extra; wc.name = name;
    wc.cursor = cursor; wc.brush = brush;
    return register_class((WNDCLASS __far *)&wc, TRUE);
}

/* ordinal 403 */
BOOL API UnregisterClass(LPCSTR name, WORD hinst)
{
    int i = cls_find(name, hinst);
    if (i < 0 || g_cls[i].system) return FALSE;
    g_cls[i].used = 0;
    return TRUE;
}

/* ordinal 404 */
BOOL API GetClassInfo(WORD hinst, LPCSTR name, WNDCLASS __far *wc)
{
    int i;
    user_init();
    i = cls_find(name, hinst);
    if (i < 0) return FALSE;
    wc->style = g_cls[i].style; wc->proc = g_cls[i].proc;
    wc->clsextra = g_cls[i].clsextra; wc->wndextra = g_cls[i].wndextra;
    wc->hinst = g_cls[i].hinst; wc->icon = g_cls[i].icon; wc->cursor = g_cls[i].cursor;
    wc->brush = g_cls[i].brush; wc->menu = g_cls[i].menu; wc->name = name;
    return TRUE;
}

/* ordinal 58 */
int API GetClassName(HWND h, LPSTR buf, int n)
{
    WND __far *w = WP(h);
    if (!w || n <= 0) return 0;
    ustrncpy(buf, (LPCSTR)(char __far *)g_cls[w->cls].name, n);
    return ustrlen(buf);
}

/* ============================================================
 * Kolejnosc Z
 * ============================================================ */
static void unlink_wnd(HWND h)
{
    WND __far *w = WP(h), __far *p = WP(w->parent);
    HWND c;
    if (!p) return;
    if (p->child == h) { p->child = w->next; w->next = 0; return; }
    for (c = p->child; c; c = WP(c)->next)
        if (WP(c)->next == h) { WP(c)->next = w->next; break; }
    w->next = 0;
}

/* after: 0 = na gore, 1 = na dol, inne = za tym oknem */
static void link_wnd(HWND h, HWND after)
{
    WND __far *w = WP(h), __far *p = WP(w->parent);
    HWND c;
    if (!p) return;
    if (after == 0 || !p->child) {
        /* okna "topmost" zostaja na gorze */
        if (!(w->exstyle & WS_EX_TOPMOST) && p->child && (WP(p->child)->exstyle & WS_EX_TOPMOST)) {
            c = p->child;
            while (WP(c)->next && (WP(WP(c)->next)->exstyle & WS_EX_TOPMOST)) c = WP(c)->next;
            w->next = WP(c)->next; WP(c)->next = h;
            return;
        }
        w->next = p->child; p->child = h;
        return;
    }
    if (after == 1) {
        for (c = p->child; WP(c)->next; c = WP(c)->next);
        WP(c)->next = h; w->next = 0;
        return;
    }
    if (WP(after) && WP(after)->parent == w->parent) {
        w->next = WP(after)->next; WP(after)->next = h;
    } else {
        w->next = p->child; p->child = h;
    }
}

/* ============================================================
 * Geometria, obszar klienta
 * ============================================================ */
static int frame_w(WND __far *w)
{
    if (w->style & WS_THICKFRAME) return 4;
    if ((w->exstyle & WS_EX_DLGMODALFRAME) || ((w->style & WS_DLGFRAME) && !(w->style & WS_BORDER))) return 4;
    if (w->style & (WS_BORDER | WS_DLGFRAME)) return 1;
    return 0;
}

void wnd_calc_client(HWND h)
{
    WND __far *w = WP(h);
    int f;
    if (!w) return;
    w->cr = w->wr;
    if (h == HDESKTOP) return;
    f = frame_w(w);
    w->cr.left += f; w->cr.right -= f; w->cr.top += f; w->cr.bottom -= f;
    if ((w->style & WS_CAPTION) == WS_CAPTION) w->cr.top += 19 - (f == 1 ? 0 : 1) + (f > 1 ? 1 : 0);
    if (!(w->style & WS_CHILD) && w->id) w->cr.top += menu_bar_height(h);
    if (w->style & WS_VSCROLL) w->cr.right -= 16;
    if (w->style & WS_HSCROLL) w->cr.bottom -= 16;
    if (w->cr.right < w->cr.left) w->cr.right = w->cr.left;
    if (w->cr.bottom < w->cr.top) w->cr.bottom = w->cr.top;
}

/* Przesun okno i potomkow o dx, dy */
static void offset_tree(HWND h, int dx, int dy)
{
    WND __far *w = WP(h);
    HWND c;
    w->wr.left += dx; w->wr.right += dx; w->wr.top += dy; w->wr.bottom += dy;
    w->cr.left += dx; w->cr.right += dx; w->cr.top += dy; w->cr.bottom += dy;
    if (w->upd) OffsetRgn(w->upd, dx, dy);
    for (c = w->child; c; c = WP(c)->next) offset_tree(c, dx, dy);
}

BOOL wnd_visible(HWND h)
{
    WND __far *w;
    while ((w = WP(h)) != 0) {
        if (!(w->style & WS_VISIBLE)) return FALSE;
        if (h == HDESKTOP) return TRUE;
        h = w->parent;
    }
    return FALSE;
}

HWND wnd_toplevel(HWND h)
{
    WND __far *w;
    while ((w = WP(h)) != 0 && (w->style & WS_CHILD)) h = w->parent;
    return h;
}

/* ============================================================
 * Region widoczny okna (lista prostokatow ekranu)
 * ============================================================ */
static int sub_rect(LPRECT r, int n, int max, const RECT __far *s)
{
    static RECT tmp[64];
    int i, k = 0;
    for (i = 0; i < n; i++) {
        RECT o;
        if (!rect_and((LPRECT)&o, &r[i], s)) { if (k < 64) tmp[k++] = r[i]; continue; }
        if (r[i].top < o.top && k < 64) { tmp[k] = r[i]; tmp[k].bottom = o.top; k++; }
        if (o.bottom < r[i].bottom && k < 64) { tmp[k] = r[i]; tmp[k].top = o.bottom; k++; }
        if (r[i].left < o.left && k < 64) { tmp[k].left = r[i].left; tmp[k].right = o.left; tmp[k].top = o.top; tmp[k].bottom = o.bottom; k++; }
        if (o.right < r[i].right && k < 64) { tmp[k].left = o.right; tmp[k].right = r[i].right; tmp[k].top = o.top; tmp[k].bottom = o.bottom; k++; }
    }
    if (k > max) k = max;
    for (i = 0; i < k; i++) r[i] = tmp[i];
    return k;
}

void wnd_vis_rects(HWND h, BOOL client, LPRECT out, int __far *pn, int max)
{
    WND __far *w = WP(h), __far *a;
    HWND cur, s;
    int n = 1;
    *pn = 0;
    if (!w || !wnd_visible(h)) return;
    out[0] = client ? w->cr : w->wr;
    /* przytnij do obszarow klienta przodkow */
    for (cur = w->parent; (a = WP(cur)) != 0; cur = a->parent) {
        if (!rect_and(&out[0], &out[0], &a->cr)) return;
        if (cur == HDESKTOP) break;
    }
    /* odejmij rodzenstwo powyzej (okno i jego przodkowie) */
    for (cur = h; cur && cur != HDESKTOP; cur = WP(cur)->parent) {
        WND __far *cw = WP(cur);
        BOOL clipsib = !(cw->style & WS_CHILD) || (cw->style & WS_CLIPSIBLINGS);
        if (!clipsib) continue;
        for (s = WP(cw->parent)->child; s && s != cur; s = WP(s)->next)
            if (WP(s)->style & WS_VISIBLE) n = sub_rect(out, n, max, &WP(s)->wr);
    }
    /* odejmij potomkow (WS_CLIPCHILDREN; pulpit zawsze) */
    if (client && ((w->style & WS_CLIPCHILDREN) || h == HDESKTOP))
        for (s = w->child; s; s = WP(s)->next)
            if (WP(s)->style & WS_VISIBLE) n = sub_rect(out, n, max, &WP(s)->wr);
    *pn = n;
}

/* ============================================================
 * Uniewaznianie
 * ============================================================ */
void wnd_invalidate(HWND h, RECT __far *r, BOOL erase, BOOL children)
{
    WND __far *w = WP(h);
    RECT a;
    HWND c;
    if (!w || !(w->style & WS_VISIBLE)) return;
    if (r) { if (!rect_and((LPRECT)&a, r, &w->cr)) goto kids; }
    else a = w->cr;
    if (a.right > a.left && a.bottom > a.top) {
        WORD t = CreateRectRgn(a.left, a.top, a.right, a.bottom);
        if (!w->upd) w->upd = t;
        else { CombineRgn(w->upd, w->upd, t, 2); DeleteObject(t); }
        if (erase) w->flags |= WF_ERASE;
    }
kids:
    if (children)
        for (c = w->child; c; c = WP(c)->next) {
            WND __far *cw = WP(c);
            RECT o;
            if (!(cw->style & WS_VISIBLE)) continue;
            if (!r || rect_and((LPRECT)&o, r, &cw->wr)) {
                if (!r || o.left < cw->cr.left || o.top < cw->cr.top || o.right > cw->cr.right || o.bottom > cw->cr.bottom)
                    cw->flags |= WF_NCPAINT;
                wnd_invalidate(c, r ? (RECT __far *)&o : 0, erase, TRUE);
            }
        }
}

/* Uniewaznij wszystko, co na ekranie przecina r (po przesunieciu/ukryciu okna) */
static void inval_tree(HWND h, RECT __far *r, HWND except)
{
    WND __far *w = WP(h);
    HWND c;
    RECT o;
    if (!w || !(w->style & WS_VISIBLE) || h == except) return;
    if (!rect_and((LPRECT)&o, r, &w->wr)) return;
    if (o.left < w->cr.left || o.top < w->cr.top || o.right > w->cr.right || o.bottom > w->cr.bottom)
        w->flags |= WF_NCPAINT;
    wnd_invalidate(h, (RECT __far *)&o, TRUE, FALSE);
    for (c = w->child; c; c = WP(c)->next) inval_tree(c, r, except);
}

void wnd_invalidate_screen(RECT __far *r, HWND except)
{
    inval_tree(HDESKTOP, r, except);
}

/* ============================================================
 * Tworzenie
 * ============================================================ */
typedef struct {
    LPVOID lpCreateParams;
    WORD   hInstance, hMenu, hwndParent;
    int    cy, cx, y, x;
    DWORD  style;
    LPCSTR lpszName, lpszClass;
    DWORD  dwExStyle;
} CREATESTRUCT;

static int g_cascade;

HWND API CreateWindowEx(DWORD ex, LPCSTR cname, LPCSTR title, DWORD style, int x, int y,
                        int cx, int cy, HWND parent, HMENU menu, WORD hinst, LPVOID param)
{
    WND __far *w, __far *pw;
    HWND h;
    WORD i;
    int ci;
    CREATESTRUCT cs;
    LRESULT r;
    user_init();
    ci = cls_find(cname, hinst);
    if (ci < 0) {
        udbg("USER: CreateWindow - nieznana klasa ");
        if (HIWORD(cname)) udbg(cname); else udbg_hex(LOWORD(cname), 4);
        udbg("\n");
        return 0;
    }
    if (*(WORD __far *)MK_FP(SEL_KCB, 536)) {          /* KCB_TRACE (loader -t) */
        udbg("USER: CreateWindow "); udbg(g_cls[ci].name[0] ? (LPCSTR)g_cls[ci].name : "?");
        udbg(" st="); udbg_hex(style, 8); udbg(" "); udbg_hex((WORD)x, 4); udbg(","); udbg_hex((WORD)y, 4);
        udbg(" "); udbg_hex((WORD)cx, 4); udbg("x"); udbg_hex((WORD)cy, 4); udbg(" par="); udbg_hex(parent, 4); udbg("\n");
    }
    if ((style & WS_CHILD) && !WP(parent)) return 0;
    for (i = 1; i < MAX_WND; i++) if (!((WND __far *)MK_FP(g_wndmem, 0))[i].used) break;
    if (i == MAX_WND) { udbg("USER: brak miejsca na okna\n"); return 0; }
    h = wnd_handle(i);
    w = (WND __far *)MK_FP(g_wndmem, 0) + i;
    umemset((LPVOID)w, 0, sizeof(WND));
    w->used = 1;
    w->cls = ci;
    w->style = style & ~WS_VISIBLE;
    w->exstyle = ex;
    w->proc = g_cls[ci].proc;
    w->hinst = hinst;
    if (style & WS_CHILD) { w->parent = parent; w->id = menu; }
    else {
        w->parent = HDESKTOP;
        w->owner = parent ? wnd_toplevel(parent) : 0;
        if (!menu && g_cls[ci].menu) {
            extern HMENU API LoadMenu(WORD, LPCSTR);
            menu = LoadMenu(hinst, g_cls[ci].menu);
        }
        w->id = menu;
        if (!(style & WS_POPUP)) w->style |= WS_CLIPSIBLINGS | WS_CAPTION;   /* WS_OVERLAPPED */
    }
    pw = WP(w->parent);
    if (x == (int)0x8000 && !(style & WS_CHILD)) {
        x = 20 + (g_cascade % 8) * 20; g_cascade++;
        if (y == (int)0x8000 || (style & WS_POPUP)) y = x;
        else if (y < 0) y = x;          /* y = nCmdShow w tym przypadku */
    }
    if (cx == (int)0x8000) { cx = SCR_W * 3 / 4; cy = SCR_H * 3 / 4; }
    if (x == (int)0x8000) x = 0;
    if (y == (int)0x8000) y = 0;
    if (style & WS_CHILD) { x += pw->cr.left; y += pw->cr.top; }
    w->wr.left = x; w->wr.top = y; w->wr.right = x + cx; w->wr.bottom = y + cy;
    if (HIWORD(title)) { ustrncpy((LPSTR)w->text, title, MAX_TITLE); w->textlen = ustrlen((LPCSTR)w->text); }
    link_wnd(h, 0);
    wnd_calc_client(h);

    cs.lpCreateParams = param; cs.hInstance = hinst; cs.hMenu = menu;
    cs.hwndParent = parent; cs.cy = cy; cs.cx = cx; cs.y = y; cs.x = x;
    cs.style = style; cs.lpszName = title; cs.lpszClass = cname; cs.dwExStyle = ex;
    if (style & WS_CHILD) { cs.x = x - pw->cr.left; cs.y = y - pw->cr.top; }

    if (!send_msg(h, WM_NCCREATE, 0, (LONG)(CREATESTRUCT __far *)&cs)) {
        udbg("USER: WM_NCCREATE=0\n");
        DestroyWindow(h);
        return 0;
    }
    wnd_calc_client(h);
    r = send_msg(h, WM_CREATE, 0, (LONG)(CREATESTRUCT __far *)&cs);
    if (r == -1L || !WP(h)) {
        udbg("USER: WM_CREATE=-1\n");
        if (WP(h)) DestroyWindow(h);
        return 0;
    }
    w = WP(h);
    w->flags |= WF_CREATED | WF_NEEDSIZE;    /* WM_SIZE/WM_MOVE przy pierwszym ShowWindow (jak Windows) */
    if ((style & WS_CHILD) && !(ex & 0x0004))           /* WS_EX_NOPARENTNOTIFY */
        send_msg(parent, WM_PARENTNOTIFY, WM_CREATE, MAKELONG(h, w->id));
    if (!(style & WS_CHILD)) {
        extern void shell_hook(int code, WORD w);
        shell_hook(1, h);                               /* HSHELL_WINDOWCREATED */
    }
    if (style & WS_MAXIMIZE) ShowWindow(h, 3);
    else if (style & WS_MINIMIZE) ShowWindow(h, 6);
    else if (style & WS_VISIBLE) ShowWindow(h, 5);
    return WP(h) ? h : 0;
}

/* ordinal 41 */
HWND API CreateWindow(LPCSTR cname, LPCSTR title, DWORD style, int x, int y, int cx, int cy,
                      HWND parent, HMENU menu, WORD hinst, LPVOID param)
{
    return CreateWindowEx(0, cname, title, style, x, y, cx, cy, parent, menu, hinst, param);
}

/* ============================================================
 * Niszczenie
 * ============================================================ */
extern void wnd_destroying(HWND h);   /* umsg.c: focus/capture/aktywne/timery */

static void destroy_tree(HWND h)
{
    WND __far *w = WP(h);
    if (!w || (w->flags & WF_DESTROYING)) return;
    w->flags |= WF_DESTROYING;
    send_msg(h, WM_DESTROY, 0, 0);
    while ((w = WP(h)) != 0 && w->child) {
        HWND c = w->child;
        destroy_tree(c);
        if (WP(c) && WP(h) && WP(h)->child == c) { unlink_wnd(c); WP(c)->used = 0; }
    }
    if (!WP(h)) return;
    send_msg(h, WM_NCDESTROY, 0, 0);
    w = WP(h);
    if (!w) return;
    wnd_destroying(h);
    {
        extern void props_remove_all(HWND h);
        props_remove_all(h);
    }
    if (w->upd) { DeleteObject(w->upd); w->upd = 0; }
    unlink_wnd(h);
    w->used = 0;
}

/* ordinal 53 */
BOOL API DestroyWindow(HWND h)
{
    WND __far *w = WP(h);
    RECT r;
    BOOL vis;
    WORD i;
    if (!w || h == HDESKTOP) return FALSE;
    if (w->flags & WF_DESTROYING) return TRUE;
    /* najpierw okna posiadane */
    for (i = 1; i < MAX_WND; i++) {
        WND __far *o = (WND __far *)MK_FP(g_wndmem, 0) + i;
        if (o->used && o->owner == h && !(o->flags & WF_DESTROYING)) DestroyWindow(wnd_handle(i));
    }
    if (!(w = WP(h))) return TRUE;
    vis = wnd_visible(h);
    r = w->wr;
    if (!(w->style & WS_CHILD)) {
        extern void shell_hook(int code, WORD w);
        shell_hook(2, h);                               /* HSHELL_WINDOWDESTROYED */
    }
    if ((w->style & WS_CHILD) && !(w->exstyle & 0x0004) && WP(w->parent))
        send_msg(w->parent, WM_PARENTNOTIFY, WM_DESTROY, MAKELONG(h, w->id));
    if (vis && WP(h)) WP(h)->style &= ~WS_VISIBLE;
    destroy_tree(h);
    if (vis) wnd_invalidate_screen((RECT __far *)&r, 0);
    dce_update();
    return TRUE;
}

/* ============================================================
 * Pozycja i widocznosc
 * ============================================================ */
typedef struct { HWND hwnd, after; int x, y, cx, cy; WORD flags; } WINDOWPOS;

#define SWP_NOSIZE       0x0001
#define SWP_NOMOVE       0x0002
#define SWP_NOZORDER     0x0004
#define SWP_NOREDRAW     0x0008
#define SWP_NOACTIVATE   0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW   0x0040
#define SWP_HIDEWINDOW   0x0080

BOOL API SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, WORD fl)
{
    WND __far *w = WP(h), __far *p;
    WINDOWPOS wp;
    RECT old;
    BOOL wasvis, moved, sized;
    if (!w || h == HDESKTOP) return FALSE;
    p = WP(w->parent);
    wp.hwnd = h; wp.after = after; wp.x = x; wp.y = y; wp.cx = cx; wp.cy = cy; wp.flags = fl;
    if (fl & SWP_NOMOVE) {
        wp.x = w->wr.left - ((w->style & WS_CHILD) ? p->cr.left : 0);
        wp.y = w->wr.top - ((w->style & WS_CHILD) ? p->cr.top : 0);
    }
    if (fl & SWP_NOSIZE) { wp.cx = w->wr.right - w->wr.left; wp.cy = w->wr.bottom - w->wr.top; }
    send_msg(h, WM_WINDOWPOSCHANGING, 0, (LONG)(WINDOWPOS __far *)&wp);
    if (!(w = WP(h))) return FALSE;
    fl = wp.flags;
    old = w->wr;
    wasvis = wnd_visible(h);
    x = wp.x + ((w->style & WS_CHILD) ? p->cr.left : 0);
    y = wp.y + ((w->style & WS_CHILD) ? p->cr.top : 0);
    moved = !(fl & SWP_NOMOVE) && (x != w->wr.left || y != w->wr.top);
    sized = !(fl & SWP_NOSIZE) && (wp.cx != w->wr.right - w->wr.left || wp.cy != w->wr.bottom - w->wr.top);
    if (moved) offset_tree(h, x - w->wr.left, y - w->wr.top);
    if (sized) {
        w->wr.right = w->wr.left + wp.cx;
        w->wr.bottom = w->wr.top + wp.cy;
    }
    if (sized || (fl & SWP_FRAMECHANGED)) wnd_calc_client(h);
    if (!(fl & SWP_NOZORDER)) {
        HWND a = wp.after;
        if (a == 0xFFFF) { w->exstyle |= WS_EX_TOPMOST; a = 0; }
        else if (a == 0xFFFE) { w->exstyle &= ~WS_EX_TOPMOST; a = 0; }
        if (a != h) { unlink_wnd(h); link_wnd(h, a); }
    }
    if (fl & SWP_SHOWWINDOW) w->style |= WS_VISIBLE;
    if (fl & SWP_HIDEWINDOW) w->style &= ~WS_VISIBLE;
    if (!(fl & SWP_NOREDRAW)) {
        if (wasvis && (moved || sized || (fl & SWP_HIDEWINDOW) || !(fl & SWP_NOZORDER)))
            wnd_invalidate_screen((RECT __far *)&old, (fl & SWP_HIDEWINDOW) ? 0 : h);
        if (wnd_visible(h) && (moved || sized || (fl & (SWP_SHOWWINDOW | SWP_FRAMECHANGED)) || !(fl & SWP_NOZORDER))) {
            w->flags |= WF_NCPAINT;
            wnd_invalidate(h, 0, TRUE, TRUE);
        }
    }
    if ((fl & SWP_SHOWWINDOW) && !(fl & SWP_NOACTIVATE) && !(w->style & WS_CHILD)) activate_window(h, FALSE);
    if (fl & SWP_HIDEWINDOW) {
        extern void wnd_hidden(HWND h);
        wnd_hidden(h);
    }
    if (moved || sized || (fl & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) || !(fl & SWP_NOZORDER))
        dce_update();
    wp.x = w->wr.left - ((w->style & WS_CHILD) ? p->cr.left : 0);
    wp.y = w->wr.top - ((w->style & WS_CHILD) ? p->cr.top : 0);
    wp.cx = w->wr.right - w->wr.left; wp.cy = w->wr.bottom - w->wr.top;
    if (!moved) wp.flags |= SWP_NOMOVE;
    if (!sized) wp.flags |= SWP_NOSIZE;
    send_msg(h, WM_WINDOWPOSCHANGED, 0, (LONG)(WINDOWPOS __far *)&wp);
    return TRUE;
}

/* DefWindowProc(WM_WINDOWPOSCHANGED): WM_MOVE / WM_SIZE */
void wnd_posdone(HWND h, WINDOWPOS __far *wp)
{
    WND __far *w = WP(h);
    if (!w) return;
    if (!(wp->flags & SWP_NOMOVE)) {
        int mx = w->cr.left, my = w->cr.top;
        if (w->style & WS_CHILD) { mx -= WP(w->parent)->cr.left; my -= WP(w->parent)->cr.top; }
        send_msg(h, WM_MOVE, 0, MAKELONG(mx, my));
    }
    if (!(wp->flags & SWP_NOSIZE) && WP(h)) {
        WORD t = (w->flags & WF_MAXIMIZED) ? 2 : (w->flags & WF_MINIMIZED) ? 1 : 0;
        send_msg(h, WM_SIZE, t, MAKELONG(w->cr.right - w->cr.left, w->cr.bottom - w->cr.top));
    }
}

/* ordinal 56 */
BOOL API MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint)
{
    return SetWindowPos(h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

/* ordinal 45 */
BOOL API BringWindowToTop(HWND h) { return SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }

static BOOL show_window(HWND h, int cmd);

/* ordinal 42 */
BOOL API ShowWindow(HWND h, int cmd)
{
    BOOL r = show_window(h, cmd);
    WND __far *w = WP(h);
    if (cmd && w && (w->flags & WF_NEEDSIZE)) {
        WORD t = (w->flags & WF_MAXIMIZED) ? 2 : (w->flags & WF_MINIMIZED) ? 1 : 0;
        int mx = w->cr.left, my = w->cr.top;
        w->flags &= ~WF_NEEDSIZE;
        send_msg(h, WM_SIZE, t, MAKELONG(w->cr.right - w->cr.left, w->cr.bottom - w->cr.top));
        if ((w = WP(h)) != 0) {
            if (w->style & WS_CHILD) { mx -= WP(w->parent)->cr.left; my -= WP(w->parent)->cr.top; }
            send_msg(h, WM_MOVE, 0, MAKELONG(mx, my));
        }
    }
    return r;
}

static BOOL show_window(HWND h, int cmd)
{
    WND __far *w = WP(h);
    BOOL was;
    WORD fl = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
    if (!w || h == HDESKTOP) return FALSE;
    was = (w->style & WS_VISIBLE) != 0;
    switch (cmd) {
    case 0:                                             /* SW_HIDE */
        if (!was) return FALSE;
        send_msg(h, WM_SHOWWINDOW, 0, 0);
        SetWindowPos(h, 0, 0, 0, 0, 0, fl | SWP_HIDEWINDOW | SWP_NOACTIVATE);
        return was;
    case 3:                                             /* SW_SHOWMAXIMIZED */
        if (!(w->flags & WF_MAXIMIZED)) {
            RECT a;
            if (!(w->flags & WF_MINIMIZED)) w->normal = w->wr;
            w->flags = (w->flags | WF_MAXIMIZED) & ~WF_MINIMIZED;
            w->style |= WS_MAXIMIZE; w->style &= ~WS_MINIMIZE;
            if (w->style & WS_CHILD) a = WP(w->parent)->cr;
            else { a.left = 0; a.top = 0; a.right = SCR_W; a.bottom = SCR_H; }
            {
                int f = frame_w(w);
                InflateRect((LPRECT)&a, f, f);
                if (w->style & WS_CHILD) OffsetRect((LPRECT)&a, -WP(w->parent)->cr.left, -WP(w->parent)->cr.top);
            }
            if (!was) send_msg(h, WM_SHOWWINDOW, 1, 0);
            SetWindowPos(h, 0, a.left, a.top, a.right - a.left, a.bottom - a.top, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
            return was;
        }
        break;
    case 2: case 6: case 7:                             /* minimalizacja */
        if (!(w->flags & WF_MAXIMIZED) && !(w->flags & WF_MINIMIZED)) w->normal = w->wr;
        w->flags = (w->flags | WF_MINIMIZED) & ~WF_MAXIMIZED;
        w->style = (w->style | WS_MINIMIZE) & ~WS_MAXIMIZE;
        if (was) {
            send_msg(h, WM_SHOWWINDOW, 0, 0);
            SetWindowPos(h, 0, 0, 0, 0, 0, fl | SWP_HIDEWINDOW | SWP_NOACTIVATE);
            if (WP(h)) { WP(h)->style |= WS_VISIBLE; }   /* ikona: logicznie widoczne */
            if (WP(h)) send_msg(h, WM_SIZE, 1, 0);
        }
        return was;
    case 1: case 9:                                     /* SW_SHOWNORMAL / SW_RESTORE */
        if (w->flags & (WF_MAXIMIZED | WF_MINIMIZED)) {
            RECT n = w->normal;
            BOOL wasmin = (w->flags & WF_MINIMIZED) != 0;
            w->flags &= ~(WF_MAXIMIZED | WF_MINIMIZED);
            w->style &= ~(WS_MAXIMIZE | WS_MINIMIZE);
            if (wasmin) w->style &= ~WS_VISIBLE;
            if (w->style & WS_CHILD) OffsetRect((LPRECT)&n, -WP(w->parent)->cr.left, -WP(w->parent)->cr.top);
            send_msg(h, WM_SHOWWINDOW, 1, 0);
            SetWindowPos(h, 0, n.left, n.top, n.right - n.left, n.bottom - n.top, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
            return was;
        }
        break;
    }
    if (!was) {
        send_msg(h, WM_SHOWWINDOW, 1, 0);
        if (!WP(h)) return FALSE;
        fl |= SWP_SHOWWINDOW;
        if (cmd == 4 || cmd == 8 || cmd == 7) fl |= SWP_NOACTIVATE;
        if (!(WP(h)->style & WS_CHILD) && !(fl & SWP_NOACTIVATE)) fl &= ~SWP_NOZORDER;
        SetWindowPos(h, 0, 0, 0, 0, 0, fl);
    } else if (cmd != 4 && cmd != 8 && !(w->style & WS_CHILD)) {
        activate_window(h, FALSE);
    }
    return was;
}

/* ============================================================
 * Zapytania o okna
 * ============================================================ */
BOOL API IsWindow(HWND h)         { return WP(h) != 0; }
BOOL API IsWindowVisible(HWND h)  { return wnd_visible(h); }
BOOL API IsWindowEnabled(HWND h)  { WND __far *w = WP(h); return w && !(w->style & WS_DISABLED); }
BOOL API IsIconic(HWND h)         { WND __far *w = WP(h); return w && (w->flags & WF_MINIMIZED); }
BOOL API IsZoomed(HWND h)         { WND __far *w = WP(h); return w && (w->flags & WF_MAXIMIZED); }
HWND API GetDesktopWindow(void)   { user_init(); return HDESKTOP; }
HWND API GetDesktopHwnd(void)     { return GetDesktopWindow(); }

BOOL API IsChild(HWND parent, HWND h)
{
    WND __far *w;
    while ((w = WP(h)) != 0 && (w->style & WS_CHILD)) {
        h = w->parent;
        if (h == parent) return TRUE;
    }
    return FALSE;
}

/* ordinal 46: rodzic (potomne) albo wlasciciel (okna glowne) */
HWND API GetParent(HWND h)
{
    WND __far *w = WP(h);
    if (!w) return 0;
    return (w->style & WS_CHILD) ? w->parent : w->owner;
}

/* ordinal 233 */
HWND API SetParent(HWND h, HWND np)
{
    WND __far *w = WP(h), __far *p;
    HWND old;
    RECT r;
    if (!w) return 0;
    if (!np) np = HDESKTOP;
    p = WP(np);
    if (!p) return 0;
    old = w->parent;
    r = w->wr;
    unlink_wnd(h);
    w->parent = np;
    link_wnd(h, 0);
    if (wnd_visible(h) || (w->style & WS_VISIBLE)) {
        wnd_invalidate_screen((RECT __far *)&r, 0);
        w->flags |= WF_NCPAINT;
        wnd_invalidate(h, 0, TRUE, TRUE);
    }
    return old == HDESKTOP ? 0 : old;
}

/* ordinal 262 */
HWND API GetWindow(HWND h, WORD cmd)
{
    WND __far *w = WP(h);
    HWND c, prev = 0;
    if (!w) return 0;
    switch (cmd) {
    case 0: return WP(w->parent) ? WP(w->parent)->child : 0;               /* GW_HWNDFIRST */
    case 1: for (c = WP(w->parent)->child; c && WP(c)->next; c = WP(c)->next); return c;
    case 2: return w->next;                                                /* GW_HWNDNEXT */
    case 3: for (c = WP(w->parent)->child; c && c != h; c = WP(c)->next) prev = c; return prev;
    case 4: return w->owner;                                               /* GW_OWNER */
    case 5: return w->child;                                               /* GW_CHILD */
    }
    return 0;
}

HWND API GetTopWindow(HWND h)               { if (!h) h = HDESKTOP; return WP(h) ? WP(h)->child : 0; }
HWND API GetNextWindow(HWND h, WORD cmd)    { return GetWindow(h, cmd); }
HWND API GetLastActivePopup(HWND h)         { return h; }

void API GetWindowRect(HWND h, LPRECT r)
{
    WND __far *w = WP(h);
    if (w) *r = w->wr; else SetRectEmpty(r);
}

void API GetClientRect(HWND h, LPRECT r)
{
    WND __far *w = WP(h);
    r->left = r->top = 0;
    r->right = w ? w->cr.right - w->cr.left : 0;
    r->bottom = w ? w->cr.bottom - w->cr.top : 0;
}

BOOL API ClientToScreen(HWND h, LPPOINT p)
{
    WND __far *w = WP(h);
    if (w) { p->x += w->cr.left; p->y += w->cr.top; }
    return TRUE;
}

BOOL API ScreenToClient(HWND h, LPPOINT p)
{
    WND __far *w = WP(h);
    if (w) { p->x -= w->cr.left; p->y -= w->cr.top; }
    return TRUE;
}

/* ordinal 258 */
void API MapWindowPoints(HWND from, HWND to, LPPOINT p, WORD n)
{
    WND __far *a = WP(from), __far *b = WP(to);
    int dx = (a ? a->cr.left : 0) - (b ? b->cr.left : 0);
    int dy = (a ? a->cr.top : 0) - (b ? b->cr.top : 0);
    while (n--) { p->x += dx; p->y += dy; p++; }
}

/* Najglebsze widoczne okno pod punktem (wsp. ekranu); hit = kod NC */
static HWND child_at(HWND h, int x, int y)
{
    HWND c;
    for (c = WP(h)->child; c; c = WP(c)->next) {
        WND __far *w = WP(c);
        if ((w->style & WS_VISIBLE) && x >= w->wr.left && x < w->wr.right && y >= w->wr.top && y < w->wr.bottom)
            return c;
    }
    return 0;
}

HWND wnd_from_point(int x, int y, int __far *hit)
{
    HWND h = HDESKTOP, c;
    for (;;) {
        WND __far *w = WP(h);
        if (h != HDESKTOP && !(x >= w->cr.left && x < w->cr.right && y >= w->cr.top && y < w->cr.bottom))
            break;                                   /* w obszarze nieklienta - nie schodz nizej */
        c = child_at(h, x, y);
        if (!c || (WP(c)->style & WS_DISABLED)) {
            if (c) h = c;
            break;
        }
        h = c;
    }
    if (hit) *hit = (int)send_msg(h, WM_NCHITTEST, 0, MAKELONG(x, y));
    while (hit && *hit == HTTRANSPARENT && h != HDESKTOP) {      /* okno przezroczyste -> rodzic */
        h = WP(h)->parent;
        *hit = (int)send_msg(h, WM_NCHITTEST, 0, MAKELONG(x, y));
    }
    return h;
}

HWND API WindowFromPoint(POINT p) { return wnd_from_point(p.x, p.y, 0); }

HWND API ChildWindowFromPoint(HWND h, POINT p)
{
    WND __far *w = WP(h);
    HWND c;
    if (!w) return 0;
    p.x += w->cr.left; p.y += w->cr.top;
    if (p.x < w->wr.left || p.x >= w->wr.right || p.y < w->wr.top || p.y >= w->wr.bottom) return 0;
    c = child_at(h, p.x, p.y);
    return c ? c : h;
}

/* ordinal 50 */
HWND API FindWindow(LPCSTR cname, LPCSTR title)
{
    HWND c;
    user_init();
    for (c = WP(HDESKTOP)->child; c; c = WP(c)->next) {
        WND __far *w = WP(c);
        if (cname && HIWORD(cname) && ustricmp((LPCSTR)(char __far *)g_cls[w->cls].name, cname)) continue;
        if (cname && !HIWORD(cname) && cls_find(cname, 0) != (int)w->cls) continue;
        if (title && ustricmp((LPCSTR)w->text, title)) continue;
        return c;
    }
    return 0;
}

typedef BOOL (__far __pascal *WNDENUMPROC)(HWND, LONG);

/* ordinal 54 */
BOOL API EnumWindows(WNDENUMPROC proc, LONG lp)
{
    HWND list[MAX_WND];
    int n = 0, i;
    HWND c;
    user_init();
    for (c = WP(HDESKTOP)->child; c && n < MAX_WND; c = WP(c)->next) list[n++] = c;
    for (i = 0; i < n; i++) if (WP(list[i]) && !proc(list[i], lp)) return FALSE;
    return TRUE;
}

static BOOL enum_children(HWND h, WNDENUMPROC proc, LONG lp)
{
    HWND list[64];
    int n = 0, i;
    HWND c;
    for (c = WP(h)->child; c && n < 64; c = WP(c)->next) list[n++] = c;
    for (i = 0; i < n; i++) {
        if (!WP(list[i])) continue;
        if (!proc(list[i], lp)) return FALSE;
        if (WP(list[i]) && !enum_children(list[i], proc, lp)) return FALSE;
    }
    return TRUE;
}

/* ordinal 55 */
BOOL API EnumChildWindows(HWND h, WNDENUMPROC proc, LONG lp)
{
    if (!WP(h)) return FALSE;
    return enum_children(h, proc, lp);
}

/* ordinal 225 */
BOOL API EnumTaskWindows(WORD task, WNDENUMPROC proc, LONG lp)
{
    (void)task;
    return EnumWindows(proc, lp);
}

/* ordinal 224 */
WORD API GetWindowTask(HWND h)
{
    extern WORD API GetCurrentTask(void);
    (void)h;
    return GetCurrentTask();
}

/* ============================================================
 * Tekst
 * ============================================================ */
int API GetWindowText(HWND h, LPSTR buf, int n)
{
    if (!WP(h) || n <= 0) return 0;
    return (int)send_msg(h, WM_GETTEXT, n, (LONG)buf);
}

int API GetWindowTextLength(HWND h)
{
    if (!WP(h)) return 0;
    return (int)send_msg(h, WM_GETTEXTLENGTH, 0, 0);
}

void API SetWindowText(HWND h, LPCSTR s)
{
    if (WP(h)) send_msg(h, WM_SETTEXT, 0, (LONG)s);
}

/* ============================================================
 * Get/SetWindowWord/Long, Get/SetClassWord/Long
 * ============================================================ */
LONG API GetWindowLong(HWND h, int i)
{
    WND __far *w = WP(h);
    if (!w) return 0;
    switch (i) {
    case -4:  return (LONG)w->proc;                       /* GWL_WNDPROC */
    case -16: return (LONG)w->style;                      /* GWL_STYLE */
    case -20: return (LONG)w->exstyle;                    /* GWL_EXSTYLE */
    }
    if (i >= 0 && i + 4 <= MAX_EXTRA) return *(LONG __far *)(w->extra + i);
    return 0;
}

LONG API SetWindowLong(HWND h, int i, LONG v)
{
    WND __far *w = WP(h);
    LONG o;
    if (!w) return 0;
    switch (i) {
    case -4:  o = (LONG)w->proc; w->proc = (WNDPROC)v; return o;
    case -16: o = (LONG)w->style; w->style = (DWORD)v; return o;
    case -20: o = (LONG)w->exstyle; w->exstyle = (DWORD)v; return o;
    }
    if (i >= 0 && i + 4 <= MAX_EXTRA) { o = *(LONG __far *)(w->extra + i); *(LONG __far *)(w->extra + i) = v; return o; }
    return 0;
}

WORD API GetWindowWord(HWND h, int i)
{
    WND __far *w = WP(h);
    if (!w) return 0;
    switch (i) {
    case -6:  return w->hinst;                            /* GWW_HINSTANCE */
    case -8:  return (w->style & WS_CHILD) ? w->parent : w->owner;   /* GWW_HWNDPARENT */
    case -12: return w->id;                               /* GWW_ID */
    }
    if (i >= 0 && i + 2 <= MAX_EXTRA) return *(WORD __far *)(w->extra + i);
    return 0;
}

WORD API SetWindowWord(HWND h, int i, WORD v)
{
    WND __far *w = WP(h);
    WORD o;
    if (!w) return 0;
    switch (i) {
    case -6:  o = w->hinst; w->hinst = v; return o;
    case -8:  o = w->owner; w->owner = v; return o;
    case -12: o = w->id; w->id = v; return o;
    }
    if (i >= 0 && i + 2 <= MAX_EXTRA) { o = *(WORD __far *)(w->extra + i); *(WORD __far *)(w->extra + i) = v; return o; }
    return 0;
}

LONG API GetClassLong(HWND h, int i)
{
    WND __far *w = WP(h);
    CLASS *c;
    if (!w) return 0;
    c = &g_cls[w->cls];
    switch (i) {
    case -24: return (LONG)c->proc;                       /* GCL_WNDPROC */
    case -8:  return (LONG)c->menu;                       /* GCL_MENUNAME */
    }
    if (i >= 0 && i + 4 <= 32) return *(LONG __far *)(c->extra + i);
    return 0;
}

LONG API SetClassLong(HWND h, int i, LONG v)
{
    WND __far *w = WP(h);
    CLASS *c;
    LONG o;
    if (!w) return 0;
    c = &g_cls[w->cls];
    switch (i) {
    case -24: o = (LONG)c->proc; c->proc = (WNDPROC)v; return o;
    case -8:  o = (LONG)c->menu; c->menu = (LPCSTR)v; return o;
    }
    if (i >= 0 && i + 4 <= 32) { o = *(LONG __far *)(c->extra + i); *(LONG __far *)(c->extra + i) = v; return o; }
    return 0;
}

WORD API GetClassWord(HWND h, int i)
{
    WND __far *w = WP(h);
    CLASS *c;
    if (!w) return 0;
    c = &g_cls[w->cls];
    switch (i) {
    case -10: return c->brush;                            /* GCW_HBRBACKGROUND */
    case -12: return c->cursor;                           /* GCW_HCURSOR */
    case -14: return c->icon;                             /* GCW_HICON */
    case -16: return c->hinst;                            /* GCW_HMODULE */
    case -18: return (WORD)c->wndextra;
    case -20: return (WORD)c->clsextra;
    case -26: return c->style;                            /* GCW_STYLE */
    case -32: return (WORD)(0xC000 + w->cls);             /* GCW_ATOM */
    }
    if (i >= 0 && i + 2 <= 32) return *(WORD __far *)(c->extra + i);
    return 0;
}

WORD API SetClassWord(HWND h, int i, WORD v)
{
    WND __far *w = WP(h);
    CLASS *c;
    WORD o;
    if (!w) return 0;
    c = &g_cls[w->cls];
    switch (i) {
    case -10: o = c->brush; c->brush = v; return o;
    case -12: o = c->cursor; c->cursor = v; return o;
    case -14: o = c->icon; c->icon = v; return o;
    case -26: o = c->style; c->style = v; return o;
    }
    if (i >= 0 && i + 2 <= 32) { o = *(WORD __far *)(c->extra + i); *(WORD __far *)(c->extra + i) = v; return o; }
    return 0;
}

/* ============================================================
 * Wlasciwosci (SetProp / GetProp)
 * ============================================================ */
#define MAX_PROPS 512
static struct { HWND h; WORD atom; char name[24]; HANDLE v; } g_prop[MAX_PROPS];

static int prop_find(HWND h, LPCSTR name)
{
    int i;
    for (i = 0; i < MAX_PROPS; i++) {
        if (g_prop[i].h != h) continue;
        if (!HIWORD(name)) { if (g_prop[i].atom == LOWORD(name)) return i; }
        else if (!g_prop[i].atom && !ustricmp((LPCSTR)(char __far *)g_prop[i].name, name)) return i;
    }
    return -1;
}

BOOL API SetProp(HWND h, LPCSTR name, HANDLE v)
{
    int i = prop_find(h, name);
    if (!WP(h)) return FALSE;
    if (i < 0) {
        for (i = 0; i < MAX_PROPS; i++) if (!g_prop[i].h) break;
        if (i == MAX_PROPS) return FALSE;
        g_prop[i].h = h;
        if (HIWORD(name)) { g_prop[i].atom = 0; ustrncpy((LPSTR)(char __far *)g_prop[i].name, name, 24); }
        else g_prop[i].atom = LOWORD(name);
    }
    g_prop[i].v = v;
    return TRUE;
}

HANDLE API GetProp(HWND h, LPCSTR name)
{
    int i = prop_find(h, name);
    return i < 0 ? 0 : g_prop[i].v;
}

HANDLE API RemoveProp(HWND h, LPCSTR name)
{
    int i = prop_find(h, name);
    HANDLE v;
    if (i < 0) return 0;
    v = g_prop[i].v;
    g_prop[i].h = 0;
    return v;
}

void props_remove_all(HWND h)
{
    int i;
    for (i = 0; i < MAX_PROPS; i++) if (g_prop[i].h == h) g_prop[i].h = 0;
}

typedef int (__far __pascal *PROPENUMPROC)(HWND, LPCSTR, HANDLE);
int API EnumProps(HWND h, PROPENUMPROC proc)
{
    int i, r = -1;
    for (i = 0; i < MAX_PROPS; i++)
        if (g_prop[i].h == h) {
            LPCSTR n = g_prop[i].atom ? (LPCSTR)MAKELONG(g_prop[i].atom, 0) : (LPCSTR)(char __far *)g_prop[i].name;
            r = proc(h, n, g_prop[i].v);
            if (!r) break;
        }
    return r;
}

/* ============================================================
 * AdjustWindowRect, Placement
 * ============================================================ */
void API AdjustWindowRectEx(LPRECT r, DWORD style, BOOL menu, DWORD ex)
{
    WND tmp;
    tmp.style = style; tmp.exstyle = ex;
    {
        int f = frame_w((WND __far *)&tmp);
        r->left -= f; r->top -= f; r->right += f; r->bottom += f;
    }
    if ((style & WS_CAPTION) == WS_CAPTION) r->top -= 19;
    if (menu) r->top -= 19;
    if (style & WS_VSCROLL) r->right += 16;
    if (style & WS_HSCROLL) r->bottom += 16;
}

void API AdjustWindowRect(LPRECT r, DWORD style, BOOL menu) { AdjustWindowRectEx(r, style, menu, 0); }

typedef struct { WORD length, flags, showCmd; POINT ptMin, ptMax; RECT rcNormal; } WINDOWPLACEMENT;

BOOL API GetWindowPlacement(HWND h, WINDOWPLACEMENT __far *p)
{
    WND __far *w = WP(h);
    if (!w) return FALSE;
    p->length = sizeof(WINDOWPLACEMENT);
    p->flags = 0;
    p->showCmd = (w->flags & WF_MINIMIZED) ? 2 : (w->flags & WF_MAXIMIZED) ? 3 : (w->style & WS_VISIBLE) ? 1 : 0;
    p->ptMin.x = p->ptMin.y = -1;
    p->ptMax.x = p->ptMax.y = -1;
    p->rcNormal = (w->flags & (WF_MINIMIZED | WF_MAXIMIZED)) ? w->normal : w->wr;
    return TRUE;
}

BOOL API SetWindowPlacement(HWND h, WINDOWPLACEMENT __far *p)
{
    WND __far *w = WP(h);
    if (!w) return FALSE;
    if (w->flags & (WF_MINIMIZED | WF_MAXIMIZED)) w->normal = p->rcNormal;
    else MoveWindow(h, p->rcNormal.left, p->rcNormal.top, p->rcNormal.right - p->rcNormal.left,
                    p->rcNormal.bottom - p->rcNormal.top, TRUE);
    if (p->showCmd) ShowWindow(h, p->showCmd);
    return TRUE;
}

WORD API ArrangeIconicWindows(HWND h) { (void)h; return 0; }
BOOL API CloseWindow(HWND h)          { ShowWindow(h, 6); return TRUE; }
BOOL API OpenIcon(HWND h)             { ShowWindow(h, 9); return TRUE; }

/* ordinal 34 */
BOOL API EnableWindow(HWND h, BOOL en)
{
    WND __far *w = WP(h);
    BOOL was;
    if (!w) return FALSE;
    was = (w->style & WS_DISABLED) != 0;
    if (en) w->style &= ~WS_DISABLED; else w->style |= WS_DISABLED;
    if (was == !en) return was;
    if (!en && (GetFocus() == h || IsChild(h, GetFocus()))) SetFocus(0);
    send_msg(h, WM_ENABLE, en, 0);
    if (WP(h)) { WP(h)->flags |= WF_NCPAINT; wnd_invalidate(h, 0, TRUE, FALSE); }
    return was;
}

/* ============================================================
 * RegisterWindowMessage (ordinal 118) - wspolna tablica atomow
 * ============================================================ */
#define MAX_ATOMS 128
static char g_atom[MAX_ATOMS][32];

WORD user_atom(LPCSTR s)
{
    int i;
    for (i = 0; i < MAX_ATOMS; i++)
        if (g_atom[i][0] && !ustricmp((LPCSTR)(char __far *)g_atom[i], s)) return 0xC100 + i;
    for (i = 0; i < MAX_ATOMS; i++)
        if (!g_atom[i][0]) { ustrncpy((LPSTR)(char __far *)g_atom[i], s, 32); return 0xC100 + i; }
    return 0;
}

WORD user_atom_find(LPCSTR s)
{
    int i;
    for (i = 0; i < MAX_ATOMS; i++)
        if (g_atom[i][0] && !ustricmp((LPCSTR)(char __far *)g_atom[i], s)) return 0xC100 + i;
    return 0;
}

LPCSTR user_atom_name(WORD a)
{
    WORD i = a - 0xC100;
    return i < MAX_ATOMS ? (LPCSTR)(char __far *)g_atom[i] : 0;
}

WORD API RegisterWindowMessage(LPCSTR s) { return user_atom(s); }

/* ordinal 265: pokaz/ukryj okna popup posiadane przez h */
void API ShowOwnedPopups(HWND h, BOOL show)
{
    WORD i;
    for (i = 1; i < MAX_WND; i++) {
        WND __far *o = (WND __far *)MK_FP(g_wndmem, 0) + i;
        if (!o->used || o->owner != h || !(o->style & WS_POPUP)) continue;
        if (show && !(o->style & WS_VISIBLE) && (o->flags & WF_HIDDENPOPUP)) {
            o->flags &= ~WF_HIDDENPOPUP;
            ShowWindow(wnd_handle(i), 8);                       /* SW_SHOWNA */
        } else if (!show && (o->style & WS_VISIBLE)) {
            o->flags |= WF_HIDDENPOPUP;
            ShowWindow(wnd_handle(i), 0);
        }
    }
}
