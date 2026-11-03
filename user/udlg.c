/* udlg.c - USER: okna dialogowe i MessageBox */
#include "user.h"

extern ATOM register_system_class(LPCSTR, WORD, WNDPROC, int, WORD, WORD);
void API SetRect(LPRECT, int, int, int, int);
int  API GetKeyState(int);
HICON API LoadIcon(WORD, LPCSTR);
WORD API SetWindowWord(HWND, int, WORD);
LONG API SetWindowLong(HWND, int, LONG);
int  API wvsprintf(LPSTR, LPCSTR, WORD __far *);

/* Dane dialogu w extra okna: DWL_MSGRESULT 0, DWL_DLGPROC 4, DWL_USER 8,
 * nasze: 12 = wynik EndDialog, 14 = zakonczony, 16 = font, 18 = domyslny ID */
#define DWL_MSGRESULT 0
#define DWL_DLGPROC   4
#define DLG_RESULT    12
#define DLG_ENDED     14
#define DLG_FONT      16
#define DLG_DEFID     18
#define DLGWINDOWEXTRA 30

#define WS_TABSTOP 0x00010000UL
#define WS_GROUP   0x00020000UL

static int g_bx = 6, g_by = 13;          /* jednostki bazowe dialogu (font systemowy) */

LRESULT API DefDlgProc(HWND h, WORD m, WORD wp, LONG lp);

static void dlg_units(HWND h)
{
    struct { int tmHeight, a, b, c, d, tmAveCharWidth; BYTE rest[20]; } tm;
    HDC dc = GetDC(h);
    WORD f = GetWindowWord(h, DLG_FONT);
    if (f) SelectObject(dc, f);
    GetTextMetrics(dc, (LPVOID)&tm);
    ReleaseDC(h, dc);
    g_bx = tm.tmAveCharWidth ? tm.tmAveCharWidth : 6;
    g_by = tm.tmHeight ? tm.tmHeight : 13;
}

DWORD API GetDialogBaseUnits(void) { return MAKELONG(g_bx, g_by); }

void API MapDialogRect(HWND h, LPRECT r)
{
    (void)h;
    r->left = r->left * g_bx / 4; r->right = r->right * g_bx / 4;
    r->top = r->top * g_by / 8;   r->bottom = r->bottom * g_by / 8;
}

/* ============================================================
 * Tworzenie z szablonu
 * ============================================================ */
static LPCSTR tmpl_str(BYTE __far * __far *pp, WORD __far *ord)
{
    BYTE __far *p = *pp;
    LPCSTR s;
    *ord = 0;
    if (*p == 0xFF) { *ord = *(WORD __far *)(p + 1); *pp = p + 3; return 0; }
    s = (LPCSTR)p;
    *pp = p + ustrlen(s) + 1;
    return *s ? s : 0;                                   /* pusty = brak (menu, klasa) */
}

