/* upaint.c - USER: DC okien, malowanie, uniewaznianie, przewijanie, karetka,
 * rysowanie pomocnicze, obszar nieklienta, DefWindowProc, inicjalizacja. */
#include "user.h"

extern BOOL modal_next(LPMSG m);
extern int  mouse_x(void), mouse_y(void);
extern WORD mouse_btn(void);
extern void wnd_posdone(HWND h, LPVOID wp);
extern HMENU API GetSystemMenu(HWND, BOOL);
extern int  menu_track_popup(HMENU m, int x, int y, HWND owner, BOOL sys);
extern void misc_init(void);
extern ATOM register_system_class(LPCSTR, WORD, WNDPROC, int, WORD, WORD);
void API MessageBeep(WORD);
HCURSOR API SetCursor(HCURSOR);
HCURSOR API LoadCursor(WORD, LPCSTR);
void API GetClientRect(HWND, LPRECT);
void API OffsetRect(LPRECT, int, int);
void API InflateRect(LPRECT, int, int);
BOOL API IsIconic(HWND);
BOOL API PtInRect(const RECT __far *, POINT);

#define ROP_PATCOPY   0x00F00021UL
#define ROP_DSTINVERT 0x00550009UL

/* ============================================================
 * DC
 * ============================================================ */
static RECT g_vr[64];

/* Zywe DC okien (GetDC bez ReleaseDC, np. CS_OWNDC): po zmianie polozenia,
 * widocznosci lub kolejnosci Z okien ich region widoczny jest przeliczany
 * (dce_update), jak cache DCE w Windows. */
#define MAX_DCE 32
static struct { HDC dc; HWND h; BYTE client; } g_dce[MAX_DCE];

static HDC dce_add(HDC dc, HWND h, BOOL client)
{
    int i;
    if (!dc || !h) return dc;
    for (i = 0; i < MAX_DCE; i++)
        if (!g_dce[i].dc) { g_dce[i].dc = dc; g_dce[i].h = h; g_dce[i].client = (BYTE)client; break; }
    return dc;
}

void dce_update(void)
{
    int i, n;
    for (i = 0; i < MAX_DCE; i++) {
        WND __far *w;
        if (!g_dce[i].dc) continue;
        if (!(w = WP(g_dce[i].h))) { DeleteDC(g_dce[i].dc); g_dce[i].dc = 0; continue; }   /* okno zniszczone */
        wnd_vis_rects(g_dce[i].h, g_dce[i].client, (LPRECT)g_vr, (int __far *)&n, 64);
        if (g_dce[i].client) GdiSetDCVis(g_dce[i].dc, w->cr.left, w->cr.top, (LPRECT)g_vr, n);
        else GdiSetDCVis(g_dce[i].dc, w->wr.left, w->wr.top, (LPRECT)g_vr, n);
    }
}

HDC API GetDC(HWND h)
{
    WND __far *w;
    int n;
    user_init();
    if (!h) {
        RECT s;
        s.left = 0; s.top = 0; s.right = SCR_W; s.bottom = SCR_H;
        return GdiCreateWindowDC(0, 0, (LPRECT)&s, 1, 0);
    }
    w = WP(h);
    if (!w) return 0;
    wnd_vis_rects(h, TRUE, (LPRECT)g_vr, (int __far *)&n, 64);
    return dce_add(GdiCreateWindowDC(w->cr.left, w->cr.top, (LPRECT)g_vr, n, h), h, TRUE);
}

HDC API GetWindowDC(HWND h)
{
    WND __far *w;
    int n;
    user_init();
    if (!h) return GetDC(0);
    w = WP(h);
    if (!w) return 0;
    wnd_vis_rects(h, FALSE, (LPRECT)g_vr, (int __far *)&n, 64);
    return dce_add(GdiCreateWindowDC(w->wr.left, w->wr.top, (LPRECT)g_vr, n, h), h, FALSE);
}

HDC API GetDCEx(HWND h, HRGN rgn, DWORD fl)
{
    (void)rgn;
    return (fl & 1) ? GetWindowDC(h) : GetDC(h);
}

int API ReleaseDC(HWND h, HDC dc)
{
    int i;
    (void)h;
    for (i = 0; i < MAX_DCE; i++) if (g_dce[i].dc == dc) g_dce[i].dc = 0;
    return DeleteDC(dc);
}
HWND API WindowFromDC(HDC dc)     { return GdiDCWindow(dc); }

/* ============================================================
 * Malowanie
 * ============================================================ */
typedef struct { HDC hdc; BOOL fErase; RECT rc; BOOL fRestore, fIncUpdate; BYTE res[16]; } PAINTSTRUCT;

static HWND find_paint(HWND h, HWND filt)
{
    WND __far *w = WP(h);
    HWND c, list[64];
    int n = 0, i;
    if (!w || !(w->style & WS_VISIBLE) || (w->flags & WF_MINIMIZED)) return 0;
    if (w->flags & WF_NCPAINT) {
        w->flags &= ~WF_NCPAINT;
        if (h != HDESKTOP) send_msg(h, WM_NCPAINT, 1, 0);
        if (!(w = WP(h))) return 0;
    }
    if (w->upd && (!filt || filt == h)) return h;
    for (c = w->child; c && n < 64; c = WP(c)->next) list[n++] = c;
    for (i = n - 1; i >= 0; i--) {                           /* od dolu do gory */
        HWND r = WP(list[i]) ? find_paint(list[i], filt) : 0;
        if (r) return r;
    }
    return 0;
}

HWND paint_pending(HWND filt) { return find_paint(HDESKTOP, filt); }

HDC API BeginPaint(HWND h, PAINTSTRUCT __far *ps)
{
    WND __far *w = WP(h);
    HDC dc;
    static RECT ur[64];
    int n = 0;
    if (!w) return 0;
    caret_on_paint(h, TRUE);
    if (w->flags & WF_NCPAINT) { w->flags &= ~WF_NCPAINT; send_msg(h, WM_NCPAINT, 1, 0); if (!(w = WP(h))) return 0; }
    dc = GetDC(h);
    umemset(ps, 0, sizeof(PAINTSTRUCT));
    ps->hdc = dc;
    if (w->upd) {
        RECT b;
        n = GdiGetRgnRects(w->upd, (LPRECT)ur, 64);
        GetRgnBox(w->upd, (LPRECT)&b);
        ps->rc.left = b.left - w->cr.left; ps->rc.right = b.right - w->cr.left;
        ps->rc.top = b.top - w->cr.top;    ps->rc.bottom = b.bottom - w->cr.top;
        DeleteObject(w->upd);
        w->upd = 0;
    }
    GdiIntersectVis(dc, (LPRECT)ur, n);
    if (w->flags & WF_ERASE) {
        w->flags &= ~WF_ERASE;
        ps->fErase = !send_msg(h, WM_ERASEBKGND, dc, 0);
    }
    return dc;
}

void API EndPaint(HWND h, PAINTSTRUCT __far *ps)
{
    ReleaseDC(h, ps->hdc);
    caret_on_paint(h, FALSE);
}

static void to_screen(HWND h, LPRECT r)
{
    WND __far *w = WP(h);
    if (w) OffsetRect(r, w->cr.left, w->cr.top);
}

