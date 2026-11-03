/* uctl.c - USER: kontrolki standardowe (BUTTON, STATIC, EDIT, LISTBOX,
 * COMBOBOX, SCROLLBAR) i schowek. */
#include "user.h"

extern ATOM register_system_class(LPCSTR, WORD, WNDPROC, int, WORD, WORD);
extern void menu_register(void);
extern void dlg_register(void);
extern int  mouse_x(void), mouse_y(void);
extern WORD mouse_btn(void);
extern BOOL modal_next(LPMSG m);
void API SetRect(LPRECT, int, int, int, int);
void API InflateRect(LPRECT, int, int);
void API OffsetRect(LPRECT, int, int);
BOOL API PtInRect(const RECT __far *, POINT);
int  API GetKeyState(int);
WORD API SetWindowWord(HWND, int, WORD);
LONG API SetWindowLong(HWND, int, LONG);
void API DrawFocusRect(HDC, LPRECT);
BOOL API DrawIcon(HDC, int, int, WORD);
HCURSOR API LoadCursor(WORD, LPCSTR);
int  API SetScrollPos(HWND, int, int, BOOL);
void API SetScrollRange(HWND, int, int, int, BOOL);
void API CreateCaret(HWND, HBITMAP, int, int);
void API DestroyCaret(void);
void API SetCaretPos(int, int);
void API ShowCaret(HWND);
void API HideCaret(HWND);
int  API wvsprintf(LPSTR, LPCSTR, WORD __far *);
HANDLE API SetClipboardData(WORD, HANDLE);
HANDLE API GetClipboardData(WORD);
BOOL API OpenClipboard(HWND);
BOOL API CloseClipboard(void);
BOOL API EmptyClipboard(void);
HDC  API BeginPaint(HWND, LPVOID);
void API EndPaint(HWND, LPVOID);
int  API lstrcmpi(LPCSTR, LPCSTR);

typedef struct { HDC hdc; BOOL e; RECT rc; BYTE r[20]; } PS;

#define XW(h, o)   (*(WORD __far *)(WP(h)->extra + (o)))
#define XL(h, o)   (*(DWORD __far *)(WP(h)->extra + (o)))
#define STYLE(h)   (WP(h)->style)
#define NOTIFY(h, code) send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, code))

/* Przygotuj DC kontrolki: font + kolory przez WM_CTLCOLOR rodzica */
static HBRUSH ctl_dc(HWND h, HDC dc, WORD type)
{
    HBRUSH br;
    if (XW(h, 0)) SelectObject(dc, XW(h, 0));
    SetBkMode(dc, 2);
    br = (HBRUSH)send_msg((STYLE(h) & WS_CHILD) ? WP(h)->parent : h, WM_CTLCOLOR, dc, MAKELONG(h, type));
    if (!br) {
        br = DefWindowProc(h, WM_CTLCOLOR, dc, MAKELONG(h, type));
    }
    return br;
}

static LRESULT common(HWND h, WORD m, WORD wp, LONG lp)
{
    switch (m) {
    case WM_SETFONT: XW(h, 0) = wp; if (lp) InvalidateRect(h, 0, TRUE); return 0;
    case WM_GETFONT: return XW(h, 0);
    case WM_ENABLE:  InvalidateRect(h, 0, TRUE); return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

/* ============================================================
 * BUTTON (extra: 0 font, 2 stan: 3=zaznaczenie, 4=wcisniety, 8=fokus)
 * ============================================================ */
#define BST_PUSHED 4
#define BST_FOCUS  8

static void btn_paint(HWND h, HDC dc)
{
    WND __far *w = WP(h);
    WORD st = XW(h, 2), type = (WORD)(w->style & 0x0F);
    RECT r;
    HBRUSH br;
    char t[MAX_TITLE];
    COLORREF tc;
    GetClientRect(h, (LPRECT)&r);
    br = ctl_dc(h, dc, 3);
    GetWindowText(h, (LPSTR)(char __far *)t, sizeof(t));
    tc = (w->style & WS_DISABLED) ? syscolor(COLOR_GRAYTEXT) : syscolor(COLOR_BTNTEXT);
    if (type == 0x0B) {                                   /* BS_OWNERDRAW */
        struct { WORD type, id, item, action, state; HWND hw; HDC dc; RECT rc; DWORD data; } di;
        di.type = 4; di.id = w->id; di.item = 0; di.action = 1;
        di.state = ((st & BST_PUSHED) ? 1 : 0) | ((w->style & WS_DISABLED) ? 4 : 0) | ((st & BST_FOCUS) ? 0x10 : 0);
        di.hw = h; di.dc = dc; di.rc = r; di.data = 0;
        send_msg(w->parent, WM_DRAWITEM, w->id, (LONG)(LPVOID)&di);
        return;
    }
    if (type == 0 || type == 1 || type == 8) {             /* przycisk */
        BOOL def = type == 1, down = (st & BST_PUSHED) != 0;
        fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_BTNFACE));
        fill_color(dc, r.left + 1, r.top, r.right - 1, r.top + 1, 0);
        fill_color(dc, r.left + 1, r.bottom - 1, r.right - 1, r.bottom, 0);
        fill_color(dc, r.left, r.top + 1, r.left + 1, r.bottom - 1, 0);
        fill_color(dc, r.right - 1, r.top + 1, r.right, r.bottom - 1, 0);
        InflateRect((LPRECT)&r, -1, -1);
        if (def) { draw_3d(dc, r.left, r.top, r.right, r.bottom, 0, 0); InflateRect((LPRECT)&r, -1, -1); }
        if (down) draw_3d(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_BTNSHADOW), syscolor(COLOR_BTNFACE));
        else {
            draw_3d(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_BTNHIGHLIGHT), syscolor(COLOR_BTNSHADOW));
            draw_3d(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, syscolor(COLOR_BTNHIGHLIGHT), syscolor(COLOR_BTNSHADOW));
        }
        SetBkMode(dc, 1);
        SetTextColor(dc, tc);
        if (down) OffsetRect((LPRECT)&r, 1, 1);
        DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&r, 0x0001 | 0x0004 | 0x0020);
        if (st & BST_FOCUS) { InflateRect((LPRECT)&r, -3, -3); DrawFocusRect(dc, (LPRECT)&r); }
        return;
    }
    FillRect(dc, (LPRECT)&r, br);
    if (type == 7) {                                      /* BS_GROUPBOX */
        int th = 8;
        COLORREF sh = syscolor(COLOR_WINDOWFRAME);
        fill_color(dc, r.left, r.top + th, r.right, r.top + th + 1, sh);
        fill_color(dc, r.left, r.bottom - 1, r.right, r.bottom, sh);
        fill_color(dc, r.left, r.top + th, r.left + 1, r.bottom, sh);
        fill_color(dc, r.right - 1, r.top + th, r.right, r.bottom, sh);
        if (t[0]) {
            RECT tr;
            SetRect((LPRECT)&tr, 8, 0, r.right - 8, 16);
            SetTextColor(dc, tc);
            DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&tr, 0x0020 | 0x0400);
            tr.right += 4; tr.left -= 2;
            FillRect(dc, (LPRECT)&tr, br);
            OffsetRect((LPRECT)&tr, 2, 0);
            SetBkMode(dc, 1);
            DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&tr, 0x0020);
        }
        return;
    }
    {                                                     /* checkbox / radio */
        int by = (r.bottom - 13) / 2, bx = (w->style & 0x20) ? r.right - 13 : 0;
        BOOL radio = type == 4 || type == 9;
        RECT tr;
        fill_color(dc, bx, by, bx + 13, by + 13, (st & BST_PUSHED) ? syscolor(COLOR_BTNFACE) : RGB(255, 255, 255));
        if (radio) {
            fill_color(dc, bx + 4, by, bx + 9, by + 1, 0); fill_color(dc, bx + 4, by + 12, bx + 9, by + 13, 0);
            fill_color(dc, bx, by + 4, bx + 1, by + 9, 0); fill_color(dc, bx + 12, by + 4, bx + 13, by + 9, 0);
            fill_color(dc, bx + 1, by + 1, bx + 4, by + 4, br == 0 ? 0 : syscolor(COLOR_BTNFACE));
            if (st & 3) fill_color(dc, bx + 4, by + 4, bx + 9, by + 9, 0);
        } else {
            draw_3d(dc, bx, by, bx + 13, by + 13, 0, 0);
            if ((st & 3) == 1) {
                int i;
                for (i = 0; i < 9; i++) { fill_color(dc, bx + 2 + i, by + 2 + i, bx + 3 + i, by + 3 + i, 0); fill_color(dc, bx + 10 - i, by + 2 + i, bx + 11 - i, by + 3 + i, 0); }
            } else if ((st & 3) == 2) fill_color(dc, bx + 2, by + 2, bx + 11, by + 11, syscolor(COLOR_BTNSHADOW));
        }
        SetRect((LPRECT)&tr, (w->style & 0x20) ? 0 : 17, 0, (w->style & 0x20) ? r.right - 17 : r.right, r.bottom);
        SetBkMode(dc, 1);
        SetTextColor(dc, tc);
        DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&tr, 0x0004 | 0x0020);
        if (st & BST_FOCUS) {
            DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&tr, 0x0004 | 0x0020 | 0x0400);
            tr.top = (r.bottom - (tr.bottom - tr.top)) / 2 - 1; tr.bottom = tr.top + 16; tr.right += 2; tr.left -= 1;
            DrawFocusRect(dc, (LPRECT)&tr);
        }
    }
}

static void btn_redraw(HWND h)
{
    HDC dc = GetDC(h);
    btn_paint(h, dc);
    ReleaseDC(h, dc);
}

static void btn_click(HWND h)
{
    WORD type = (WORD)(STYLE(h) & 0x0F);
    if (type == 3) XW(h, 2) = (XW(h, 2) & ~3) | ((XW(h, 2) & 3) ? 0 : 1);
    else if (type == 6) XW(h, 2) = (XW(h, 2) & ~3) | (((XW(h, 2) & 3) + 1) % 3);
    else if (type == 9) {                                 /* auto radio: odznacz grupe */
        HWND c;
        for (c = WP(WP(h)->parent)->child; c; c = WP(c)->next)
            if (c != h && !lstrcmpi((LPCSTR)(char __far *)g_cls[WP(c)->cls].name, "BUTTON") &&
                (WP(c)->style & 0x0F) == 9 && (XW(c, 2) & 3)) { XW(c, 2) &= ~3; InvalidateRect(c, 0, TRUE); }
        XW(h, 2) |= 1;
    }
    btn_redraw(h);
    NOTIFY(h, 0);                                          /* BN_CLICKED */
}