HWND API CreateDialogIndirectParam(WORD hinst, LPVOID tmpl, HWND owner, WNDPROC proc, LONG param)
{
    BYTE __far *p = (BYTE __far *)tmpl;
    DWORD style = *(DWORD __far *)p;
    BYTE n = p[4];
    int x = *(int __far *)(p + 5), y = *(int __far *)(p + 7), cx = *(int __far *)(p + 9), cy = *(int __far *)(p + 11);
    LPCSTR menu, cls, cap;
    WORD ord, font = 0;
    HWND h;
    RECT r;
    int i;
    p += 13;
    menu = tmpl_str((BYTE __far * __far *)&p, (WORD __far *)&ord);
    if (!menu && ord) menu = (LPCSTR)MAKELONG(ord, 0);
    cls = tmpl_str((BYTE __far * __far *)&p, (WORD __far *)&ord);
    cap = tmpl_str((BYTE __far * __far *)&p, (WORD __far *)&ord);
    if (style & 0x0040) {                                /* DS_SETFONT */
        int pt = *(int __far *)p;
        LPCSTR face;
        extern WORD API CreateFont(int, int, int, int, int, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, LPCSTR);
        p += 2;
        face = (LPCSTR)p;
        p += ustrlen(face) + 1;
        font = CreateFont(-(pt * 96 / 72), 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, face);
    }
    {   /* jednostki dialogowe wg fontu */
        HDC dc = GetDC(0);
        struct { int tmHeight, a, b, c, d, tmAveCharWidth; BYTE rest[20]; } tm;
        WORD of = font ? SelectObject(dc, font) : 0;
        GetTextMetrics(dc, (LPVOID)&tm);
        if (of) SelectObject(dc, of);
        ReleaseDC(0, dc);
        g_bx = tm.tmAveCharWidth ? tm.tmAveCharWidth : 6;
        g_by = tm.tmHeight ? tm.tmHeight : 13;
    }
    SetRect((LPRECT)&r, x, y, x + cx, y + cy);
    MapDialogRect(0, (LPRECT)&r);
    AdjustWindowRectEx((LPRECT)&r, style & ~WS_CHILD, menu != 0, (style & 0x0080) ? WS_EX_DLGMODALFRAME : 0);
    if (!(style & WS_CHILD)) {
        /* wsp. wzgledem obszaru klienta wlasciciela (chyba ze DS_ABSALIGN), potem w ekran */
        WND __far *ow = owner ? WP(owner) : 0;
        int w = r.right - r.left, hh = r.bottom - r.top;
        if (ow && !(style & 0x0001)) OffsetRect((LPRECT)&r, ow->cr.left, ow->cr.top);
        if (r.right > SCR_W) OffsetRect((LPRECT)&r, SCR_W - r.right, 0);
        if (r.bottom > SCR_H) OffsetRect((LPRECT)&r, 0, SCR_H - r.bottom);
        if (r.left < 0) { r.left = 0; r.right = w; }
        if (r.top < 0) { r.top = 0; r.bottom = hh; }
    }
    if (style & 0x0800) {                                /* DS_CENTER (3.1+) */
        int w = r.right - r.left, hh = r.bottom - r.top;
        r.left = (SCR_W - w) / 2; r.top = (SCR_H - hh) / 2; r.right = r.left + w; r.bottom = r.top + hh;
    }
    h = CreateWindowEx((style & 0x0080) ? WS_EX_DLGMODALFRAME : 0, cls ? cls : "#32770", cap ? cap : "",
                       style & ~WS_VISIBLE, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       owner, menu ? LoadMenu(hinst, menu) : 0, hinst, 0);
    if (!h) return 0;
    WP(h)->flags |= WF_DLG;
    SetWindowLong(h, DWL_DLGPROC, (LONG)proc);
    SetWindowWord(h, DLG_FONT, font);
    for (i = 0; i < n; i++) {
        int ix = *(int __far *)p, iy = *(int __far *)(p + 2), icx = *(int __far *)(p + 4), icy = *(int __far *)(p + 6);
        WORD id = *(WORD __far *)(p + 8);
        DWORD ist = *(DWORD __far *)(p + 10);
        LPCSTR icls, itext;
        WORD iord;
        HWND c;
        RECT ir;
        static const char * const cn[6] = { "BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX" };
        p += 14;
        if (*p & 0x80) { icls = cn[(*p & 0x7F) < 6 ? (*p & 0x7F) : 2]; p++; }
        else { icls = (LPCSTR)p; p += ustrlen(icls) + 1; }
        itext = tmpl_str((BYTE __far * __far *)&p, (WORD __far *)&iord);
        if (!itext) itext = iord ? (LPCSTR)MAKELONG(iord, 0) : "";
        p += 1 + *p;                                         /* dane dodatkowe */
        SetRect((LPRECT)&ir, ix, iy, ix + icx, iy + icy);
        MapDialogRect(0, (LPRECT)&ir);
        c = CreateWindowEx(0, icls, itext, ist | WS_CHILD, ir.left, ir.top, ir.right - ir.left,
                           ir.bottom - ir.top, h, id, hinst, 0);
        if (c && font) SendMessage(c, WM_SETFONT, font, 0);
        if (c && (ist & 0x0F) == 1 && !lstrcmpi(icls, "BUTTON")) SetWindowWord(h, DLG_DEFID, id);  /* BS_DEFPUSHBUTTON */
    }
    {
        HWND first = 0, c;
        for (c = WP(h)->child; c; c = WP(c)->next) if (WP(c)->style & WS_TABSTOP) first = c;   /* lista od konca */
        if (send_msg(h, WM_INITDIALOG, first, param) && first) SetFocus(first);
    }
    if (WP(h) && (style & WS_VISIBLE)) ShowWindow(h, 5);
    return h;
}

