/* sysdll.c - test malych bibliotek systemowych: KEYBOARD.DRV (ANSI<->OEM,
 * mapowanie klawiszy), atomy globalne USER, TOOLHELP, DDEML, VER, MMSYSTEM,
 * COMMDLG oraz drobnych funkcji USER dodanych dla Calmiry.
 * Okno pokazuje tekst przekonwertowany przez AnsiToOem/OemToAnsi. Alt+F4 zamyka.
 */
#include <windows.h>
#include <toolhelp.h>
#include <ddeml.h>
#include <ver.h>
#include <mmsystem.h>
#include <commdlg.h>
#include "testlog.h"

static char g_line[3][80];

static void keyboard_tests(void)
{
    static char ansi[] = "Zazolc gesla jazn: \xE4\xF6\xFC\xC4\xD6\xDC\xDF";   /* aouAOUss z umlautami */
    char oem[40], back[40];
    AnsiToOem(ansi, oem);
    CHECK_INT((BYTE)oem[19], 0x84, "AnsiToOem: a-umlaut -> 0x84 (cp437)");
    CHECK_INT((BYTE)oem[25], 0xE1, "AnsiToOem: ostre s -> 0xE1");
    OemToAnsi(oem, back);
    CHECK_STR(back, ansi, "OemToAnsi(AnsiToOem(x)) == x");
    AnsiToOemBuff("abc\xE9", oem, 4);
    CHECK_INT((BYTE)oem[3], 0x82, "AnsiToOemBuff: e-ostre -> 0x82");
    CHECK_INT(VkKeyScan('a'), 'A', "VkKeyScan('a')");
    CHECK_INT(VkKeyScan('A'), 0x141, "VkKeyScan('A') z Shift");
    CHECK_INT(MapVirtualKey('A', 0), 0x1E, "MapVirtualKey VK->scancode");
    CHECK_INT(MapVirtualKey(0x1E, 1), 'A', "MapVirtualKey scancode->VK");
    CHECK_INT(GetKeyboardType(0), 4, "GetKeyboardType: 101/102");
    CHECK_INT(GetKBCodePage(), 437, "GetKBCodePage");
    lstrcpy(g_line[0], "ANSI: ");
    lstrcat(g_line[0], ansi);
    lstrcpy(g_line[1], "po OEM i z powrotem: ");
    lstrcat(g_line[1], back);
}

static void atom_tests(void)
{
    ATOM a, b;
    char buf[40];
    a = GlobalAddAtom("SysDllTestAtom");
    CHECK(a >= 0xC000, "GlobalAddAtom: atom napisowy >= 0xC000");
    b = GlobalAddAtom("sysdlltestatom");
    CHECK_INT(b, a, "GlobalAddAtom: bez rozrozniania wielkosci liter");
    CHECK_INT(GlobalFindAtom("SYSDLLTESTATOM"), a, "GlobalFindAtom");
    GlobalGetAtomName(a, buf, sizeof(buf));
    CHECK_STR(buf, "SysDllTestAtom", "GlobalGetAtomName");
    CHECK_INT(GlobalAddAtom("#123"), 123, "GlobalAddAtom(\"#123\") = atom calkowity");
    GlobalGetAtomName(123, buf, sizeof(buf));
    CHECK_STR(buf, "#123", "GlobalGetAtomName(atom calkowity)");
    CHECK_INT(GlobalFindAtom("NieIstnieje"), 0, "GlobalFindAtom: brak");
    GlobalDeleteAtom(a);
}

HDDEDATA CALLBACK __export DdeCb(UINT t, UINT f, HCONV c, HSZ a, HSZ b, HDDEDATA d, DWORD x, DWORD y)
{
    (void)t; (void)f; (void)c; (void)a; (void)b; (void)d; (void)x; (void)y;
    return 0;
}

static void misc_tests(HINSTANCE hi)
{
    TIMERINFO ti;
    MODULEENTRY me;
    DWORD inst = 0, vh;
    HSZ s1, s2, s3;
    char buf[64];
    FARPROC cb;

    /* TOOLHELP */
    ti.dwSize = sizeof(ti);
    {   /* wsprintf: precyzja liczb ("%2.2u" = format czasu w SkiFree) */
        char b[40];
        wsprintf(b, "%2u:%2.2u:%2.2u.%2.2u", 1, 2, 3, 4);
        CHECK_STR(b, " 1:02:03.04", "wsprintf %2.2u");
        wsprintf(b, "%.3d|%5.3d|%05d|%3d", -5, 7, 42, 9);
        CHECK_STR(b, "-005|  007|00042|  9", "wsprintf precyzja, szerokosc, zera");
    }
    CHECK(TimerCount(&ti), "TimerCount");
    CHECK(ti.dwmsSinceStart > 0, "TimerCount: czas > 0");
    me.dwSize = sizeof(me);
    CHECK(ModuleFindHandle(&me, GetModuleHandle("SYSDLL")) != 0, "ModuleFindHandle");
    CHECK_STR(me.szModule, "SYSDLL", "MODULEENTRY.szModule");

    /* DDEML */
    cb = MakeProcInstance((FARPROC)DdeCb, hi);
    CHECK_INT(DdeInitialize(&inst, (PFNCALLBACK)cb, APPCMD_CLIENTONLY, 0), DMLERR_NO_ERROR, "DdeInitialize");
    CHECK(inst != 0, "DdeInitialize: identyfikator");
    s1 = DdeCreateStringHandle(inst, "Progman", CP_WINANSI);
    s2 = DdeCreateStringHandle(inst, "PROGMAN", CP_WINANSI);
    s3 = DdeCreateStringHandle(inst, "Inny", CP_WINANSI);
    CHECK(s1 != 0, "DdeCreateStringHandle");
    CHECK_INT(DdeCmpStringHandles(s1, s2), 0, "DdeCmpStringHandles: rowne");
    CHECK(DdeCmpStringHandles(s1, s3) != 0, "DdeCmpStringHandles: rozne");
    DdeQueryString(inst, s1, buf, sizeof(buf), CP_WINANSI);
    CHECK_STR(buf, "Progman", "DdeQueryString");
    CHECK(DdeConnect(inst, s1, s1, NULL) == 0, "DdeConnect: brak serwera = 0");
    DdeFreeStringHandle(inst, s1);
    DdeUninitialize(inst);

    /* VER, MMSYSTEM, COMMDLG */
    CHECK_INT(GetFileVersionInfoSize("A:\\SYSDLL.EXE", &vh), 0, "GetFileVersionInfoSize: brak VERSIONINFO");
    CHECK(!sndPlaySound("ding.wav", SND_ASYNC), "sndPlaySound: brak dzwieku = FALSE");
    CHECK_INT(CommDlgExtendedError(), 0, "CommDlgExtendedError");

    /* USER: drobne funkcje */
    CHECK_INT(GetSystemDebugState(), 0, "GetSystemDebugState");
    CHECK(GetFreeSystemResources(0) > 0, "GetFreeSystemResources");
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        keyboard_tests();
        atom_tests();
        misc_tests(((LPCREATESTRUCT)lp)->hInstance);
        wsprintf(g_line[2], "wynik: pass=%d fail=%d", t_pass, t_fail);
        t_done();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        int i;
        for (i = 0; i < 3; i++) TextOut(dc, 10, 10 + i * 20, g_line[i], lstrlen(g_line[i]));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "SYSDLL";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "SysDllTest";
        RegisterClass(&wc);
    }
    h = CreateWindow("SysDllTest", "Biblioteki systemowe - test", WS_OVERLAPPEDWINDOW, 40, 40, 420, 130, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
