/* dosirq.c - test regresyjny: mysz i klawiatura przezywaja wywolania DOS.
 * Na czas INT 21h z PM (rm_int_service) IRQ1/IRQ12 sa zamaskowane, a powrot do PM
 * reinicjalizuje PIC i kasuje zapamietane zadanie przerwania. Bajt czekajacy w 8042
 * nie byl juz nigdy odczytany, wiec kontroler nie przyjmowal nastepnych: kursor i
 * klawiatura stawaly na zawsze (np. po otwarciu dysku w Calmirze).
 * Petla: czytaj plik przez DOS; w tym czasie run_examples.py rusza mysza i klawiszem.
 */
#include <windows.h>
#include "testlog.h"

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void run_test(void)
{
    static char buf[512];
    DWORD start = GetTickCount();
    POINT last, p;
    long reads = 0;
    int moves = 0, keys = 0, down = 0;
    char d[80];
    GetCursorPos(&last);
    while (moves < 120 && GetTickCount() - start < 40000UL) {
        int f = _lopen("A:\\LOADER.EXE", 0), s;
        if (f != -1) {
            _lread(f, buf, sizeof(buf));
            _lclose(f);
            reads++;
        }
        GetCursorPos(&p);
        if (p.x != last.x || p.y != last.y) { moves++; last = p; }
        s = GetAsyncKeyState(VK_SHIFT) < 0;
        if (s != down) { keys++; down = s; }
    }
    wsprintf(d, "odczyty=%ld ruchy=%d klawisz=%d", reads, moves, keys);
    t_log("przebieg", reads > 0, d);
    CHECK(moves >= 120, "mysz dziala przez caly czas operacji DOS");
    CHECK(keys >= 2, "klawiatura dziala w trakcie operacji DOS");
    t_done();
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "DOSIRQ";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "DosIrq";
        RegisterClass(&wc);
    }
    h = CreateWindow("DosIrq", "Mysz a DOS", WS_OVERLAPPEDWINDOW, 60, 60, 400, 200, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    run_test();
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