void API InvalidateRect(HWND h, LPRECT r, BOOL erase)
{
    WND __far *w;
    RECT s;
    user_init();
    if (!h) { s.left = 0; s.top = 0; s.right = SCR_W; s.bottom = SCR_H; wnd_invalidate_screen((RECT __far *)&s, 0); return; }
    w = WP(h);
    if (!w) return;
    if (r) { s = *r; to_screen(h, (LPRECT)&s); }
    wnd_invalidate(h, r ? (RECT __far *)&s : 0, erase, !(w->style & WS_CLIPCHILDREN));
}

void API InvalidateRgn(HWND h, HRGN rgn, BOOL erase)
{
    static RECT rr[64];
    int n, i;
    if (!rgn) { InvalidateRect(h, 0, erase); return; }
    n = GdiGetRgnRects(rgn, (LPRECT)rr, 64);
    for (i = 0; i < n; i++) InvalidateRect(h, (LPRECT)&rr[i], erase);
}

void API ValidateRect(HWND h, LPRECT r)
{
    WND __far *w = WP(h);
    RECT s;
    WORD t;
    if (!w || !w->upd) return;
    if (!r) { DeleteObject(w->upd); w->upd = 0; return; }
    s = *r;
    to_screen(h, (LPRECT)&s);
    t = CreateRectRgn(s.left, s.top, s.right, s.bottom);
    if (CombineRgn(w->upd, w->upd, t, 4) == 1) { DeleteObject(w->upd); w->upd = 0; }
    DeleteObject(t);
}

void API ValidateRgn(HWND h, HRGN rgn)
{
    static RECT rr[64];
    int n, i;
    if (!rgn) { ValidateRect(h, 0); return; }
    n = GdiGetRgnRects(rgn, (LPRECT)rr, 64);
    for (i = 0; i < n; i++) ValidateRect(h, (LPRECT)&rr[i]);
}

void API UpdateWindow(HWND h)
{
    WND __far *w = WP(h);
    if (!w || !wnd_visible(h)) return;
    if (w->flags & WF_NCPAINT) { w->flags &= ~WF_NCPAINT; send_msg(h, WM_NCPAINT, 1, 0); }
    if ((w = WP(h)) != 0 && w->upd) send_msg(h, WM_PAINT, 0, 0);
    if ((w = WP(h)) != 0 && w->upd) { DeleteObject(w->upd); w->upd = 0; }
}

BOOL API RedrawWindow(HWND h, LPRECT r, HRGN rgn, WORD fl)
{
    WND __far *w;
    if (!h) h = HDESKTOP;
    w = WP(h);
    if (!w) return FALSE;
    if (fl & 0x0001) {                                       /* RDW_INVALIDATE */
        if (fl & 0x0400) w->flags |= WF_NCPAINT;            /* RDW_FRAME */
        if (rgn) InvalidateRgn(h, rgn, (fl & 0x0004) != 0);
        else if (h == HDESKTOP) InvalidateRect(0, r, TRUE);
        else {
            RECT s;
            if (r) { s = *r; to_screen(h, (LPRECT)&s); }
            wnd_invalidate(h, r ? (RECT __far *)&s : 0, (fl & 0x0004) != 0, (fl & 0x0080) != 0);
        }
    }
    if (fl & 0x0008) { if (rgn) ValidateRgn(h, rgn); else ValidateRect(h, r); }
    if (fl & 0x0300) UpdateWindow(h);                        /* UPDATENOW / ERASENOW */
    return TRUE;
}

BOOL API GetUpdateRect(HWND h, LPRECT r, BOOL erase)
{
    WND __far *w = WP(h);
    RECT b;
    if (!w || !w->upd) { if (r) SetRectEmpty(r); return FALSE; }
    GetRgnBox(w->upd, (LPRECT)&b);
    if (r) { *r = b; OffsetRect(r, -w->cr.left, -w->cr.top); }
    if (erase && (w->flags & WF_ERASE)) {
        HDC dc = GetDC(h);
        w->flags &= ~WF_ERASE;
        send_msg(h, WM_ERASEBKGND, dc, 0);
        ReleaseDC(h, dc);
    }
    return TRUE;
}

int API GetUpdateRgn(HWND h, HRGN rgn, BOOL erase)
{
    WND __far *w = WP(h);
    (void)erase;
    if (!w || !w->upd) { SetRectRgn(rgn, 0, 0, 0, 0); return 1; }
    CombineRgn(rgn, w->upd, 0, 5);
    OffsetRgn(rgn, -w->cr.left, -w->cr.top);
    return 2;
}

int API ExcludeUpdateRgn(HDC dc, HWND h)
{
    WND __far *w = WP(h);
    static RECT rr[64];
    int n, i;
    if (!w || !w->upd) return 1;
    n = GdiGetRgnRects(w->upd, (LPRECT)rr, 64);
    for (i = 0; i < n; i++)
        ExcludeClipRect(dc, rr[i].left - w->cr.left, rr[i].top - w->cr.top,
                        rr[i].right - w->cr.left, rr[i].bottom - w->cr.top);
    return 2;
}

/* ============================================================
 * Przewijanie
 * ============================================================ */
void API ScrollWindow(HWND h, int dx, int dy, LPRECT scroll, LPRECT clip)
{
    WND __far *w = WP(h);
    RECT r, c;
    HDC dc;
    if (!w) return;
    GetClientRect(h, (LPRECT)&r);
    if (scroll) r = *scroll;
    c = r;
    if (clip) rect_and((LPRECT)&c, (LPRECT)&c, clip);
    dc = GetDC(h);
    IntersectClipRect(dc, c.left, c.top, c.right, c.bottom);
    BitBlt(dc, r.left + dx, r.top + dy, r.right - r.left, r.bottom - r.top, dc, r.left, r.top, 0x00CC0020UL);
    ReleaseDC(h, dc);
    {   /* odsloniete pasy */
        RECT u = c;
        if (dx > 0) { u.right = c.left + dx; InvalidateRect(h, (LPRECT)&u, TRUE); }
        else if (dx < 0) { u.left = c.right + dx; InvalidateRect(h, (LPRECT)&u, TRUE); }
        u = c;
        if (dy > 0) { u.bottom = c.top + dy; InvalidateRect(h, (LPRECT)&u, TRUE); }
        else if (dy < 0) { u.top = c.bottom + dy; InvalidateRect(h, (LPRECT)&u, TRUE); }
    }
    if (!scroll) {                                            /* przesun okna potomne */
        HWND ch, next;
        for (ch = WP(h)->child; ch; ch = next) {
            WND __far *cw = WP(ch);
            next = cw->next;
            SetWindowPos(ch, 0, cw->wr.left - WP(h)->cr.left + dx, cw->wr.top - WP(h)->cr.top + dy, 0, 0,
                         0x0001 | 0x0004 | 0x0010);
        }
    }
}

BOOL API ScrollDC(HDC dc, int dx, int dy, LPRECT scroll, LPRECT clip, HRGN upd, LPRECT ru)
{
    RECT r = *scroll;
    (void)clip;
    BitBlt(dc, r.left + dx, r.top + dy, r.right - r.left, r.bottom - r.top, dc, r.left, r.top, 0x00CC0020UL);
    if (ru) {
        *ru = r;
        if (dx > 0) ru->right = r.left + dx; else if (dx < 0) ru->left = r.right + dx;
        if (dy > 0) ru->bottom = r.top + dy; else if (dy < 0) ru->top = r.bottom + dy;
    }
    if (upd && ru) SetRectRgn(upd, ru->left, ru->top, ru->right, ru->bottom);
    return TRUE;
}

