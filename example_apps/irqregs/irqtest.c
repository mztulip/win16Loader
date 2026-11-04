/* irqtest.c - test regresyjny: przerwania sprzetowe (IRQ1 klawiatura, IRQ12 mysz)
 * nie moga zmieniac rejestrow przerwanego kodu. Kiedys procedury w pm_call.asm
 * wysylaly EOI (mov al,20h) PO pop ax - kazde przerwanie zostawialo AL=20h, co
 * dawalo losowe #GP w GDI/USER przy ruchu mysza (zly selektor ekranu itd.).
 * Petla: wpisz wzorce do rejestrow, odczekaj (loop $), sprawdz; w tym czasie
 * run_examples.py rusza mysza i naciska klawisze. Alt+F4 zamyka.
 */
#include <windows.h>
#include "testlog.h"

/* AX, BX, DX, SI, DI = wzorce, potem ~kilka tys. obrotow 'loop $' (przerwania
 * trafiaja tu najczesciej), wynik: OR roznic wzgledem wzorcow (0 = rejestry nietkniete) */
unsigned spin_regs(void);
#pragma aux spin_regs = \
    "mov ax, 5A5Ah"  \
    "mov bx, 0A5A5h" \
    "mov dx, 1234h"  \
    "mov si, 4321h"  \
    "mov di, 0F00Fh" \
    "mov cx, 4000"   \
    "db 0E2h, 0FEh"  \
    "xor ax, 5A5Ah"  \
    "xor bx, 0A5A5h" \
    "xor dx, 1234h"  \
    "xor si, 4321h"  \
    "xor di, 0F00Fh" \
    "or  ax, bx"     \
    "or  ax, dx"     \
    "or  ax, si"     \
    "or  ax, di"     \
    value [ax] modify [bx cx dx si di];

static void run_test(void)
{
    DWORD start = GetTickCount(), n = 0, bad = 0;
    POINT last, p;
    int moves = 0;
    GetCursorPos(&last);
    /* do 150 zmian pozycji myszy albo 40 s */
    while (moves < 150 && GetTickCount() - start < 40000UL) {
        int i;
        for (i = 0; i < 50; i++) { if (spin_regs()) bad++; n++; }
        GetCursorPos(&p);
        if (p.x != last.x || p.y != last.y) { moves++; last = p; }
    }
    {
        char d[80];
        wsprintf(d, "iteracje=%lu ruchy=%d bledne=%lu", n, moves, bad);
        t_log("przebieg", TRUE, d);
    }
    CHECK(moves >= 50, "mysz ruszala sie w trakcie testu (IRQ12)");
    CHECK_INT(bad, 0, "przerwania nie zmieniaja AX/BX/DX/SI/DI");
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
    t_app = "IRQTEST";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "IrqTest";
        RegisterClass(&wc);
    }
    h = CreateWindow("IrqTest", "Rejestry a przerwania", WS_OVERLAPPEDWINDOW, 60, 60, 400, 200, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    run_test();
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
