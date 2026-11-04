/* libchld.c - program z importem statycznym DEMODLL.DLL, uruchamiany przez LIBDEMO.EXE.
 * LibEntry biblioteki musi sie wykonac przed WinMain. */
#include <windows.h>
#include "testlog.h"
#include "demodll.h"

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    DEMOINFO i;
    char d[70];
    (void)hi; (void)prev; (void)show;
    t_app = "LIBCHLD";
    DemoInfo(&i);
    wsprintf(d, "wywolan=%d hInst=%04X heap=%u", i.calls, i.hinst, i.heap);
    t_log("import statyczny: LibEntry przed WinMain, raz", i.calls == 1, d);
    CHECK(i.heap_ok, "import statyczny: sterta DLL dziala");
    CHECK(i.hinst != 0 && i.hinst == i.ds, "import statyczny: hInstance = DS biblioteki");
    /* tresc linii polecen: ktore to uruchomienie (LibEntry nie moze wykonac sie drugi raz) */
    (void)cmd;
    return i.calls == 1 ? 0 : 1;
}
