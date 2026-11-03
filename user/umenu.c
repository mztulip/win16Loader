/* umenu.c - USER: menu (pasek, rozwijane, systemowe, TrackPopupMenu) */
#include "user.h"

extern BOOL modal_next(LPMSG m);
extern int  mouse_x(void), mouse_y(void);
extern WORD mouse_btn(void);
extern ATOM register_system_class(LPCSTR, WORD, WNDPROC, int, WORD, WORD);
void API SetRect(LPRECT, int, int, int, int);
BOOL API PtInRect(const RECT __far *, POINT);
int  API GetKeyState(int);

#define MAX_MENUS  128
#define MAX_ITEMS  40
#define MENUBASE   0x0400

#define MF_GRAYED     0x0001
#define MF_DISABLED   0x0002
#define MF_BITMAP     0x0004
#define MF_CHECKED    0x0008
#define MF_POPUP      0x0010
#define MF_HILITE     0x0080
#define MF_OWNERDRAW  0x0100
#define MF_BYPOSITION 0x0400
#define MF_SEPARATOR  0x0800

typedef struct {
    WORD  flags, id;
    DWORD data;                    /* MF_OWNERDRAW: dane aplikacji */
    WORD  odw, odh;                /* MF_OWNERDRAW: rozmiar z WM_MEASUREITEM (0 = jeszcze nie) */
    char  text[40];
} MITEM;
typedef struct { WORD used, n; MITEM it[MAX_ITEMS]; } MENU;

static WORD g_menusel[MAX_MENUS];              /* kazde menu we wlasnym bloku GlobalAlloc */
static HWND g_mowner;                          /* wlasciciel menu (WM_MEASUREITEM/WM_DRAWITEM) */

static MENU __far *mptr(HMENU h)
{
    WORD i;
    MENU __far *m;
    if (h < MENUBASE || ((h - MENUBASE) & 3)) return 0;
    i = (h - MENUBASE) >> 2;
    if (i >= MAX_MENUS || !g_menusel[i]) return 0;
    m = (MENU __far *)MK_FP(g_menusel[i], 0);
    return m->used ? m : 0;
}

static HMENU menu_handle(MENU __far *m)
{
    WORD i;
    for (i = 0; i < MAX_MENUS; i++) if (g_menusel[i] && g_menusel[i] == FP_SEG(m)) return MENUBASE + i * 4;
    return 0;
}

static HMENU menu_new(void)
{
    WORD i;
    for (i = 0; i < MAX_MENUS; i++)
        if (!g_menusel[i]) {
            MENU __far *m;
            g_menusel[i] = GlobalAlloc(0x40, sizeof(MENU));
            if (!g_menusel[i]) return 0;
            m = (MENU __far *)MK_FP(g_menusel[i], 0);
            m->used = 1;
            return MENUBASE + i * 4;
        }
    return 0;
}

HMENU API CreateMenu(void)      { return menu_new(); }
HMENU API CreatePopupMenu(void) { return menu_new(); }
BOOL  API IsMenu(HMENU h)       { return mptr(h) != 0; }

BOOL API DestroyMenu(HMENU h)
{
    MENU __far *m = mptr(h);
    WORD i;
    if (!m) return FALSE;
    for (i = 0; i < m->n; i++) if (m->it[i].flags & MF_POPUP) DestroyMenu(m->it[i].id);
    m->used = 0;
    i = (h - MENUBASE) >> 2;
    GlobalFree(g_menusel[i]);
    g_menusel[i] = 0;
    return TRUE;
}

/* Indeks pozycji: MF_BYPOSITION albo po ID (rekurencyjnie w podmenu) */
static MENU __far *find_item(HMENU h, WORD pos, WORD fl, WORD __far *idx)
{
    MENU __far *m = mptr(h);
    WORD i;
    if (!m) return 0;
    if (fl & MF_BYPOSITION) { if (pos >= m->n) return 0; *idx = pos; return m; }
    for (i = 0; i < m->n; i++) {
        if (!(m->it[i].flags & MF_POPUP) && m->it[i].id == pos) { *idx = i; return m; }
        if (m->it[i].flags & MF_POPUP) {
            MENU __far *s = find_item(m->it[i].id, pos, fl, idx);
            if (s) return s;
        }
    }
    return 0;
}

