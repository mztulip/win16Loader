/* tasktest.c - zadania: WinExec uruchamia TASKCHLD.EXE, oba przelaczaja sie,
 * kazde ma wlasna kolejke, SendMessage dziala na stosie odbiorcy, PostQuitMessage konczy tylko
 * jedno zadanie, koniec dziecka sprzata jego okna i nie konczy rodzica. */
#include <windows.h>
#include "testlog.h"
#include <direct.h>
#include <shellapi.h>
#include <toolhelp.h>

void FAR PASCAL PostEvent(HANDLE h);

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
    t_init(hi);
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

    CHECK(WinExec(T("NIEMA.EXE"), SW_SHOWNORMAL) == 2, "WinExec: brak pliku = 2");
    h = WinExec(T("TASKCHLD.EXE arg1 arg2"), SW_SHOWMINNOACTIVE);
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

    /* kursor nad pulpitem: komunikaty pulpitu nie moga zablokowac kolejki (byla pelna po 256) */
    {
        int i, got = 0;
        for (i = 0; i < 600; i++) {
            POINT p;
            SetCursorPos(600 - (i & 1) * 20, 460 - (i & 2) * 10);
            GetCursorPos(&p);
            while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
        }
        for (i = 0; i < 300; i++) {
            if (!PostMessage(me, WM_USER + 7, 0, 0)) break;
            if (PeekMessage(&m, me, WM_USER + 7, WM_USER + 7, PM_REMOVE)) got++;
        }
        CHECK_INT(got, 300, "kolejka dziala po ruchu myszy nad pulpitem");
    }

    /* ponowne uruchomienie: klasy dziecka musialy zniknac */
    h = WinExec(T("TASKCHLD.EXE arg1 arg2"), SW_SHOWMINNOACTIVE);
    CHECK(h > 32, "drugie uruchomienie po koncu pierwszego");
    t0 = GetTickCount();
    while (!(ch = FindWindow("TaskChildW", NULL)) && GetTickCount() - t0 < 10000UL) Yield();
    CHECK(ch != 0, "drugie uruchomienie ma okno");
    pump(200);
    PostMessage(ch, WM_USER + 2, 0, 0);
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
    CHECK_INT(GetNumTasks(), 1, "drugie dziecko zakonczone");
    /* katalog biezacy: osobny dla kazdego zadania, nowe zadanie dziedziczy */
    {
        char cur[80];
        char cmdb[120];
        chdir(TD("\\FREEDOS"));
        wsprintf(cmdb, "%sTASKCHLD.EXE wd %s", T(""), TD("\\FREEDOS"));
        h = WinExec(cmdb, SW_SHOWNORMAL);
        CHECK(h > 32, "uruchomienie: test katalogu");
        getcwd(cur, sizeof(cur));
        CHECK_STR(cur, TD("\\FREEDOS"), "chdir dziecka nie zmienia katalogu rodzica");
        chdir(TD("\\FREEDOS\\BIN"));
        t0 = GetTickCount();
        while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
        getcwd(cur, sizeof(cur));
        CHECK_STR(cur, TD("\\FREEDOS\\BIN"), "katalog rodzica po koncu dziecka");
        /* ShellExecute: katalog startowy nowego programu, bez zmiany u wolajacego */
        wsprintf(cmdb, "wd %s", TD("\\FREEDOS"));
        h = (UINT)ShellExecute(0, "open", T("TASKCHLD.EXE"), cmdb, TD("\\FREEDOS"), SW_SHOWNORMAL);
        CHECK(h > 32, "ShellExecute: katalog startowy");
        getcwd(cur, sizeof(cur));
        CHECK_STR(cur, TD("\\FREEDOS\\BIN"), "ShellExecute nie zmienia katalogu wolajacego");
        t0 = GetTickCount();
        while (GetNumTasks() > 1 && GetTickCount() - t0 < 10000UL) Yield();
        chdir(TD("\\"));
    }

    /* kopie jednego programu: hPrevInstance, wspolna klasa, klasa przezywa pierwsza kopie */
    {
        UINT h1, h2, h3;
        HWND w1, w2, w3;
        /* inny program z ta sama nazwa klasy: kopie dziecka nie moga dostac tej klasy */
        wc.lpszClassName = "TaskInstW";
        CHECK(RegisterClass(&wc) != 0, "rodzic rejestruje klase o tej samej nazwie");
        h1 = WinExec(T("TASKCHLD.EXE inst"), SW_SHOWNORMAL);
        h2 = WinExec(T("TASKCHLD.EXE inst"), SW_SHOWNORMAL);
        CHECK(h1 > 32 && h2 > 32 && h1 != h2, "dwie kopie dzialaja razem");
        CHECK_INT(GetNumTasks(), 3, "GetNumTasks: dwie kopie i rodzic");
        CHECK_INT(GetModuleUsage((HINSTANCE)h1), 2, "GetModuleUsage programu = liczba kopii");
        t0 = GetTickCount();
        w2 = 0;
        while (GetTickCount() - t0 < 300) Yield();
        for (w1 = GetWindow(me, GW_HWNDFIRST); w1; w1 = GetWindow(w1, GW_HWNDNEXT))
            if ((UINT)GetWindowWord(w1, GWW_HINSTANCE) == h2) w2 = w1;
        for (w1 = GetWindow(me, GW_HWNDFIRST); w1; w1 = GetWindow(w1, GW_HWNDNEXT))
            if ((UINT)GetWindowWord(w1, GWW_HINSTANCE) == h1) break;
        CHECK(w1 && w2, "okna obu kopii");
        if (w1 && w2) {
            CHECK_INT((WORD)SendMessage(w1, WM_USER + 1, 0, 0), 1234 + h1, "okno 1 uzywa danych swojej kopii");
            CHECK_INT((WORD)SendMessage(w2, WM_USER + 1, 0, 0), 1234 + h2, "okno 2 uzywa danych swojej kopii");
            CHECK_INT((WORD)SendMessage(w1, WM_USER + 3, 0, 0), 0, "hPrevInstance pierwszej kopii = 0");
            CHECK_INT((WORD)SendMessage(w2, WM_USER + 3, 0, 0), h1, "hPrevInstance drugiej = pierwsza kopia");
            /* pierwsza kopia konczy sie: klasa i okno drugiej zostaja */
            PostMessage(w1, WM_USER + 2, 0, 0);
            t0 = GetTickCount();
            while (GetNumTasks() > 2 && GetTickCount() - t0 < 5000UL) Yield();
            CHECK_INT(GetNumTasks(), 2, "pierwsza kopia zakonczona");
            CHECK_INT((WORD)SendMessage(w2, WM_USER + 1, 0, 0), 1234 + h2, "druga kopia dziala po koncu pierwszej");
            h3 = WinExec(T("TASKCHLD.EXE inst"), SW_SHOWNORMAL);
            CHECK(h3 > 32, "trzecia kopia po koncu pierwszej");
            t0 = GetTickCount();
            while (GetTickCount() - t0 < 300) Yield();
            for (w3 = GetWindow(me, GW_HWNDFIRST); w3; w3 = GetWindow(w3, GW_HWNDNEXT))
                if ((UINT)GetWindowWord(w3, GWW_HINSTANCE) == h3) break;
            CHECK(w3 != 0, "trzecia kopia ma okno klasy przejetej od pierwszej");
            if (w3) { CHECK_INT((WORD)SendMessage(w3, WM_USER + 1, 0, 0), 1234 + h3, "trzecia kopia: wlasne dane"); PostMessage(w3, WM_USER + 2, 0, 0); }
            PostMessage(w2, WM_USER + 2, 0, 0);
            t0 = GetTickCount();
            while (GetNumTasks() > 1 && GetTickCount() - t0 < 5000UL) Yield();
            CHECK_INT(GetNumTasks(), 1, "wszystkie kopie zakonczone");
            CHECK(GetModuleHandle("TASKCHLD") == 0, "GetModuleHandle: zakonczony program nie jest zaladowany");
        }
    }

    /* WaitEvent / PostEvent, LoadModule, TaskFirst / TaskNext */
    {
        struct { WORD env; LPSTR cmd; WORD FAR *show; DWORD res; } lp;
        WORD sh[2];
        static char tail[] = "\x02" "ev\r";
        TASKENTRY te;
        int n = 0;
        HTASK ct = 0;
        sh[0] = 2; sh[1] = SW_SHOWNORMAL;
        lp.env = 0; lp.cmd = tail; lp.show = sh; lp.res = 0;
        h = (UINT)LoadModule(T("TASKCHLD.EXE"), &lp);
        CHECK(h > 32, "LoadModule z parametrami");
        te.dwSize = sizeof(te);
        if (TaskFirst(&te)) do {
            n++;
            if ((UINT)te.hInst == h) ct = te.hTask;
        } while (TaskNext(&te));
        CHECK_INT(n, 2, "TaskFirst/TaskNext: wszystkie zadania");
        CHECK(ct != 0, "TaskFirst: dziecko na liscie");
        CHECK_INT(GetNumTasks(), 2, "dziecko czeka w WaitEvent");
        for (n = 0; n < 5; n++) Yield();
        CHECK_INT(GetNumTasks(), 2, "WaitEvent czeka mimo przelaczen");
        PostEvent(ct);
        t0 = GetTickCount();
        while (GetNumTasks() > 1 && GetTickCount() - t0 < 5000UL) Yield();
        CHECK_INT(GetNumTasks(), 1, "PostEvent budzi zadanie");
    }

    /* wielokrotne uruchomienia: pamiec i uchwyty wracaja, adresy programu te same */
    {
        DWORD free0 = 0;
        int i, bad = 0;
        UINT first = 0;
        char d[60];
        for (i = 0; i < 12; i++) {
            h = WinExec(T("TASKCHLD.EXE quick"), SW_SHOWNORMAL);
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
    h = WinExec(T("TASKCHLD.EXE crash"), SW_SHOWNORMAL);
    CHECK(h > 32, "uruchomienie programu z wyjatkiem");
    h = WinExec(T("TASKCHLD.EXE crash"), SW_SHOWNORMAL);
    CHECK(h > 32, "drugi program z wyjatkiem w tym samym czasie");
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 30000UL) pump(100);
    CHECK_INT(GetNumTasks(), 1, "wyjatek konczy tylko winne zadanie");
    CHECK(FindWindow("TaskCrashW", NULL) == 0, "okno po wyjatku zniszczone");
    CHECK(IsWindow(me), "rodzic po wyjatku dziecka zyje");
    t_done();
    return 0;
}
