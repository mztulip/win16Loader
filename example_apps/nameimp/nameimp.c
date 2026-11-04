/* nameimp.c - import po nazwie z KERNEL: stale (__AHIncr, __AHShift, __WinFlags, __0040H, __ROMBIOS)
 * i funkcje (Catch, Throw). Stale nie sa eksportami pliku KERNEL.EXE, a zly import to pusty stub:
 * nie zdejmuje argumentow ze stosu, wiec powrot konczy sie wyjatkiem. Throw sprawdzany tez przez
 * zagniezdzone wywolania API (SendMessage). */
#include <windows.h>
#include "testlog.h"

extern void __far __AHIncr(void), __far __AHShift(void), __far __WinFlags(void), __far __0040H(void), __far __ROMBIOS(void);
#pragma aux __AHIncr "*";
#pragma aux __AHShift "*";
#pragma aux __WinFlags "*";
#pragma aux __0040H "*";
#pragma aux __ROMBIOS "*";
int  FAR PASCAL NCatch(int FAR *buf);
void FAR PASCAL NThrow(const int FAR *buf, int val);

static int g_buf[9];
static int g_depth;

#define OFF(f) ((WORD)(DWORD)(void __far *)(f))

static void deep(int n, int val)
{
    volatile int pad[8];
    pad[0] = n;
    if (n > 0) deep(n - 1, val);
    else NThrow(g_buf, val);
    pad[1] = pad[0];
}

LRESULT FAR PASCAL __export WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER + 1) {
        if (wp) { g_depth++; SendMessage(h, WM_USER + 1, wp - 1, lp); }
        else NThrow(g_buf, (int)lp);
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, msg, wp, lp);
}

static void tests(HWND h)
{
    int i, r, keep = 0x1234, bad = 0;
    int top1, top2;
    char d[60];
    CHECK_INT(OFF(__AHIncr), 8, "__AHIncr po nazwie");
    CHECK_INT(OFF(__AHShift), 3, "__AHShift po nazwie");
    CHECK(OFF(__WinFlags) == LOWORD(GetWinFlags()), "__WinFlags po nazwie");
    CHECK(*(WORD __far *)(((DWORD)OFF(__0040H) << 16) | 0x13) >= 512, "__0040H po nazwie: dane BIOS");
    CHECK(OFF(__ROMBIOS) != 0, "__ROMBIOS po nazwie");

    r = NCatch(g_buf);
    if (r == 0) {
        CHECK(TRUE, "Catch: pierwszy powrot = 0");
        NThrow(g_buf, 77);
        CHECK(FALSE, "Throw nie wraca");
    } else {
        CHECK_INT(r, 77, "Throw: Catch wraca z wartoscia");
        CHECK_INT(keep, 0x1234, "zmienne po Throw");
    }

    r = NCatch(g_buf);
    if (!r) deep(20, 5);
    CHECK_INT(r, 5, "Throw z glebi wywolan");

    /* Throw przez kilka poziomow SendMessage: stan thunka API musi wrocic do poziomu Catch */
    r = NCatch(g_buf);
    if (!r) SendMessage(h, WM_USER + 1, 6, 9);
    CHECK_INT(r, 9, "Throw przez SendMessage");
    CHECK(IsWindow(h), "API dziala po Throw przez SendMessage");

    /* powtarzanie: stos i poziomy thunka nie moga uciekac */
    top1 = (int)&keep;
    for (i = 0; i < 500; i++) {
        r = NCatch(g_buf);
        if (!r) SendMessage(h, WM_USER + 1, 3, i + 1);
        else if (r != i + 1) bad++;
    }
    top2 = (int)&keep;
    wsprintf(d, "bledy=%d", bad);
    t_log("500 razy Catch/Throw", bad == 0 && top1 == top2 && IsWindow(h), d);
    t_done();
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "NAMEIMP";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "NameImp";
        RegisterClass(&wc);
    }
    h = CreateWindow("NameImp", "Import po nazwie", WS_OVERLAPPEDWINDOW, 60, 60, 360, 160, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    tests(h);
    DestroyWindow(h);
    while (GetMessage(&m, NULL, 0, 0)) DispatchMessage(&m);
    return m.wParam;
}