static void set_item(MITEM __far *it, WORD fl, WORD id, LPCSTR s)
{
    it->flags = fl & ~MF_BYPOSITION;
    it->id = id;
    it->text[0] = 0;
    it->data = (fl & MF_OWNERDRAW) ? (DWORD)s : 0;
    it->odw = it->odh = 0;
    if (!(fl & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)) && s && HIWORD(s))
        ustrncpy((LPSTR)it->text, s, sizeof(it->text));
}

BOOL API InsertMenu(HMENU h, WORD pos, WORD fl, WORD id, LPCSTR s)
{
    MENU __far *m = mptr(h);
    WORD i, at;
    if (!m || m->n >= MAX_ITEMS) return FALSE;
    if (fl & MF_BYPOSITION) at = pos > m->n ? m->n : pos;
    else {
        WORD k;
        MENU __far *f = find_item(h, pos, fl, (WORD __far *)&k);
        if (f && f != m) return InsertMenu(menu_handle(f), k, fl | MF_BYPOSITION, id, s);
        at = f ? k : m->n;
    }
    for (i = m->n; i > at; i--) m->it[i] = m->it[i - 1];
    set_item(&m->it[at], fl, id, s);
    m->n++;
    return TRUE;
}

BOOL API AppendMenu(HMENU h, WORD fl, WORD id, LPCSTR s)
{
    MENU __far *m = mptr(h);
    if (!m) return FALSE;
    return InsertMenu(h, m->n, fl | MF_BYPOSITION, id, s);
}

BOOL API ModifyMenu(HMENU h, WORD pos, WORD fl, WORD id, LPCSTR s)
{
    WORD k;
    MENU __far *m = find_item(h, pos, fl, (WORD __far *)&k);
    if (!m) return FALSE;
    set_item(&m->it[k], fl, id, s);
    return TRUE;
}

BOOL API RemoveMenu(HMENU h, WORD pos, WORD fl)
{
    WORD k, i;
    MENU __far *m = find_item(h, pos, fl, (WORD __far *)&k);
    if (!m) return FALSE;
    for (i = k; i + 1 < m->n; i++) m->it[i] = m->it[i + 1];
    m->n--;
    return TRUE;
}

BOOL API DeleteMenu(HMENU h, WORD pos, WORD fl)
{
    WORD k;
    MENU __far *m = find_item(h, pos, fl, (WORD __far *)&k);
    if (!m) return FALSE;
    if (m->it[k].flags & MF_POPUP) DestroyMenu(m->it[k].id);
    return RemoveMenu(h, pos, fl);
}

BOOL API ChangeMenu(HMENU h, WORD pos, LPCSTR s, WORD id, WORD fl)
{
    if (fl & 0x0100) return AppendMenu(h, fl & ~0x0100, id, s);   /* MF_APPEND */
    if (fl & 0x1000) return DeleteMenu(h, pos, fl & ~0x1000);     /* MF_DELETE */
    if (fl & 0x0080) return ModifyMenu(h, pos, fl & ~0x0080, id, s);
    return InsertMenu(h, pos, fl, id, s);
}

HMENU API GetSubMenu(HMENU h, int pos)
{
    MENU __far *m = mptr(h);
    if (!m || pos < 0 || pos >= (int)m->n || !(m->it[pos].flags & MF_POPUP)) return 0;
    return m->it[pos].id;
}

int  API GetMenuItemCount(HMENU h) { MENU __far *m = mptr(h); return m ? m->n : -1; }

WORD API GetMenuItemID(HMENU h, int pos)
{
    MENU __far *m = mptr(h);
    if (!m || pos < 0 || pos >= (int)m->n) return 0xFFFF;
    return (m->it[pos].flags & MF_POPUP) ? 0xFFFF : m->it[pos].id;
}

WORD API GetMenuState(HMENU h, WORD pos, WORD fl)
{
    WORD k;
    MENU __far *m = find_item(h, pos, fl, (WORD __far *)&k);
    if (!m) return 0xFFFF;
    if (m->it[k].flags & MF_POPUP) {
        MENU __far *s = mptr(m->it[k].id);
        return (WORD)((s ? s->n : 0) << 8) | (m->it[k].flags & 0xFF);
    }
    return m->it[k].flags;
}