/* ============================================================
 * Karetka
 * ============================================================ */
static struct { HWND h; int w, hh, x, y, hide; BOOL on; DWORD last; WORD blink; } g_car = { 0, 0, 0, 0, 0, 1, 0, 0, 530 };

static void caret_xor(void)
{
    HDC dc;
    if (!g_car.h || !WP(g_car.h)) return;
    dc = GetDC(g_car.h);
    PatBlt(dc, g_car.x, g_car.y, g_car.w, g_car.hh, ROP_DSTINVERT);
    ReleaseDC(g_car.h, dc);
    g_car.on = !g_car.on;
    g_car.last = tick();
}

static void caret_off(void) { if (g_car.on) caret_xor(); }

void caret_blink(void)
{
    if (!g_car.h || g_car.hide > 0) return;
    if (tick() - g_car.last >= g_car.blink) caret_xor();
}

void caret_on_paint(HWND h, BOOL before)
{
    if (h != g_car.h) return;
    if (before) { caret_off(); g_car.hide++; }
    else if (g_car.hide > 0) g_car.hide--;
}

void API CreateCaret(HWND h, HBITMAP bm, int w, int hh)
{
    (void)bm;
    caret_off();
    g_car.h = h; g_car.w = w ? w : 1; g_car.hh = hh ? hh : 16;
    g_car.x = g_car.y = 0; g_car.hide = 1; g_car.on = FALSE;
}
void API DestroyCaret(void)         { caret_off(); g_car.h = 0; }
void API SetCaretPos(int x, int y)  { BOOL was = g_car.on; caret_off(); g_car.x = x; g_car.y = y; if (was) caret_xor(); }
void API GetCaretPos(LPPOINT p)     { p->x = g_car.x; p->y = g_car.y; }
void API HideCaret(HWND h)          { if (!h || h == g_car.h) { caret_off(); g_car.hide++; } }
void API ShowCaret(HWND h)          { if ((!h || h == g_car.h) && g_car.hide > 0) { g_car.hide--; if (!g_car.hide) caret_xor(); } }
void API SetCaretBlinkTime(WORD t)  { g_car.blink = t ? t : 530; }
WORD API GetCaretBlinkTime(void)    { return g_car.blink; }

/* ============================================================
 * Rysowanie pomocnicze
 * ============================================================ */
static HBRUSH brush_arg(HBRUSH b) { return (b && b <= NCOLORS) ? sysbrush(b - 1) : b; }

int API FillRect(HDC dc, LPRECT r, HBRUSH b)
{
    WORD old = SelectObject(dc, brush_arg(b));
    PatBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, ROP_PATCOPY);
    SelectObject(dc, old);
    return 1;
}

int API FrameRect(HDC dc, LPRECT r, HBRUSH b)
{
    WORD old = SelectObject(dc, brush_arg(b));
    int w = r->right - r->left, h = r->bottom - r->top;
    PatBlt(dc, r->left, r->top, w, 1, ROP_PATCOPY);
    PatBlt(dc, r->left, r->bottom - 1, w, 1, ROP_PATCOPY);
    PatBlt(dc, r->left, r->top, 1, h, ROP_PATCOPY);
    PatBlt(dc, r->right - 1, r->top, 1, h, ROP_PATCOPY);
    SelectObject(dc, old);
    return 1;
}

void API InvertRect(HDC dc, LPRECT r)
{
    PatBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, ROP_DSTINVERT);
}

void API DrawFocusRect(HDC dc, LPRECT r)
{
    int x, y;
    for (x = r->left; x < r->right; x += 2) {
        PatBlt(dc, x, r->top, 1, 1, ROP_DSTINVERT);
        PatBlt(dc, x, r->bottom - 1, 1, 1, ROP_DSTINVERT);
    }
    for (y = r->top + 2; y < r->bottom - 1; y += 2) {
        PatBlt(dc, r->left, y, 1, 1, ROP_DSTINVERT);
        PatBlt(dc, r->right - 1, y, 1, 1, ROP_DSTINVERT);
    }
}

void fill_color(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    WORD br = CreateSolidBrush(c), old = SelectObject(dc, br);
    PatBlt(dc, l, t, r - l, b - t, ROP_PATCOPY);
    SelectObject(dc, old);
    DeleteObject(br);
}

void draw_3d(HDC dc, int l, int t, int r, int b, COLORREF hi, COLORREF sh)
{
    fill_color(dc, l, t, r - 1, t + 1, hi);
    fill_color(dc, l, t, l + 1, b - 1, hi);
    fill_color(dc, l, b - 1, r, b, sh);
    fill_color(dc, r - 1, t, r, b, sh);
}

/* DrawText (ordinal 85) */
#define DT_CENTER     0x0001
#define DT_RIGHT      0x0002
#define DT_VCENTER    0x0004
#define DT_BOTTOM     0x0008
#define DT_WORDBREAK  0x0010
#define DT_SINGLELINE 0x0020
#define DT_EXPANDTABS 0x0040
#define DT_NOCLIP     0x0100
#define DT_CALCRECT   0x0400
#define DT_NOPREFIX   0x0800

static int text_w(HDC dc, LPCSTR s, int n, BOOL pfx)
{
    int w = 0, i, st = 0;
    if (!pfx) return LOWORD(GetTextExtent(dc, s, n));
    for (i = 0; i <= n; i++)
        if (i == n || s[i] == '&') {
            if (i > st) w += LOWORD(GetTextExtent(dc, s + st, i - st));
            st = i + 1;
            if (i + 1 < n && s[i + 1] == '&') { w += LOWORD(GetTextExtent(dc, "&", 1)); i++; st = i + 1; }
        }
    return w;
}

static void out_line(HDC dc, int x, int y, LPCSTR s, int n, BOOL pfx, int lh)
{
    int i, st = 0, cx = x;
    if (!pfx) { TextOut(dc, x, y, s, n); return; }
    for (i = 0; i <= n; i++) {
        if (i < n && s[i] != '&') continue;
        if (i > st) { TextOut(dc, cx, y, s + st, i - st); cx += LOWORD(GetTextExtent(dc, s + st, i - st)); }
        if (i < n) {
            if (i + 1 < n && s[i + 1] == '&') { TextOut(dc, cx, y, "&", 1); cx += LOWORD(GetTextExtent(dc, "&", 1)); i++; }
            else if (i + 1 < n) {                         /* podkreslenie nastepnego znaku */
                int cw = LOWORD(GetTextExtent(dc, s + i + 1, 1));
                MoveTo(dc, cx, y + lh - 1);
                {
                    extern int API GetTextColor(HDC);
                    WORD pen = CreatePen(0, 1, GetTextColor(dc)), op = SelectObject(dc, pen);
                    LineTo(dc, cx + cw, y + lh - 1);
                    SelectObject(dc, op);
                    DeleteObject(pen);
                }
            }
        }
        st = i + 1;
    }
}