static LRESULT __far __pascal ButtonProc(HWND h, WORD m, WORD wp, LONG lp)
{
    WORD type = WP(h) ? (WORD)(STYLE(h) & 0x0F) : 0;
    switch (m) {
    case WM_PAINT: { PS ps; HDC dc = BeginPaint(h, (LPVOID)&ps); btn_paint(h, dc); EndPaint(h, (LPVOID)&ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE:
        if (type == 0) return 0x2000 | 0x0020;            /* DLGC_BUTTON | UNDEFPUSHBUTTON */
        if (type == 1) return 0x2000 | 0x0010;            /* DEFPUSHBUTTON */
        if (type == 4 || type == 9) return 0x2000 | 0x0040;
        if (type == 7) return 0x0100;                     /* DLGC_STATIC */
        return 0x2000;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        if (type == 7) return 0;
        SetCapture(h);
        if (GetFocus() != h) SetFocus(h);
        XW(h, 2) |= BST_PUSHED;
        btn_redraw(h);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            RECT r;
            POINT p;
            WORD o = XW(h, 2);
            GetClientRect(h, (LPRECT)&r);
            p.x = (int)LOWORD(lp); p.y = (int)HIWORD(lp);
            if (PtInRect((LPRECT)&r, p)) XW(h, 2) |= BST_PUSHED; else XW(h, 2) &= ~BST_PUSHED;
            if (o != XW(h, 2)) btn_redraw(h);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) {
            BOOL in = (XW(h, 2) & BST_PUSHED) != 0;
            ReleaseCapture();
            XW(h, 2) &= ~BST_PUSHED;
            btn_redraw(h);
            if (in) btn_click(h);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == 0x20 && !(lp & (1L << 30))) { XW(h, 2) |= BST_PUSHED; btn_redraw(h); }
        return 0;
    case WM_KEYUP:
        if (wp == 0x20 && (XW(h, 2) & BST_PUSHED)) { XW(h, 2) &= ~BST_PUSHED; btn_click(h); }
        return 0;
    case WM_SETFOCUS:  XW(h, 2) |= BST_FOCUS; btn_redraw(h); return 0;
    case WM_KILLFOCUS: XW(h, 2) &= ~(BST_FOCUS | BST_PUSHED); if (WP(h)) btn_redraw(h); return 0;
    case WM_SETTEXT:   DefWindowProc(h, m, wp, lp); InvalidateRect(h, 0, TRUE); return 1;
    case WM_USER + 0:  return XW(h, 2) & 3;               /* BM_GETCHECK */
    case WM_USER + 1:                                     /* BM_SETCHECK */
        if ((XW(h, 2) & 3) != (wp & 3)) { XW(h, 2) = (XW(h, 2) & ~3) | (wp & 3); InvalidateRect(h, 0, FALSE); }
        return 0;
    case WM_USER + 2:  return XW(h, 2);                   /* BM_GETSTATE */
    case WM_USER + 3:                                     /* BM_SETSTATE */
        if (wp) XW(h, 2) |= BST_PUSHED; else XW(h, 2) &= ~BST_PUSHED;
        btn_redraw(h);
        return 0;
    case WM_USER + 4:                                     /* BM_SETSTYLE */
        STYLE(h) = (STYLE(h) & ~0xFFFFUL) | wp;
        if (lp) InvalidateRect(h, 0, TRUE);
        return 0;
    }
    return common(h, m, wp, lp);
}

/* ============================================================
 * STATIC (extra: 0 font, 2 ikona)
 * ============================================================ */
static LRESULT __far __pascal StaticProc(HWND h, WORD m, WORD wp, LONG lp)
{
    switch (m) {
    case WM_CREATE: {
        WORD type = (WORD)(STYLE(h) & 0x0F);
        if (type == 3) {                                  /* SS_ICON: tekst = nazwa ikony */
            extern HICON API LoadIcon(WORD, LPCSTR);
            LPCSTR n = *(LPCSTR __far *)((BYTE __far *)lp + CS_NAME_OFF);
            if (n) XW(h, 2) = LoadIcon(WP(h)->hinst, n);
            SetWindowPos(h, 0, 0, 0, 32, 32, 0x0002 | 0x0004 | 0x0010);
        }
        return 0;
    }
    case WM_PAINT: {
        PS ps;
        HDC dc = BeginPaint(h, (LPVOID)&ps);
        RECT r;
        WORD type = (WORD)(STYLE(h) & 0x0F);
        HBRUSH br = ctl_dc(h, dc, 6);
        GetClientRect(h, (LPRECT)&r);
        switch (type) {
        case 3: FillRect(dc, (LPRECT)&r, br); if (XW(h, 2)) DrawIcon(dc, 0, 0, XW(h, 2)); break;
        case 4: fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_WINDOWFRAME)); break;
        case 5: fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_BACKGROUND)); break;
        case 6: fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_WINDOW)); break;
        case 7: case 8: case 9: {
            COLORREF c = type == 7 ? syscolor(COLOR_WINDOWFRAME) : type == 8 ? syscolor(COLOR_BACKGROUND) : syscolor(COLOR_WINDOW);
            draw_3d(dc, r.left, r.top, r.right, r.bottom, c, c);
            break;
        }
        default: {
            char t[MAX_TITLE];
            WORD fmt = (type == 1) ? 1 : (type == 2) ? 2 : 0;
            if (type == 0xB || type == 0xC) fmt = 0x0020; else fmt |= 0x0010;
            if (STYLE(h) & 0x80) fmt |= 0x0800;
            FillRect(dc, (LPRECT)&r, br);
            GetWindowText(h, (LPSTR)(char __far *)t, sizeof(t));
            if (STYLE(h) & WS_DISABLED) SetTextColor(dc, syscolor(COLOR_GRAYTEXT));
            SetBkMode(dc, 1);
            DrawText(dc, (LPCSTR)(char __far *)t, -1, (LPRECT)&r, fmt);
        }
        }
        EndPaint(h, (LPVOID)&ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST:  return HTTRANSPARENT;
    case WM_GETDLGCODE: return 0x0100;
    case WM_SETTEXT:    DefWindowProc(h, m, wp, lp); InvalidateRect(h, 0, TRUE); return 1;
    case WM_USER:       { WORD o = XW(h, 2); XW(h, 2) = wp; InvalidateRect(h, 0, TRUE); return o; }   /* STM_SETICON */
    case WM_USER + 1:   return XW(h, 2);                                                            /* STM_GETICON */
    }
    return common(h, m, wp, lp);
}

/* ============================================================
 * EDIT (extra: 0 font, 2 bufor (selektor), 4 dlugosc, 6 karetka, 8 zazn. poczatek,
 *       10 zazn. koniec, 12 przesuniecie x, 14 pierwsza linia, 16 zmieniony, 18 limit,
 *       20 znak hasla, 22 fokus)
 * ============================================================ */
#define E_BUF   2
#define E_LEN   4
#define E_CAR   6
#define E_SS    8
#define E_SE    10
#define E_XOFF  12
#define E_TOP   14
#define E_MOD   16
#define E_LIM   18
#define E_PWD   20
#define E_FOC   22
#define ES_MULTILINE 0x0004
#define ES_READONLY  0x0800
#define ES_PASSWORD  0x0020
#define EBUF 0x7FF0u                   /* < 0x8000: ustrncpy ma limit typu int */

static LPSTR etext(HWND h) { return (LPSTR)MK_FP(XW(h, E_BUF), 0); }

static int e_lineh(HWND h)
{
    HDC dc = GetDC(h);
    struct { int tmHeight; BYTE r[30]; } tm;
    if (XW(h, 0)) SelectObject(dc, XW(h, 0));
    GetTextMetrics(dc, (LPVOID)&tm);
    ReleaseDC(h, dc);
    return tm.tmHeight ? tm.tmHeight : 13;
}

/* Poczatek linii zawierajacej pos i numer linii */
static int e_line_start(HWND h, int pos, int __far *lineno)
{
    LPSTR t = etext(h);
    int i, ln = 0, st = 0;
    for (i = 0; i < pos; i++) if (t[i] == '\n') { ln++; st = i + 1; }
    if (lineno) *lineno = ln;
    return st;
}

static int e_line_end(HWND h, int pos)
{
    LPSTR t = etext(h);
    int n = XW(h, E_LEN);
    while (pos < n && t[pos] != '\r' && t[pos] != '\n') pos++;
    return pos;
}

static int e_textx(HWND h, HDC dc, int st, int pos)
{
    LPSTR t = etext(h);
    if (XW(h, E_PWD)) {
        char c = (char)XW(h, E_PWD);
        return (pos - st) * LOWORD(GetTextExtent(dc, (LPCSTR)(char __far *)&c, 1));
    }
    return pos > st ? LOWORD(GetTextExtent(dc, t + st, pos - st)) : 0;
}

static void e_caret(HWND h)
{
    HDC dc;
    int ln, st, x, lh = e_lineh(h);
    if (!XW(h, E_FOC)) return;
    dc = GetDC(h);
    if (XW(h, 0)) SelectObject(dc, XW(h, 0));
    st = e_line_start(h, XW(h, E_CAR), (int __far *)&ln);
    x = e_textx(h, dc, st, XW(h, E_CAR));
    ReleaseDC(h, dc);
    if (!(STYLE(h) & ES_MULTILINE)) {                     /* przewijanie poziome */
        RECT r;
        int xo = (int)XW(h, E_XOFF);
        GetClientRect(h, (LPRECT)&r);
        if (x - xo > r.right - 4) xo = x - r.right + r.right / 3;
        if (x - xo < 0) xo = x > r.right / 3 ? x - r.right / 3 : 0;
        if (xo != (int)XW(h, E_XOFF)) { XW(h, E_XOFF) = xo; InvalidateRect(h, 0, TRUE); }
        x -= xo;
    } else {
        RECT r;
        int top = XW(h, E_TOP), vis;
        GetClientRect(h, (LPRECT)&r);
        vis = r.bottom / lh;
        if (vis < 1) vis = 1;
        if (ln < top) top = ln;
        if (ln >= top + vis) top = ln - vis + 1;
        if (top != (int)XW(h, E_TOP)) { XW(h, E_TOP) = top; InvalidateRect(h, 0, TRUE); }
        ln -= top;
    }
    SetCaretPos(x + 2, (STYLE(h) & ES_MULTILINE) ? ln * lh + 1 : 2);
}