int API GetMenuString(HMENU h, WORD pos, LPSTR buf, int n, WORD fl)
{
    WORD k;
    MENU __far *m = find_item(h, pos, fl, (WORD __far *)&k);
    if (!m || n <= 0) return 0;
    ustrncpy(buf, (LPCSTR)m->it[k].text, n);
    return ustrlen(buf);
}

BOOL API CheckMenuItem(HMENU h, WORD id, WORD fl)
{
    WORD k, o;
    MENU __far *m = find_item(h, id, fl, (WORD __far *)&k);
    if (!m) return (BOOL)0xFFFF;
    o = m->it[k].flags & MF_CHECKED;
    m->it[k].flags = (m->it[k].flags & ~MF_CHECKED) | (fl & MF_CHECKED);
    return o;
}

BOOL API EnableMenuItem(HMENU h, WORD id, WORD fl)
{
    WORD k, o;
    MENU __far *m = find_item(h, id, fl, (WORD __far *)&k);
    if (!m) return (BOOL)0xFFFF;
    o = m->it[k].flags & (MF_GRAYED | MF_DISABLED);
    m->it[k].flags = (m->it[k].flags & ~(MF_GRAYED | MF_DISABLED)) | (fl & (MF_GRAYED | MF_DISABLED));
    return o;
}

BOOL API HiliteMenuItem(HWND w, HMENU h, WORD id, WORD fl)
{
    WORD k;
    MENU __far *m = find_item(h, id, fl, (WORD __far *)&k);
    if (!m) return FALSE;
    if (fl & MF_HILITE) m->it[k].flags |= MF_HILITE; else m->it[k].flags &= ~MF_HILITE;
    (void)w;
    return TRUE;
}

DWORD API GetMenuCheckMarkDimensions(void) { return MAKELONG(13, 13); }
BOOL  API SetMenuItemBitmaps(HMENU h, WORD p, WORD f, HBITMAP a, HBITMAP b) { (void)h; (void)p; (void)f; (void)a; (void)b; return TRUE; }

HMENU API GetMenu(HWND h)
{
    WND __far *w = WP(h);
    return (w && !(w->style & WS_CHILD)) ? w->id : 0;
}

BOOL API SetMenu(HWND h, HMENU m)
{
    WND __far *w = WP(h);
    if (!w || (w->style & WS_CHILD)) return FALSE;
    w->id = m;
    SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020);
    return TRUE;
}

void API DrawMenuBar(HWND h) { if (WP(h)) nc_paint(h); }

/* ============================================================
 * Szablon RT_MENU (Win16): naglowek 4 B, pozycje: WORD flagi [WORD id], tekst
 * ============================================================ */
static BYTE __far *parse_menu(HMENU h, BYTE __far *p)
{
    for (;;) {
        WORD fl = *(WORD __far *)p, id = 0;
        LPCSTR text;
        p += 2;
        if (fl & MF_POPUP) {
            HMENU sub = CreatePopupMenu();
            text = (LPCSTR)p;
            p += ustrlen(text) + 1;
            p = parse_menu(sub, p);
            AppendMenu(h, (fl & ~0x0080) | MF_POPUP, sub, text);
        } else {
            id = *(WORD __far *)p;
            p += 2;
            text = (LPCSTR)p;
            p += ustrlen(text) + 1;
            if (!id && !*text && !(fl & ~0x0080)) AppendMenu(h, MF_SEPARATOR, 0, 0);
            else AppendMenu(h, fl & ~0x0080, id, text);
        }
        if (fl & 0x0080) break;                     /* MF_END */
    }
    return p;
}

HMENU API LoadMenuIndirect(LPVOID tmpl)
{
    HMENU h = CreateMenu();
    BYTE __far *p = (BYTE __far *)tmpl;
    if (!h) return 0;
    p += 4 + *(WORD __far *)(p + 2);
    parse_menu(h, p);
    return h;
}

HMENU API LoadMenu(WORD hinst, LPCSTR name)
{
    WORD hr = FindResource(hinst, name, (LPCSTR)MAKELONG(4, 0));
    LPVOID p;
    if (!hr) return 0;
    p = LockResource(LoadResource(hinst, hr));
    return p ? LoadMenuIndirect(p) : 0;
}

