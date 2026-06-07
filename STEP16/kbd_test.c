/*
 * kbd_test.c - STEP16: test klawiatury
 *
 * Prosta aplikacja Win16 sprawdzajaca obsluge klawiatury:
 *   - Czeka na WM_KEYDOWN przez PeekMessage
 *   - Drukuje kazdy klawisz przez OutputDebugString ("KBDT VK=0xXX SC=...")
 *   - Po Esc lub 20 klawiszach: koniec (PostQuitMessage)
 *
 * Oczekiwane klawisze w tescie: Up/Down/Left/Right/F1/Esc
 * VK: 0x26/0x28/0x25/0x27/0x70/0x1B
 *
 * Kompilacja: wcc -ms -q -zl -s kbd_test.c
 * Link: wlink system windows ... (patrz Makefile)
 */

/* ============================================================
 * Win16 types / API stubs
 * ============================================================ */
typedef unsigned int   UINT;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned int   BOOL;
typedef int            INT;
typedef void __far    *LPVOID;
typedef const char __far *LPCSTR;
typedef char __far    *LPSTR;
typedef unsigned long  LRESULT;
typedef unsigned int   WPARAM;
typedef unsigned long  LPARAM;
typedef unsigned int   HWND;
typedef unsigned int   HDC;
typedef unsigned int   HINSTANCE;
typedef unsigned int   HMENU;
typedef void (__far __pascal *WNDPROC)(void);

/* ============================================================
 * Win16 messages
 * ============================================================ */
#define WM_CREATE    0x0001
#define WM_DESTROY   0x0002
#define WM_SIZE      0x0005
#define WM_PAINT     0x000F
#define WM_QUIT      0x0012
#define WM_KEYDOWN   0x0100

#define VK_ESCAPE    0x1B
#define SW_SHOW      5
#define CS_HREDRAW   0x0002
#define CS_VREDRAW   0x0001

/* ============================================================
 * MSG struct
 * ============================================================ */
#pragma pack(push,1)
typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    WORD   ptx, pty;
} MSG;

typedef struct {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    UINT      hIcon;
    UINT      hCursor;
    UINT      hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
} WNDCLASS;
#pragma pack(pop)

/* ============================================================
 * Win16 API imports (przez thunk INT 3F)
 * ============================================================ */
extern void __far __pascal OutputDebugString(const char __far *s);
extern BOOL __far __pascal RegisterClass(const WNDCLASS __far *wc);
extern HWND __far __pascal CreateWindow(
    LPCSTR cls, LPCSTR title, DWORD style,
    int x, int y, int w, int h,
    HWND parent, HMENU hMenu, HINSTANCE hInst, LPVOID lpParam);
extern BOOL __far __pascal ShowWindow(HWND hwnd, int nCmd);
extern BOOL __far __pascal UpdateWindow(HWND hwnd);
extern BOOL __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                       UINT min, UINT max);
extern BOOL __far __pascal PeekMessage(MSG __far *pmsg, HWND hwnd,
                                        UINT min, UINT max, UINT remove);
extern BOOL __far __pascal TranslateMessage(const MSG __far *pmsg);
extern LRESULT __far __pascal DispatchMessage(const MSG __far *pmsg);
extern LRESULT __far __pascal DefWindowProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp);
extern void __far __pascal PostQuitMessage(int code);

/* ============================================================
 * hex helpers (bez printf/snprintf - nie ma libc)
 * ============================================================ */
static char g_buf[32];

static void hex8(unsigned char v, char *out)
{
    static const char h[] = "0123456789ABCDEF";
    out[0] = h[(v >> 4) & 0xF];
    out[1] = h[v & 0xF];
    out[2] = 0;
}

static const char __far *vk_name(WPARAM vk)
{
    switch (vk) {
        case 0x1B: return "ESC";
        case 0x26: return "UP";
        case 0x28: return "DOWN";
        case 0x25: return "LEFT";
        case 0x27: return "RIGHT";
        case 0x0D: return "ENTER";
        case 0x20: return "SPACE";
        case 0x70: return "F1";
        case 0x71: return "F2";
        case 0x72: return "F3";
        case 0x73: return "F4";
        case 0x74: return "F5";
        case 0x75: return "F6";
        case 0x76: return "F7";
        case 0x77: return "F8";
        default:   return "?";
    }
}

/* ============================================================
 * Global state
 * ============================================================ */
static int g_key_count = 0;

/* ============================================================
 * WndProc
 * ============================================================ */
LRESULT __far __pascal WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_CREATE) {
        OutputDebugString("KBDT: ready, waiting for keys...\r\n");
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        char buf[40];
        char hx[3];
        /* Build: "KBDT VK=0xXX (NAME)\r\n" */
        buf[0] = 'K'; buf[1] = 'B'; buf[2] = 'D'; buf[3] = 'T';
        buf[4] = ' '; buf[5] = 'V'; buf[6] = 'K'; buf[7] = '=';
        buf[8] = '0'; buf[9] = 'x';
        hex8((unsigned char)(wp & 0xFF), hx);
        buf[10] = hx[0]; buf[11] = hx[1];
        buf[12] = ' '; buf[13] = '(';
        {
            const char __far *name = vk_name(wp);
            int i = 14;
            while (*name) buf[i++] = *name++;
            buf[i++] = ')';
            buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = 0;
        }
        OutputDebugString(buf);
        g_key_count++;
        if (wp == VK_ESCAPE || g_key_count >= 20) {
            OutputDebugString("KBDT: done\r\n");
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ============================================================
 * Entry point (app_entry_ symbol, jak win16app.c)
 * ============================================================ */
unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];

void __far app_entry(void)
{
    HINSTANCE hInst = (HINSTANCE)get_ds();
    WNDCLASS wc;
    HWND     hwnd;
    MSG      msg;

    OutputDebugString("KBDT: WinMain start\r\n");

    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = (WNDPROC)WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInst;
    wc.hIcon         = 0;
    wc.hCursor       = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName  = 0;
    wc.lpszClassName = "KbdTest";

    if (!RegisterClass(&wc)) {
        OutputDebugString("KBDT: RegisterClass failed\r\n");
        return;
    }

    hwnd = CreateWindow("KbdTest", "Keyboard Test",
                        0x00CF0000UL,  /* WS_OVERLAPPEDWINDOW */
                        0, 0, 320, 200, 0, 0, hInst, 0);
    if (!hwnd) {
        OutputDebugString("KBDT: CreateWindow failed\r\n");
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    OutputDebugString("KBDT: entering message loop\r\n");

    /* game-style PeekMessage loop */
    for (;;) {
        if (PeekMessage(&msg, 0, 0, 0, 1)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    OutputDebugString("KBDT: exit\r\n");
}