int API DrawText(HDC dc, LPCSTR s, int n, LPRECT r, WORD fmt)
{
    struct { int tmHeight, a, b, c, ext; } tm;
    int lh, y, total = 0, maxw = 0, width = r->right - r->left;
    BOOL pfx = !(fmt & DT_NOPREFIX);
    LPCSTR p;
    if (n < 0) n = ustrlen(s);
    GetTextMetrics(dc, (LPVOID)&tm);
    lh = tm.tmHeight;
    /* pomiar linii */
    {
        static int ls[64], le[64];
        int nl = 0, i = 0;
        while (i < n && nl < 64) {
            int st = i, last_sp = -1, w;
            if (fmt & DT_SINGLELINE) { ls[nl] = 0; le[nl] = n; nl++; break; }
            while (i < n && s[i] != '\n' && s[i] != '\r') {
                if (s[i] == ' ') last_sp = i;
                if ((fmt & DT_WORDBREAK) && text_w(dc, s + st, i - st + 1, pfx) > width && i > st) {
                    if (last_sp > st) i = last_sp;
                    break;
                }
                i++;
            }
            ls[nl] = st; le[nl] = i; nl++;
            if (i < n && s[i] == '\r') i++;
            if (i < n && s[i] == '\n') i++;
            else if (i < n && s[i] == ' ') i++;
        }
        if (!nl) { ls[0] = 0; le[0] = 0; nl = 1; }
        total = nl * lh;
        for (i = 0; i < nl; i++) { int w = text_w(dc, s + ls[i], le[i] - ls[i], pfx); if (w > maxw) maxw = w; }
        if (fmt & DT_CALCRECT) {
            r->right = r->left + ((fmt & DT_WORDBREAK) ? (maxw < width ? maxw : width) : maxw);
            r->bottom = r->top + total;
            return total;
        }
        y = r->top;
        if ((fmt & DT_SINGLELINE) && (fmt & DT_VCENTER)) y = r->top + (r->bottom - r->top - lh) / 2;
        else if ((fmt & DT_SINGLELINE) && (fmt & DT_BOTTOM)) y = r->bottom - lh;
        if (!(fmt & DT_NOCLIP)) {
            extern int API SaveDC(HDC);
            SaveDC(dc);
            IntersectClipRect(dc, r->left, r->top, r->right, r->bottom);
        }
        for (i = 0; i < nl; i++, y += lh) {
            int w = text_w(dc, s + ls[i], le[i] - ls[i], pfx), x = r->left;
            if (fmt & DT_CENTER) x = r->left + (width - w) / 2;
            else if (fmt & DT_RIGHT) x = r->right - w;
            p = s + ls[i];
            out_line(dc, x, y, p, le[i] - ls[i], pfx, lh);
        }
        if (!(fmt & DT_NOCLIP)) {
            extern BOOL API RestoreDC(HDC, int);
            RestoreDC(dc, -1);
        }
    }
    return total;
}

BOOL API GrayString(HDC dc, HBRUSH br, LPVOID proc, LONG data, int n, int x, int y, int cx, int cy)
{
    (void)br; (void)cx; (void)cy;
    if (proc) {
        extern LONG __far __cdecl call_proc_args(LPVOID, WORD, WORD __far *);
        WORD a[4];
        a[0] = dc; a[1] = HIWORD(data); a[2] = LOWORD(data); a[3] = (WORD)n;
        return (BOOL)call_proc_args(proc, 4, (WORD __far *)a);
    }
    {
        COLORREF o = SetTextColor(dc, syscolor(COLOR_GRAYTEXT));
        TextOut(dc, x, y, (LPCSTR)data, n ? n : ustrlen((LPCSTR)data));
        SetTextColor(dc, o);
    }
    return TRUE;
}

static int tabbed(HDC dc, int x, int y, LPCSTR s, int n, int nt, int __far *tabs, int org, BOOL draw)
{
    int cx = x, i, st = 0, ti = 0, h = HIWORD(GetTextExtent(dc, "X", 1));
    int deftab = 8 * LOWORD(GetTextExtent(dc, "X", 1));
    for (i = 0; i <= n; i++) {
        if (i < n && s[i] != '\t') continue;
        if (i > st) { if (draw) TextOut(dc, cx, y, s + st, i - st); cx += LOWORD(GetTextExtent(dc, s + st, i - st)); }
        if (i < n) {
            int stop;
            if (nt > 0 && tabs) {
                while (ti < nt && org + tabs[ti] <= cx) ti++;
                stop = ti < nt ? org + tabs[ti] : cx + (nt == 1 ? tabs[0] : deftab);
            } else stop = org + ((cx - org) / deftab + 1) * deftab;
            cx = stop;
        }
        st = i + 1;
    }
    return (int)MAKELONG(cx - x, h);
}

LONG API TabbedTextOut(HDC dc, int x, int y, LPCSTR s, int n, int nt, int __far *tabs, int org)
{
    int w = tabbed(dc, x, y, s, n, nt, tabs, org, TRUE);
    return MAKELONG(w, HIWORD(GetTextExtent(dc, "X", 1)));
}

DWORD API GetTabbedTextExtent(HDC dc, LPCSTR s, int n, int nt, int __far *tabs)
{
    int w = tabbed(dc, 0, 0, s, n, nt, tabs, 0, FALSE);
    return MAKELONG(w, HIWORD(GetTextExtent(dc, "X", 1)));
}

/* ============================================================
 * Obszar nieklienta - wlasny styl win16Loader (teal, przyciski "=", "-", "+", "X",
 * tytul od lewej fontem VGA/Fixedsys). Zgodnosc z Win16 dotyczy API i metryk,
 * nie wygladu Windows.
 * ============================================================ */
#define NCB_FACE RGB(0x60, 0xA8, 0xB4)       /* przyciski paska tytulu */
#define NCB_HI   RGB(0xA0, 0xD8, 0xE0)
#define NCB_SH   RGB(0x00, 0x40, 0x48)

/* Font ramek okien i menu (Fixedsys z VGAFIX.FON, jak font VGA starego USER) */
WORD chrome_font(void)
{
    static WORD f;
    if (!f) f = CreateFont(0, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0x01, "Fixedsys");
    return f;
}

static void chrome_text(HDC dc, LPCSTR s, int n, LPRECT r, WORD fmt, COLORREF c)
{
    WORD of = SelectObject(dc, chrome_font());
    SetBkMode(dc, 1);
    SetTextColor(dc, c);
    DrawText(dc, s, n, r, fmt);
    SelectObject(dc, of);
}
static int nc_frame(WND __far *w)
{
    if (w->style & WS_THICKFRAME) return 4;
    if ((w->exstyle & WS_EX_DLGMODALFRAME) || ((w->style & WS_DLGFRAME) && !(w->style & WS_BORDER))) return 4;
    if (w->style & (WS_BORDER | WS_DLGFRAME)) return 1;
    return 0;
}

static BOOL has_caption(WND __far *w) { return (w->style & WS_CAPTION) == WS_CAPTION; }