/* ============================================================
 * Menu systemowe
 * ============================================================ */
static HMENU g_sysmenu;

HMENU API GetSystemMenu(HWND h, BOOL revert)
{
    WND __far *w = WP(h);
    (void)revert;
    if (!w) return 0;
    if (!g_sysmenu) {
        g_sysmenu = CreatePopupMenu();
        AppendMenu(g_sysmenu, 0, SC_RESTORE, "&Restore");
        AppendMenu(g_sysmenu, 0, SC_MOVE, "&Move");
        AppendMenu(g_sysmenu, 0, SC_SIZE, "&Size");
        AppendMenu(g_sysmenu, 0, SC_MINIMIZE, "Mi&nimize");
        AppendMenu(g_sysmenu, 0, SC_MAXIMIZE, "Ma&ximize");
        AppendMenu(g_sysmenu, MF_SEPARATOR, 0, 0);
        AppendMenu(g_sysmenu, 0, SC_CLOSE, "&Close\tAlt+F4");
    }
    EnableMenuItem(g_sysmenu, SC_RESTORE, (w->flags & (WF_MAXIMIZED | WF_MINIMIZED)) ? 0 : MF_GRAYED);
    EnableMenuItem(g_sysmenu, SC_MAXIMIZE, ((w->style & WS_MAXIMIZEBOX) && !(w->flags & WF_MAXIMIZED)) ? 0 : MF_GRAYED);
    EnableMenuItem(g_sysmenu, SC_MINIMIZE, (w->style & WS_MINIMIZEBOX) ? 0 : MF_GRAYED);
    EnableMenuItem(g_sysmenu, SC_SIZE, (w->style & WS_THICKFRAME) ? 0 : MF_GRAYED);
    EnableMenuItem(g_sysmenu, SC_MOVE, MF_GRAYED);
    return g_sysmenu;
}

/* ============================================================
 * Rysowanie
 * ============================================================ */
int menu_bar_height(HWND h)
{
    WND __far *w = WP(h);
    return (w && !(w->style & WS_CHILD) && mptr(w->id)) ? 19 : 0;
}

/* MF_OWNERDRAW: zapytaj wlasciciela o rozmiar (raz) */
static void od_measure(MITEM __far *it)
{
    struct { WORD type, id, item, w, h; DWORD data; } mi;
    if (!(it->flags & MF_OWNERDRAW) || it->odh || !WP(g_mowner)) return;
    mi.type = 1; mi.id = 0; mi.item = it->id; mi.w = 0; mi.h = 18; mi.data = it->data;
    send_msg(g_mowner, WM_MEASUREITEM, 0, (LONG)(LPVOID)&mi);
    it->odw = mi.w + 14;                     /* jak Windows: + 2 srednie szerokosci znaku (Wine MENU_CalcItemSize) */
    it->odh = mi.h ? mi.h : 1;
}

static int item_w(HDC dc, MITEM __far *it)
{
    if (it->flags & MF_OWNERDRAW) { od_measure(it); return it->odw; }
    if (it->flags & MF_SEPARATOR) return 8;
    {
        RECT r;
        WORD of = SelectObject(dc, chrome_font());
        SetRect((LPRECT)&r, 0, 0, 0, 0);
        DrawText(dc, (LPCSTR)it->text, -1, (LPRECT)&r, 0x0400 | 0x0020);   /* CALCRECT|SINGLELINE */
        SelectObject(dc, of);
        return r.right;
    }
}

/* Prostokat pozycji paska menu (wsp. okna) */
static BOOL bar_item_rect(HWND h, HDC dc, WORD i, LPRECT r)
{
    WND __far *w = WP(h);
    MENU __far *m = mptr(w->id);
    int x, k, top;
    if (!m || i >= m->n) return FALSE;
    top = w->cr.top - w->wr.top - 19;
    x = w->cr.left - w->wr.left;
    for (k = 0; k <= (int)i; k++) {
        int iw = item_w(dc, &m->it[k]) + 16;
        if (k == (int)i) { SetRect(r, x, top, x + iw, top + 18); return TRUE; }
        x += iw;
    }
    return FALSE;
}

