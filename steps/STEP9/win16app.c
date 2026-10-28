/*
 * win16app.c - STEP8c: pelny flow WM_CREATE->WM_PAINT->WM_DESTROY->WM_QUIT
 *
 * Importuje:
 *   OUTPUTDEBUGSTRING z KERNEL (ordinal 1)
 *   REGISTERCLASS     z USER   (ordinal 1)
 *   CREATEWINDOW      z USER   (ordinal 2)
 *   POSTQUITMESSAGE   z USER   (ordinal 6)
 *   GETMESSAGE        z USER   (ordinal 7)
 *   TRANSLATEMESSAGE  z USER   (ordinal 8)
 *   DISPATCHMESSAGE   z USER   (ordinal 9)
 *   SHOWWINDOW        z USER   (ordinal 10)
 *   UPDATEWINDOW      z USER   (ordinal 11)
 *   BEGINPAINT        z USER   (ordinal 12)
 *   ENDPAINT          z USER   (ordinal 13)
 *   DEFWINDOWPROC     z USER   (ordinal 14)
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

typedef struct {
    unsigned hdc;
    unsigned fErase;
    unsigned rcPaint[4];
    unsigned fRestore;
    unsigned fIncUpdate;
} PAINTSTRUCT;

/* ============================================================
 * Importy
 * ============================================================ */
extern void     __far __pascal OutputDebugString(const char __far *s);
extern BOOL     __far __pascal RegisterClass(const WNDCLASS __far *wc);
extern HWND     __far __pascal CreateWindow(
    const char __far *, const char __far *,
    unsigned long, int, int, int, int,
    HWND, unsigned, unsigned, void __far *);
extern BOOL     __far __pascal PostMessage(HWND hwnd, UINT msg,
                                            WPARAM wp, LPARAM lp);
extern void     __far __pascal PostQuitMessage(int exitCode);
extern BOOL     __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                           UINT msgMin, UINT msgMax);
extern BOOL     __far __pascal TranslateMessage(const MSG __far *pmsg);
extern LRESULT  __far __pascal DispatchMessage(const MSG __far *pmsg);
extern BOOL     __far __pascal ShowWindow(HWND hwnd, int nCmdShow);
extern BOOL     __far __pascal UpdateWindow(HWND hwnd);
extern unsigned __far __pascal BeginPaint(HWND hwnd, PAINTSTRUCT __far *ps);
extern BOOL     __far __pascal EndPaint(HWND hwnd, const PAINTSTRUCT __far *ps);
extern LRESULT  __far __pascal DefWindowProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp);
/* GDI.EXE */
extern BOOL     __far __pascal TextOut(unsigned hdc, int x, int y,
                                        const char __far *s, int len);

unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];

/* ============================================================
 * Dane aplikacji (w DGROUP - dostepne gdy DS = SEL_APP_DATA)
 * ============================================================ */
static char g_classname[]    = "TestClass";
static WNDCLASS g_wc;

static char msg_create[]  = "STEP9c: WM_CREATE\n";
static char msg_paint[]   = "STEP9c: WM_PAINT\n";
static char msg_destroy[] = "STEP9c: WM_DESTROY\n";
static char msg_done[]    = "STEP9c: loop done.\n";
static char gdi_text[]    = "Hello GDI!";

/* ============================================================
 * WndProc
 * WM_CREATE  -> druk na COM1
 * WM_PAINT   -> BeginPaint/druk/EndPaint, potem PostMessage(WM_DESTROY)
 * WM_DESTROY -> druk, PostQuitMessage(0)
 * ============================================================ */
LRESULT __far __pascal WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    (void)wp; (void)lp;

    if (msg == WM_CREATE) {
        OutputDebugString(msg_create);
    } else if (msg == WM_PAINT) {
        unsigned hdc = BeginPaint(hwnd, &ps);
        OutputDebugString(msg_paint);
        TextOut(hdc, 10, 2, gdi_text, 10);
        EndPaint(hwnd, &ps);
        /* Wyslij WM_DESTROY do kolejki - koniec cyklu zycia okna */
        PostMessage(hwnd, WM_DESTROY, 0, 0L);
    } else if (msg == WM_DESTROY) {
        OutputDebugString(msg_destroy);
        PostQuitMessage(0);
    } else {
        return DefWindowProc(hwnd, msg, wp, lp);
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

    ShowWindow(hwnd, 1);
    UpdateWindow(hwnd);   /* -> WM_PAINT (synchronicznie) */

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OutputDebugString(msg_done);
}
