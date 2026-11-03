/* odmenu.c - test menu: pasek z zasobu, menu tworzone w kodzie, pozycje
 * MF_OWNERDRAW (WM_MEASUREITEM / WM_DRAWITEM), podmenu, zaznaczenia,
 * wyszarzanie i TrackPopupMenu.
 *
 * Testy API menu ida od razu po utworzeniu okna; potem po starcie
 * otwiera sie samo menu kontekstowe (TrackPopupMenu) z pozycjami owner-draw.
 * Esc zamyka menu - wtedy sprawdzamy, czy WM_MEASUREITEM/WM_DRAWITEM przyszly
 * z poprawnymi danymi. Kolejny Esc konczy program.
 */
#include <windows.h>
#include "testlog.h"

#define IDM_RED    201
#define IDM_GREEN  202
#define IDM_BLUE   203
#define IDM_SUB1   211
#define IDM_PLAIN  220
#define IDM_EXIT   230

static HMENU g_popup, g_sub;
static int g_measured, g_drawn, g_baddata;
static const COLORREF colors[3] = { RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255) };
static const char *names[3] = { "Czerwony", "Zielony", "Niebieski" };

static void menu_tests(HWND hwnd)
{
    HMENU bar = GetMenu(hwnd);
    char buf[40];
    int i;

    CHECK(bar != 0, "GetMenu (pasek z zasobu)");
    CHECK_INT(GetMenuItemCount(bar), 2, "pasek: 2 pozycje");
    GetMenuString(bar, 0, buf, sizeof(buf), MF_BYPOSITION);
    CHECK_STR(buf, "&Plik", "GetMenuString");

    g_popup = CreatePopupMenu();
    g_sub = CreatePopupMenu();
    for (i = 0; i < 3; i++) AppendMenu(g_popup, MF_OWNERDRAW, IDM_RED + i, (LPCSTR)MAKELONG(i, 0x5A5A));
    AppendMenu(g_popup, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_sub, MF_STRING, IDM_SUB1, "Pozycja podmenu");
    AppendMenu(g_popup, MF_POPUP, (UINT)g_sub, "Podmenu");
    AppendMenu(g_popup, MF_STRING, IDM_PLAIN, "Zwykla &pozycja");
    CHECK_INT(GetMenuItemCount(g_popup), 6, "AppendMenu: 6 pozycji");
    CHECK_INT(GetMenuItemID(g_popup, 1), IDM_GREEN, "GetMenuItemID");
    CHECK(GetSubMenu(g_popup, 4) == g_sub, "GetSubMenu");

    CheckMenuItem(g_popup, IDM_PLAIN, MF_CHECKED);
    CHECK((GetMenuState(g_popup, IDM_PLAIN, MF_BYCOMMAND) & MF_CHECKED) != 0, "CheckMenuItem");
    EnableMenuItem(g_popup, IDM_BLUE, MF_GRAYED);
    CHECK((GetMenuState(g_popup, IDM_BLUE, MF_BYCOMMAND) & MF_GRAYED) != 0, "EnableMenuItem(MF_GRAYED)");
    ModifyMenu(g_popup, IDM_PLAIN, MF_BYCOMMAND | MF_STRING, IDM_PLAIN, "Zmieniona");
    GetMenuString(g_popup, IDM_PLAIN, buf, sizeof(buf), MF_BYCOMMAND);
    CHECK_STR(buf, "Zmieniona", "ModifyMenu");
    InsertMenu(g_popup, 0, MF_BYPOSITION | MF_STRING, IDM_EXIT, "Wstawiona");
    CHECK_INT(GetMenuItemID(g_popup, 0), IDM_EXIT, "InsertMenu na poczatek");
    DeleteMenu(g_popup, IDM_EXIT, MF_BYCOMMAND);
    CHECK_INT(GetMenuItemCount(g_popup), 6, "DeleteMenu");
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        menu_tests(hwnd);
        PostMessage(hwnd, WM_USER, 0, 0);
        return 0;
    case WM_USER: {
        BOOL ok;
        ok = TrackPopupMenu(g_popup, 0, 60, 60, 0, hwnd, NULL);
        CHECK(ok, "TrackPopupMenu");
        CHECK(g_measured >= 3, "WM_MEASUREITEM dla pozycji owner-draw");
        CHECK(g_drawn >= 3, "WM_DRAWITEM dla pozycji owner-draw");
        CHECK_INT(g_baddata, 0, "itemData/CtlType poprawne");
        t_done();
        return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT FAR *mi = (MEASUREITEMSTRUCT FAR *)lp;
        if (mi->CtlType != ODT_MENU || HIWORD(mi->itemData) != 0x5A5A) g_baddata++;
        mi->itemWidth = 110;
        mi->itemHeight = 22;
        g_measured++;
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT FAR *di = (DRAWITEMSTRUCT FAR *)lp;
        int i = LOWORD(di->itemData);
        HBRUSH br;
        RECT r = di->rcItem;
        if (di->CtlType != ODT_MENU || HIWORD(di->itemData) != 0x5A5A || i > 2) { g_baddata++; return TRUE; }
        g_drawn++;
        br = CreateSolidBrush(GetSysColor((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_MENU));
        FillRect(di->hDC, &r, br);
        DeleteObject(br);
        br = CreateSolidBrush(colors[i]);
        r.left += 4; r.top += 3; r.right = r.left + 16; r.bottom -= 3;
        FillRect(di->hDC, &r, br);
        DeleteObject(br);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor((di->itemState & ODS_GRAYED) ? COLOR_GRAYTEXT :
                                          (di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        TextOut(di->hDC, di->rcItem.left + 26, di->rcItem.top + 4, names[i], lstrlen(names[i]));
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDM_EXIT) DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        DestroyMenu(g_popup);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "ODMENU";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = "MAINMENU";
        wc.lpszClassName = "OdMenuTest";
        RegisterClass(&wc);
    }
    h = CreateWindow("OdMenuTest", "Menu owner-draw - test", WS_OVERLAPPEDWINDOW,
                     20, 20, 400, 250, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
