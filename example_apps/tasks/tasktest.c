/* tasktest.c - zadania: WinExec uruchamia TASKCHLD.EXE, oba przelaczaja sie,
 * kazde ma wlasna kolejke, SendMessage dziala na stosie odbiorcy, PostQuitMessage konczy tylko
 * jedno zadanie, koniec dziecka sprzata jego okna i nie konczy rodzica. */
#include <windows.h>
#include "testlog.h"

static int g_active, g_paint, g_moves;
static HTASK g_me;

/* Komunikaty rodzica przez zadany czas: dziecko pracuje w tym czasie */
static void pump(DWORD ms)
{
    MSG m;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
    }
}

LRESULT FAR PASCAL __export ParentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ACTIVATEAPP) g_active++;
    if (msg == WM_PAINT) { g_paint++; }
    if (msg == WM_MOUSEMOVE) g_moves++;
    if (msg == WM_USER + 5) t_log("BLAD: komunikat dziecka trafil do rodzica", FALSE, "");
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    UINT h;
    HWND me, ch;
    MSG m;
    long yields = 0;
    DWORD t0;
    (void)prev; (void)cmd; (void)show;
    t_app = "TASKTEST";
    g_me = GetCurrentTask();
    CHECK(g_me != 0, "GetCurrentTask");
    CHECK_INT(GetNumTasks(), 1, "GetNumTasks na starcie");
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ParentProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "TaskParentW";
    RegisterClass(&wc);
    me = CreateWindow("TaskParentW", "Rodzic", WS_OVERLAPPEDWINDOW, 20, 20, 240, 150, NULL, NULL, hi, NULL);
    ShowWindow(me, SW_SHOWNORMAL);
    UpdateWindow(me);

    CHECK(WinExec("A:\\NIEMA.EXE", SW_SHOWNORMAL) == 2, "WinExec: brak pliku = 2");
    h = WinExec("A:\\TASKCHLD.EXE arg1 arg2", SW_SHOWMINNOACTIVE);
    CHECK(h > 32, "WinExec: sukces > 32");
    CHECK(h != (UINT)hi, "dziecko ma inny hInstance");
    CHECK(GetCurrentTask() == g_me, "po WinExec dalej to samo zadanie");
    CHECK_INT(GetNumTasks(), 2, "GetNumTasks z dzieckiem");
    t0 = GetTickCount();
    while (!(ch = FindWindow("TaskChildW", NULL)) && GetTickCount() - t0 < 10000UL) { Yield(); yields++; }
    CHECK(ch != 0, "okno dziecka");
    CHECK(yields >= 3, "Yield przelaczal zadania");
    CHECK(GetWindowTask(ch) != g_me && GetWindowTask(ch) != 0, "GetWindowTask(dziecko)");
    CHECK(GetWindowTask(me) == g_me, "GetWindowTask(rodzic)");
    CHECK(IsTask(GetWindowTask(ch)), "IsTask(dziecko)");

    CHECK_INT(SendMessage(ch, WM_USER + 1, 21, 0), 42, "SendMessage miedzy zadaniami: wynik");
    CHECK(GetCurrentTask() == g_me, "po SendMessage z powrotem u nadawcy");

    /* komunikat do okna dziecka nie moze trafic do kolejki rodzica */
    PostMessage(ch, WM_USER + 5, 0, 0);
    CHECK(!PeekMessage(&m, ch, WM_USER + 5, WM_USER + 5, PM_NOREMOVE | PM_NOYIELD), "kolejki rozdzielone");

    pump(600);                                        /* dziecko dostaje timer i maluje */
    PostMessage(ch, WM_USER + 2, 0, 0);               /* PostQuitMessage u dziecka */
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
    CHECK_INT(GetNumTasks(), 1, "dziecko zakonczone");
    CHECK(!IsWindow(ch), "okno dziecka zniszczone po jego koncu");
    CHECK(GetCurrentTask() == g_me, "rodzic dziala dalej");
    CHECK(!PeekMessage(&m, NULL, WM_QUIT, WM_QUIT, PM_NOREMOVE), "WM_QUIT dziecka nie u rodzica");

    /* okno pojawia sie pod nieruchomym kursorem: WM_MOUSEMOVE bez ruchu myszy (SkiFree na to czeka) */
    {
        HWND w2;
        SetCursorPos(500, 400);
        pump(200);
        w2 = CreateWindow("TaskParentW", "Pod kursorem", WS_POPUP, 450, 350, 100, 100, NULL, NULL, hi, NULL);
        g_moves = 0;
        ShowWindow(w2, SW_SHOWNORMAL);
        pump(200);
        CHECK(g_moves > 0, "WM_MOUSEMOVE gdy okno pojawi sie pod kursorem");
        DestroyWindow(w2);
    }

    /* ponowne uruchomienie: klasy dziecka musialy zniknac */
    h = WinExec("A:\\TASKCHLD.EXE arg1 arg2", SW_SHOWMINNOACTIVE);
    CHECK(h > 32, "drugie uruchomienie po koncu pierwszego");
    t0 = GetTickCount();
    while (!(ch = FindWindow("TaskChildW", NULL)) && GetTickCount() - t0 < 10000UL) Yield();
    CHECK(ch != 0, "drugie uruchomienie ma okno");
    pump(200);
    PostMessage(ch, WM_USER + 2, 0, 0);
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
    CHECK_INT(GetNumTasks(), 1, "drugie dziecko zakonczone");
    /* wielokrotne uruchomienia: pamiec i uchwyty wracaja, adresy programu te same */
    {
        DWORD free0 = 0;
        int i, bad = 0;
        UINT first = 0;
        char d[60];
        for (i = 0; i < 12; i++) {
            h = WinExec("A:\\TASKCHLD.EXE quick", SW_SHOWNORMAL);
            if (h <= 32) { bad++; break; }
            if (!first) first = h;
            if (h != first) bad++;
            t0 = GetTickCount();
            while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
            if (!free0) free0 = GetFreeSpace(0);
        }
        CHECK_INT(bad, 0, "12 uruchomien: to samo hInstance, zawsze sukces");
        wsprintf(d, "%ld KB -> %ld KB", free0 / 1024, GetFreeSpace(0) / 1024);
        t_log("pamiec wraca po koncu zadania", GetFreeSpace(0) >= free0, d);
    }

    /* wyjatek w dziecku konczy tylko dziecko; dwa naraz: kazde ma swoj stos i swoj komunikat */
    h = WinExec("A:\\TASKCHLD.EXE crash", SW_SHOWNORMAL);
    CHECK(h > 32, "uruchomienie programu z wyjatkiem");
    h = WinExec("A:\\TASKCHLD.EXE crash", SW_SHOWNORMAL);
    CHECK(h > 32, "drugi program z wyjatkiem w tym samym czasie");
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 30000UL) pump(100);
    CHECK_INT(GetNumTasks(), 1, "wyjatek konczy tylko winne zadanie");
    CHECK(FindWindow("TaskCrashW", NULL) == 0, "okno po wyjatku zniszczone");
    CHECK(IsWindow(me), "rodzic po wyjatku dziecka zyje");
    t_done();
    return 0;
}