static void draw_bar_item(HWND h, HDC dc, WORD i, BOOL hi)
{
    WND __far *w = WP(h);
    MENU __far *m = mptr(w->id);
    RECT r;
    WORD of;
    if (!bar_item_rect(h, dc, i, (LPRECT)&r)) return;
    fill_color(dc, r.left, r.top, r.right, r.bottom, hi ? syscolor(COLOR_ACTIVECAPTION) : MENUBAR_BG);
    of = SelectObject(dc, chrome_font());
    SetBkMode(dc, 1);
    SetTextColor(dc, hi ? syscolor(COLOR_CAPTIONTEXT) : syscolor((m->it[i].flags & MF_GRAYED) ? COLOR_GRAYTEXT : COLOR_MENUTEXT));
    DrawText(dc, (LPCSTR)m->it[i].text, -1, (LPRECT)&r, 0x0001 | 0x0004 | 0x0020);
    SelectObject(dc, of);
}

void menu_draw_bar(HWND h, HDC dc)
{
    WND __far *w = WP(h);
    MENU __far *m = mptr(w->id);
    int top, l, rr;
    WORD i;
    if (!m) return;
    top = w->cr.top - w->wr.top - 19;
    l = w->cr.left - w->wr.left;
    rr = w->wr.right - w->wr.left - (w->cr.left - w->wr.left);
    fill_color(dc, l, top, rr, top + 18, MENUBAR_BG);
    fill_color(dc, l, top + 18, rr, top + 19, MENUBAR_LINE);
    for (i = 0; i < m->n; i++) draw_bar_item(h, dc, i, (m->it[i].flags & MF_HILITE) != 0);
}

/* ============================================================
 * Menu rozwijane (okno klasy #32768)
 * ============================================================ */
static HMENU g_pm;          /* menu aktualnie rysowane w oknie popup */
static int   g_psel = -1;

static int popup_item_h(MITEM __far *it)
{
    if (it->flags & MF_OWNERDRAW) { od_measure(it); return it->odh; }
    return (it->flags & MF_SEPARATOR) ? 8 : 18;
}

static void popup_size(HMENU hm, int __far *cw, int __far *ch)
{
    MENU __far *m = mptr(hm);
    HDC dc = GetDC(0);
    int w = 60, h = 4;
    WORD i;
    for (i = 0; i < m->n; i++) {
        int iw = item_w(dc, &m->it[i]) + ((m->it[i].flags & MF_OWNERDRAW) ? 4 : 40);
        if (iw > w) w = iw;
        h += popup_item_h(&m->it[i]);
    }
    ReleaseDC(0, dc);
    *cw = w; *ch = h;
}

