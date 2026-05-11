/*
 * win16app.c - STEP8a: test RegisterClass + CreateWindow -> WM_CREATE
 *
 * Importuje:
 *   OUTPUTDEBUGSTRING z KERNEL (ordinal 1) - druk na COM1
 *   REGISTERCLASS     z USER   (ordinal 1)
 *   CREATEWINDOW      z USER   (ordinal 2)
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s win16app.c -fo=win16app.obj
 *   wlink system windows name win16app.exe file win16app.obj
 *         import OUTPUTDEBUGSTRING KERNEL.1
 *         import REGISTERCLASS USER.1
 *         import CREATEWINDOW  USER.2
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

/* ============================================================
 * Importy
 * ============================================================ */
extern void  __far __pascal OutputDebugString(const char __far *s);
extern BOOL  __far __pascal RegisterClass(const WNDCLASS __far *wc);
extern HWND  __far __pascal CreateWindow(
    const char __far *, const char __far *,
    unsigned long, int, int, int, int,
    HWND, unsigned, unsigned, void __far *);

unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];

/* ============================================================
 * Dane aplikacji (w DGROUP - dostepne gdy DS = SEL_APP_DATA)
 * ============================================================ */
static char g_classname[]    = "TestClass";
static WNDCLASS g_wc;

static char msg_create[] = "STEP8a: WM_CREATE received in WndProc!\n";
static char msg_done[]   = "STEP8a: app_entry done.\n";

/* ============================================================
 * WndProc
 *
 * Wywolywana przez USER::SendMessage ktore wczesniej przelacza
 * DS = SEL_APP_DATA (= get_ds() z RegisterClass), wiec globale
 * tej aplikacji sa dostepne normalnie.
 * ============================================================ */
LRESULT __far __pascal WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_CREATE)
        OutputDebugString(msg_create);
    return 0;
}

/* ============================================================
 * app_entry - punkt wejscia (ne_ip = 0, ne_cs = 1)
 * ============================================================ */
void __far app_entry(void)
{
    HWND hwnd;

    g_wc.style          = 0;
    g_wc.lpfnWndProc    = WndProc;
    g_wc.cbClsExtra     = 0;
    g_wc.cbWndExtra     = 0;
    g_wc.hInstance      = get_ds();  /* SEL_APP_DATA - dla SendMessage */
    g_wc.hIcon          = 0;
    g_wc.hCursor        = 0;
    g_wc.hbrBackground  = 0;
    g_wc.lpszMenuName   = 0;
    g_wc.lpszClassName  = g_classname;

    RegisterClass(&g_wc);

    hwnd = CreateWindow(
        g_classname, "Test Window",
        0UL,
        0, 0, 320, 200,
        0, 0, get_ds(), 0);
    (void)hwnd;

    OutputDebugString(msg_done);
}