HWND API CreateDialogIndirect(WORD hinst, LPVOID t, HWND o, WNDPROC p) { return CreateDialogIndirectParam(hinst, t, o, p, 0); }

static LPVOID load_tmpl(WORD hinst, LPCSTR name)
{
    WORD hr = FindResource(hinst, name, (LPCSTR)MAKELONG(5, 0));
    return hr ? LockResource(LoadResource(hinst, hr)) : 0;
}

HWND API CreateDialogParam(WORD hinst, LPCSTR name, HWND o, WNDPROC p, LONG lp)
{
    LPVOID t = load_tmpl(hinst, name);
    return t ? CreateDialogIndirectParam(hinst, t, o, p, lp) : 0;
}

HWND API CreateDialog(WORD hinst, LPCSTR name, HWND o, WNDPROC p) { return CreateDialogParam(hinst, name, o, p, 0); }

/* ============================================================
 * Petla modalna
 * ============================================================ */
static int run_modal(HWND h, HWND owner)
{
    MSG m;
    int r;
    HWND top = owner ? wnd_toplevel(owner) : 0;
    if (top) EnableWindow(top, FALSE);
    ShowWindow(h, 5);
    while (WP(h) && !GetWindowWord(h, DLG_ENDED)) {
        if (!GetMessage((LPMSG)&m, 0, 0, 0)) { PostQuitMessage(m.wParam); break; }
        if (!IsDialogMessage(h, (LPMSG)&m)) { TranslateMessage((LPMSG)&m); DispatchMessage((LPMSG)&m); }
    }
    r = WP(h) ? (int)GetWindowWord(h, DLG_RESULT) : -1;
    if (top) EnableWindow(top, TRUE);
    if (WP(h)) DestroyWindow(h);
    if (top && WP(top)) SetActiveWindow(top);
    return r;
}

int API DialogBoxIndirectParam(WORD hinst, HANDLE ht, HWND owner, WNDPROC p, LONG lp)
{
    HWND h = CreateDialogIndirectParam(hinst, GlobalLock(ht), owner, p, lp);
    return h ? run_modal(h, owner) : -1;
}

int API DialogBoxIndirect(WORD hinst, HANDLE ht, HWND owner, WNDPROC p) { return DialogBoxIndirectParam(hinst, ht, owner, p, 0); }

int API DialogBoxParam(WORD hinst, LPCSTR name, HWND owner, WNDPROC p, LONG lp)
{
    HWND h = CreateDialogParam(hinst, name, owner, p, lp);
    return h ? run_modal(h, owner) : -1;
}

int API DialogBox(WORD hinst, LPCSTR name, HWND owner, WNDPROC p) { return DialogBoxParam(hinst, name, owner, p, 0); }

BOOL API EndDialog(HWND h, int r)
{
    if (!WP(h)) return FALSE;
    SetWindowWord(h, DLG_RESULT, (WORD)r);
    SetWindowWord(h, DLG_ENDED, 1);
    if (!(WP(h)->flags & WF_DLG)) DestroyWindow(h);
    return TRUE;
}

/* ============================================================
 * DefDlgProc / IsDialogMessage
 * ============================================================ */
