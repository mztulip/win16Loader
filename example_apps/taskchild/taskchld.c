/* taskchld.c - program potomny testu zadan (example_apps/tasks), uruchamiany przez WinExec.
 * Okno "TaskChildW": WM_USER+1 = SendMessage od rodzica (zwraca wp*2 i sprawdza swoj stos),
 * WM_USER+2 = koniec programu. */
#include <windows.h>
#include "testlog.h"

static int g_timer;

static BOOL own_stack(void)
{
    int local;
    return HIWORD((DWORD)(void __far *)&local) == HIWORD((DWORD)(void __far *)&t_pass);
}

LRESULT FAR PASCAL __export ChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_USER + 1:
        CHECK(own_stack(), "SendMessage z innego zadania: wlasny stos");
        CHECK(GetWindowTask(h) == GetCurrentTask(), "SendMessage: dziala w zadaniu odbiorcy");
        return wp * 2;
    case WM_USER + 2:
        CHECK(g_timer > 0, "timer dziecka dziala");
        PostQuitMessage(7);
        return 0;
    case WM_TIMER:
        g_timer++;
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    int i;
    t_app = "TASKCHLD";
    CHECK(prev == 0, "hPrevInstance = 0");
    CHECK(hi != 0, "hInstance");
    CHECK(_fstrncmp(cmd, "arg1 arg2", 9) == 0 && (cmd[9] == 0 || cmd[9] == 13), "linia polecen");
    CHECK_INT(show, SW_SHOWMINNOACTIVE, "nCmdShow z WinExec");
    CHECK_INT(GetNumTasks(), 2, "GetNumTasks w dziecku");
    CHECK(own_stack(), "SS = DS dziecka");
    for (i = 0; i < 5; i++) Yield();
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ChildProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "TaskChildW";
    RegisterClass(&wc);
    h = CreateWindow("TaskChildW", "Dziecko", WS_OVERLAPPEDWINDOW, 300, 200, 240, 150, NULL, NULL, hi, NULL);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    UpdateWindow(h);
    SetTimer(h, 1, 55, NULL);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    CHECK_INT(m.wParam, 7, "WM_QUIT dziecka z jego kodem");
    t_log("koniec dziecka", TRUE, "");
    return 0;                                         /* okno niszczy koniec zadania */
}
