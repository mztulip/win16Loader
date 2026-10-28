/*
 * WinMain.c - win16test: minimalna apka Win16 do testu ETAP 18g
 *
 * Cel: sprawdzic ze LOADER.EXE poprawnie:
 *   - wczytuje NE z zasobami (RT_MENU, RT_DIALOG, RT_ACCEL)
 *   - przechodzi przez InitTask -> WinMain
 *   - WinMain loguje wpis na serial (OutputDebugString)
 *
 * Celowo NIE uzywamy: LoadAccelerators, TranslateAccelerator, DialogBox,
 * GetSystemMenu, AppendMenu - nie sa potrzebne do testu rc_loader.
 *
 * Kompilacja: make (w katalogu win16test)
 *   wcc -ms -zW -bt=windows -zq WinMain.c
 *   wlink @win16test.lnk
 *   wrc Resource.res win16test.exe
 */

#include <windows.h>
#include "Resource.h"

HANDLE g_hInstance;

int PASCAL WinMain(HANDLE hInstance, HANDLE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    g_hInstance = hInstance;

    /* Log wejscia do WinMain - widoczny przez serial (KERNEL.115) */
    OutputDebugString("WIN16TEST: WinMain entered\r\n");

    /* Konczymy natychmiast - celem testu jest samo dotarcie do WinMain */
    PostQuitMessage(0);
    return 0;
}