static void e_paint(HWND h, HDC dc)
{
    LPSTR t = etext(h);
    int n = XW(h, E_LEN), lh = e_lineh(h), y, i = 0, ln = 0, top = XW(h, E_TOP);
    int ss = XW(h, E_SS), se = XW(h, E_SE), a = ss < se ? ss : se, b = ss < se ? se : ss;
    RECT r;
    HBRUSH br = ctl_dc(h, dc, 1);
    COLORREF tc = (STYLE(h) & WS_DISABLED) ? syscolor(COLOR_GRAYTEXT) : syscolor(COLOR_WINDOWTEXT);
    GetClientRect(h, (LPRECT)&r);
    FillRect(dc, (LPRECT)&r, br);
    y = (STYLE(h) & ES_MULTILINE) ? 1 : 2;
    while (i <= n) {
        int e = e_line_end(h, i), x0 = 2 - ((STYLE(h) & ES_MULTILINE) ? 0 : (int)XW(h, E_XOFF)), k;
        if (ln >= top) {
            SetBkMode(dc, 1);
            for (k = i; k < e; ) {                            /* segmenty: zaznaczone / nie */
                BOOL sel = XW(h, E_FOC) && k >= a && k < b;
                int k2 = k;
                while (k2 < e && (XW(h, E_FOC) && k2 >= a && k2 < b) == sel) k2++;
                {
                    int xa = x0 + e_textx(h, dc, i, k), xb = x0 + e_textx(h, dc, i, k2);
                    if (sel) fill_color(dc, xa, y, xb, y + lh, syscolor(COLOR_HIGHLIGHT));
                    SetTextColor(dc, sel ? syscolor(COLOR_HIGHLIGHTTEXT) : tc);
                    if (XW(h, E_PWD)) {
                        int j;
                        char c = (char)XW(h, E_PWD);
                        for (j = k; j < k2; j++) TextOut(dc, x0 + e_textx(h, dc, i, j), y, (LPCSTR)(char __far *)&c, 1);
                    } else TextOut(dc, xa, y, t + k, k2 - k);
                }
                k = k2;
            }
            y += lh;
            if (y > r.bottom) break;
        }
        ln++;
        if (!(STYLE(h) & ES_MULTILINE)) break;
        i = e;
        if (i < n && t[i] == '\r') i++;
        if (i < n && t[i] == '\n') i++;
        else if (i >= n) break;
    }
}

static void e_redraw(HWND h)
{
    HDC dc = GetDC(h);
    HideCaret(h);
    e_paint(h, dc);
    ReleaseDC(h, dc);
    e_caret(h);
    ShowCaret(h);
}

static void e_replace(HWND h, LPCSTR s, int sn)
{
    LPSTR t = etext(h);
    int n = XW(h, E_LEN), ss = XW(h, E_SS), se = XW(h, E_SE), a = ss < se ? ss : se, b = ss < se ? se : ss, i;
    int lim = XW(h, E_LIM);
    if (n - (b - a) + sn > lim) { sn = lim - (n - (b - a)); if (sn < 0) sn = 0; NOTIFY(h, 0x0501); }
    if (sn != b - a) {
        if (sn > b - a) for (i = n - 1; i >= b; i--) t[i + sn - (b - a)] = t[i];
        else for (i = b; i < n; i++) t[i - (b - a) + sn] = t[i];
    }
    for (i = 0; i < sn; i++) t[a + i] = s[i];
    n = n - (b - a) + sn;
    t[n] = 0;
    XW(h, E_LEN) = n;
    XW(h, E_CAR) = XW(h, E_SS) = XW(h, E_SE) = a + sn;
    XW(h, E_MOD) = 1;
    e_redraw(h);
    NOTIFY(h, 0x0400);                                    /* EN_UPDATE */
    NOTIFY(h, 0x0300);                                    /* EN_CHANGE */
}

static void e_move(HWND h, int pos, BOOL extend)
{
    if (pos < 0) pos = 0;
    if (pos > (int)XW(h, E_LEN)) pos = XW(h, E_LEN);
    XW(h, E_CAR) = pos;
    XW(h, E_SE) = pos;
    if (!extend) XW(h, E_SS) = pos;
    e_redraw(h);
}

static int e_pos_from_xy(HWND h, int x, int y)
{
    HDC dc = GetDC(h);
    LPSTR t = etext(h);
    int lh = e_lineh(h), ln = (STYLE(h) & ES_MULTILINE) ? y / lh + XW(h, E_TOP) : 0, i = 0, n = XW(h, E_LEN), e, k;
    if (XW(h, 0)) SelectObject(dc, XW(h, 0));
    if (!(STYLE(h) & ES_MULTILINE)) x += XW(h, E_XOFF);
    while (ln > 0 && i < n) {
        e = e_line_end(h, i);
        if (e >= n) break;
        i = e + ((t[e] == '\r') ? 1 : 0);
        if (i < n && t[i] == '\n') i++;
        ln--;
    }
    e = e_line_end(h, i);
    for (k = i; k < e; k++) {
        int a = e_textx(h, dc, i, k), b = e_textx(h, dc, i, k + 1);
        if (x - 2 < (a + b) / 2) break;
    }
    ReleaseDC(h, dc);
    return k;
}

static int e_line_index(HWND h, int line)
{
    LPSTR t = etext(h);
    int i = 0, n = XW(h, E_LEN);
    if (line < 0) return e_line_start(h, XW(h, E_CAR), 0);
    while (line > 0) {
        int e = e_line_end(h, i);
        if (e >= n) return -1;
        i = e + ((t[e] == '\r') ? 1 : 0);
        if (i < n && t[i] == '\n') i++;
        line--;
    }
    return i;
}

static void e_copy(HWND h, BOOL cut)
{
    int ss = XW(h, E_SS), se = XW(h, E_SE), a = ss < se ? ss : se, b = ss < se ? se : ss;
    HANDLE m;
    if (a == b || XW(h, E_PWD)) return;
    m = GlobalAlloc(0x2002, b - a + 1);
    umemcpy(GlobalLock(m), etext(h) + a, b - a);
    ((LPSTR)GlobalLock(m))[b - a] = 0;
    if (OpenClipboard(h)) { EmptyClipboard(); SetClipboardData(1, m); CloseClipboard(); }
    if (cut && !(STYLE(h) & ES_READONLY)) e_replace(h, "", 0);
}

