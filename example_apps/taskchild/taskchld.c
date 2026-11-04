/* taskchld.c - program potomny testu zadan (example_apps/tasks), uruchamiany przez WinExec.
 * Okno "TaskChildW": WM_USER+1 = SendMessage od rodzica (zwraca wp*2 i sprawdza swoj stos),
 * WM_USER+2 = koniec programu. */
#include <windows.h>
#include "testlog.h"
#include <direct.h>

BOOL FAR PASCAL WaitEvent(HANDLE h);

static int g_timer;
WORD g_mark;
unsigned dgroup_const(void);
#pragma aux dgroup_const = "mov ax, seg g_mark" value [ax];
static HINSTANCE g_prev;

LRESULT FAR PASCAL __export InstProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER + 1) {
        /* Stala segmentu w kodzie (fixup na DGROUP): dane swojej kopii tylko w jej wlasnym kodzie */
        return *(WORD __far *)(((DWORD)dgroup_const() << 16) | (WORD)&g_mark);
    }
    if (msg == WM_USER + 3) return (LRESULT)(WORD)g_prev;
    if (msg == WM_USER + 2) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, msg, wp, lp);
}

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
    if (cmd[0] == 'q') {                              /* "quick": wszystko zostawia nieposprzatane */
        int k, files = 0, brushes = 0, mem = 0, menus = 0;
        char d[60];
        for (k = 0; k < 10; k++) if (_lopen("A:\\LOADER.EXE", 0) != -1) files++;
        for (k = 0; k < 200; k++) if (CreateSolidBrush(RGB(k, 0, 0))) brushes++;
        for (k = 0; k < 8; k++) if (GlobalAlloc(GMEM_FIXED, 32768L)) mem++;
        for (k = 0; k < 30; k++) if (CreateMenu()) menus++;
        wsprintf(d, "pliki=%d pedzle=%d bloki=%d menu=%d", files, brushes, mem, menus);
        t_log("zasoby nieposprzatane", files == 10 && brushes == 200 && mem == 8 && menus == 30, d);
        return 0;
    }
    if (cmd[0] == 'e') {                              /* "ev": czeka na PostEvent od rodzica */
        WaitEvent(0);
        t_log("WaitEvent konczy sie po PostEvent", TRUE, "");
        return 0;
    }
    if (cmd[0] == 'i') {                              /* "inst": klasa tylko w pierwszej kopii */
        char d[60];
        wsprintf(d, "hPrev=%04X", (WORD)prev);
        t_log("kopia programu", TRUE, d);
        g_mark = 1234 + (WORD)hi;
        g_prev = prev;
        if (prev) {
            WORD other = 0;
            GetInstanceData(prev, (BYTE NEAR *)&g_mark, sizeof(g_mark));
            other = g_mark;
            g_mark = 1234 + (WORD)hi;
            CHECK_INT(other, 1234 + (WORD)prev, "GetInstanceData z poprzedniej kopii");
            memset(&wc, 0, sizeof(wc));
            wc.lpfnWndProc = InstProc;
            wc.hInstance = hi;
            wc.lpszClassName = "TaskInstW";
            CHECK(!RegisterClass(&wc), "RegisterClass: kopie to jeden program, druga rejestracja zawodzi");
        } else {
            memset(&wc, 0, sizeof(wc));
            wc.lpfnWndProc = InstProc;
            wc.hInstance = hi;
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = "TaskInstW";
            CHECK(RegisterClass(&wc), "RegisterClass w pierwszej kopii");
        }
        h = CreateWindow("TaskInstW", "Kopia", WS_OVERLAPPEDWINDOW, prev ? 340 : 60, 240, 200, 100, NULL, NULL, hi, NULL);
        CHECK(h != 0, prev ? "CreateWindow: klasa pierwszej kopii" : "CreateWindow w pierwszej kopii");
        CHECK(!prev || (HINSTANCE)GetWindowWord(h, GWW_HINSTANCE) == hi, "okno nalezy do tej kopii");
        CreateMenu();                                 /* zostaje: koniec zadania zwalnia */
        while (GetMessage(&m, NULL, 0, 0)) DispatchMessage(&m);
        return 0;
    }
    if (cmd[0] == 'w') {                              /* "wd KATALOG": katalog biezacy zadania */
        char want[80], cur[80];
        int k = 0, n;
        for (cmd += 3; *cmd && *cmd != ' ' && *cmd != 13 && k < 79; cmd++) want[k++] = *cmd;
        want[k] = 0;
        getcwd(cur, sizeof(cur));
        CHECK_STR(cur, want, "katalog dziedziczony przy uruchomieniu");
        chdir("A:\\");
        for (n = 0; n < 10; n++) Yield();             /* rodzic zmienia swoj katalog w tym czasie */
        getcwd(cur, sizeof(cur));
        CHECK_STR(cur, "A:\\", "zmiana katalogu innego zadania mnie nie dotyczy");
        return 0;
    }
    if (cmd[0] == 'c') {                              /* "crash": wyjatek w programie */
        char __far *bad = (char __far *)0x3FF00000L;      /* selektor bez deskryptora */
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = ChildProc;
        wc.hInstance = hi;
        wc.lpszClassName = "TaskCrashW";
        RegisterClass(&wc);
        ShowWindow(CreateWindow("TaskCrashW", "Crash", WS_OVERLAPPEDWINDOW, 300, 200, 200, 100, NULL, NULL, hi, NULL), SW_SHOWNOACTIVATE);
        *bad = 1;
        t_log("BLAD: wyjatek nie zadzialal", FALSE, "");
        return 0;
    }
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
