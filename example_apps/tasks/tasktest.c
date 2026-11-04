/* tasktest.c - zadania jak w Windows 3.1: WinExec uruchamia TASKCHLD.EXE, oba przelaczaja sie
 * przez Yield, koniec dziecka nie konczy rodzica. */
#include <windows.h>
#include "testlog.h"

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    UINT h;
    HTASK me;
    long yields = 0;
    DWORD t0;
    (void)prev; (void)cmd; (void)show;
    t_app = "TASKTEST";
    me = GetCurrentTask();
    CHECK(me != 0, "GetCurrentTask");
    CHECK_INT(GetNumTasks(), 1, "GetNumTasks na starcie");
    CHECK(WinExec("A:\\NIEMA.EXE", SW_SHOWNORMAL) == 2, "WinExec: brak pliku = 2");
    h = WinExec("A:\\TASKCHLD.EXE arg1 arg2", SW_SHOWMINIMIZED);
    CHECK(h > 32, "WinExec: sukces > 32");
    CHECK(h != (UINT)hi, "dziecko ma inny hInstance");
    CHECK(GetCurrentTask() == me, "po WinExec dalej to samo zadanie");
    CHECK_INT(GetNumTasks(), 2, "GetNumTasks z dzieckiem");
    t0 = GetTickCount();
    while (GetNumTasks() > 1 && GetTickCount() - t0 < 20000UL) { Yield(); yields++; }
    CHECK_INT(GetNumTasks(), 1, "dziecko zakonczone");
    CHECK(yields >= 5, "Yield przelaczal zadania");
    CHECK(GetCurrentTask() == me, "rodzic dziala dalej");
    t_done();
    return 0;
}