LRESULT API DefDlgProc(HWND h, WORD m, WORD wp, LONG lp)
{
    WNDPROC p = (WNDPROC)GetWindowLong(h, DWL_DLGPROC);
    WND __far *w = WP(h);
    if (p && w) {
        /* DLGPROC zwraca BOOL (tylko AX) - DX jest smieciem */
        LRESULT r = (WORD)call_proc(p, proc_ds(p, w->hinst), h, m, wp, lp);
        if (!WP(h)) return 0;
        if (r) {
            if (m == WM_CTLCOLOR || m == WM_COMPAREITEM || m == WM_VKEYTOITEM || m == WM_CHARTOITEM ||
                m == WM_QUERYDRAGICON || m == WM_INITDIALOG)
                return r;
            return GetWindowLong(h, DWL_MSGRESULT) ? GetWindowLong(h, DWL_MSGRESULT) : r;
        }
    }
    switch (m) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, (LPRECT)&r);
        FillRect((HDC)wp, (LPRECT)&r, sysbrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_CLOSE:
        post_msg(h, WM_COMMAND, 2, MAKELONG(GetDlgItem(h, 2), 0));
        return 0;
    case WM_CTLCOLOR:
        SetBkColor((HDC)wp, syscolor(COLOR_BTNFACE));
        SetTextColor((HDC)wp, syscolor(COLOR_BTNTEXT));
        return HIWORD(lp) == 1 || HIWORD(lp) == 2 ? DefWindowProc(h, m, wp, lp) : sysbrush(COLOR_BTNFACE);
    case WM_GETFONT:
        return GetWindowWord(h, DLG_FONT);
    case WM_USER:                                        /* DM_GETDEFID */
        return MAKELONG(GetWindowWord(h, DLG_DEFID), 0x534B);
    case WM_USER + 1:                                    /* DM_SETDEFID */
        SetWindowWord(h, DLG_DEFID, wp);
        return 1;
    case WM_NEXTDLGCTL: {
        HWND n = lp ? (HWND)wp : GetNextDlgTabItem(h, GetFocus(), wp != 0);
        if (n) SetFocus(n);
        return 0;
    }
    case WM_SETFOCUS: {
        HWND c;
        for (c = w->child; c; c = WP(c)->next) if (WP(c)->style & WS_TABSTOP) break;
        if (c) SetFocus(c);
        return 0;
    }
    }
    return DefWindowProc(h, m, wp, lp);
}

static LRESULT __far __pascal DlgClassProc(HWND h, WORD m, WORD wp, LONG lp) { return DefDlgProc(h, m, wp, lp); }

/* Kolejna kontrolka z WS_TABSTOP (cyklicznie) */
HWND API GetNextDlgTabItem(HWND dlg, HWND cur, BOOL prev)
{
    HWND list[64], c;
    int n = 0, i, k;
    WND __far *d = WP(dlg);
    if (!d) return 0;
    for (c = d->child; c && n < 64; c = WP(c)->next) list[n++] = c;      /* gora -> dol Z (= odwrotna kolejnosc tworzenia) */
    for (i = 0; i < n; i++) if (list[i] == cur) break;
    for (k = 1; k <= n; k++) {
        int j = i == n ? (prev ? 0 : n - 1) : (prev ? (i + k) % n : (i - k + n) % n);
        WND __far *w = WP(list[j]);
        if ((w->style & WS_TABSTOP) && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED)) return list[j];
    }
    return cur;
}

HWND API GetNextDlgGroupItem(HWND dlg, HWND cur, BOOL prev) { return GetNextDlgTabItem(dlg, cur, prev); }

