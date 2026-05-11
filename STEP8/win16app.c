/*
 * win16app.c - STEP8b: GetMessage + DispatchMessage + PostQuitMessage
 *
 * Importuje:
 *   OUTPUTDEBUGSTRING z KERNEL (ordinal 1)
 *   REGISTERCLASS     z USER   (ordinal 1)
 *   CREATEWINDOW      z USER   (ordinal 2)
 *   POSTQUITMESSAGE   z USER   (ordinal 6)
 *   GETMESSAGE        z USER   (ordinal 7)
 *   TRANSLATEMESSAGE  z USER   (ordinal 8)
 *   DISPATCHMESSAGE   z USER   (ordinal 9)
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s win16app.c -fo=win16app.obj
 *   wlink system windows name win16app.exe file win16app.obj
 *         import OUTPUTDEBUGSTRING KERNEL.1
 *         import REGISTERCLASS    USER.1
 *         import CREATEWINDOW     USER.2
 *         import POSTQUITMESSAGE  USER.6
 *         import GETMESSAGE       USER.7
 *         import TRANSLATEMESSAGE USER.8
 *         import DISPATCHMESSAGE  USER.9
 *         option nodefaultlibs option start=app_entry_ option quiet
 */

/* ============================================================
 * Typy Win16 (powielone, brak wspolnych headerow)
 * ============================================================ */
typedef unsigned short HWND;
typedef unsigned short UINT;
typedef unsigned short WPARAM;
typedef unsigned long  LPARAM;
typedef unsigned short BOOL;
typedef long           LRESULT;

typedef LRESULT (__far __pascal *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#define WM_CREATE   0x0001
#define WM_DESTROY  0x0002
#define WM_PAINT    0x000F

typedef struct {
    unsigned          style;
    WNDPROC           lpfnWndProc;
    int               cbClsExtra;
    int               cbWndExtra;
    unsigned          hInstance;      /* DS selector tej aplikacji */
    unsigned          hIcon;
    unsigned          hCursor;
    unsigned          hbrBackground;
    const char __far *lpszMenuName;
    const char __far *lpszClassName;
} WNDCLASS;

typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} MSG;

/* ============================================================
 * Importy
 * ============================================================ */
extern void    __far __pascal OutputDebugString(const char __far *s);
extern BOOL    __far __pascal RegisterClass(const WNDCLASS __far *wc);
extern HWND    __far __pascal CreateWindow(
    const char __far *, const char __far *,
    unsigned long, int, int, int, int,
    HWND, unsigned, unsigned, void __far *);
extern void    __far __pascal PostQuitMessage(int exitCode);
extern BOOL    __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                          UINT msgMin, UINT msgMax);
extern BOOL    __far __pascal TranslateMessage(const MSG __far *pmsg);
extern LRESULT __far __pascal DispatchMessage(const MSG __far *pmsg);

unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];

/* ============================================================
 * Dane aplikacji (w DGROUP - dostepne gdy DS = SEL_APP_DATA)
 * ============================================================ */
static char g_classname[]    = "TestClass";
static WNDCLASS g_wc;

static char msg_create[] = "STEP8b: WM_CREATE\n";
static char msg_quit[]   = "STEP8b: PostQuitMessage(0)\n";
static char msg_done[]   = "STEP8b: GetMessage loop done.\n";

/* ============================================================
 * WndProc
 * ============================================================ */
LRESULT __far __pascal WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    if (msg == WM_CREATE) {
        OutputDebugString(msg_create);
        PostQuitMessage(0);
        OutputDebugString(msg_quit);
    }
    return 0;
}

/* ============================================================
 * app_entry
 * ============================================================ */
void __far app_entry(void)
{
    HWND hwnd;
    MSG  msg;

    g_wc.style          = 0;
    g_wc.lpfnWndProc    = WndProc;
    g_wc.cbClsExtra     = 0;
    g_wc.cbWndExtra     = 0;
    g_wc.hInstance      = get_ds();
    g_wc.hIcon          = 0;
    g_wc.hCursor        = 0;
    g_wc.hbrBackground  = 0;
    g_wc.lpszMenuName   = 0;
    g_wc.lpszClassName  = g_classname;

    RegisterClass(&g_wc);

    hwnd = CreateWindow(
        g_classname, "Test Window",
        0UL, 0, 0, 320, 200,
        0, 0, get_ds(), 0);
    (void)hwnd;

    /* Petla komunikatow - jak w WinMain */
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OutputDebugString(msg_done);
}
