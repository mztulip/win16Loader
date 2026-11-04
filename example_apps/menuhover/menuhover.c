/* menuhover.c - test regresyjny: przejazd mysza bez klikania po menu z otwartym podmenu.
 * Dawniej petla podmenu ignorowala mysz poza swoim oknem do klikniecia, wiec podswietlenie
 * stawalo na pozycji z podmenu (menu Start w Calmirze). run_examples.py rusza mysza,
 * a test sprawdza kolejnosc WM_MENUSELECT.
 *
 * Menu w (100,100):  A (podmenu: S1 -> podmenu: T1 / S2) / B / C
 */
#include <windows.h>
#include "testlog.h"

#define IDM_B  201
#define IDM_C  202
#define IDM_S2 203
#define IDM_T1 204

static HMENU g_main, g_sub, g_sub2;
static WORD g_log[256];
static int g_n, g_phase;

/* Czy w logu po pierwszym wystapieniu 'a' jest 'b' */
static BOOL seen_after(WORD a, WORD b)
{
    int i;
    for (i = 0; i < g_n; i++) if (g_log[i] == a) break;
    for (; i < g_n; i++) if (g_log[i] == b) return TRUE;
    return FALSE;
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MENUSELECT:
        if (g_n < 256) g_log[g_n++] = wp;
        return 0;
    case WM_TIMER:
        KillTimer(hwnd, 1);
        g_n = 0;
        TrackPopupMenu(g_main, 0, 100, 100, 0, hwnd, NULL);
        if (g_phase == 0) {
            CHECK(seen_after((WORD)g_sub, IDM_B), "z otwartego podmenu na B bez klikania");
            CHECK(seen_after(IDM_B, IDM_C), "dalej na C");
            CHECK(seen_after(IDM_C, IDM_B), "i z powrotem na B");
            g_phase = 1;
            SetTimer(hwnd, 1, 200, NULL);
        } else {
            CHECK(seen_after((WORD)g_sub2, IDM_T1) || seen_after((WORD)g_sub, (WORD)g_sub2), "otwarte dwa poziomy podmenu");
            CHECK(seen_after((WORD)g_sub2, IDM_C), "z drugiego poziomu podmenu na C");
            t_done();
        }
        return 0;
    case WM_DESTROY:
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
    t_app = "MENUHOV";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "MenuHov";
        RegisterClass(&wc);
    }
    g_sub2 = CreatePopupMenu();
    AppendMenu(g_sub2, MF_STRING, IDM_T1, "Trzeci poziom");
    g_sub = CreatePopupMenu();
    AppendMenu(g_sub, MF_POPUP, (UINT)g_sub2, "Pozycja S1");
    AppendMenu(g_sub, MF_STRING, IDM_S2, "Pozycja S2");
    g_main = CreatePopupMenu();
    AppendMenu(g_main, MF_POPUP, (UINT)g_sub, "Pozycja A");
    AppendMenu(g_main, MF_STRING, IDM_B, "Pozycja B");
    AppendMenu(g_main, MF_STRING, IDM_C, "Pozycja C");
    h = CreateWindow("MenuHov", "Menu i mysz", WS_OVERLAPPEDWINDOW, 300, 300, 300, 150, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    SetTimer(h, 1, 1000, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