BOOL API IsDialogMessage(HWND dlg, LPMSG m)
{
    if (!WP(dlg) || (m->hwnd != dlg && !IsChild(dlg, m->hwnd))) return FALSE;
    if (m->message == WM_KEYDOWN) {
        LRESULT code = WP(m->hwnd) ? send_msg(m->hwnd, WM_GETDLGCODE, 0, 0) : 0;
        switch (m->wParam) {
        case 0x09:                                           /* TAB */
            if (!(code & 0x0002)) {                          /* DLGC_WANTTAB */
                HWND n = GetNextDlgTabItem(dlg, m->hwnd, (GetKeyState(0x10) & 0x8000) != 0);
                if (n) SetFocus(n);
                return TRUE;
            }
            break;
        case 0x0D:                                           /* ENTER */
            if (!(code & 0x0004)) {                          /* DLGC_WANTALLKEYS */
                WORD id = GetWindowWord(dlg, DLG_DEFID);
                if (!id) id = 1;
                send_msg(dlg, WM_COMMAND, id, MAKELONG(GetDlgItem(dlg, id), 0));
                return TRUE;
            }
            break;
        case 0x1B:                                           /* ESC */
            send_msg(dlg, WM_COMMAND, 2, MAKELONG(GetDlgItem(dlg, 2), 0));
            return TRUE;
        case 0x25: case 0x26: case 0x27: case 0x28:         /* strzalki w grupie */
            if (!(code & 0x0001)) {                          /* DLGC_WANTARROWS */
                HWND n = GetNextDlgTabItem(dlg, m->hwnd, m->wParam == 0x25 || m->wParam == 0x26);
                if (n) SetFocus(n);
                return TRUE;
            }
            break;
        }
    }
    TranslateMessage(m);
    DispatchMessage(m);
    return TRUE;
}

/* ============================================================
 * *DlgItem*
 * ============================================================ */
HWND API GetDlgItem(HWND h, int id)
{
    HWND c;
    if (!WP(h)) return 0;
    for (c = WP(h)->child; c; c = WP(c)->next) if (WP(c)->id == (WORD)id) return c;
    return 0;
}

int API GetDlgCtrlID(HWND h) { WND __far *w = WP(h); return w ? w->id : 0; }

LRESULT API SendDlgItemMessage(HWND h, int id, WORD m, WORD wp, LONG lp)
{
    HWND c = GetDlgItem(h, id);
    return c ? SendMessage(c, m, wp, lp) : 0;
}

void API SetDlgItemText(HWND h, int id, LPCSTR s) { HWND c = GetDlgItem(h, id); if (c) SetWindowText(c, s); }
int  API GetDlgItemText(HWND h, int id, LPSTR s, int n) { HWND c = GetDlgItem(h, id); if (c) return GetWindowText(c, s, n); if (n > 0) s[0] = 0; return 0; }

void API SetDlgItemInt(HWND h, int id, WORD v, BOOL sgn)
{
    char b[8];
    WORD a[1];
    a[0] = v;
    wvsprintf((LPSTR)(char __far *)b, sgn ? "%d" : "%u", (WORD __far *)a);
    SetDlgItemText(h, id, (LPCSTR)(char __far *)b);
}

WORD API GetDlgItemInt(HWND h, int id, BOOL __far *ok, BOOL sgn)
{
    char b[16];
    int i = 0;
    long v = 0;
    BOOL neg = FALSE;
    GetDlgItemText(h, id, (LPSTR)(char __far *)b, sizeof(b));
    while (b[i] == ' ') i++;
    if (sgn && b[i] == '-') { neg = TRUE; i++; }
    if (b[i] < '0' || b[i] > '9') { if (ok) *ok = FALSE; return 0; }
    while (b[i] >= '0' && b[i] <= '9') v = v * 10 + (b[i++] - '0');
    if (ok) *ok = TRUE;
    return (WORD)(neg ? -v : v);
}

BOOL API CheckDlgButton(HWND h, int id, WORD chk) { SendDlgItemMessage(h, id, WM_USER + 1, chk, 0); return TRUE; }
WORD API IsDlgButtonChecked(HWND h, int id)        { return (WORD)SendDlgItemMessage(h, id, WM_USER, 0, 0); }

BOOL API CheckRadioButton(HWND h, int first, int last, int chk)
{
    int i;
    for (i = first; i <= last; i++) CheckDlgButton(h, i, i == chk);
    return TRUE;
}

int  API DlgDirList(HWND h, LPSTR spec, int lb, int st, WORD type) { (void)h; (void)spec; (void)lb; (void)st; (void)type; return 0; }
BOOL API DlgDirSelect(HWND h, LPSTR s, int id) { (void)h; (void)id; if (s) s[0] = 0; return FALSE; }
int  API DlgDirListComboBox(HWND h, LPSTR spec, int cb, int st, WORD type) { (void)h; (void)spec; (void)cb; (void)st; (void)type; return 0; }
BOOL API DlgDirSelectComboBox(HWND h, LPSTR s, int id) { (void)h; (void)id; if (s) s[0] = 0; return FALSE; }