static void popup_draw(HWND h, HDC dc, HMENU hm, int sel)
{
    MENU __far *m = mptr(hm);
    RECT r;
    int y = 2, W;
    WORD i;
    GetClientRect(h, (LPRECT)&r);
    W = r.right;
    if (!m) return;
    for (i = 0; i < m->n; i++) {
        MITEM __far *it = &m->it[i];
        int ih = popup_item_h(it);
        BOOL hi = (int)i == sel && !(it->flags & MF_SEPARATOR);
        if (it->flags & MF_OWNERDRAW) {
            struct { WORD type, id, item, action, state; HWND hw; HDC dc; RECT rc; DWORD data; } di;
            di.type = 1; di.id = 0; di.item = it->id; di.action = 1;          /* ODT_MENU, ODA_DRAWENTIRE */
            di.state = (hi ? 1 : 0) | ((it->flags & MF_GRAYED) ? 2 : 0) | ((it->flags & MF_CHECKED) ? 8 : 0);
            di.hw = hm; di.dc = dc; di.data = it->data;
            SetRect((LPRECT)&di.rc, 1, y, W - 1, y + ih);
            if (WP(g_mowner)) send_msg(g_mowner, WM_DRAWITEM, 0, (LONG)(LPVOID)&di);
            if (it->flags & MF_POPUP) {
                int a, cy = y + ih / 2;
                COLORREF c = syscolor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
                for (a = 0; a < 4; a++) fill_color(dc, W - 10 + a, cy - 4 + a, W - 9 + a, cy + 4 - a, c);
            }
            y += ih;
            continue;
        }
        fill_color(dc, 1, y, W - 1, y + ih, syscolor(hi ? COLOR_HIGHLIGHT : COLOR_MENU));
        if (it->flags & MF_SEPARATOR) fill_color(dc, 2, y + 3, W - 2, y + 4, POPUP_SEP);
        else {
            LPSTR tab;
            RECT tr;
            int k;
            WORD of = SelectObject(dc, chrome_font());
            SetBkMode(dc, 1);
            SetTextColor(dc, syscolor(hi ? COLOR_HIGHLIGHTTEXT : (it->flags & MF_GRAYED) ? COLOR_GRAYTEXT : COLOR_MENUTEXT));
            SetRect((LPRECT)&tr, 18, y, W - 8, y + ih);
            for (k = 0, tab = 0; it->text[k]; k++) if (it->text[k] == '\t') { tab = (LPSTR)it->text + k; break; }
            DrawText(dc, (LPCSTR)it->text, tab ? (int)(tab - (LPSTR)it->text) : -1, (LPRECT)&tr, 0x0004 | 0x0020);
            if (tab) DrawText(dc, tab + 1, -1, (LPRECT)&tr, 0x0002 | 0x0004 | 0x0020);
            SelectObject(dc, of);
            if (it->flags & MF_CHECKED) fill_color(dc, 6, y + 7, 12, y + 11, syscolor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
            if (it->flags & MF_POPUP) {
                int a;
                for (a = 0; a < 4; a++) fill_color(dc, W - 10 + a, y + 5 + a, W - 9 + a, y + 13 - a, syscolor(COLOR_MENUTEXT));
            }
        }
        y += ih;
    }
}

static LRESULT __far __pascal MenuPopupProc(HWND h, WORD msg, WORD wp, LONG lp)
{
    if (msg == WM_PAINT) {
        struct { HDC hdc; BOOL e; RECT rc; BYTE r[20]; } ps;
        extern HDC API BeginPaint(HWND, LPVOID);
        extern void API EndPaint(HWND, LPVOID);
        RECT r;
        HDC dc = BeginPaint(h, (LPVOID)&ps);
        GetClientRect(h, (LPRECT)&r);
        fill_color(dc, 0, 0, r.right, r.bottom, syscolor(COLOR_MENU));
        draw_3d(dc, 0, 0, r.right, r.bottom, POPUP_FRAME, POPUP_FRAME);
        popup_draw(h, dc, (HMENU)GetWindowWord(h, 0), (int)GetWindowWord(h, 2) == 0xFFFF ? -1 : (int)GetWindowWord(h, 2));
        EndPaint(h, (LPVOID)&ps);
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return 3;                       /* MA_NOACTIVATE */
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProc(h, msg, wp, lp);
}

void menu_register(void)
{
    register_system_class("#32768", 0x0800 /* CS_SAVEBITS */, (WNDPROC)MenuPopupProc, 4, 0, 0);
}

static int popup_hit(HWND pw, HMENU hm, int x, int y)
{
    WND __far *w = WP(pw);
    MENU __far *m = mptr(hm);
    int yy, i;
    if (!w || x < w->wr.left || x >= w->wr.right || y < w->wr.top || y >= w->wr.bottom) return -2;
    yy = w->cr.top + 2;
    for (i = 0; i < (int)m->n; i++) {
        int ih = popup_item_h(&m->it[i]);
        if (y >= yy && y < yy + ih) return i;
        yy += ih;
    }
    return -1;
}

static void popup_select(HWND pw, int sel)
{
    HDC dc;
    if ((int)GetWindowWord(pw, 2) == sel || (sel < 0 && GetWindowWord(pw, 2) == 0xFFFF)) return;
    SetWindowWord(pw, 2, (WORD)sel);
    dc = GetDC(pw);
    popup_draw(pw, dc, (HMENU)GetWindowWord(pw, 0), sel);
    ReleaseDC(pw, dc);
}

/* Petla menu rozwijanego; wynik: ID wybranej pozycji, 0 = anulowano,
 * -1/-2 = zamkniete strzalka w lewo/prawo (dla nawigacji po pasku) */
int menu_track_popup(HMENU hm, int x, int y, HWND owner, BOOL sys)
{
    MENU __far *m = mptr(hm);
    HWND pw;
    int cw, ch, sel = -1, result = 0, i;
    MSG msg;
    BOOL moved = FALSE;
    if (!m || !m->n) return 0;
    g_mowner = owner;
    send_msg(owner, WM_INITMENUPOPUP, hm, MAKELONG(0, sys));
    popup_size(hm, (int __far *)&cw, (int __far *)&ch);
    if (x + cw > SCR_W) x = SCR_W - cw;
    if (y + ch > SCR_H) y = SCR_H - ch;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    pw = CreateWindowEx(WS_EX_TOPMOST, "#32768", "", WS_POPUP, x, y, cw, ch, owner, 0, 0, 0);
    if (!pw) return 0;
    SetWindowWord(pw, 0, hm);
    SetWindowWord(pw, 2, 0xFFFF);
    SetWindowPos(pw, 0xFFFF, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040 | 0x0010);
    UpdateWindow(pw);
    SetCapture(pw);
    while (modal_next((LPMSG)&msg)) {
        int mx = mouse_x(), my = mouse_y();
        if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
            int hit = popup_hit(pw, hm, mx, my);
            moved = TRUE;
            if (hit >= 0 && hit != sel) {
                sel = hit;
                popup_select(pw, sel);
                send_msg(owner, WM_MENUSELECT, m->it[sel].id, MAKELONG(m->it[sel].flags, hm));
                if (m->it[sel].flags & MF_POPUP) {
                    WND __far *w = WP(pw);
                    int yy = w->cr.top + 2, k;
                    for (k = 0; k < sel; k++) yy += popup_item_h(&m->it[k]);
                    result = menu_track_popup(m->it[sel].id, w->wr.right - 2, yy, owner, sys);
                    SetCapture(pw);
                    if (result > 0 || result == 0) {
                        if (result > 0) break;
                        if (!(mouse_btn() & 1) && popup_hit(pw, hm, mouse_x(), mouse_y()) == -2) break;
                    }
                    result = 0;
                }
            }
        } else if (msg.message == WM_LBUTTONUP || msg.message == WM_RBUTTONUP) {
            int hit = popup_hit(pw, hm, mx, my);
            if (hit >= 0 && !(m->it[hit].flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED | MF_POPUP))) {
                result = m->it[hit].id;
                break;
            }
            if (hit == -2 && moved) break;
        } else if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) {
            if (popup_hit(pw, hm, mx, my) == -2) { result = 0; break; }
        } else if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            WORD k = msg.wParam;
            if (k == 0x1B) { result = 0; break; }
            if (k == 0x25) { result = -1; break; }
            if (k == 0x27 && !(sel >= 0 && (m->it[sel].flags & MF_POPUP))) { result = -2; break; }
            if (k == 0x26 || k == 0x28) {
                for (i = 0; i < (int)m->n; i++) {
                    sel = sel < 0 ? (k == 0x28 ? 0 : m->n - 1) : (sel + (k == 0x28 ? 1 : (int)m->n - 1)) % m->n;
                    if (!(m->it[sel].flags & MF_SEPARATOR)) break;
                }
                popup_select(pw, sel);
            } else if (k == 0x0D || (k == 0x27 && sel >= 0)) {
                if (sel >= 0 && (m->it[sel].flags & MF_POPUP)) {
                    WND __far *w = WP(pw);
                    result = menu_track_popup(m->it[sel].id, w->wr.right - 2, w->wr.top + 2, owner, sys);
                    SetCapture(pw);
                    if (result > 0) break;
                    result = 0;
                } else if (sel >= 0 && !(m->it[sel].flags & (MF_GRAYED | MF_DISABLED))) {
                    result = m->it[sel].id;
                    break;
                }
            }
        } else if (msg.message == WM_CHAR) {             /* mnemonik */
            char c = (char)msg.wParam;
            for (i = 0; i < (int)m->n; i++) {
                LPSTR t = (LPSTR)m->it[i].text;
                int k;
                for (k = 0; t[k]; k++)
                    if (t[k] == '&' && t[k + 1] && (t[k + 1] | 0x20) == (c | 0x20)) break;
                if (t[k] && !(m->it[i].flags & (MF_GRAYED | MF_DISABLED))) {
                    if (m->it[i].flags & MF_POPUP) {
                        WND __far *w = WP(pw);
                        result = menu_track_popup(m->it[i].id, w->wr.right - 2, w->wr.top + 2, owner, sys);
                        SetCapture(pw);
                    } else result = m->it[i].id;
                    break;
                }
            }
            if (result > 0) break;
            result = 0;
        }
    }
    ReleaseCapture();
    DestroyWindow(pw);
    return result;
}