static LRESULT __far __pascal EditProc(HWND h, WORD m, WORD wp, LONG lp)
{
    BOOL shift = (GetKeyState(0x10) & 0x8000) != 0, ctrl = (GetKeyState(0x11) & 0x8000) != 0;
    switch (m) {
    case WM_CREATE: {
        LPCSTR s = *(LPCSTR __far *)((BYTE __far *)lp + CS_NAME_OFF);
        XW(h, E_BUF) = GlobalAlloc(0x40, EBUF + 2);
        XW(h, E_LIM) = (STYLE(h) & ES_MULTILINE) ? 30000 : 30000;
        if (STYLE(h) & ES_PASSWORD) XW(h, E_PWD) = '*';
        if (s && HIWORD(s)) { ustrncpy(etext(h), s, EBUF); XW(h, E_LEN) = ustrlen(etext(h)); }
        return 0;
    }
    case WM_NCDESTROY:
        if (XW(h, E_BUF)) GlobalFree(XW(h, E_BUF));
        XW(h, E_BUF) = 0;
        return DefWindowProc(h, m, wp, lp);
    case WM_PAINT: {
        PS ps;
        HDC dc = BeginPaint(h, (LPVOID)&ps);
        e_paint(h, dc);
        EndPaint(h, (LPVOID)&ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE:
        return 0x0001 | 0x0080 | ((STYLE(h) & ES_MULTILINE) ? 0x0004 : 0);   /* ARROWS|HASSETSEL|(ALLKEYS) */
    case WM_SETTEXT: {
        LPCSTR s = (LPCSTR)lp;
        ustrncpy(etext(h), s && HIWORD(s) ? s : "", EBUF);
        XW(h, E_LEN) = ustrlen(etext(h));
        XW(h, E_CAR) = XW(h, E_SS) = XW(h, E_SE) = 0;
        XW(h, E_XOFF) = XW(h, E_TOP) = 0;
        XW(h, E_MOD) = 0;
        InvalidateRect(h, 0, TRUE);
        NOTIFY(h, 0x0300);
        return 1;
    }
    case WM_GETTEXT: {
        int n = XW(h, E_LEN) < (int)wp - 1 ? XW(h, E_LEN) : (int)wp - 1;
        if ((int)wp <= 0) return 0;
        umemcpy((LPSTR)lp, etext(h), n);
        ((LPSTR)lp)[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH: return XW(h, E_LEN);
    case WM_SETFOCUS:
        XW(h, E_FOC) = 1;
        CreateCaret(h, 0, 1, e_lineh(h) - 2);
        e_caret(h);
        ShowCaret(h);
        InvalidateRect(h, 0, FALSE);
        NOTIFY(h, 0x0100);
        return 0;
    case WM_KILLFOCUS:
        XW(h, E_FOC) = 0;
        DestroyCaret();
        InvalidateRect(h, 0, FALSE);
        NOTIFY(h, 0x0200);
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        if (GetFocus() != h) SetFocus(h);
        SetCapture(h);
        e_move(h, e_pos_from_xy(h, (int)LOWORD(lp), (int)HIWORD(lp)), shift);
        if (m == WM_LBUTTONDBLCLK) { XW(h, E_SS) = 0; XW(h, E_SE) = XW(h, E_CAR) = XW(h, E_LEN); e_redraw(h); }
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h && (wp & 1)) e_move(h, e_pos_from_xy(h, (int)LOWORD(lp), (int)HIWORD(lp)), TRUE);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_KEYDOWN: {
        int c = XW(h, E_CAR);
        switch (wp) {
        case 0x25: e_move(h, c - 1, shift); break;
        case 0x27: e_move(h, c + 1, shift); break;
        case 0x24: e_move(h, ctrl ? 0 : e_line_start(h, c, 0), shift); break;
        case 0x23: e_move(h, ctrl ? XW(h, E_LEN) : e_line_end(h, c), shift); break;
        case 0x26: case 0x28:
            if (STYLE(h) & ES_MULTILINE) {
                int ln, st = e_line_start(h, c, (int __far *)&ln), col = c - st, ni = e_line_index(h, ln + (wp == 0x28 ? 1 : -1));
                if (ni >= 0 && ln + (wp == 0x28 ? 1 : -1) >= 0) {
                    int e = e_line_end(h, ni);
                    e_move(h, ni + col < e ? ni + col : e, shift);
                }
            }
            break;
        case 0x2E:                                         /* DEL */
            if (STYLE(h) & ES_READONLY) break;
            if (shift) { e_copy(h, TRUE); break; }
            if (XW(h, E_SS) == XW(h, E_SE)) { if (c >= (int)XW(h, E_LEN)) break; XW(h, E_SE) = c + 1 + ((etext(h)[c] == '\r') ? 1 : 0); }
            e_replace(h, "", 0);
            break;
        case 0x2D:                                         /* INS: ctrl=kopiuj, shift=wklej */
            if (ctrl) e_copy(h, FALSE);
            else if (shift) send_msg(h, WM_PASTE, 0, 0);
            break;
        }
        return 0;
    }
    case WM_CHAR: {
        char ch = (char)wp;
        if (ch == 3) { e_copy(h, FALSE); return 0; }       /* Ctrl+C */
        if (ch == 24) { e_copy(h, TRUE); return 0; }       /* Ctrl+X */
        if (ch == 22) { send_msg(h, WM_PASTE, 0, 0); return 0; }
        if (STYLE(h) & ES_READONLY) return 0;
        if (ch == 8) {
            if (XW(h, E_SS) == XW(h, E_SE)) {
                int c = XW(h, E_CAR);
                if (!c) return 0;
                XW(h, E_SS) = c - 1 - ((c >= 2 && etext(h)[c - 1] == '\n' && etext(h)[c - 2] == '\r') ? 1 : 0);
            }
            e_replace(h, "", 0);
            return 0;
        }
        if (ch == '\r') {
            if (STYLE(h) & ES_MULTILINE) e_replace(h, "\r\n", 2);
            return 0;
        }
        if (ch == 9 && !(STYLE(h) & ES_MULTILINE)) return 0;
        if ((BYTE)ch < 32 && ch != 9) return 0;
        if (STYLE(h) & 0x0008) ch = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);   /* ES_UPPERCASE */
        if (STYLE(h) & 0x0010) ch = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);   /* ES_LOWERCASE */
        e_replace(h, (LPCSTR)(char __far *)&ch, 1);
        return 0;
    }
    case WM_CUT:   e_copy(h, TRUE); return 0;
    case WM_COPY:  e_copy(h, FALSE); return 0;
    case WM_CLEAR: if (!(STYLE(h) & ES_READONLY)) e_replace(h, "", 0); return 0;
    case WM_PASTE:
        if (!(STYLE(h) & ES_READONLY) && OpenClipboard(h)) {
            HANDLE d = GetClipboardData(1);
            if (d) {
                LPSTR s = (LPSTR)GlobalLock(d);
                int n = ustrlen(s);
                if (!(STYLE(h) & ES_MULTILINE)) { int k; for (k = 0; k < n; k++) if (s[k] == '\r' || s[k] == '\n') { n = k; break; } }
                e_replace(h, s, n);
            }
            CloseClipboard();
        }
        return 0;
    case WM_USER + 0:  return MAKELONG(XW(h, E_SS) < XW(h, E_SE) ? XW(h, E_SS) : XW(h, E_SE), XW(h, E_SS) < XW(h, E_SE) ? XW(h, E_SE) : XW(h, E_SS));   /* EM_GETSEL */
    case WM_USER + 1: {                                    /* EM_SETSEL(0, MAKELONG(start, end)) */
        int a = (int)LOWORD(lp), b = (int)HIWORD(lp), n = XW(h, E_LEN);
        if (a < 0 || a > n) a = n;
        if (b < 0 || b > n) b = n;
        XW(h, E_SS) = a; XW(h, E_SE) = b; XW(h, E_CAR) = b;
        e_redraw(h);
        return 1;
    }
    case WM_USER + 2:  GetClientRect(h, (LPRECT)lp); return 0;          /* EM_GETRECT */
    case WM_USER + 6:                                      /* EM_LINESCROLL */
        if ((int)XW(h, E_TOP) + (int)HIWORD(lp) >= 0) XW(h, E_TOP) += (int)HIWORD(lp);
        InvalidateRect(h, 0, TRUE);
        return 1;
    case WM_USER + 8:  return XW(h, E_MOD);               /* EM_GETMODIFY */
    case WM_USER + 9:  XW(h, E_MOD) = wp; return 0;        /* EM_SETMODIFY */
    case WM_USER + 10: {                                   /* EM_GETLINECOUNT */
        int i, n = XW(h, E_LEN), c = 1;
        for (i = 0; i < n; i++) if (etext(h)[i] == '\n') c++;
        return c;
    }
    case WM_USER + 11: return e_line_index(h, (int)wp);   /* EM_LINEINDEX */
    case WM_USER + 17: {                                   /* EM_LINELENGTH */
        int st = (int)wp < 0 ? e_line_start(h, XW(h, E_CAR), 0) : e_line_start(h, (int)wp, 0);
        return e_line_end(h, st) - st;
    }
    case WM_USER + 18: e_replace(h, (LPCSTR)lp, ustrlen((LPCSTR)lp)); return 0;   /* EM_REPLACESEL */
    case WM_USER + 20: {                                   /* EM_GETLINE */
        int st = e_line_index(h, (int)wp), e, n, max = *(WORD __far *)lp;
        if (st < 0) return 0;
        e = e_line_end(h, st);
        n = e - st < max ? e - st : max;
        umemcpy((LPSTR)lp, etext(h) + st, n);
        return n;
    }
    case WM_USER + 21: XW(h, E_LIM) = wp ? (wp < EBUF ? wp : EBUF) : EBUF; return 0;   /* EM_LIMITTEXT */
    case WM_USER + 22: return 0;                           /* EM_CANUNDO */
    case WM_USER + 23: return 0;                           /* EM_UNDO */
    case WM_USER + 25: {                                   /* EM_LINEFROMCHAR */
        int ln;
        e_line_start(h, (int)wp < 0 ? XW(h, E_CAR) : (int)wp, (int __far *)&ln);
        return ln;
    }
    case WM_USER + 28: XW(h, E_PWD) = wp; InvalidateRect(h, 0, TRUE); return 0;  /* EM_SETPASSWORDCHAR */
    case WM_USER + 29: return 0;                           /* EM_EMPTYUNDOBUFFER */
    case WM_USER + 30: return XW(h, E_TOP);                /* EM_GETFIRSTVISIBLELINE */
    case WM_USER + 31:                                     /* EM_SETREADONLY */
        if (wp) STYLE(h) |= ES_READONLY; else STYLE(h) &= ~(DWORD)ES_READONLY;
        return 1;
    }
    return common(h, m, wp, lp);
}

/* ============================================================
 * LISTBOX (extra: 0 font, 2 pozycje (selektor), 4 liczba, 6 cursel, 8 top,
 *          10 wys. pozycji, 12 fokus, 14 kotwica, 16 pula tekstu (selektor),
 *          18 zajetosc puli, 20 okno powiadomien (combo), 22 kolor tla?)
 * ============================================================ */
#define L_ITEMS 2
#define L_CNT   4
#define L_SEL   6
#define L_TOP   8
#define L_IH    10
#define L_CAR   12
#define L_ANC   14
#define L_POOL  16
#define L_PUSED 18
#define L_NOTE  20
#define L_MAX   2000

typedef struct { DWORD data; WORD str; WORD sel; } LBITEM;

#define LBS_NOTIFY   0x0001
#define LBS_SORT     0x0002
#define LBS_MULTI    0x0008
#define LBS_ODFIXED  0x0010
#define LBS_ODVAR    0x0020
#define LBS_HASSTR   0x0040
#define LBS_EXTSEL   0x0800

static LBITEM __far *lbi(HWND h) { return (LBITEM __far *)MK_FP(XW(h, L_ITEMS), 0); }
static BOOL lb_od(HWND h) { return (STYLE(h) & (LBS_ODFIXED | LBS_ODVAR)) != 0; }
static BOOL lb_str(HWND h) { return !lb_od(h) || (STYLE(h) & LBS_HASSTR); }
static LPSTR lb_text(HWND h, int i) { return (LPSTR)MK_FP(XW(h, L_POOL), lbi(h)[i].str); }
static HWND lb_owner(HWND h) { return XW(h, L_NOTE) ? XW(h, L_NOTE) : WP(h)->parent; }

static void lb_notify(HWND h, WORD code)
{
    if (!(STYLE(h) & LBS_NOTIFY) && !XW(h, L_NOTE)) return;
    send_msg(lb_owner(h), WM_COMMAND, WP(h)->id, MAKELONG(h, code));
}

static int lb_vis(HWND h)
{
    RECT r;
    GetClientRect(h, (LPRECT)&r);
    return r.bottom / (int)XW(h, L_IH) > 0 ? r.bottom / (int)XW(h, L_IH) : 1;
}

static void lb_sb(HWND h)
{
    int vis = lb_vis(h), n = XW(h, L_CNT);
    if (!(STYLE(h) & WS_VSCROLL) && n <= vis) return;
    SetScrollRange(h, 1, 0, n > vis ? n - vis : 0, FALSE);
    SetScrollPos(h, 1, XW(h, L_TOP), TRUE);
}

static void lb_draw_item(HWND h, HDC dc, int i, HBRUSH br)
{
    RECT r;
    int ih = XW(h, L_IH);
    BOOL sel = (STYLE(h) & (LBS_MULTI | LBS_EXTSEL)) ? lbi(h)[i].sel : (int)XW(h, L_SEL) == i;
    GetClientRect(h, (LPRECT)&r);
    r.top = (i - (int)XW(h, L_TOP)) * ih;
    r.bottom = r.top + ih;
    if (lb_od(h)) {
        struct { WORD type, id, item, action, state; HWND hw; HDC dc; RECT rc; DWORD data; } di;
        di.type = XW(h, L_NOTE) ? 3 : 2; di.id = WP(h)->id; di.item = i; di.action = 1;
        di.state = (sel ? 1 : 0) | ((GetFocus() == h && (int)XW(h, L_CAR) == i) ? 0x10 : 0);
        di.hw = h; di.dc = dc; di.rc = r; di.data = lbi(h)[i].data;
        if (XW(h, L_NOTE)) { di.id = WP(XW(h, L_NOTE))->id; di.hw = XW(h, L_NOTE); }
        send_msg(XW(h, L_NOTE) ? WP(XW(h, L_NOTE))->parent : WP(h)->parent, WM_DRAWITEM, di.id, (LONG)(LPVOID)&di);
        return;
    }
    if (sel) fill_color(dc, r.left, r.top, r.right, r.bottom, syscolor(COLOR_HIGHLIGHT));
    else FillRect(dc, (LPRECT)&r, br);
    SetBkMode(dc, 1);
    SetTextColor(dc, syscolor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    if (lb_str(h)) TextOut(dc, r.left + 2, r.top, lb_text(h, i), ustrlen(lb_text(h, i)));
    if (GetFocus() == h && (int)XW(h, L_CAR) == i) DrawFocusRect(dc, (LPRECT)&r);
}

static void lb_paint(HWND h, HDC dc)
{
    RECT r;
    int i, top = XW(h, L_TOP), vis = lb_vis(h) + 1;
    HBRUSH br = ctl_dc(h, dc, 2);
    GetClientRect(h, (LPRECT)&r);
    FillRect(dc, (LPRECT)&r, br);
    for (i = top; i < (int)XW(h, L_CNT) && i < top + vis; i++) lb_draw_item(h, dc, i, br);
}

static void lb_redraw(HWND h)
{
    HDC dc;
    if (!wnd_visible(h)) return;
    dc = GetDC(h);
    lb_paint(h, dc);
    ReleaseDC(h, dc);
}

static WORD lb_addstr(HWND h, LPCSTR s)
{
    WORD n = s && HIWORD(s) ? (WORD)ustrlen(s) + 1 : 1, off = XW(h, L_PUSED);
    if ((DWORD)off + n > 0xFFF0UL) {                     /* kompaktowanie puli */
        WORD np = GlobalAlloc(0x40, 0xFFF0UL), u = 2, i;
        for (i = 0; i < XW(h, L_CNT); i++) {
            LPSTR t = lb_text(h, i);
            WORD l = (WORD)ustrlen(t) + 1;
            umemcpy(MK_FP(np, u), t, l);
            lbi(h)[i].str = u;
            u += l;
        }
        GlobalFree(XW(h, L_POOL));
        XW(h, L_POOL) = np;
        XW(h, L_PUSED) = u;
        off = u;
        if ((DWORD)off + n > 0xFFF0UL) return 0;
    }
    if (s && HIWORD(s)) ustrcpy((LPSTR)MK_FP(XW(h, L_POOL), off), s);
    else *(LPSTR)MK_FP(XW(h, L_POOL), off) = 0;
    XW(h, L_PUSED) = off + n;
    return off;
}

static int lb_insert(HWND h, int at, LPCSTR s)
{
    int n = XW(h, L_CNT), i;
    LBITEM __far *it = lbi(h);
    if (n >= L_MAX) return -2;                             /* LB_ERRSPACE */
    if (at < 0 || at > n) at = n;
    for (i = n; i > at; i--) it[i] = it[i - 1];
    it[at].sel = 0;
    if (lb_str(h)) { it[at].str = lb_addstr(h, s); it[at].data = 0; }
    else { it[at].str = 0; it[at].data = (DWORD)s; }
    XW(h, L_CNT) = n + 1;
    if ((int)XW(h, L_SEL) >= at && XW(h, L_SEL) != 0xFFFF) XW(h, L_SEL)++;
    if (STYLE(h) & LBS_ODVAR) {
        struct { WORD type, id, item, w, hgt; DWORD data; } mi;
        mi.type = 2; mi.id = WP(h)->id; mi.item = at; mi.w = 0; mi.hgt = XW(h, L_IH); mi.data = it[at].data;
        send_msg(WP(h)->parent, WM_MEASUREITEM, mi.id, (LONG)(LPVOID)&mi);
    }
    lb_sb(h);
    lb_redraw(h);
    return at;
}

static int lb_find(HWND h, int start, LPCSTR s, BOOL exact)
{
    int n = XW(h, L_CNT), i, k, len = ustrlen(s);
    for (k = 1; k <= n; k++) {
        i = (start + k) % n;
        if (start < 0) i = k - 1;
        if (!lb_str(h)) { if (lbi(h)[i].data == (DWORD)s) return i; continue; }
        if (exact) { if (!lstrcmpi(lb_text(h, i), s)) return i; }
        else {
            LPSTR t = lb_text(h, i);
            int j;
            for (j = 0; j < len; j++) {
                char a = t[j], b = s[j];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) break;
            }
            if (j == len) return i;
        }
    }
    return -1;
}

static void lb_set_cur(HWND h, int i, BOOL notify)
{
    int vis = lb_vis(h);
    if (i >= (int)XW(h, L_CNT)) i = XW(h, L_CNT) - 1;
    XW(h, L_SEL) = (WORD)i;
    XW(h, L_CAR) = (WORD)(i < 0 ? 0 : i);
    if (i >= 0) {
        if (i < (int)XW(h, L_TOP)) XW(h, L_TOP) = i;
        if (i >= (int)XW(h, L_TOP) + vis) XW(h, L_TOP) = i - vis + 1;
    }
    lb_sb(h);
    lb_redraw(h);
    if (notify) lb_notify(h, 1);                          /* LBN_SELCHANGE */
}

static int lb_hit(HWND h, int y)
{
    int i = y / (int)XW(h, L_IH) + XW(h, L_TOP);
    return i < (int)XW(h, L_CNT) ? i : -1;
}

static LRESULT __far __pascal ListBoxProc(HWND h, WORD m, WORD wp, LONG lp)
{
    switch (m) {
    case WM_CREATE:
        XW(h, L_ITEMS) = GlobalAlloc(0x40, (DWORD)L_MAX * sizeof(LBITEM));
        XW(h, L_POOL) = GlobalAlloc(0x40, 0xFFF0UL);
        XW(h, L_PUSED) = 2;
        XW(h, L_SEL) = 0xFFFF;
        XW(h, L_IH) = 16;
        if (STYLE(h) & LBS_ODFIXED) {
            struct { WORD type, id, item, w, hgt; DWORD data; } mi;
            mi.type = 2; mi.id = WP(h)->id; mi.item = 0; mi.w = 0; mi.hgt = 16; mi.data = 0;
            send_msg(WP(h)->parent, WM_MEASUREITEM, mi.id, (LONG)(LPVOID)&mi);
            if (WP(h)) XW(h, L_IH) = mi.hgt ? mi.hgt : 16;
        }
        return 0;
    case WM_NCDESTROY:
        if (XW(h, L_ITEMS)) GlobalFree(XW(h, L_ITEMS));
        if (XW(h, L_POOL)) GlobalFree(XW(h, L_POOL));
        XW(h, L_ITEMS) = XW(h, L_POOL) = 0;
        return DefWindowProc(h, m, wp, lp);
    case WM_SETFONT:
        XW(h, 0) = wp;
        if (!lb_od(h)) {
            HDC dc = GetDC(h);
            struct { int tmHeight; BYTE r[30]; } tm;
            SelectObject(dc, wp);
            GetTextMetrics(dc, (LPVOID)&tm);
            ReleaseDC(h, dc);
            XW(h, L_IH) = tm.tmHeight ? tm.tmHeight : 16;
        }
        if (lp) InvalidateRect(h, 0, TRUE);
        return 0;
    case WM_PAINT: { PS ps; HDC dc = BeginPaint(h, (LPVOID)&ps); lb_paint(h, dc); EndPaint(h, (LPVOID)&ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return 0x0001 | 0x0004;           /* ARROWS | (CHARS) */
    case WM_SETFOCUS:   lb_redraw(h); lb_notify(h, 4); return 0;
    case WM_KILLFOCUS:  lb_redraw(h); lb_notify(h, 5); return 0;
    case WM_SIZE:       lb_sb(h); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        int i = lb_hit(h, (int)HIWORD(lp));
        if (GetFocus() != h && !XW(h, L_NOTE)) SetFocus(h);
        if (i < 0) return 0;
        if (STYLE(h) & (LBS_MULTI | LBS_EXTSEL)) {
            if ((STYLE(h) & LBS_EXTSEL) && !(wp & 8) && !(wp & 4)) {
                int k;
                for (k = 0; k < (int)XW(h, L_CNT); k++) lbi(h)[k].sel = 0;
                lbi(h)[i].sel = 1;
            } else if ((STYLE(h) & LBS_EXTSEL) && (wp & 4)) {
                int a = XW(h, L_ANC), k;
                for (k = 0; k < (int)XW(h, L_CNT); k++) lbi(h)[k].sel = (k >= (a < i ? a : i) && k <= (a < i ? i : a));
            } else lbi(h)[i].sel = !lbi(h)[i].sel;
            if (!(wp & 4)) XW(h, L_ANC) = i;
            XW(h, L_CAR) = i;
            XW(h, L_SEL) = i;
            lb_redraw(h);
            lb_notify(h, 1);
        } else if (i != (int)XW(h, L_SEL)) lb_set_cur(h, i, TRUE);
        if (m == WM_LBUTTONDBLCLK) lb_notify(h, 2);       /* LBN_DBLCLK */
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == h && (wp & 1) && !(STYLE(h) & (LBS_MULTI | LBS_EXTSEL))) {
            int i = lb_hit(h, (int)HIWORD(lp));
            if (i >= 0 && i != (int)XW(h, L_SEL)) lb_set_cur(h, i, TRUE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_KEYDOWN: {
        int c = XW(h, L_SEL) == 0xFFFF ? -1 : (int)XW(h, L_SEL), n = XW(h, L_CNT), vis = lb_vis(h), nc = c;
        switch (wp) {
        case 0x26: nc = c - 1; break;
        case 0x28: nc = c + 1; break;
        case 0x21: nc = c - vis; break;
        case 0x22: nc = c + vis; break;
        case 0x24: nc = 0; break;
        case 0x23: nc = n - 1; break;
        case 0x20:
            if (STYLE(h) & LBS_MULTI && c >= 0) { lbi(h)[c].sel = !lbi(h)[c].sel; lb_redraw(h); lb_notify(h, 1); }
            return 0;
        default: return 0;
        }
        if (nc < 0) nc = 0;
        if (nc >= n) nc = n - 1;
        if (nc != c && n) lb_set_cur(h, nc, TRUE);
        return 0;
    }
    case WM_CHAR: {
        char s[2];
        int i;
        s[0] = (char)wp; s[1] = 0;
        if ((BYTE)wp < 32) return 0;
        i = lb_find(h, XW(h, L_SEL) == 0xFFFF ? -1 : (int)XW(h, L_SEL), (LPCSTR)(char __far *)s, FALSE);
        if (i >= 0) lb_set_cur(h, i, TRUE);
        return 0;
    }
    case WM_VSCROLL: {
        int t = XW(h, L_TOP), n = XW(h, L_CNT), vis = lb_vis(h);
        switch (wp) {
        case 0: t--; break; case 1: t++; break;
        case 2: t -= vis; break; case 3: t += vis; break;
        case 4: case 5: t = (int)LOWORD(lp); break;
        default: return 0;
        }
        if (t > n - vis) t = n - vis;
        if (t < 0) t = 0;
        if (t != (int)XW(h, L_TOP)) { XW(h, L_TOP) = t; lb_sb(h); lb_redraw(h); }
        return 0;
    }
    case WM_USER + 1: {                                    /* LB_ADDSTRING */
        int at = XW(h, L_CNT);
        if ((STYLE(h) & LBS_SORT) && lb_str(h)) {
            for (at = 0; at < (int)XW(h, L_CNT); at++) if (lstrcmpi(lb_text(h, at), (LPCSTR)lp) > 0) break;
        }
        return lb_insert(h, at, (LPCSTR)lp);
    }
    case WM_USER + 2:  return lb_insert(h, (int)wp, (LPCSTR)lp);      /* LB_INSERTSTRING */
    case WM_USER + 3: {                                    /* LB_DELETESTRING */
        int i = (int)wp, n = XW(h, L_CNT), k;
        if (i < 0 || i >= n) return -1;
        if (lb_od(h)) {
            struct { WORD type, id, item; HWND hw; DWORD data; } dl;
            dl.type = 2; dl.id = WP(h)->id; dl.item = i; dl.hw = h; dl.data = lbi(h)[i].data;
            send_msg(WP(h)->parent, WM_DELETEITEM, dl.id, (LONG)(LPVOID)&dl);
        }
        for (k = i; k + 1 < n; k++) lbi(h)[k] = lbi(h)[k + 1];
        XW(h, L_CNT) = n - 1;
        if ((int)XW(h, L_SEL) == i) XW(h, L_SEL) = 0xFFFF;
        else if ((int)XW(h, L_SEL) > i && XW(h, L_SEL) != 0xFFFF) XW(h, L_SEL)--;
        if ((int)XW(h, L_TOP) > 0 && (int)XW(h, L_TOP) >= n - 1) XW(h, L_TOP)--;
        lb_sb(h);
        InvalidateRect(h, 0, TRUE);
        return n - 1;
    }
    case WM_USER + 5:                                      /* LB_RESETCONTENT */
        XW(h, L_CNT) = 0; XW(h, L_SEL) = 0xFFFF; XW(h, L_TOP) = 0; XW(h, L_PUSED) = 2;
        lb_sb(h);
        InvalidateRect(h, 0, TRUE);
        return 0;
    case WM_USER + 6: {                                    /* LB_SETSEL(sel, index) */
        int i = (int)lp, k;
        if (i == -1) { for (k = 0; k < (int)XW(h, L_CNT); k++) lbi(h)[k].sel = wp; }
        else if (i >= 0 && i < (int)XW(h, L_CNT)) { lbi(h)[i].sel = wp; if (wp) XW(h, L_CAR) = i; }
        else return -1;
        lb_redraw(h);
        return 0;
    }
    case WM_USER + 7:                                      /* LB_SETCURSEL */
        if ((int)wp >= (int)XW(h, L_CNT) && wp != 0xFFFF) return -1;
        lb_set_cur(h, (int)wp, FALSE);
        return wp == 0xFFFF ? -1 : (int)wp;
    case WM_USER + 8: {                                    /* LB_GETSEL */
        int i = (int)wp;
        if (i < 0 || i >= (int)XW(h, L_CNT)) return -1;
        return (STYLE(h) & (LBS_MULTI | LBS_EXTSEL)) ? lbi(h)[i].sel : i == (int)XW(h, L_SEL);
    }
    case WM_USER + 9:  return XW(h, L_SEL) == 0xFFFF ? -1L : (LONG)XW(h, L_SEL);   /* LB_GETCURSEL */
    case WM_USER + 10: {                                   /* LB_GETTEXT */
        int i = (int)wp;
        if (i < 0 || i >= (int)XW(h, L_CNT)) return -1;
        if (!lb_str(h)) { *(DWORD __far *)lp = lbi(h)[i].data; return 4; }
        ustrcpy((LPSTR)lp, lb_text(h, i));
        return ustrlen((LPSTR)lp);
    }
    case WM_USER + 11: {                                   /* LB_GETTEXTLEN */
        int i = (int)wp;
        if (i < 0 || i >= (int)XW(h, L_CNT)) return -1;
        return lb_str(h) ? ustrlen(lb_text(h, i)) : 4;
    }
    case WM_USER + 12: return XW(h, L_CNT);               /* LB_GETCOUNT */
    case WM_USER + 13: {                                   /* LB_SELECTSTRING */
        int i = lb_find(h, (int)wp, (LPCSTR)lp, FALSE);
        if (i >= 0) lb_set_cur(h, i, FALSE);
        return i;
    }
    case WM_USER + 15: return XW(h, L_TOP);               /* LB_GETTOPINDEX */
    case WM_USER + 16: return lb_find(h, (int)wp, (LPCSTR)lp, FALSE);   /* LB_FINDSTRING */
    case WM_USER + 17: {                                   /* LB_GETSELCOUNT */
        int k, c = 0;
        if (!(STYLE(h) & (LBS_MULTI | LBS_EXTSEL))) return -1;
        for (k = 0; k < (int)XW(h, L_CNT); k++) if (lbi(h)[k].sel) c++;
        return c;
    }
    case WM_USER + 18: {                                   /* LB_GETSELITEMS */
        int k, c = 0;
        for (k = 0; k < (int)XW(h, L_CNT) && c < (int)wp; k++) if (lbi(h)[k].sel) ((int __far *)lp)[c++] = k;
        return c;
    }
    case WM_USER + 19: case WM_USER + 21: case WM_USER + 22: return 1;   /* SETTABSTOPS, H.EXTENT, COLUMNWIDTH */
    case WM_USER + 20: return 0;
    case WM_USER + 24: {                                   /* LB_SETTOPINDEX */
        int t = (int)wp;
        if (t < 0 || t >= (int)XW(h, L_CNT)) return -1;
        XW(h, L_TOP) = t; lb_sb(h); lb_redraw(h);
        return 0;
    }
    case WM_USER + 25: {                                   /* LB_GETITEMRECT */
        LPRECT r = (LPRECT)lp;
        int ih = XW(h, L_IH);
        GetClientRect(h, r);
        r->top = ((int)wp - (int)XW(h, L_TOP)) * ih;
        r->bottom = r->top + ih;
        return 1;
    }
    case WM_USER + 26: {                                   /* LB_GETITEMDATA */
        int i = (int)wp;
        return (i >= 0 && i < (int)XW(h, L_CNT)) ? (LONG)lbi(h)[i].data : -1L;
    }
    case WM_USER + 27: {                                   /* LB_SETITEMDATA */
        int i = (int)wp;
        if (i < 0 || i >= (int)XW(h, L_CNT)) return -1;
        lbi(h)[i].data = (DWORD)lp;
        return 1;
    }
    case WM_USER + 28: {                                   /* LB_SELITEMRANGE */
        int k;
        for (k = (int)LOWORD(lp); k <= (int)HIWORD(lp) && k < (int)XW(h, L_CNT); k++) lbi(h)[k].sel = wp;
        lb_redraw(h);
        return 0;
    }
    case WM_USER + 31: XW(h, L_CAR) = wp; lb_redraw(h); return 0;   /* LB_SETCARETINDEX */
    case WM_USER + 32: return XW(h, L_CAR);                         /* LB_GETCARETINDEX */
    case WM_USER + 33: XW(h, L_IH) = LOWORD(lp) ? LOWORD(lp) : 16; lb_sb(h); InvalidateRect(h, 0, TRUE); return 0;
    case WM_USER + 34: return XW(h, L_IH);
    case WM_USER + 35: return lb_find(h, (int)wp, (LPCSTR)lp, TRUE);   /* LB_FINDSTRINGEXACT */
    }
    return common(h, m, wp, lp);
}

/* ============================================================
 * COMBOBOX (extra: 0 font, 2 lista (popup LISTBOX), 4 edycja, 6 rozwinieta,
 *           8 wysokosc listy)
 * ============================================================ */
#define C_LB    2
#define C_ED    4
#define C_DROP  6
#define C_LH    8

static void cb_show(HWND h, BOOL show)
{
    HWND lb = XW(h, C_LB);
    if (!lb || (BOOL)XW(h, C_DROP) == show) return;
    XW(h, C_DROP) = show;
    if (show) {
        WND __far *w = WP(h);
        int n = (int)SendMessage(lb, WM_USER + 12, 0, 0), ih = (int)SendMessage(lb, WM_USER + 34, 0, 0);
        int hh = (n < 8 ? (n ? n : 1) : 8) * ih + 2, y = w->wr.top + 22;
        send_msg(w->parent, WM_COMMAND, w->id, MAKELONG(h, 7));   /* CBN_DROPDOWN */
        if (y + hh > SCR_H) y = w->wr.top - hh;
        SetWindowPos(lb, 0xFFFF, w->wr.left, y, w->wr.right - w->wr.left, hh, 0x0040 | 0x0010);
        SetCapture(lb);
    } else {
        ShowWindow(lb, 0);
        if (GetCapture() == lb) ReleaseCapture();
        send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 8));   /* CBN_CLOSEUP */
    }
}

static void cb_sync_text(HWND h)
{
    int i = (int)SendMessage(XW(h, C_LB), WM_USER + 9, 0, 0);
    char t[MAX_TITLE];
    t[0] = 0;
    if (i >= 0 && (!(STYLE(h) & 0x30) || (STYLE(h) & 0x200))) SendMessage(XW(h, C_LB), WM_USER + 10, i, (LONG)(LPSTR)(char __far *)t);
    if (XW(h, C_ED)) SetWindowText(XW(h, C_ED), (LPCSTR)(char __far *)t);
    else DefWindowProc(h, WM_SETTEXT, 0, (LONG)(LPCSTR)(char __far *)t);
    InvalidateRect(h, 0, FALSE);
}

static void cb_paint(HWND h, HDC dc)
{
    RECT r, b;
    HBRUSH br = ctl_dc(h, dc, 2);
    GetClientRect(h, (LPRECT)&r);
    if (XW(h, C_ED)) {                                   /* CBS_DROPDOWN: ramka pola edycji, przerwa, przycisk */
        fill_color(dc, r.left, r.top, r.right - 18, r.bottom, 0);
        FillRect(dc, (LPRECT)&r, br);
        fill_color(dc, r.left, r.top, r.right - 18, r.top + 1, 0);
        fill_color(dc, r.left, r.bottom - 1, r.right - 18, r.bottom, 0);
        fill_color(dc, r.left, r.top, r.left + 1, r.bottom, 0);
        fill_color(dc, r.right - 19, r.top, r.right - 18, r.bottom, 0);
        fill_color(dc, r.right - 18, r.top, r.right - 17, r.bottom, syscolor(COLOR_BTNFACE));
        fill_color(dc, r.right - 17, r.top, r.right, r.bottom, 0);
    } else fill_color(dc, r.left, r.top, r.right, r.bottom, 0);
    SetRect((LPRECT)&b, r.right - 17, r.top, r.right, r.bottom);
    fill_color(dc, b.left, b.top, b.right, b.bottom, syscolor(COLOR_BTNFACE));
    draw_3d(dc, b.left + 1, b.top + 1, b.right - 1, b.bottom - 1, syscolor(COLOR_BTNHIGHLIGHT), syscolor(COLOR_BTNSHADOW));
    {
        int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2, i;
        for (i = 0; i < 4; i++) fill_color(dc, cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i, 0);
    }
    if (!XW(h, C_ED)) {
        RECT t;
        int sel = (int)SendMessage(XW(h, C_LB), WM_USER + 9, 0, 0);
        SetRect((LPRECT)&t, 1, 1, r.right - 18, r.bottom - 1);
        if (STYLE(h) & 0x30) {                           /* owner-draw: element w polu */
            struct { WORD type, id, item, action, state; HWND hw; HDC dc; RECT rc; DWORD data; } di;
            FillRect(dc, (LPRECT)&t, br);
            if (sel >= 0) {
                di.type = 3; di.id = WP(h)->id; di.item = sel; di.action = 1;
                di.state = GetFocus() == h ? 0x11 : 0; di.hw = h; di.dc = dc; di.rc = t;
                di.data = SendMessage(XW(h, C_LB), WM_USER + 26, sel, 0);
                send_msg(WP(h)->parent, WM_DRAWITEM, di.id, (LONG)(LPVOID)&di);
            }
            return;
        }
        if (GetFocus() == h) fill_color(dc, t.left, t.top, t.right, t.bottom, syscolor(COLOR_HIGHLIGHT));
        else FillRect(dc, (LPRECT)&t, br);
        SetBkMode(dc, 1);
        SetTextColor(dc, syscolor(GetFocus() == h ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawText(dc, (LPCSTR)WP(h)->text, -1, (LPRECT)&t, 0x0004 | 0x0020 | 0x0800);
    }
}

static LRESULT __far __pascal ComboProc(HWND h, WORD m, WORD wp, LONG lp)
{
    if (m >= WM_USER && m <= WM_USER + 24 && WP(h) && XW(h, C_LB)) {
        HWND lb = XW(h, C_LB);
        static const WORD map[25] = { 0, 0, 0, WM_USER + 1, WM_USER + 3, 0, WM_USER + 12, WM_USER + 9,
            WM_USER + 10, WM_USER + 11, WM_USER + 2, WM_USER + 5, WM_USER + 16, WM_USER + 13, WM_USER + 7,
            0, WM_USER + 26, WM_USER + 27, 0, WM_USER + 33, WM_USER + 34, 0, 0, 0, WM_USER + 35 };
        WORD k = m - WM_USER;
        LRESULT r;
        switch (k) {
        case 0: return XW(h, C_ED) ? SendMessage(XW(h, C_ED), WM_USER, 0, 0) : 0;   /* CB_GETEDITSEL */
        case 1: if (XW(h, C_ED)) SendMessage(XW(h, C_ED), WM_USER + 21, wp, 0); return 1;
        case 2: if (XW(h, C_ED)) SendMessage(XW(h, C_ED), WM_USER + 1, 0, lp); return 1;
        case 15: cb_show(h, wp != 0); return 1;                       /* CB_SHOWDROPDOWN */
        case 18: { WND __far *w = WP(h); *(LPRECT)lp = w->wr; ((LPRECT)lp)->bottom += XW(h, C_LH); return 1; }
        case 23: return XW(h, C_DROP);                                /* CB_GETDROPPEDSTATE */
        case 21: case 22: return 0;
        }
        if (!map[k]) return 0;
        r = SendMessage(lb, map[k], wp, lp);
        if (k == 14 || k == 13 || k == 11) cb_sync_text(h);           /* SETCURSEL, SELECTSTRING, RESET */
        return r;
    }
    switch (m) {
    case WM_NCCREATE:                                    /* paski przewijania naleza do listy */
        STYLE(h) &= ~(WS_VSCROLL | WS_HSCROLL);
        return DefWindowProc(h, m, wp, lp);
    case WM_CREATE: {
        WND __far *w = WP(h);
        int ch = w->wr.bottom - w->wr.top;
        DWORD lst = WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | (w->style & (LBS_ODFIXED | LBS_ODVAR));
        if (w->style & 0x0100) lst |= LBS_SORT;
        if (w->style & 0x0200) lst |= LBS_HASSTR;
        XW(h, C_LH) = ch > 24 ? ch - 22 : 100;
        XW(h, C_LB) = CreateWindowEx(WS_EX_TOPMOST, "LISTBOX", "", lst, 0, 0, 100, 100, h, 0, w->hinst, 0);
        if (XW(h, C_LB)) XW(XW(h, C_LB), L_NOTE) = h;
        if ((w->style & 3) != 3) {                       /* CBS_SIMPLE / DROPDOWN: pole edycji */
            XW(h, C_ED) = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | 0x0080, 2, 2,
                                         w->wr.right - w->wr.left - 21, 18, h, 1001, w->hinst, 0);
        }
        SetWindowPos(h, 0, 0, 0, w->wr.right - w->wr.left, 22, 0x0002 | 0x0004 | 0x0010);
        return 0;
    }
    case WM_NCDESTROY:
        if (XW(h, C_LB) && WP(XW(h, C_LB))) DestroyWindow(XW(h, C_LB));
        return DefWindowProc(h, m, wp, lp);
    case WM_SETFONT:
        XW(h, 0) = wp;
        if (XW(h, C_LB)) SendMessage(XW(h, C_LB), WM_SETFONT, wp, 0);
        if (XW(h, C_ED)) SendMessage(XW(h, C_ED), WM_SETFONT, wp, 0);
        return 0;
    case WM_PAINT: { PS ps; HDC dc = BeginPaint(h, (LPVOID)&ps); cb_paint(h, dc); EndPaint(h, (LPVOID)&ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return 0x0001 | 0x0004;
    case WM_SETFOCUS:
        if (XW(h, C_ED)) SetFocus(XW(h, C_ED));
        InvalidateRect(h, 0, FALSE);
        send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 3));
        return 0;
    case WM_KILLFOCUS:
        InvalidateRect(h, 0, FALSE);
        send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 4));
        return 0;
    case WM_LBUTTONDOWN:
        if (GetFocus() != h && !XW(h, C_ED)) SetFocus(h);
        cb_show(h, !XW(h, C_DROP));
        return 0;
    case WM_KEYDOWN:
        if (wp == 0x73 || ((GetKeyState(0x12) & 0x8000) && wp == 0x28)) { cb_show(h, !XW(h, C_DROP)); return 0; }
        if (wp == 0x26 || wp == 0x28) {
            int c = (int)SendMessage(XW(h, C_LB), WM_USER + 9, 0, 0) + (wp == 0x28 ? 1 : -1);
            if (c >= 0 && c < (int)SendMessage(XW(h, C_LB), WM_USER + 12, 0, 0)) {
                SendMessage(XW(h, C_LB), WM_USER + 7, c, 0);
                cb_sync_text(h);
                send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 1));
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(lp) == XW(h, C_LB) || HIWORD(lp) == 1) {
            if (HIWORD(lp) == 1 && (WP(XW(h, C_LB))->style & WS_VISIBLE) && !(mouse_btn() & 1)) {   /* LBN_SELCHANGE po puszczeniu */
                cb_show(h, FALSE);
                cb_sync_text(h);
                send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 1));   /* CBN_SELCHANGE */
                send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 9));   /* CBN_SELENDOK */
            }
            return 0;
        }
        if (wp == 1001 && HIWORD(lp) == 0x300)
            send_msg(WP(h)->parent, WM_COMMAND, WP(h)->id, MAKELONG(h, 5));       /* CBN_EDITCHANGE */
        return 0;
    case WM_SETTEXT:
        if (XW(h, C_ED)) return SendMessage(XW(h, C_ED), m, wp, lp);
        DefWindowProc(h, m, wp, lp);
        InvalidateRect(h, 0, FALSE);
        return 1;
    case WM_GETTEXT: case WM_GETTEXTLENGTH:
        if (XW(h, C_ED)) return SendMessage(XW(h, C_ED), m, wp, lp);
        break;
    }
    return common(h, m, wp, lp);
}