/* ============================================================
 * MessageBox
 * ============================================================ */
static LRESULT __far __pascal MsgBoxProc(HWND h, WORD m, WORD wp, LONG lp)
{
    (void)lp;
    if (m == WM_COMMAND && wp >= 1 && wp <= 7) { EndDialog(h, wp); return 1; }
    if (m == WM_INITDIALOG) return 1;
    return 0;
}

int API MessageBox(HWND owner, LPCSTR text, LPCSTR cap, WORD type)
{
    static const WORD btns[6][3] = { {1,0,0}, {1,2,0}, {3,4,5}, {6,7,2}, {6,7,0}, {4,2,0} };
    static const char * const names[8] = { "", "OK", "Cancel", "&Abort", "&Retry", "&Ignore", "&Yes", "&No" };
    HWND h, c;
    RECT tr;
    HDC dc;
    int nb, i, W, H, tx, bw = 70, iconw, def;
    WORD bset = type & 0x0F, icon = (type >> 4) & 0x0F;
    user_init();
    udbg("USER: MessageBox '"); if (text) udbg(text); udbg("'\n");
    if (bset > 5) bset = 0;
    for (nb = 0; nb < 3 && btns[bset][nb]; nb++);
    dc = GetDC(0);
    SelectObject(dc, GetStockObject(13));
    SetRect((LPRECT)&tr, 0, 0, 360, 0);
    DrawText(dc, text ? text : "", -1, (LPRECT)&tr, 0x0400 | 0x0010 | 0x0800);
    ReleaseDC(0, dc);
    iconw = icon ? 48 : 0;
    W = tr.right + iconw + 32;
    if (W < nb * (bw + 10) + 20) W = nb * (bw + 10) + 20;
    H = (tr.bottom > 32 ? tr.bottom : 32) + 70 + 19;
    h = CreateWindowEx(WS_EX_DLGMODALFRAME, "#32770", cap ? cap : "Error",
                       WS_POPUP | WS_CAPTION | WS_SYSMENU, (SCR_W - W) / 2, (SCR_H - H) / 2, W, H,
                       owner, 0, 0, 0);
    if (!h) return 0;
    WP(h)->flags |= WF_DLG;
    SetWindowLong(h, DWL_DLGPROC, (LONG)(WNDPROC)MsgBoxProc);
    tx = 16 + iconw;
    if (icon) {
        HWND ic = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE | 3 /* SS_ICON */, 16, 14, 32, 32, h, 99, 0, 0);
        WORD k = icon == 1 ? 32513 : icon == 2 ? 32514 : icon == 3 ? 32515 : 32516;
        SendMessage(ic, WM_USER, LoadIcon(0, (LPCSTR)MAKELONG(k, 0)), 0);   /* STM_SETICON */
    }
    CreateWindowEx(0, "STATIC", text ? text : "", WS_CHILD | WS_VISIBLE | 0x0000, tx, 14, tr.right + 4,
                   tr.bottom > 32 ? tr.bottom : 32, h, 98, 0, 0);
    def = (type >> 8) & 3;
    for (i = 0; i < nb; i++) {
        int x = (W - nb * (bw + 10)) / 2 + i * (bw + 10);
        c = CreateWindowEx(0, "BUTTON", names[btns[bset][i]],
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == def ? 1 : 0),
                           x, H - 19 - 8 - 32, bw, 24, h, btns[bset][i], 0, 0);
        if (i == def) { SetWindowWord(h, DLG_DEFID, btns[bset][i]); SetFocus(c); }
    }
    MessageBeep(type & 0xF0);
    return run_modal(h, owner);
}

void dlg_register(void)
{
    register_system_class("#32770", 0x0008, (WNDPROC)DlgClassProc, DLGWINDOWEXTRA, 0, 0);
}
