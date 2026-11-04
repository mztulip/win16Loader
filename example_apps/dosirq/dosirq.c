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

void load_fs(unsigned sel);
#pragma aux load_fs = "db 08Eh, 0E0h" parm [ax] modify [];   /* mov fs, ax */
void load_gs(unsigned sel);
#pragma aux load_gs = "db 08Eh, 0E8h" parm [ax] modify [];   /* mov gs, ax */

unsigned read_fs(void);
#pragma aux read_fs = "db 08Ch, 0E0h" value [ax];            /* mov ax, fs */
unsigned sel_valid(unsigned sel);
#pragma aux sel_valid = "db 00Fh, 000h, 0E0h" "mov ax, 0" "jnz done" "inc ax" "done:" parm [ax] value [ax]; /* verr ax */

/* FS/GS z selektorem zwolnionego bloku: moduly Watcom -3 uzywaja FS/GS do far pointerow i moga
 * je tak zostawic. rm_int_service odkladal je i ladowal z powrotem, co dawalo #GP (Calmira:
 * otwarcie katalogu z DESCRIPT.ION). _lclose: bez far pointerow, wiec FS dochodzi do DOS-a. */
static void test_stale_seg(void)
{
    /* dwa bloki: kolejny przydzial trafi w nizszy selektor, wyzszy zostaje wolny */
    HGLOBAL g1 = GlobalAlloc(GMEM_MOVEABLE, 256), g2 = GlobalAlloc(GMEM_MOVEABLE, 256);
    HGLOBAL hi = g1 > g2 ? g1 : g2, lo = g1 > g2 ? g2 : g1;
    unsigned sel = HIWORD((DWORD)GlobalLock(hi)), fs;
    int f = _lopen("A:\\LOADER.EXE", 0), r;
    load_fs(sel);
    load_gs(sel);
    GlobalUnlock(hi);
    GlobalFree(hi);
    GlobalFree(lo);
    fs = read_fs();
    r = _lclose(f);
    {
        char d[60];
        wsprintf(d, "sel=%04X FS=%04X waznosc=%d", sel, fs, sel_valid(fs));
        t_log("FS trzymal zwolniony selektor", fs == sel && !sel_valid(fs), d);
    }
    CHECK_INT(r, 0, "wywolanie DOS z nieaktualnym FS/GS");
}

static void run_test(void)
{
    static char buf[512];
    DWORD start = GetTickCount();
    POINT last, p;
    long reads = 0;
    int moves = 0, keys = 0, down = 0;
    char d[80];
    test_stale_seg();
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