/* Prostokaty elementow paska tytulu (wsp. okna) */
static void cap_rects(WND __far *w, LPRECT cap, LPRECT sys, LPRECT mn, LPRECT mx, LPRECT cl)
{
    int f = nc_frame(w), W = w->wr.right - w->wr.left;
    SetRect(cap, f, f, W - f, f + 19);
    SetRectEmpty(sys); SetRectEmpty(mn); SetRectEmpty(mx); SetRectEmpty(cl);
    if (w->style & WS_SYSMENU) {
        SetRect(sys, cap->left, cap->top, cap->left + 18, cap->bottom); cap->left += 18;
        SetRect(cl, cap->right - 18, cap->top, cap->right, cap->bottom); cap->right -= 18;
    }
    if (w->style & WS_MAXIMIZEBOX) { SetRect(mx, cap->right - 18, cap->top, cap->right, cap->bottom); cap->right -= 18; }
    if (w->style & WS_MINIMIZEBOX) { SetRect(mn, cap->right - 18, cap->top, cap->right, cap->bottom); cap->right -= 18; }
}

/* Przycisk paska tytulu ze znakiem ('=', '-', '+', 'X'); pressed = wcisniety */
static void nc_btn(HDC dc, LPRECT r, char glyph, BOOL pressed)
{
    RECT t = *r;
    fill_color(dc, r->left, r->top, r->right, r->bottom, NCB_FACE);
    draw_3d(dc, r->left, r->top, r->right, r->bottom, pressed ? NCB_SH : NCB_HI, pressed ? NCB_HI : NCB_SH);
    if (pressed) OffsetRect((LPRECT)&t, 1, 1);
    chrome_text(dc, (LPCSTR)(char __far *)&glyph, 1, (LPRECT)&t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, 0);
}

static void draw_glyph_btn(HDC dc, LPRECT r, int kind, BOOL pressed)
{
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2, i;
    COLORREF face = syscolor(COLOR_BTNFACE), sh = syscolor(COLOR_BTNSHADOW);
    fill_color(dc, r->left, r->top, r->right, r->bottom, face);
    if (pressed) draw_3d(dc, r->left, r->top, r->right, r->bottom, sh, syscolor(COLOR_BTNHIGHLIGHT));
    else {
        draw_3d(dc, r->left, r->top, r->right, r->bottom, syscolor(COLOR_BTNHIGHLIGHT), sh);
        fill_color(dc, r->left + 1, r->bottom - 2, r->right - 1, r->bottom - 1, sh);
        fill_color(dc, r->right - 2, r->top + 1, r->right - 1, r->bottom - 1, sh);
    }
    if (pressed) { cx++; cy++; }
    for (i = 0; i < 4; i++) {                       /* trojkat; 5 = bez znaku (suwak) */
        if (kind == 0) fill_color(dc, cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i, 0);       /* w dol */
        else if (kind == 1) fill_color(dc, cx - 3 + i, cy + 2 - i, cx + 4 - i, cy + 3 - i, 0); /* w gore */
        else if (kind == 3) fill_color(dc, cx - 1 + i, cy - i, cx + i, cy + 1 + i, 0);       /* w lewo */
        else if (kind == 4) fill_color(dc, cx + 1 - i, cy - i, cx + 2 - i, cy + 1 + i, 0);   /* w prawo */
        else if (kind == 2) {                                                               /* przywroc */
            fill_color(dc, cx - 3 + i, cy - 1 - i, cx + 4 - i, cy - i, 0);
            fill_color(dc, cx - 3 + i, cy + 1 + i, cx + 4 - i, cy + 2 + i, 0);
        }
    }
}

/* Pasek przewijania w prostokacie r (wsp. DC): strzalki, tlo, suwak */
void sb_paint(HDC dc, LPRECT r, BOOL vert, int pos, int mn, int mx)
{
    int len = vert ? r->bottom - r->top : r->right - r->left, a, track, range = mx - mn, p;
    RECT b1, b2, th;
    a = vert ? r->right - r->left : r->bottom - r->top;
    if (a > 16) a = 16;
    if (2 * a > len) a = len / 2;
    fill_color(dc, r->left, r->top, r->right, r->bottom, syscolor(COLOR_SCROLLBAR));
    if (vert) { SetRect((LPRECT)&b1, r->left, r->top, r->right, r->top + a); SetRect((LPRECT)&b2, r->left, r->bottom - a, r->right, r->bottom); }
    else { SetRect((LPRECT)&b1, r->left, r->top, r->left + a, r->bottom); SetRect((LPRECT)&b2, r->right - a, r->top, r->right, r->bottom); }
    draw_glyph_btn(dc, (LPRECT)&b1, vert ? 1 : 3, FALSE);
    draw_glyph_btn(dc, (LPRECT)&b2, vert ? 0 : 4, FALSE);
    track = len - 2 * a;
    if (track > a && range > 0) {
        if (pos < mn) pos = mn;
        if (pos > mx) pos = mx;
        p = (int)((long)(pos - mn) * (track - a) / range);
        if (vert) SetRect((LPRECT)&th, r->left, r->top + a + p, r->right, r->top + 2 * a + p);
        else SetRect((LPRECT)&th, r->left + a + p, r->top, r->left + 2 * a + p, r->bottom);
        draw_glyph_btn(dc, (LPRECT)&th, 5, FALSE);
    }
}

static void draw_sb(HDC dc, WND __far *w, int bar, LPRECT r)
{
    sb_paint(dc, r, bar == 1, w->sbpos[bar], w->sbmin[bar], w->sbmax[bar]);
}

