/* icontest.c - test okien zminimalizowanych (ikon) USER:
 * ShowWindow SW_MINIMIZE/SW_SHOWMINNOACTIVE/SW_RESTORE, IsIconic, prostokat ikony,
 * WM_SIZE(SIZE_MINIMIZED), WM_PAINTICON/WM_ICONERASEBKGND (klasa z ikona) i WM_PAINT
 * (klasa bez ikony), WindowFromPoint, ArrangeIconicWindows, WM_QUERYOPEN, OpenIcon,
 * powrot do maksymalizacji, Get/SetWindowPlacement (ptMinPosition), WS_MINIMIZE
 * w CreateWindow, okno tytulu ikony. Na koniec czeka na dwuklik w ikone (krok
 * run_examples.py) - przywrocenie mysza. Alt+F4 zamyka.
 */
#include <windows.h>
#include "testlog.h"

#define ICON_X 300                  /* tu ikona A czeka na dwuklik */
#define ICON_Y 200

static HWND g_a, g_b;
static int g_sizemin, g_painticon, g_iconerase, g_paint_b_iconic, g_queryopen;
static BOOL g_deny, g_waiting;

static void pump(void)
{
    MSG m;
    DWORD end = GetTickCount() + 300;
    while (GetTickCount() < end)
        if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
}

static int rw(RECT *r) { return r->right - r->left; }
static int rh(RECT *r) { return r->bottom - r->top; }