/* Popup listy combo: klik poza lista zamyka, puszczenie wybiera */
static LRESULT __far __pascal ComboListProc(HWND h, WORD m, WORD wp, LONG lp)
{
    if (XW(h, L_NOTE)) {
        HWND cb = XW(h, L_NOTE);
        if (m == WM_LBUTTONDOWN) {
            RECT r;
            POINT p;
            GetClientRect(h, (LPRECT)&r);
            p.x = (int)LOWORD(lp); p.y = (int)HIWORD(lp);
            if (!PtInRect((LPRECT)&r, p)) { cb_show(cb, FALSE); return 0; }
        }
        if (m == WM_LBUTTONUP) {
            RECT r;
            POINT p;
            GetClientRect(h, (LPRECT)&r);
            p.x = (int)LOWORD(lp); p.y = (int)HIWORD(lp);
            if (PtInRect((LPRECT)&r, p)) {
                cb_show(cb, FALSE);
                cb_sync_text(cb);
                send_msg(WP(cb)->parent, WM_COMMAND, WP(cb)->id, MAKELONG(cb, 1));
                send_msg(WP(cb)->parent, WM_COMMAND, WP(cb)->id, MAKELONG(cb, 9));
            }
            return 0;
        }
        if (m == WM_MOUSEMOVE && (STYLE(h) & WS_VISIBLE)) {
            int i = lb_hit(h, (int)HIWORD(lp));
            if (i >= 0 && i != (int)XW(h, L_SEL) && (int)HIWORD(lp) >= 0) { XW(h, L_SEL) = i; XW(h, L_CAR) = i; lb_redraw(h); }
            return 0;
        }
        if (m == WM_MOUSEACTIVATE) return 3;
    }
    return ListBoxProc(h, m, wp, lp);
}

