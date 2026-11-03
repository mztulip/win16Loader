/* shelltst.c - test SHELL.DLL: rejestr 3.1 (RegCreateKey/SetValue/QueryValue/
 * EnumKey/DeleteKey), ExtractIcon z wlasnego pliku i z .ICO, Drag*.
 * Okno pokazuje wyciagniete ikony (DrawIcon). Alt+F4 zamyka.
 */
#include <windows.h>
#include <shellapi.h>
#include "testlog.h"

static HICON g_icon_exe, g_icon_ico;
static char g_exe[128];

static void reg_tests(void)
{
    HKEY k, k2;
    char buf[80];
    LONG cb;
    int n;

    CHECK_INT(RegCreateKey(HKEY_CLASSES_ROOT, "TestApp\\shell\\open", &k), ERROR_SUCCESS, "RegCreateKey (sciezka)");
    CHECK_INT(RegSetValue(k, "command", REG_SZ, "testapp.exe %1", 14), ERROR_SUCCESS, "RegSetValue podklucz");
    RegCloseKey(k);
    CHECK_INT(RegSetValue(HKEY_CLASSES_ROOT, ".tst", REG_SZ, "TestApp", 7), ERROR_SUCCESS, "RegSetValue .tst");

    cb = sizeof(buf);
    CHECK_INT(RegQueryValue(HKEY_CLASSES_ROOT, "TestApp\\shell\\open\\command", buf, &cb), ERROR_SUCCESS, "RegQueryValue");
    CHECK_STR(buf, "testapp.exe %1", "RegQueryValue: wartosc");
    CHECK_INT(cb, 15, "RegQueryValue: dlugosc z zerem");

    CHECK_INT(RegOpenKey(HKEY_CLASSES_ROOT, "TestApp\\shell", &k), ERROR_SUCCESS, "RegOpenKey");
    for (n = 0; RegEnumKey(k, n, buf, sizeof(buf)) == ERROR_SUCCESS && n < 50; n++) ;
    CHECK_INT(n, 1, "RegEnumKey konczy wyliczanie");
    RegEnumKey(k, 0, buf, sizeof(buf));
    CHECK_STR(buf, "open", "RegEnumKey(0)");
    RegCloseKey(k);

    CHECK(RegOpenKey(HKEY_CLASSES_ROOT, "NieMaTakiego", &k2) != ERROR_SUCCESS, "RegOpenKey: brak klucza = blad");
    CHECK_INT(RegDeleteKey(HKEY_CLASSES_ROOT, "TestApp"), ERROR_SUCCESS, "RegDeleteKey (poddrzewo)");
    CHECK(RegOpenKey(HKEY_CLASSES_ROOT, "TestApp\\shell\\open", &k2) != ERROR_SUCCESS, "po RegDeleteKey brak podklucza");
    RegDeleteKey(HKEY_CLASSES_ROOT, ".tst");
}

static void icon_tests(HINSTANCE hi)
{
    int n;
    GetModuleFileName(hi, g_exe, sizeof(g_exe));
    n = (int)ExtractIcon(hi, g_exe, (UINT)-1);
    CHECK_INT(n, 1, "ExtractIcon(-1): liczba ikon w EXE");
    g_icon_exe = ExtractIcon(hi, g_exe, 0);
    CHECK(g_icon_exe > (HICON)1, "ExtractIcon(0) z EXE");
    CHECK(ExtractIcon(hi, g_exe, 5) == 0, "ExtractIcon: indeks poza zakresem = 0");
    CHECK(ExtractIcon(hi, "A:\\NIEMA.EXE", 0) == 0, "ExtractIcon: brak pliku = 0");
    g_icon_ico = ExtractIcon(hi, "A:\\SHELLTST.ICO", 0);
    CHECK(g_icon_ico > (HICON)1, "ExtractIcon z pliku .ICO");
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        reg_tests();
        icon_tests(((LPCREATESTRUCT)lp)->hInstance);
        CHECK_INT(DragQueryFile(0, (UINT)-1, NULL, 0), 0, "DragQueryFile bez upuszczenia");
        t_done();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        TextOut(dc, 10, 10, "Ikona z EXE:", 12);
        if (g_icon_exe > (HICON)1) DrawIcon(dc, 120, 4, g_icon_exe);
        TextOut(dc, 10, 50, "Ikona z .ICO:", 13);
        if (g_icon_ico > (HICON)1) DrawIcon(dc, 120, 44, g_icon_ico);
        TextOut(dc, 10, 90, g_exe, lstrlen(g_exe));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
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
    t_app = "SHELLTST";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "ShellTest";
        RegisterClass(&wc);
    }
    h = CreateWindow("ShellTest", "SHELL.DLL - test", WS_OVERLAPPEDWINDOW, 30, 30, 380, 160, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