void nc_paint(HWND h)
{
    WND __far *w = WP(h);
    HDC dc;
    int f, W, H, y;
    if (!w || !wnd_visible(h) || h == HDESKTOP) return;
    dc = GetWindowDC(h);
    f = nc_frame(w);
    W = w->wr.right - w->wr.left; H = w->wr.bottom - w->wr.top;
    if (f) {
        BOOL act = (w->flags & WF_ACTIVE) != 0;
        fill_color(dc, 0, 0, W, 1, 0); fill_color(dc, 0, H - 1, W, H, 0);
        fill_color(dc, 0, 0, 1, H, 0); fill_color(dc, W - 1, 0, W, H, 0);
        if (f > 1) {                                  /* 1 px czarny + jasny teal */
            COLORREF bc = syscolor(act ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER);
            fill_color(dc, 1, 1, W - 1, f, bc); fill_color(dc, 1, H - f, W - 1, H - 1, bc);
            fill_color(dc, 1, 1, f, H - 1, bc); fill_color(dc, W - f, 1, W - 1, H - 1, bc);
        }
    }
    y = f;
    if (has_caption(w)) {
        RECT cap, sys, mn, mx, cl;
        BOOL act = (w->flags & WF_ACTIVE) != 0;
        cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
        fill_color(dc, f, f, W - f, f + 19, syscolor(act ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
        if (sys.right) nc_btn(dc, (LPRECT)&sys, '=', FALSE);
        if (mn.right) nc_btn(dc, (LPRECT)&mn, '-', FALSE);
        if (mx.right) nc_btn(dc, (LPRECT)&mx, '+', FALSE);
        if (cl.right) nc_btn(dc, (LPRECT)&cl, 'X', FALSE);
        if (w->textlen) {
            cap.left += 4;
            chrome_text(dc, (LPCSTR)w->text, w->textlen, (LPRECT)&cap, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        syscolor(act ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
        }
        y += 19;
    }
    if (!(w->style & WS_CHILD) && w->id) {
        int mh = menu_bar_height(h);
        (void)mh;
        menu_draw_bar(h, dc);
    }
    {
        RECT r;
        int cl = w->cr.left - w->wr.left, ct = w->cr.top - w->wr.top;
        int cr2 = w->cr.right - w->wr.left, cb = w->cr.bottom - w->wr.top;
        if (w->style & WS_VSCROLL) { SetRect((LPRECT)&r, cr2, ct, cr2 + 16, cb); draw_sb(dc, w, 1, (LPRECT)&r); }
        if (w->style & WS_HSCROLL) { SetRect((LPRECT)&r, cl, cb, cr2, cb + 16); draw_sb(dc, w, 0, (LPRECT)&r); }
        if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL)) fill_color(dc, cr2, cb, cr2 + 16, cb + 16, syscolor(COLOR_BTNFACE));
    }
    ReleaseDC(h, dc);
}

int nc_hittest(HWND h, int x, int y)
{
    WND __far *w = WP(h);
    int f, lx, ly, W, H;
    if (!w) return HTNOWHERE;
    if (h == HDESKTOP) return HTCLIENT;
    if (x < w->wr.left || x >= w->wr.right || y < w->wr.top || y >= w->wr.bottom) return HTNOWHERE;
    if (x >= w->cr.left && x < w->cr.right && y >= w->cr.top && y < w->cr.bottom) return HTCLIENT;
    lx = x - w->wr.left; ly = y - w->wr.top;
    W = w->wr.right - w->wr.left; H = w->wr.bottom - w->wr.top;
    f = nc_frame(w);
    if ((w->style & WS_THICKFRAME) && !(w->flags & WF_MAXIMIZED) &&
        (lx < f || ly < f || lx >= W - f || ly >= H - f)) {
        BOOL l = lx < 18, r = lx >= W - 18, t = ly < 18, b = ly >= H - 18;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (lx < f) return HTLEFT;
        if (lx >= W - f) return HTRIGHT;
        if (ly < f) return HTTOP;
        return HTBOTTOM;
    }
    if (has_caption(w) && ly >= f && ly < f + 19) {
        RECT cap, sys, mn, mx, cl;
        POINT p;
        cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
        p.x = lx; p.y = ly;
        if (PtInRect((LPRECT)&sys, p)) return HTSYSMENU;
        if (PtInRect((LPRECT)&cl, p)) return HTCLOSE;
        if (PtInRect((LPRECT)&mn, p)) return HTREDUCE;
        if (PtInRect((LPRECT)&mx, p)) return HTZOOM;
        return HTCAPTION;
    }
    if ((w->style & WS_VSCROLL) && x >= w->cr.right && x < w->cr.right + 16 && y >= w->cr.top && y < w->cr.bottom) return HTVSCROLL;
    if ((w->style & WS_HSCROLL) && y >= w->cr.bottom && y < w->cr.bottom + 16 && x >= w->cr.left && x < w->cr.right) return HTHSCROLL;
    if (!(w->style & WS_CHILD) && w->id && y < w->cr.top) return HTMENU;
    return HTBORDER;
}

/* Prostokat XOR podczas przesuwania / zmiany rozmiaru */
static void xor_frame(LPRECT r)
{
    HDC dc = GetDC(0);
    int w = r->right - r->left, h = r->bottom - r->top;
    PatBlt(dc, r->left, r->top, w, 3, ROP_DSTINVERT);
    PatBlt(dc, r->left, r->bottom - 3, w, 3, ROP_DSTINVERT);
    PatBlt(dc, r->left, r->top + 3, 3, h - 6, ROP_DSTINVERT);
    PatBlt(dc, r->right - 3, r->top + 3, 3, h - 6, ROP_DSTINVERT);
    ReleaseDC(0, dc);
}

static void track_frame(HWND h, int hit, int x0, int y0)
{
    WND __far *w = WP(h);
    RECT r = w->wr, o;
    MSG m;
    int dx = 0, dy = 0;
    o = r;
    xor_frame((LPRECT)&r);
    while (modal_next((LPMSG)&m)) {
        if (m.message == WM_LBUTTONUP || m.message == WM_NCLBUTTONUP || (!(mouse_btn() & 1) && m.message != WM_KEYDOWN)) break;
        if (m.message == WM_KEYDOWN && m.wParam == 0x1B) { dx = dy = 0; r = o; break; }
        dx = mouse_x() - x0; dy = mouse_y() - y0;
        xor_frame((LPRECT)&r);
        r = o;
        switch (hit) {
        case HTCAPTION: OffsetRect((LPRECT)&r, dx, dy); break;
        case HTLEFT: r.left += dx; break;
        case HTRIGHT: r.right += dx; break;
        case HTTOP: r.top += dy; break;
        case HTBOTTOM: r.bottom += dy; break;
        case HTTOPLEFT: r.left += dx; r.top += dy; break;
        case HTTOPRIGHT: r.right += dx; r.top += dy; break;
        case HTBOTTOMLEFT: r.left += dx; r.bottom += dy; break;
        case HTBOTTOMRIGHT: r.right += dx; r.bottom += dy; break;
        }
        if (r.right - r.left < 100) { if (r.left != o.left) r.left = r.right - 100; else r.right = r.left + 100; }
        if (r.bottom - r.top < 27) { if (r.top != o.top) r.top = r.bottom - 27; else r.bottom = r.top + 27; }
        xor_frame((LPRECT)&r);
    }
    xor_frame((LPRECT)&r);
    if (WP(h) && (r.left != o.left || r.top != o.top || r.right != o.right || r.bottom != o.bottom)) {
        if (w->style & WS_CHILD) OffsetRect((LPRECT)&r, -WP(w->parent)->cr.left, -WP(w->parent)->cr.top);
        SetWindowPos(h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0x0004 | 0x0010);
    }
}

static BOOL track_button(HWND h, LPRECT rr, char glyph)
{
    MSG m;
    BOOL in = TRUE;
    HDC dc = GetWindowDC(h);
    WND __far *w = WP(h);
    nc_btn(dc, rr, glyph, TRUE);
    while (modal_next((LPMSG)&m)) {
        POINT p;
        BOOL now;
        p.x = mouse_x() - w->wr.left; p.y = mouse_y() - w->wr.top;
        now = PtInRect(rr, p);
        if (now != in) { in = now; nc_btn(dc, rr, glyph, in); }
        if (!(mouse_btn() & 1)) break;
    }
    nc_btn(dc, rr, glyph, FALSE);
    ReleaseDC(h, dc);
    return in;
}

/* Pasek przewijania okna: wiadomosci WM_HSCROLL/WM_VSCROLL do okna */
static void track_scroll(HWND h, int bar, int x, int y)
{
    WND __far *w = WP(h);
    WORD msg = bar ? WM_VSCROLL : WM_HSCROLL;
    int a = 16, len, track, pos, p0, code, range;
    MSG m;
    DWORD next;
    len = bar ? w->cr.bottom - w->cr.top : w->cr.right - w->cr.left;
    p0 = bar ? y - w->cr.top : x - w->cr.left;
    track = len - 2 * a;
    range = w->sbmax[bar] - w->sbmin[bar];
    pos = range > 0 && track > a ? (int)((long)(w->sbpos[bar] - w->sbmin[bar]) * (track - a) / range) : 0;
    if (p0 < a) code = 0;
    else if (p0 >= len - a) code = 1;
    else if (p0 < a + pos) code = 2;
    else if (p0 >= 2 * a + pos) code = 3;
    else code = 5;
    if (code == 5) {                                          /* przeciaganie suwaka */
        int off = p0 - a - pos, np = w->sbpos[bar];
        while (modal_next((LPMSG)&m)) {
            int c = (bar ? mouse_y() - w->cr.top : mouse_x() - w->cr.left) - a - off;
            if (track > a) {
                if (c < 0) c = 0;
                if (c > track - a) c = track - a;
                np = w->sbmin[bar] + (int)((long)c * range / (track - a));
                send_msg(h, msg, 5, MAKELONG(np, 0));       /* SB_THUMBTRACK */
            }
            if (!(mouse_btn() & 1)) break;
            if (!(w = WP(h))) return;
        }
        send_msg(h, msg, 4, MAKELONG(np, 0));               /* SB_THUMBPOSITION */
    } else {
        send_msg(h, msg, code, 0);
        next = tick() + 400;
        for (;;) {
            if (PeekMessage((LPMSG)&m, 0, 0, 0, 1)) {
                if (m.message == WM_LBUTTONUP || m.message == WM_NCLBUTTONUP) break;
                if (m.message == WM_PAINT) send_msg(m.hwnd, WM_PAINT, 0, 0);
            }
            if (!(mouse_btn() & 1)) break;
            if ((long)(tick() - next) >= 0) { send_msg(h, msg, code, 0); next = tick() + 60; }
            if (!WP(h)) return;
        }
    }
    send_msg(h, msg, 8, 0);                                  /* SB_ENDSCROLL */
}

void nc_buttondown(HWND h, int hit, int x, int y)
{
    WND __far *w = WP(h);
    RECT cap, sys, mn, mx, cl;
    if (!w) return;
    if (!(w->style & WS_CHILD)) activate_window(h, TRUE);
    switch (hit) {
    case HTCAPTION:
        if (!(w->flags & WF_MAXIMIZED)) track_frame(h, hit, x, y);
        break;
    case HTLEFT: case HTRIGHT: case HTTOP: case HTBOTTOM:
    case HTTOPLEFT: case HTTOPRIGHT: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
        track_frame(h, hit, x, y);
        break;
    case HTSYSMENU: {
        HMENU m = GetSystemMenu(h, FALSE);
        if (m) {
            int cmd;
            cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
            cmd = menu_track_popup(m, w->wr.left + sys.left, w->wr.top + sys.bottom + 1, h, TRUE);
            if (cmd) send_msg(h, WM_SYSCOMMAND, cmd, MAKELONG(x, y));
        }
        break;
    }
    case HTREDUCE:
        cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
        if (track_button(h, (LPRECT)&mn, '-')) send_msg(h, WM_SYSCOMMAND, SC_MINIMIZE, MAKELONG(x, y));
        break;
    case HTCLOSE:
        cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
        if (track_button(h, (LPRECT)&cl, 'X')) send_msg(h, WM_SYSCOMMAND, SC_CLOSE, MAKELONG(x, y));
        break;
    case HTZOOM:
        cap_rects(w, (LPRECT)&cap, (LPRECT)&sys, (LPRECT)&mn, (LPRECT)&mx, (LPRECT)&cl);
        if (track_button(h, (LPRECT)&mx, '+'))
            send_msg(h, WM_SYSCOMMAND, (w->flags & WF_MAXIMIZED) ? SC_RESTORE : SC_MAXIMIZE, MAKELONG(x, y));
        break;
    case HTMENU:
        menu_track_bar(h, x, y, 0);
        break;
    case HTVSCROLL: track_scroll(h, 1, x, y); break;
    case HTHSCROLL: track_scroll(h, 0, x, y); break;
    }
}

/* ============================================================
 * Paski przewijania okna (API)
 * ============================================================ */
static void sb_redraw(HWND h) { WND __far *w = WP(h); if (w && wnd_visible(h)) nc_paint(h); }

int API SetScrollPos(HWND h, int bar, int pos, BOOL redraw)
{
    WND __far *w = WP(h);
    int o;
    if (!w) return 0;
    if (bar == 2) return (int)SendMessage(h, 0x0400 + 1, pos, redraw);    /* SB_CTL -> kontrolka */
    bar = bar ? 1 : 0;
    o = w->sbpos[bar];
    if (pos < w->sbmin[bar]) pos = w->sbmin[bar];
    if (pos > w->sbmax[bar]) pos = w->sbmax[bar];
    w->sbpos[bar] = pos;
    if (redraw) sb_redraw(h);
    return o;
}

int API GetScrollPos(HWND h, int bar)
{
    WND __far *w = WP(h);
    if (!w) return 0;
    if (bar == 2) return (int)SendMessage(h, 0x0400 + 2, 0, 0);
    return w->sbpos[bar ? 1 : 0];
}

void API SetScrollRange(HWND h, int bar, int mn, int mx, BOOL redraw)
{
    WND __far *w = WP(h);
    DWORD flag;
    if (!w) return;
    if (bar == 2) { SendMessage(h, 0x0400 + 3, mn, MAKELONG(mx, redraw)); return; }
    flag = bar ? WS_VSCROLL : WS_HSCROLL;
    bar = bar ? 1 : 0;
    w->sbmin[bar] = mn; w->sbmax[bar] = mx;
    if (w->sbpos[bar] < mn) w->sbpos[bar] = mn;
    if (w->sbpos[bar] > mx) w->sbpos[bar] = mx;
    if ((mn == mx) == ((w->style & flag) != 0)) {            /* pokaz/ukryj pasek */
        if (mn == mx) w->style &= ~flag; else w->style |= flag;
        SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020);
    } else if (redraw) sb_redraw(h);
}

void API GetScrollRange(HWND h, int bar, int __far *mn, int __far *mx)
{
    WND __far *w = WP(h);
    if (!w) { *mn = *mx = 0; return; }
    if (bar == 2) { DWORD r = SendMessage(h, 0x0400 + 4, 0, 0); *mn = (int)LOWORD(r); *mx = (int)HIWORD(r); return; }
    bar = bar ? 1 : 0;
    *mn = w->sbmin[bar]; *mx = w->sbmax[bar];
}

void API ShowScrollBar(HWND h, int bar, BOOL show)
{
    WND __far *w = WP(h);
    DWORD f;
    if (!w) return;
    if (bar == 2) { ShowWindow(h, show ? 5 : 0); return; }
    f = bar == 3 ? (WS_HSCROLL | WS_VSCROLL) : bar ? WS_VSCROLL : WS_HSCROLL;
    if (show) w->style |= f; else w->style &= ~f;
    SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020);
}

