/* dosprompt.c - test regresyjny: DOS nie moze czekac na klawisz w trakcie wywolania z PM.
 * Klawiatura jest wtedy zamaskowana, wiec kazda prosba DOS-a wieszala caly system:
 *  - dysk B: przy jednej stacji ("Remove diskette in drive A:"); loader ukrywa wtedy B:
 *    (jak Windows NT: B: nie istnieje), bo drugiej dyskietki nie ma gdzie wlozyc,
 *  - blad krytyczny, np. brak dyskietki ("Abort, Retry, Fail?", INT 24h).
 * Zawieszenie = brak linii DONE.
 */
#include <windows.h>
#include "testlog.h"

static int try_open(const char *name)
{
    char buf[16];
    int f = _lopen(name, 0), n = -1;
    if (f != -1) {
        n = _lread(f, buf, sizeof(buf));
        _lclose(f);
    }
    return n;
}

static void run_test(void)
{
    DWORD t;
    /* jedna stacja: B: nie istnieje, zadnej prosby o zamiane dyskietki */
    CHECK_INT(GetDriveType(0), DRIVE_REMOVABLE, "GetDriveType(A:)");
    CHECK_INT(GetDriveType(1), 0, "GetDriveType(B:) = brak dysku przy jednej stacji");
    CHECK_INT(try_open("B:\\LOADER.EXE"), -1, "B: sciezka zwraca blad, nie czyta dyskietki z A:");
    CHECK_INT(try_open("A:\\LOADER.EXE"), 16, "A: dziala dalej");
    /* run_examples.py wyjmuje dyskietke i daje znac klawiszem Shift */
    t = GetTickCount();
    while (GetAsyncKeyState(VK_SHIFT) >= 0 && GetTickCount() - t < 60000UL) ;
    CHECK(GetAsyncKeyState(VK_SHIFT) < 0 || GetTickCount() - t < 60000UL, "sygnal: dyskietka wyjeta");
    CHECK(try_open("A:\\NIEMA123.XYZ") == -1, "brak dyskietki: odczyt zwraca blad");
    {   /* zapis musi dojsc do stacji (odczyt moze skonczyc sie w buforach DOS-a) */
        static char data[2048];
        int f = _lcreat("A:\\NOWY123.TMP", 0), ok = 0;
        if (f != -1) {
            ok = _lwrite(f, data, sizeof(data)) == sizeof(data);
            _lclose(f);
        }
        CHECK(!ok, "brak dyskietki: zapis zwraca blad zamiast zawieszenia");
    }
    t_done();
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "DOSPROMP";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "DosPrompt";
        RegisterClass(&wc);
    }
    h = CreateWindow("DosPrompt", "Prosby DOS", WS_OVERLAPPEDWINDOW, 60, 60, 400, 200, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    run_test();
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
