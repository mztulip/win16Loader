/* uicon.c - USER: okna zminimalizowane (ikony), zachowanie jak w Windows 3.1:
 * ikona 32x32 w wolnym miejscu na dole obszaru klienta rodzica (rzedy od lewej, od dolu,
 * odstepy SM_CXICONSPACING/SM_CYICONSPACING), ArrangeIconicWindows, osobne okno tytulu
 * ikony (klasa #32772), przeciaganie ikony, klik = menu systemowe, dwuklik = przywroc.
 * Malowanie ikony: WM_PAINTICON/WM_ICONERASEBKGND (upaint.c). Wyglad tytulu: kolory paska
 * tytulu (wlasny styl), font MS Sans Serif 8 jak w Windows 3.1: aplikacje projektuja pulpit
 * pod te szerokosci ("My Computer" w Fixedsys nie miescilo sie w odstepie ikon). */
#include "user.h"

extern BOOL modal_next(LPMSG m);
extern int  mouse_x(void), mouse_y(void);
extern WORD mouse_btn(void);
extern HMENU API GetSystemMenu(HWND, BOOL);
extern int  menu_track_popup(HMENU m, int x, int y, HWND owner, BOOL sys);
extern int  menu_popup_height(HMENU m);
extern ATOM register_system_class(LPCSTR, WORD, WNDPROC, int, WORD, WORD);
typedef struct { HDC hdc; BOOL fErase; RECT rc; BOOL fRestore, fIncUpdate; BYTE res[16]; } PAINTSTRUCT;
HDC  API BeginPaint(HWND, PAINTSTRUCT __far *);
void API EndPaint(HWND, PAINTSTRUCT __far *);

#define ICON_CX   sysmetric(11)
#define ICON_CY   sysmetric(12)
#define SPACE_X   sysmetric(38)
#define SPACE_Y   sysmetric(39)
#define TITLE_GAP 2
#define TITLE_H   15                  /* MS Sans Serif 8 (13) + 1 px z gory i z dolu */

#define SWP_NOSIZE     0x0001
#define SWP_NOZORDER   0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040

/* ============================================================
 * Miejsca ikon
 * ============================================================ */
/* Prostokat ikony w miejscu nr i (wsp. ekranu); a = obszar klienta rodzica */
static void icon_slot(const RECT __far *a, int i, LPRECT r)
{
    int cols = (a->right - a->left) / SPACE_X, col, row;
    if (cols < 1) cols = 1;
    col = i % cols; row = i / cols;
    r->left = a->left + col * SPACE_X + (SPACE_X - ICON_CX) / 2;
    r->top = a->bottom - (row + 1) * SPACE_Y + (SPACE_Y - ICON_CY - TITLE_GAP - TITLE_H) / 2;
    r->right = r->left + ICON_CX;
    r->bottom = r->top + ICON_CY;
}

/* Czy w r nie lezy juz inna widoczna ikona tego samego rodzica */
static BOOL slot_free(HWND h, const RECT __far *r)
{
    HWND c;
    for (c = WP(WP(h)->parent)->child; c; c = WP(c)->next) {
        WND __far *w = WP(c);
        RECT o;
        if (c == h || !(w->flags & WF_MINIMIZED) || !(w->style & WS_VISIBLE)) continue;
        if (rect_and((LPRECT)&o, &w->wr, r)) return FALSE;
    }
    return TRUE;
}

/* Miejsce ikony okna h (wsp. klienta rodzica): zapamietane albo pierwsze wolne */
void icon_place(HWND h, LPRECT r)
{
    WND __far *w = WP(h), __far *p = WP(w->parent);
    int i;
    if (w->flags & WF_ICONPOS) {
        SetRect(r, w->iconpos.x, w->iconpos.y, w->iconpos.x + ICON_CX, w->iconpos.y + ICON_CY);
        return;
    }
    for (i = 0; i < 256; i++) {
        icon_slot(&p->cr, i, r);
        if (slot_free(h, r)) break;
    }
    OffsetRect(r, -p->cr.left, -p->cr.top);
}