BOOL API EnableScrollBar(HWND h, int bar, WORD fl) { (void)h; (void)bar; (void)fl; return TRUE; }

/* ============================================================
 * DefWindowProc (ordinal 107)
 * ============================================================ */
static void set_text(HWND h, LPCSTR s)
{
    WND __far *w = WP(h);
    if (!w) return;
    if (s && HIWORD(s)) ustrncpy((LPSTR)w->text, s, MAX_TITLE); else w->text[0] = 0;
    w->textlen = ustrlen((LPCSTR)w->text);
}

LRESULT API DefWindowProc(HWND h, WORD m, WORD wp, LONG lp)
{
    WND __far *w = WP(h);
    if (!w) return 0;
    switch (m) {
    case WM_NCCREATE:
        if (lp) set_text(h, *(LPCSTR __far *)((BYTE __far *)lp + CS_NAME_OFF));
        return 1;
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        nc_paint(h);
        return 0;
    case WM_NCACTIVATE:
        if (wp) w->flags |= WF_ACTIVE; else w->flags &= ~WF_ACTIVE;
        nc_paint(h);
        return 1;
    case WM_NCHITTEST:
        return nc_hittest(h, (int)LOWORD(lp), (int)HIWORD(lp));
    case WM_NCLBUTTONDOWN:
        nc_buttondown(h, wp, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTSYSMENU) send_msg(h, WM_SYSCOMMAND, SC_CLOSE, lp);
        else if (wp == HTCAPTION && (w->style & WS_MAXIMIZEBOX))
            send_msg(h, WM_SYSCOMMAND, (w->flags & WF_MAXIMIZED) ? SC_RESTORE : SC_MAXIMIZE, lp);
        else return DefWindowProc(h, WM_NCLBUTTONDOWN, wp, lp);
        return 0;
    case WM_SETCURSOR:
        if ((w->style & WS_CHILD) && WP(w->parent) && send_msg(w->parent, WM_SETCURSOR, wp, lp)) return 1;
        if (LOWORD(lp) == HTCLIENT && g_cls[w->cls].cursor) SetCursor(g_cls[w->cls].cursor);
        else if (LOWORD(lp) == (WORD)HTERROR && (HIWORD(lp) == WM_LBUTTONDOWN)) MessageBeep(0);
        else SetCursor(LoadCursor(0, (LPCSTR)MAKELONG(32512, 0)));
        return 1;
    case WM_MOUSEACTIVATE:
        if ((w->style & WS_CHILD) && WP(w->parent)) {
            LRESULT r = send_msg(w->parent, WM_MOUSEACTIVATE, wp, lp);
            if (r) return r;
        }
        return 1;
    case WM_ERASEBKGND: {
        HBRUSH b = g_cls[w->cls].brush;
        RECT r;
        if (!b) return 0;
        GetClientRect(h, (LPRECT)&r);
        FillRect((HDC)wp, (LPRECT)&r, b);
        return 1;
    }
    case WM_ICONERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, (PAINTSTRUCT __far *)&ps);
        EndPaint(h, (PAINTSTRUCT __far *)&ps);
        return 0;
    }
    case WM_GETTEXT: {
        int n = w->textlen < (int)wp - 1 ? w->textlen : (int)wp - 1;
        if ((int)wp <= 0) return 0;
        umemcpy((LPVOID)lp, (LPVOID)w->text, n);
        ((LPSTR)lp)[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH:
        return w->textlen;
    case WM_SETTEXT:
        set_text(h, (LPCSTR)lp);
        if (has_caption(w) && wnd_visible(h)) nc_paint(h);
        return 1;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_SYSCOMMAND:
        switch (wp & 0xFFF0) {
        case SC_CLOSE:    send_msg(h, WM_CLOSE, 0, 0); break;
        case SC_MINIMIZE: ShowWindow(h, 6); break;
        case SC_MAXIMIZE: ShowWindow(h, 3); break;
        case SC_RESTORE:  ShowWindow(h, 9); break;
        case SC_KEYMENU:  if (w->id && !(w->style & WS_CHILD)) menu_track_bar(h, -1, -1, (WORD)LOWORD(lp)); break;
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == 0x73 && (lp & (1L << 29))) {               /* Alt+F4 */
            post_msg(wnd_toplevel(h), WM_SYSCOMMAND, SC_CLOSE, 0);
            return 0;
        }
        if (wp == 0x79) {                                    /* F10 */
            HWND t = wnd_toplevel(h);
            send_msg(t, WM_SYSCOMMAND, SC_KEYMENU, 0);
        }
        return 0;
    case WM_SYSCHAR:
        if (lp & (1L << 29)) {
            HWND t = wnd_toplevel(h);
            if (WP(t) && WP(t)->id) send_msg(t, WM_SYSCOMMAND, SC_KEYMENU, (BYTE)wp);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) && !HIWORD(lp)) {
            HWND f = GetFocus();
            if (!f || (f != h && !IsChild(h, f))) SetFocus(h);
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        wnd_posdone(h, (LPVOID)lp);
        return 0;
    case WM_GETMINMAXINFO:
        return 0;
    case WM_CTLCOLOR: {
        WORD type = HIWORD(lp);
        if (type == 1 || type == 2 || type == 3) {           /* EDIT, LISTBOX, BTN?... */
            if (type == 3) {
                SetTextColor((HDC)wp, syscolor(COLOR_BTNTEXT));
                SetBkColor((HDC)wp, syscolor(COLOR_BTNFACE));
                return sysbrush(COLOR_BTNFACE);
            }
            SetTextColor((HDC)wp, syscolor(COLOR_WINDOWTEXT));
            SetBkColor((HDC)wp, syscolor(COLOR_WINDOW));
            return sysbrush(COLOR_WINDOW);
        }
        SetTextColor((HDC)wp, syscolor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wp, syscolor(type == 4 || type == 6 ? COLOR_BTNFACE : COLOR_WINDOW));
        return sysbrush(type == 4 || type == 6 ? COLOR_BTNFACE : COLOR_WINDOW);
    }
    case WM_QUERYOPEN:
    case WM_QUERYENDSESSION:
        return 1;
    case WM_CHARTOITEM:
    case WM_VKEYTOITEM:
        return -1;
    case WM_CANCELMODE:
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_SHOWWINDOW:
        return 0;
    }
    return 0;
}