/* ============================================================
 * SCROLLBAR (extra: 0 font, 2 pozycja, 4 min, 6 max)
 * ============================================================ */
static LRESULT __far __pascal ScrollBarProc(HWND h, WORD m, WORD wp, LONG lp)
{
    BOOL vert = (STYLE(h) & 1) != 0;
    switch (m) {
    case WM_CREATE: XW(h, 6) = 100; return 0;
    case WM_PAINT: {
        PS ps;
        HDC dc = BeginPaint(h, (LPVOID)&ps);
        RECT r;
        GetClientRect(h, (LPRECT)&r);
        sb_paint(dc, (LPRECT)&r, vert, (int)XW(h, 2), (int)XW(h, 4), (int)XW(h, 6));
        EndPaint(h, (LPVOID)&ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT r;
        int p, len, a = 16, track, pos, range = (int)XW(h, 6) - (int)XW(h, 4), code;
        WORD msg = vert ? WM_VSCROLL : WM_HSCROLL;
        GetClientRect(h, (LPRECT)&r);
        len = vert ? r.bottom : r.right;
        p = vert ? (int)HIWORD(lp) : (int)LOWORD(lp);
        track = len - 2 * a;
        pos = range > 0 && track > a ? (int)((long)((int)XW(h, 2) - (int)XW(h, 4)) * (track - a) / range) : 0;
        code = p < a ? 0 : p >= len - a ? 1 : p < a + pos ? 2 : p >= 2 * a + pos ? 3 : 5;
        if (code == 5) {
            MSG mm;
            int off = p - a - pos, np = XW(h, 2);
            while (modal_next((LPMSG)&mm)) {
                int c;
                POINT pt;
                pt.x = mouse_x(); pt.y = mouse_y();
                ScreenToClient(h, (LPPOINT)&pt);
                c = (vert ? pt.y : pt.x) - a - off;
                if (track > a) {
                    if (c < 0) c = 0;
                    if (c > track - a) c = track - a;
                    np = (int)XW(h, 4) + (int)((long)c * range / (track - a));
                    send_msg(WP(h)->parent, msg, 5, MAKELONG(np, h));
                }
                if (!(mouse_btn() & 1) || !WP(h)) break;
            }
            if (WP(h)) send_msg(WP(h)->parent, msg, 4, MAKELONG(np, h));
        } else send_msg(WP(h)->parent, msg, code, MAKELONG(0, h));
        if (WP(h)) send_msg(WP(h)->parent, msg, 8, MAKELONG(0, h));
        return 0;
    }
    case WM_USER + 1: {                                    /* SetScrollPos(SB_CTL) */
        int o = XW(h, 2), p = (int)wp;
        if (p < (int)XW(h, 4)) p = XW(h, 4);
        if (p > (int)XW(h, 6)) p = XW(h, 6);
        XW(h, 2) = p;
        if (lp) InvalidateRect(h, 0, FALSE);
        return o;
    }
    case WM_USER + 2: return XW(h, 2);
    case WM_USER + 3: XW(h, 4) = wp; XW(h, 6) = LOWORD(lp); if (HIWORD(lp)) InvalidateRect(h, 0, FALSE); return 0;
    case WM_USER + 4: return MAKELONG(XW(h, 4), XW(h, 6));
    case WM_ERASEBKGND: return 1;
    }
    return common(h, m, wp, lp);
}

/* ============================================================
 * Schowek
 * ============================================================ */
#define MAX_CLIP 8
static struct { WORD fmt; HANDLE h; } g_clip[MAX_CLIP];
static HWND g_clipopen, g_clipowner;

BOOL API OpenClipboard(HWND h)     { if (g_clipopen) return FALSE; g_clipopen = h ? h : 1; return TRUE; }
BOOL API CloseClipboard(void)      { g_clipopen = 0; return TRUE; }
HWND API GetOpenClipboardWindow(void) { return g_clipopen; }
HWND API GetClipboardOwner(void)   { return g_clipowner; }

BOOL API EmptyClipboard(void)
{
    int i;
    for (i = 0; i < MAX_CLIP; i++) if (g_clip[i].h) { GlobalFree(g_clip[i].h); g_clip[i].h = 0; g_clip[i].fmt = 0; }
    g_clipowner = g_clipopen;
    return TRUE;
}

HANDLE API SetClipboardData(WORD fmt, HANDLE h)
{
    int i;
    for (i = 0; i < MAX_CLIP; i++) if (g_clip[i].fmt == fmt) break;
    if (i == MAX_CLIP) for (i = 0; i < MAX_CLIP; i++) if (!g_clip[i].fmt) break;
    if (i == MAX_CLIP) return 0;
    g_clip[i].fmt = fmt; g_clip[i].h = h;
    return h;
}

HANDLE API GetClipboardData(WORD fmt)
{
    int i;
    for (i = 0; i < MAX_CLIP; i++) if (g_clip[i].fmt == fmt && g_clip[i].h) return g_clip[i].h;
    return 0;
}

BOOL API IsClipboardFormatAvailable(WORD fmt) { return GetClipboardData(fmt) != 0; }

int API CountClipboardFormats(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_CLIP; i++) if (g_clip[i].h) n++;
    return n;
}