/* ordinal 170: ustaw ikony okien potomnych h kolejno w miejscach; zwraca wysokosc rzedu */
WORD API ArrangeIconicWindows(HWND h)
{
    WND __far *p;
    HWND c, list[64];
    int n = 0, i, k = 0;
    if (!h) h = HDESKTOP;
    if (!(p = WP(h))) return 0;
    for (c = p->child; c && n < 64; c = WP(c)->next) list[n++] = c;
    for (i = n - 1; i >= 0; i--) {                          /* od dolu kolejnosci Z */
        WND __far *w = WP(list[i]);
        RECT r;
        if (!w || !(w->flags & WF_MINIMIZED) || !(w->style & WS_VISIBLE)) continue;
        icon_slot(&p->cr, k++, (LPRECT)&r);
        SetWindowPos(list[i], 0, r.left - p->cr.left, r.top - p->cr.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return k ? SPACE_Y : 0;
}

/* ============================================================
 * Okno tytulu ikony (#32772): w polu 'title' trzyma uchwyt swojej ikony
 * ============================================================ */
static WORD title_font(void)
{
    static WORD f;
    if (!f) f = CreateFont(-11, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "MS Sans Serif");
    return f;
}

static void title_paint(HWND t)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(t, (PAINTSTRUCT __far *)&ps);
    WND __far *tw = WP(t), __far *w = WP(tw->title);
    if (dc && w) {
        BOOL act = (w->flags & WF_ACTIVE) != 0;
        RECT r;
        WORD of = SelectObject(dc, title_font());
        int tx = LOWORD(GetTextExtent(dc, (LPCSTR)w->text, w->textlen));
        GetClientRect(t, (LPRECT)&r);
        fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(act ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
        InflateRect((LPRECT)&r, -3, 0);
        /* za dlugi tytul (nieaktywna ikona) - poczatek tytulu od lewej, reszta obcieta */
        SetBkMode(dc, 1);
        SetTextColor(dc, syscolor(act ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
        DrawText(dc, (LPCSTR)w->text, w->textlen, (LPRECT)&r,
                 (tx > r.right - r.left ? 0 : 0x0001) | 0x0004 | 0x0020 | 0x0800);     /* DT_CENTER VCENTER SINGLELINE NOPREFIX */
        SelectObject(dc, of);
    }
    EndPaint(t, (PAINTSTRUCT __far *)&ps);
}

static LRESULT __far __pascal IconTitleProc(HWND h, WORD m, WORD wp, LONG lp)
{
    WND __far *w = WP(h);
    HWND ic = w ? w->title : 0;
    switch (m) {
    case WM_NCHITTEST:
        return HTCAPTION;
    case WM_MOUSEACTIVATE:                                  /* aktywna staje sie ikona, nie tytul */
        if (WP(ic) && !(WP(ic)->style & WS_CHILD)) activate_window(ic, TRUE);
        return 3;                                           /* MA_NOACTIVATE */
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
        return WP(ic) ? send_msg(ic, m, wp, lp) : 0;        /* jak klik w ikone */
    case WM_NCPAINT:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        title_paint(h);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

void icon_init(void)
{
    register_system_class("#32772", 0, (WNDPROC)IconTitleProc, 0, 0, 0);
}

/* Prostokat tytulu pod ikona (wsp. ekranu): aktywna ikona - caly tytul,
 * nieaktywna - przyciety do odstepu miedzy ikonami */
static void title_rect(HWND h, LPRECT t)
{
    WND __far *w = WP(h), __far *p = WP(w->parent);
    HDC dc = GetDC(0);
    WORD of = SelectObject(dc, title_font());
    int cx = LOWORD(GetTextExtent(dc, (LPCSTR)w->text, w->textlen)) + 6;
    int mx = (w->flags & WF_ACTIVE) ? p->cr.right - p->cr.left : SPACE_X - 2;
    SelectObject(dc, of);
    ReleaseDC(0, dc);
    if (cx > mx) cx = mx;
    t->left = (w->wr.left + w->wr.right - cx) / 2;
    if (t->left + cx > p->cr.right) t->left = p->cr.right - cx;
    if (t->left < p->cr.left) t->left = p->cr.left;
    t->right = t->left + cx;
    t->top = w->wr.bottom + TITLE_GAP;
    t->bottom = t->top + TITLE_H;
}

/* Po kazdej zmianie ikony (SetWindowPos, aktywacja, tekst): tytul widoczny tylko
 * pod widoczna ikona z niepustym tekstem, w kolejnosci Z tuz nad nia */
void icon_title_sync(HWND h)
{
    WND __far *w = WP(h), __far *tw;
    HWND t, prev = 0, c;
    RECT r;
    if (!w || (w->flags & WF_ICONTITLE)) return;
    t = w->title;
    if (t && (!(tw = WP(t)) || !(tw->flags & WF_ICONTITLE) || tw->title != h)) t = w->title = 0;
    if (!(w->flags & WF_MINIMIZED) || !(w->style & WS_VISIBLE) || !w->textlen) {
        if (t && (WP(t)->style & WS_VISIBLE)) ShowWindow(t, 0);
        return;
    }
    if (!t) {
        if (w->style & WS_CHILD)
            t = CreateWindowEx(0, "#32772", 0, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, w->parent, 0, 0, 0);
        else
            t = CreateWindowEx(0, "#32772", 0, WS_POPUP | WS_CLIPSIBLINGS, 0, 0, 1, 1, h, 0, 0, 0);
        if (!t || !(w = WP(h))) return;
        WP(t)->flags |= WF_ICONTITLE;
        WP(t)->title = h;
        w->title = t;
    }
    title_rect(h, (LPRECT)&r);
    if (w->style & WS_CHILD) OffsetRect((LPRECT)&r, -WP(w->parent)->cr.left, -WP(w->parent)->cr.top);
    for (c = WP(w->parent)->child; c && c != h; c = WP(c)->next) prev = c;     /* okno nad ikona */
    SetWindowPos(t, prev, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | (prev == t ? SWP_NOZORDER : 0));
    InvalidateRect(t, 0, TRUE);
}

/* ============================================================
 * Mysz na ikonie (WM_NCLBUTTONDOWN, HTCAPTION)
 * ============================================================ */
static void icon_sysmenu(HWND h)
{
    HMENU m = GetSystemMenu(h, FALSE);
    WND __far *w = WP(h);
    int cmd, y;
    if (!m) return;
    y = w->wr.top - menu_popup_height(m) - 1;                 /* nad ikona, gdy sie miesci */
    if (y < 0) y = w->wr.bottom + TITLE_GAP + TITLE_H;
    cmd = menu_track_popup(m, w->wr.left, y, h, TRUE);
    if (cmd && WP(h)) send_msg(h, WM_SYSCOMMAND, cmd, 0);
}

void icon_track(HWND h, int x0, int y0)
{
    WND __far *w = WP(h);
    RECT r = w->wr, o = r;
    MSG m;
    BOOL drag = FALSE;
    DWORD until;
    /* przytrzymanie: przesuniecie o > 2 px = przeciaganie ramki ikony */
    while (modal_next((LPMSG)&m)) {
        int dx = mouse_x() - x0, dy = mouse_y() - y0;
        if (m.message == WM_KEYDOWN && m.wParam == 0x1B) { if (drag) xor_frame((LPRECT)&r); return; }
        if (!drag && (dx > 2 || dx < -2 || dy > 2 || dy < -2)) { drag = TRUE; xor_frame((LPRECT)&r); }
        if (drag) {
            xor_frame((LPRECT)&r);
            r = o;
            OffsetRect((LPRECT)&r, dx, dy);
            xor_frame((LPRECT)&r);
        }
        if (!(mouse_btn() & 1)) break;
    }
    if (!(w = WP(h))) return;
    if (drag) {
        xor_frame((LPRECT)&r);
        if (r.left != o.left || r.top != o.top)
            SetWindowPos(h, 0, r.left - WP(w->parent)->cr.left, r.top - WP(w->parent)->cr.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    /* klikniecie: drugie w czasie dwukliku przywraca okno, inaczej menu systemowe */
    until = tick() + GetDoubleClickTime();
    while ((long)(tick() - until) < 0) {
        if (!PeekMessage((LPMSG)&m, 0, 0, 0, 1)) continue;
        if (m.message == WM_PAINT || m.message == WM_PAINTICON) { DispatchMessage((LPMSG)&m); continue; }
        if (m.message == WM_LBUTTONDOWN || m.message == WM_NCLBUTTONDOWN ||
            m.message == WM_LBUTTONDBLCLK || m.message == WM_NCLBUTTONDBLCLK ||
            m.message == WM_RBUTTONDOWN || m.message == WM_NCRBUTTONDOWN) {
            if ((m.message == WM_NCLBUTTONDOWN || m.message == WM_NCLBUTTONDBLCLK) && WP(h) &&
                (m.hwnd == h || m.hwnd == WP(h)->title)) {
                send_msg(h, WM_SYSCOMMAND, SC_RESTORE, m.lParam);
                return;
            }
            post_msg(m.hwnd, m.message, m.wParam, m.lParam); /* klik gdzie indziej - oddaj */
            return;
        }
        if ((m.message < WM_MOUSEMOVE || m.message > WM_MBUTTONUP) &&
            (m.message < WM_NCMOUSEMOVE || m.message > WM_NCRBUTTONUP)) DispatchMessage((LPMSG)&m);
    }
    if (WP(h) && IsIconic(h)) icon_sysmenu(h);
}
