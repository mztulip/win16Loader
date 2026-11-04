/* taskchld.c - program potomny testu zadan (example_apps/tasks), uruchamiany przez WinExec. */
#include <windows.h>
#include "testlog.h"

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    int i;
    t_app = "TASKCHLD";
    CHECK(prev == 0, "hPrevInstance = 0");
    CHECK(hi != 0, "hInstance");
    CHECK(_fstrncmp(cmd, "arg1 arg2", 9) == 0 && (cmd[9] == 0 || cmd[9] == 13), "linia polecen");
    CHECK_INT(show, SW_SHOWMINIMIZED, "nCmdShow z WinExec");
    CHECK_INT(GetNumTasks(), 2, "GetNumTasks w dziecku");
    CHECK(HIWORD((DWORD)(void __far *)&i) == HIWORD((DWORD)(void __far *)&t_pass), "SS = DS dziecka");
    for (i = 0; i < 20; i++) Yield();                 /* rodzic musi dostawac CPU */
    t_log("koniec dziecka", TRUE, "");
    return 0;
}
