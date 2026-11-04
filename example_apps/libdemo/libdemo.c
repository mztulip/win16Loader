/* libdemo.c - LibEntry bibliotek ladowanych w trakcie pracy: import statyczny programu z WinExec
 * i LoadLibrary. Wyniki w oknie aplikacji i na COM1. DLL-e leza w A:\LIBDEMO, a nie obok programu,
 * bo tamte loader wczytuje przy starcie. Alt+F4 zamyka. */
#include <windows.h>
#include "testlog.h"
#include "demodll.h"

#define DIR "A:\\LIBDEMO\\"
#define MAX_LINES 20
static char g_lines[MAX_LINES][64];
static int g_nl;

static void show_line(const char *name, BOOL ok, const char *detail)
{
    t_log(name, ok, detail);
    if (g_nl < MAX_LINES)
        wsprintf(g_lines[g_nl++], "%s %s %s", (LPSTR)(ok ? "OK " : "BLAD"), (LPSTR)name, (LPSTR)(detail ? detail : ""));
}

static void wait_child(void)
{
    DWORD t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 15000UL) Yield();
}

static void demo(void)
{
    typedef void (FAR PASCAL *INFOPROC)(DEMOINFO FAR *);
    char d[64];
    UINT h;
    HINSTANCE a, b;
    INFOPROC f;
    DEMOINFO i;

    /* import statyczny: test w LIBCHLD.EXE, drugi raz biblioteka juz jest w pamieci */
    h = WinExec(DIR "LIBCHLD.EXE", SW_SHOWNORMAL);
    wsprintf(d, "wynik %u", h);
    show_line("WinExec z importem DLL", h > 32, d);
    wait_child();
    h = WinExec(DIR "LIBCHLD.EXE", SW_SHOWNORMAL);
    show_line("drugie uruchomienie", h > 32, "");
    wait_child();
    show_line("programy potomne zakonczone", GetNumTasks() == 1, "");

    /* LoadLibrary biblioteki, ktorej jeszcze nie ma w pamieci */
    a = LoadLibrary(DIR "DEMODYN.DLL");
    wsprintf(d, "uchwyt %04X", (WORD)a);
    show_line("LoadLibrary DEMODYN", (UINT)a > 32, d);
    if ((UINT)a > 32) {
        f = (INFOPROC)GetProcAddress(a, "DEMOINFO");
        show_line("GetProcAddress", f != NULL, "");
        if (f) {
            f(&i);
            wsprintf(d, "wywolan=%d heap=%u", i.calls, i.heap);
            show_line("LibEntry wywolane raz", i.calls == 1, d);
            show_line("sterta DLL dziala", i.heap_ok, "");
            b = LoadLibrary(DIR "DEMODYN.DLL");
            f(&i);
            show_line("drugi LoadLibrary bez LibEntry", b == a && i.calls == 1, "");
            wsprintf(d, "%d", GetModuleUsage(a));
            show_line("GetModuleUsage po dwoch LoadLibrary", GetModuleUsage(a) == 2, d);
            FreeLibrary(b);
            show_line("FreeLibrary: DLL zostaje, bez WEP", GetModuleUsage(a) == 1 && !GlobalFindAtom(WEP_ATOM), "");
            FreeLibrary(a);
            show_line("ostatni FreeLibrary wola WEP", GlobalFindAtom(WEP_ATOM) != 0, "");
            show_line("po FreeLibrary modulu nie ma", GetModuleHandle("DEMODYN") == 0, "");
            b = LoadLibrary(DIR "DEMODYN.DLL");
            wsprintf(d, "uchwyt %04X", (WORD)b);
            show_line("ponowny LoadLibrary", (UINT)b > 32 && b == a, d);
            f(&i);                                 /* wciaz wazny: te same adresy */
            wsprintf(d, "wywolan=%d odczyt=%d", i.calls, i.reads);
            show_line("LibEntry od nowa, swieze dane", i.calls == 1 && i.reads == 1, d);
        }
    }
    a = LoadLibrary(DIR "NIEMA.DLL");
    wsprintf(d, "kod %u", (UINT)a);
    show_line("LoadLibrary: brak pliku", (UINT)a == 2, d);
    a = LoadLibrary(DIR "DEMOBAD.DLL");
    wsprintf(d, "kod %u", (UINT)a);
    show_line("LibEntry zwraca blad", (UINT)a < 32, d);
    b = LoadLibrary(DIR "DEMOBAD.DLL");
    show_line("nieudana DLL nie wraca jako dobra", (UINT)b < 32, "");
    t_done();
}

LRESULT FAR PASCAL __export WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        int i;
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < g_nl; i++) TextOut(dc, 8, 8 + i * 18, g_lines[i], lstrlen(g_lines[i]));
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "LIBDEMO";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "LibDemo";
        RegisterClass(&wc);
    }
    h = CreateWindow("LibDemo", "LibEntry bibliotek DLL", WS_OVERLAPPEDWINDOW, 40, 40, 520, 320, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    demo();
    InvalidateRect(h, NULL, TRUE);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