static HWND find_title(HWND icon)
{
    HWND c;
    char cls[16];
    for (c = GetWindow(GetDesktopWindow(), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        GetClassName(c, cls, sizeof(cls));
        if (!lstrcmp(cls, "#32772") && GetWindow(c, GW_OWNER) == icon) return c;
    }
    return 0;
}

static void tests(HINSTANCE hi)
{
    RECT orig, r, rb, rt;
    POINT p;
    HWND c, t;
    WINDOWPLACEMENT wp;
    WORD row;

    GetWindowRect(g_a, &orig);

    /* minimalizacja okna z ikona klasy */
    g_sizemin = 0;
    ShowWindow(g_a, SW_MINIMIZE);
    pump();
    CHECK(IsIconic(g_a), "SW_MINIMIZE: IsIconic");
    CHECK(IsWindowVisible(g_a), "ikona jest widoczna (IsWindowVisible)");
    GetWindowRect(g_a, &r);
    CHECK_INT(rw(&r), GetSystemMetrics(SM_CXICON), "ikona: szerokosc = SM_CXICON");
    CHECK_INT(rh(&r), GetSystemMetrics(SM_CYICON), "ikona: wysokosc = SM_CYICON");
    CHECK(r.top >= GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYICONSPACING), "ikona na dole ekranu");
    CHECK(g_sizemin, "WM_SIZE(SIZE_MINIMIZED)");
    CHECK(g_painticon, "WM_PAINTICON (klasa z ikona)");
    CHECK(g_iconerase, "WM_ICONERASEBKGND (klasa z ikona)");
    p.x = (orig.left + orig.right) / 2; p.y = (orig.top + orig.bottom) / 2;
    CHECK(WindowFromPoint(p) != g_a, "stare miejsce okna nie trafia w ikone");
    p.x = (r.left + r.right) / 2; p.y = (r.top + r.bottom) / 2;
    CHECK(WindowFromPoint(p) == g_a, "WindowFromPoint(srodek ikony) = okno");
    CHECK(GetActiveWindow() != g_a, "SW_MINIMIZE aktywuje nastepne okno");
    t = find_title(g_a);
    CHECK(t && IsWindowVisible(t), "okno tytulu ikony (#32772) widoczne");
    if (t) {
        GetWindowRect(t, &rt);
        CHECK(rt.top >= r.bottom && rt.top < r.bottom + 8, "tytul tuz pod ikona");
    }

    /* okno bez ikony klasy: maluje ikone samo w WM_PAINT */
    ShowWindow(g_b, SW_SHOWMINNOACTIVE);
    pump();
    CHECK(IsIconic(g_b), "SW_SHOWMINNOACTIVE: IsIconic");
    CHECK(g_paint_b_iconic, "klasa bez ikony: WM_PAINT gdy IsIconic");
    GetWindowRect(g_b, &rb);
    CHECK(!IntersectRect(&rt, &r, &rb), "druga ikona w innym miejscu");
    {   /* WS_CLIPCHILDREN + okno potomne: ikona malowana w calosci (potomkowie ikony
         * niewidoczni i nie wycinani - dawniej zostawal tam stary obraz ekranu) */
        HDC sdc = GetDC(NULL);
        COLORREF px = GetPixel(sdc, rb.left + 20, rb.top + 26);   /* pod przyciskiem potomnym */
        ReleaseDC(NULL, sdc);
        CHECK(px == RGB(255, 200, 0), "ikona z WS_CLIPCHILDREN: obszar pod oknem potomnym namalowany");
    }

    /* ArrangeIconicWindows po przesunieciu ikony */
    SetWindowPos(g_a, 0, 200, 150, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(g_a, &r);
    CHECK(r.left == 200 && r.top == 150, "SetWindowPos przesuwa ikone");
    row = ArrangeIconicWindows(GetDesktopWindow());
    CHECK(row > 0, "ArrangeIconicWindows: wysokosc rzedu > 0");
    GetWindowRect(g_a, &r);
    GetWindowRect(g_b, &rb);
    CHECK(r.top == rb.top && r.left != rb.left, "ArrangeIconicWindows: ikony w jednym rzedzie");
    CHECK(r.top >= GetSystemMetrics(SM_CYSCREEN) - (int)row, "ArrangeIconicWindows: rzad na dole");
    wp.length = sizeof(wp);
    GetWindowPlacement(g_b, &wp);
    CHECK_INT(wp.showCmd, SW_SHOWMINIMIZED, "GetWindowPlacement: showCmd");
    CHECK(wp.ptMinPosition.x == rb.left && wp.ptMinPosition.y == rb.top, "GetWindowPlacement: ptMinPosition");

    /* WM_QUERYOPEN: aplikacja moze nie pozwolic */
    g_deny = TRUE; g_queryopen = 0;
    ShowWindow(g_a, SW_RESTORE);
    CHECK(g_queryopen && IsIconic(g_a), "WM_QUERYOPEN = FALSE: zostaje ikona");
    g_deny = FALSE;
    OpenIcon(g_a);
    pump();
    CHECK(!IsIconic(g_a), "OpenIcon: przywrocone");
    GetWindowRect(g_a, &r);
    CHECK(EqualRect(&r, &orig), "przywrocone: dawny prostokat");
    CHECK(!IsWindowVisible(find_title(g_a)) , "tytul ikony ukryty po przywroceniu");

    /* zminimalizowane z maksymalizacji -> SW_RESTORE maksymalizuje */
    ShowWindow(g_a, SW_SHOWMAXIMIZED);
    ShowWindow(g_a, SW_SHOWMINIMIZED);
    CHECK(IsIconic(g_a) && GetActiveWindow() == g_a, "SW_SHOWMINIMIZED: ikona aktywna");
    ShowWindow(g_a, SW_RESTORE);
    CHECK(IsZoomed(g_a), "SW_RESTORE ikony: z powrotem zmaksymalizowane");
    ShowWindow(g_a, SW_SHOWNORMAL);
    GetWindowRect(g_a, &r);
    CHECK(EqualRect(&r, &orig), "SW_SHOWNORMAL: dawny prostokat");

    /* SetWindowPlacement z WPF_SETMINPOSITION */
    wp.length = sizeof(wp);
    GetWindowPlacement(g_a, &wp);
    wp.flags = WPF_SETMINPOSITION;
    wp.ptMinPosition.x = ICON_X; wp.ptMinPosition.y = ICON_Y;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    SetWindowPlacement(g_a, &wp);
    pump();
    GetWindowRect(g_a, &r);
    CHECK(IsIconic(g_a) && r.left == ICON_X && r.top == ICON_Y, "SetWindowPlacement: ikona w ptMinPosition");

    /* CreateWindow z WS_MINIMIZE */
    c = CreateWindow("IconTest", "Od razu ikona", WS_OVERLAPPEDWINDOW | WS_MINIMIZE | WS_VISIBLE,
                     100, 100, 200, 120, NULL, NULL, hi, NULL);
    pump();
    GetWindowRect(c, &r);
    CHECK(IsIconic(c) && IsWindowVisible(c) && rw(&r) == GetSystemMetrics(SM_CXICON), "CreateWindow(WS_MINIMIZE)");
    ShowWindow(c, SW_RESTORE);
    GetWindowRect(c, &r);
    CHECK(!IsIconic(c) && rw(&r) == 200 && rh(&r) == 120, "WS_MINIMIZE: przywrocone do rozmiaru z CreateWindow");
    DestroyWindow(c);
    /* LoadIcon z nazwa, ktorej nie ma w module: pierwsza ikona modulu (SkiFree: "iconSki") */
    CHECK(LoadIcon(hi, "BrakTakiejIkony") != NULL, "LoadIcon: brak nazwy -> pierwsza ikona modulu");
    /* klasa bez ikony, WM_PAINT z DefWindowProc: domyslna ikona na tle pulpitu, nie bialy kwadrat */
    c = CreateWindow("NoIcon", "Domyslna", WS_OVERLAPPEDWINDOW, 10, 10, 200, 100, NULL, NULL, hi, NULL);
    ShowWindow(c, SW_SHOWMINNOACTIVE);
    UpdateWindow(c);
    pump();
    GetWindowRect(c, &r);
    {
        HDC sdc = GetDC(0);
        COLORREF bg = GetSysColor(COLOR_BACKGROUND);
        CHECK(GetPixel(sdc, r.left, r.top) == bg, "ikona domyslna: rog ma tlo pulpitu");
        CHECK(GetPixel(sdc, r.left + 16, r.top + 8) == RGB(0x0A, 0x6E, 0x7E), "ikona domyslna: pasek tytulu");
        CHECK(GetPixel(sdc, r.left + 16, r.top + 20) == RGB(255, 255, 255), "ikona domyslna: wnetrze");
        /* wlasne malowanie aplikacji bez ikony klasy zostaje nietkniete */
        ShowWindow(g_b, SW_SHOWMINNOACTIVE);
        UpdateWindow(g_b);
        pump();
        GetWindowRect(g_b, &rb);
        CHECK(GetPixel(sdc, rb.left + 16, rb.top + 16) == RGB(255, 200, 0), "wlasny WM_PAINT ikony: rysunek aplikacji");
        CHECK(GetPixel(sdc, rb.left + 1, rb.top + 1) == RGB(255, 255, 255), "wlasny WM_PAINT ikony: tlo klasy");
        ReleaseDC(0, sdc);
    }
    DestroyWindow(c);
    pump();
    /* Tytul ikony: MS Sans Serif jak w Windows 3.1; szeroki font ucinal "My Computer" (Calmira) */
    c = CreateWindow("NoIcon", "My Computer", WS_OVERLAPPEDWINDOW, 10, 10, 200, 100, NULL, NULL, hi, NULL);
    ShowWindow(c, SW_SHOWMINNOACTIVE);
    pump();
    t = find_title(c);
    GetWindowRect(t, &rt);
    {
        HDC sdc = GetDC(0);
        HFONT f = CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "MS Sans Serif"), of = SelectObject(sdc, f);
        int tw = LOWORD(GetTextExtent(sdc, "My Computer", 11));
        SelectObject(sdc, of);
        DeleteObject(f);
        ReleaseDC(0, sdc);
        CHECK_INT(rw(&rt), tw + 6, "tytul ikony: caly tekst fontem MS Sans Serif");
        CHECK_INT(rh(&rt), 15, "tytul ikony: wysokosc dla fontu 13 px");
    }
    DestroyWindow(c);
    pump();
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) g_sizemin++;
        if (wp != SIZE_MINIMIZED && g_waiting && hwnd == g_a) {
            g_waiting = FALSE;
            KillTimer(hwnd, 1);
            CHECK(!IsIconic(g_a), "dwuklik w ikone przywraca okno");
            t_done();
        }
        break;
    case WM_PAINTICON:
        g_painticon++;
        break;
    case WM_ICONERASEBKGND:
        g_iconerase++;
        break;
    case WM_PAINT:
        if (hwnd == g_b && IsIconic(hwnd)) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT r;
            HBRUSH br = CreateSolidBrush(RGB(255, 200, 0));
            g_paint_b_iconic++;
            GetClientRect(hwnd, &r);
            InflateRect(&r, -4, -4);
            FillRect(dc, &r, br);
            DeleteObject(br);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_QUERYOPEN:
        g_queryopen++;
        return !g_deny;
    case WM_TIMER:
        if (g_waiting) {
            g_waiting = FALSE;
            KillTimer(hwnd, 1);
            CHECK(FALSE, "dwuklik w ikone przywraca okno (brak dwukliku)");
            t_done();
        }
        return 0;
    case WM_DESTROY:
        if (hwnd == g_a) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    (void)cmd; (void)show;
    t_app = "ICONTEST";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "IconTest";
        RegisterClass(&wc);
        wc.hIcon = NULL;
        wc.lpszClassName = "NoIcon";
        RegisterClass(&wc);
    }
    g_b = CreateWindow("NoIcon", "Bez ikony klasy", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 330, 60, 260, 160, NULL, NULL, hi, NULL);
    CreateWindow("BUTTON", "Potomne", WS_CHILD | WS_VISIBLE, 0, 0, 200, 100, g_b, (HMENU)1, hi, NULL);
    ShowWindow(g_b, SW_SHOWNORMAL);
    g_a = CreateWindow("IconTest", "Ikony - test", WS_OVERLAPPEDWINDOW, 40, 40, 300, 200, NULL, NULL, hi, NULL);
    ShowWindow(g_a, SW_SHOWNORMAL);
    UpdateWindow(g_a);
    pump();
    tests(hi);
    /* ikona A w (ICON_X, ICON_Y) czeka na dwuklik (run_examples.py), limit 60 s (zegar goscia QEMU biegnie szybciej) */
    g_waiting = TRUE;
    SetTimer(g_a, 1, 60000, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