WORD API EnumClipboardFormats(WORD fmt)
{
    int i, found = fmt == 0;
    for (i = 0; i < MAX_CLIP; i++) {
        if (!g_clip[i].h) continue;
        if (found) return g_clip[i].fmt;
        if (g_clip[i].fmt == fmt) found = 1;
    }
    return 0;
}

WORD API RegisterClipboardFormat(LPCSTR n) { extern WORD user_atom(LPCSTR); return user_atom(n); }
int  API GetClipboardFormatName(WORD f, LPSTR b, int n)
{
    extern LPCSTR user_atom_name(WORD);
    LPCSTR s = user_atom_name(f);
    if (!s || n <= 0) return 0;
    ustrncpy(b, s, n);
    return ustrlen(b);
}
HWND API SetClipboardViewer(HWND h)     { (void)h; return 0; }
HWND API GetClipboardViewer(void)       { return 0; }
BOOL API ChangeClipboardChain(HWND a, HWND b) { (void)a; (void)b; return FALSE; }
int  API GetPriorityClipboardFormat(WORD __far *l, int n)
{
    int i;
    for (i = 0; i < n; i++) if (GetClipboardData(l[i])) return l[i];
    return CountClipboardFormats() ? -1 : 0;
}

/* ============================================================
 * Rejestracja
 * ============================================================ */
void ctl_register_classes(void)
{
    WORD arrow = LoadCursor(0, (LPCSTR)MAKELONG(32512, 0));
    WORD ibeam = LoadCursor(0, (LPCSTR)MAKELONG(32513, 0));
    register_system_class("BUTTON", 0x0008 | 0x0003, (WNDPROC)ButtonProc, 8, arrow, 0);
    register_system_class("STATIC", 0, (WNDPROC)StaticProc, 8, arrow, 0);
    register_system_class("EDIT", 0x0008, (WNDPROC)EditProc, 24, ibeam, 0);
    register_system_class("LISTBOX", 0x0008, (WNDPROC)ComboListProc, 24, arrow, 0);
    register_system_class("COMBOBOX", 0x0008, (WNDPROC)ComboProc, 12, arrow, 0);
    register_system_class("SCROLLBAR", 0x0008, (WNDPROC)ScrollBarProc, 8, arrow, 0);
    menu_register();
    dlg_register();
}
