/* firstsiz.c - test pierwszego WM_SIZE/WM_MOVE okna:
 * okno utworzone bez WS_VISIBLE dostaje je przy pierwszym pokazaniu, takze przez
 * SetWindowPos(SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE), i tylko raz. Tak pokazuje kontrolki
 * VCL Delphi: strona TNotebook (Align = alClient) dostaje rozmiar dopiero w WM_SIZE rodzica.
 */
#include <windows.h>
#include "testlog.h"

#define SHOW_FLAGS (SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW)
#define HIDE_FLAGS (SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW)

static int g_size, g_move, g_cx, g_cy, g_mx, g_my;
static HWND g_page;

LONG FAR PASCAL _export BookProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_SIZE) {
        g_size++; g_cx = LOWORD(lp); g_cy = HIWORD(lp);
        if (g_page) MoveWindow(g_page, 0, 0, g_cx, g_cy, TRUE);    /* jak Align = alClient */
    }
    if (m == WM_MOVE) { g_move++; g_mx = (int)LOWORD(lp); g_my = (int)HIWORD(lp); }
    return DefWindowProc(h, m, wp, lp);
}

static void reset(void) { g_size = g_move = 0; }

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int sh)
{
    WNDCLASS wc;
    HWND top, book, b2;
    RECT r;
    t_app = "FIRSTSIZE";
    (void)hp; (void)cl; (void)sh;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BookProc;
    wc.hInstance = hi;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "Book";
    RegisterClass(&wc);

    top = CreateWindow("Book", "Pierwszy rozmiar", WS_OVERLAPPEDWINDOW, 20, 20, 300, 200, NULL, NULL, hi, NULL);
    ShowWindow(top, SW_SHOW);
    reset();

    /* kontrolka bez WS_VISIBLE, strona 0x0 w srodku (jak TNotebook i TPage) */
    book = CreateWindow("Book", "", WS_CHILD, 10, 20, 150, 90, top, (HMENU)1, hi, NULL);
    CHECK_INT(g_size, 0, "CreateWindow bez WS_VISIBLE: brak WM_SIZE");
    g_page = CreateWindow("Book", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, book, (HMENU)2, hi, NULL);
    reset();

    SetWindowPos(book, 0, 0, 0, 0, 0, SHOW_FLAGS);
    CHECK(IsWindowVisible(book), "SetWindowPos pokazuje okno");
    CHECK(g_size >= 1 && g_cx == 150 && g_cy == 90, "pierwszy SetWindowPos(SHOW): WM_SIZE z rozmiarem");
    CHECK(g_move >= 1 && g_mx == 10 && g_my == 20, "pierwszy SetWindowPos(SHOW): WM_MOVE z pozycja");
    GetClientRect(g_page, &r);
    CHECK(r.right == 150 && r.bottom == 90, "strona dostala rozmiar rodzica");

    /* ukrycie i kolejne pokazania niczego nie wysylaja */
    g_page = 0;
    SetWindowPos(book, 0, 0, 0, 0, 0, HIDE_FLAGS);
    reset();
    SetWindowPos(book, 0, 0, 0, 0, 0, SHOW_FLAGS);
    CHECK_INT(g_size, 0, "drugi SetWindowPos(SHOW): bez WM_SIZE");
    CHECK_INT(g_move, 0, "drugi SetWindowPos(SHOW): bez WM_MOVE");
    ShowWindow(book, SW_HIDE);
    ShowWindow(book, SW_SHOW);
    CHECK_INT(g_size, 0, "ShowWindow po SetWindowPos: bez WM_SIZE");

    /* ShowWindow nadal wysyla pierwszy raz, dokladnie raz */
    b2 = CreateWindow("Book", "", WS_CHILD, 30, 40, 60, 50, top, (HMENU)3, hi, NULL);
    reset();
    ShowWindow(b2, SW_SHOW);
    CHECK_INT(g_size, 1, "pierwszy ShowWindow: jeden WM_SIZE");
    CHECK_INT(g_move, 1, "pierwszy ShowWindow: jeden WM_MOVE");
    CHECK(g_cx == 60 && g_cy == 50, "pierwszy ShowWindow: rozmiar");

    DestroyWindow(top);
    t_done();
    return 0;
}