/* ============================================================
 * Pulpit i inicjalizacja
 * ============================================================ */
static LRESULT __far __pascal DesktopProc(HWND h, WORD m, WORD wp, LONG lp)
{
    if (m == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, (LPRECT)&r);
        FillRect((HDC)wp, (LPRECT)&r, sysbrush(COLOR_BACKGROUND));
        return 1;
    }
    if (m == WM_NCHITTEST) return HTCLIENT;
    return DefWindowProc(h, m, wp, lp);
}

void sys_colors_changed(void)
{
    InvalidateRect(0, 0, TRUE);
}

static BOOL g_uinit;

void user_init(void)
{
    WND __far *d;
    if (g_uinit) return;
    g_uinit = TRUE;
    g_wndmem = GlobalAlloc(0x40, (DWORD)MAX_WND * sizeof(WND));
    misc_init();
    register_system_class("#32769", 0, (WNDPROC)DesktopProc, 0, 0, 0);
    ctl_register_classes();
    d = (WND __far *)MK_FP(g_wndmem, 0);
    d->used = 1;
    d->cls = cls_find("#32769", 0);
    d->style = WS_VISIBLE | WS_CLIPCHILDREN;
    d->proc = (WNDPROC)DesktopProc;
    d->wr.right = SCR_W; d->wr.bottom = SCR_H;
    d->cr = d->wr;
    wnd_invalidate(HDESKTOP, 0, TRUE, FALSE);
    GdiCursorDefault();
    GdiShowCursor(TRUE);
    udbg("USER: init OK\n");
}

/* ordinal 5 */
int API InitApp(WORD hinst) { (void)hinst; user_init(); return 1; }
int API LibMain(WORD a, WORD b, WORD c, LPCSTR d) { (void)a; (void)b; (void)c; (void)d; return 1; }