BOOL API TrackPopupMenu(HMENU hm, WORD fl, int x, int y, int res, HWND owner, LPRECT r)
{
    int cmd;
    (void)res; (void)r;
    g_mowner = owner;
    if (fl & 0x0004) { int cw, ch; popup_size(hm, (int __far *)&cw, (int __far *)&ch); x -= cw / 2; }  /* TPM_CENTERALIGN */
    else if (fl & 0x0008) { int cw, ch; popup_size(hm, (int __far *)&cw, (int __far *)&ch); x -= cw; } /* TPM_RIGHTALIGN */
    send_msg(owner, WM_INITMENU, hm, 0);
    cmd = menu_track_popup(hm, x, y, owner, FALSE);
    if (cmd > 0) post_msg(owner, WM_COMMAND, cmd, 0);
    send_msg(owner, WM_MENUSELECT, 0, MAKELONG(0xFFFF, 0));
    return TRUE;
}

/* Pasek menu: klik (x, y >= 0) albo klawiatura (x < 0; key = mnemonik lub 0) */
void menu_track_bar(HWND h, int x, int y, WORD key)
{
    WND __far *w = WP(h);
    MENU __far *m;
    HDC dc;
    int sel = 0, i, cmd = 0;
    if (!w || !(m = mptr(w->id)) || !m->n) return;
    dc = GetWindowDC(h);
    if (x >= 0) {
        for (sel = 0; sel < (int)m->n; sel++) {
            RECT r;
            POINT p;
            bar_item_rect(h, dc, sel, (LPRECT)&r);
            p.x = x - w->wr.left; p.y = y - w->wr.top;
            if (PtInRect((LPRECT)&r, p)) break;
        }
        if (sel >= (int)m->n) { ReleaseDC(h, dc); return; }
    } else if (key) {
        for (sel = 0; sel < (int)m->n; sel++) {
            LPSTR t = (LPSTR)m->it[sel].text;
            int k;
            for (k = 0; t[k]; k++) if (t[k] == '&' && (t[k + 1] | 0x20) == ((char)key | 0x20)) break;
            if (t[k]) break;
        }
        if (sel >= (int)m->n) { ReleaseDC(h, dc); return; }
    }
    send_msg(h, WM_INITMENU, w->id, 0);
    for (;;) {
        RECT r;
        draw_bar_item(h, dc, sel, TRUE);
        bar_item_rect(h, dc, sel, (LPRECT)&r);
        if (m->it[sel].flags & MF_POPUP) {
            cmd = menu_track_popup(m->it[sel].id, w->wr.left + r.left, w->wr.top + r.bottom + 1, h, FALSE);
        } else {
            cmd = (m->it[sel].flags & (MF_GRAYED | MF_DISABLED)) ? 0 : m->it[sel].id;
        }
        draw_bar_item(h, dc, sel, FALSE);
        if (cmd == -1) { sel = (sel + m->n - 1) % m->n; continue; }
        if (cmd == -2) { sel = (sel + 1) % m->n; continue; }
        /* klik w inna pozycje paska podczas otwartego menu */
        if (cmd == 0 && (mouse_btn() & 1)) {
            int mx2 = mouse_x() - w->wr.left, my2 = mouse_y() - w->wr.top;
            for (i = 0; i < (int)m->n; i++) {
                RECT r2;
                POINT p;
                bar_item_rect(h, dc, i, (LPRECT)&r2);
                p.x = mx2; p.y = my2;
                if (PtInRect((LPRECT)&r2, p)) break;
            }
            if (i < (int)m->n && i != sel) { sel = i; continue; }
        }
        break;
    }
    ReleaseDC(h, dc);
    send_msg(h, WM_MENUSELECT, 0, MAKELONG(0xFFFF, 0));
    if (cmd > 0) post_msg(h, WM_COMMAND, cmd, 0);
}
