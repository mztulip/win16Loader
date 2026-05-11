/*
 * user.c - USER.EXE (STEP8a)
 *
 * Eksportowane funkcje:
 *   ordinal 1: RegisterClass
 *   ordinal 2: CreateWindow
 *   ordinal 3: SendMessage
 *   ordinal 4: LibMain
 *
 * INT 3F handler (pm_call.asm) ustawia DS = SEL_DLL_DATA(1) = 0x60
 * przed wejsciem do kazdej z tych funkcji.
 *
 * SendMessage przelacza DS na app's DS (przechowane w g_classes[].inst_ds)
 * przed wywolaniem WndProc, i przywraca po powrocie.
 *
 * Kompilacja:
 *   wcc -ms -q -zl -s user.c -fo=user.obj
 *   wlink system windows_dll name user.exe file user.obj,libstubs.obj
 *         export REGISTERCLASS.1 export CREATEWINDOW.2
 *         export SENDMESSAGE.3   export LIBMAIN.4
 *         option nodefaultlibs option quiet
 */

/* Port I/O (bez conio.h) */
unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

/* Odczyt/zapis DS (do przelaczania przy wywolaniu WndProc) */
unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];
void set_ds(unsigned sel);
#pragma aux set_ds = "mov ds, ax" parm [ax] modify [];

#define COM1 0x3F8

static void serial_putc(char c)
{
    while (!(io_inb(COM1 + 5) & 0x20));
    if (c == '\n') {
        io_outb(COM1, '\r');
        while (!(io_inb(COM1 + 5) & 0x20));
    }
    io_outb(COM1, c);
}

static void serial_puts(const char *s)
{
    while (*s) serial_putc(*s++);
}

/* ============================================================
 * Typy Win16
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
#define WM_QUIT     0x0012

typedef struct {
    unsigned          style;
    WNDPROC           lpfnWndProc;
    int               cbClsExtra;
    int               cbWndExtra;
    unsigned          hInstance;      /* DS selector aplikacji */
    unsigned          hIcon;
    unsigned          hCursor;
    unsigned          hbrBackground;
    const char __far *lpszMenuName;
    const char __far *lpszClassName;
} WNDCLASS;

/* ============================================================
 * Wewnetrzne tablice klas i okien
 * ============================================================ */
#define MAX_CLASSES 8
#define MAX_WINDOWS 8

static struct {
    char    name[32];
    WNDPROC proc;
    unsigned inst_ds;   /* DS aplikacji ktora zarejestrowala klase */
    int     used;
} g_classes[MAX_CLASSES];

static struct {
    HWND hwnd;
    int  class_idx;
    int  used;
} g_windows[MAX_WINDOWS];

static unsigned g_next_hwnd = 1;

/* ============================================================
 * Kolejka komunikatow (ring buffer, jak Windows 3.1)
 * Jedna globalna kolejka w DGROUP USER.
 * PostMessage: wstawia (asynchronicznie).
 * SendMessage: omija kolejke, wywoluje WndProc bezposrednio.
 * GetMessage:  pobiera z kolejki; FALSE gdy WM_QUIT.
 * ============================================================ */
typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} MSG;

#define MSG_QUEUE_SIZE 32

static MSG     g_msg_queue[MSG_QUEUE_SIZE];
static unsigned g_msg_head = 0;   /* indeks do odczytu */
static unsigned g_msg_tail = 0;   /* indeks do zapisu  */
static unsigned g_msg_count = 0;

/* Forward declaration - SendMessage zdefiniowany pozniej */
LRESULT __far __pascal SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ============================================================
 * PostMessage / PostQuitMessage / GetMessage / TranslateMessage / DispatchMessage
 * ============================================================ */
BOOL __far __pascal PostMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msg_count >= MSG_QUEUE_SIZE) {
        serial_puts("USER: queue full!\n");
        return 0;
    }
    g_msg_queue[g_msg_tail].hwnd    = hwnd;
    g_msg_queue[g_msg_tail].message = msg;
    g_msg_queue[g_msg_tail].wParam  = wp;
    g_msg_queue[g_msg_tail].lParam  = lp;
    g_msg_tail = (g_msg_tail + 1) % MSG_QUEUE_SIZE;
    g_msg_count++;
    return 1;
}

void __far __pascal PostQuitMessage(int exitCode)
{
    serial_puts("USER: PostQuitMessage\n");
    PostMessage(0, WM_QUIT, (WPARAM)exitCode, 0L);
}

BOOL __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                UINT msgMin, UINT msgMax)
{
    (void)hwnd; (void)msgMin; (void)msgMax;

    if (g_msg_count == 0) return 1;   /* kolejka pusta: brak WM_QUIT -> TRUE */

    pmsg->hwnd    = g_msg_queue[g_msg_head].hwnd;
    pmsg->message = g_msg_queue[g_msg_head].message;
    pmsg->wParam  = g_msg_queue[g_msg_head].wParam;
    pmsg->lParam  = g_msg_queue[g_msg_head].lParam;
    g_msg_head = (g_msg_head + 1) % MSG_QUEUE_SIZE;
    g_msg_count--;

    return (pmsg->message != WM_QUIT) ? 1 : 0;
}

BOOL __far __pascal TranslateMessage(const MSG __far *pmsg)
{
    (void)pmsg;
    return 1;
}

LRESULT __far __pascal DispatchMessage(const MSG __far *pmsg)
{
    return SendMessage(pmsg->hwnd, pmsg->message, pmsg->wParam, pmsg->lParam);
}

/* ============================================================
 * Pomocnicze funkcje na far pointerach
 * ============================================================ */
static void far_strncpy(char *dst, const char __far *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = (char)src[i]; i++; }
    dst[i] = 0;
}

static int far_strcmp(const char *a, const char __far *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int find_class(const char __far *name)
{
    int i;
    for (i = 0; i < MAX_CLASSES; i++) {
        if (g_classes[i].used && far_strcmp(g_classes[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ============================================================
 * SendMessage - wywoluje WndProc bezposrednio (synchronicznie)
 *
 * Przed wywolaniem WndProc przelacza DS na inst_ds aplikacji,
 * po powrocie przywraca DS = USER's DGROUP.
 * Dzieki temu WndProc ma poprawny DS i moze uzyc swoich globalnych.
 * ============================================================ */
LRESULT __far __pascal SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC  proc   = 0;
    unsigned app_ds = 0;
    unsigned save_ds;
    LRESULT  result = 0;
    int      i;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            int ci  = g_windows[i].class_idx;
            proc    = g_classes[ci].proc;
            app_ds  = g_classes[ci].inst_ds;
            break;
        }
    }
    if (!proc) return 0;

    /* Przelacz DS na DGROUP aplikacji przed wywolaniem WndProc */
    save_ds = get_ds();
    set_ds(app_ds);
    result = proc(hwnd, msg, wp, lp);
    set_ds(save_ds);
    return result;
}

/* ============================================================
 * RegisterClass
 * ============================================================ */
BOOL __far __pascal RegisterClass(const WNDCLASS __far *wc)
{
    int i;
    serial_puts("USER: RegisterClass\n");
    for (i = 0; i < MAX_CLASSES; i++) {
        if (!g_classes[i].used) {
            far_strncpy(g_classes[i].name, wc->lpszClassName, 32);
            g_classes[i].proc    = wc->lpfnWndProc;
            g_classes[i].inst_ds = wc->hInstance;
            g_classes[i].used    = 1;
            serial_puts("USER: RegisterClass OK\n");
            return 1;
        }
    }
    serial_puts("USER: RegisterClass FULL\n");
    return 0;
}

/* ============================================================
 * CreateWindow - alokuje okno i wysyla WM_CREATE przez SendMessage
 * ============================================================ */
HWND __far __pascal CreateWindow(
    const char __far *cls,
    const char __far *title,
    unsigned long     style,
    int x, int y, int w, int h,
    HWND parent, unsigned hMenu, unsigned hInst,
    void __far *lpParam)
{
    HWND hwnd;
    int  ci, wi;

    (void)title; (void)style;
    (void)x; (void)y; (void)w; (void)h;
    (void)parent; (void)hMenu; (void)hInst; (void)lpParam;

    serial_puts("USER: CreateWindow\n");

    ci = find_class(cls);
    if (ci < 0) { serial_puts("USER: class not found\n"); return 0; }

    for (wi = 0; wi < MAX_WINDOWS; wi++)
        if (!g_windows[wi].used) break;
    if (wi >= MAX_WINDOWS) { serial_puts("USER: no free window\n"); return 0; }

    hwnd = g_next_hwnd++;
    g_windows[wi].hwnd      = hwnd;
    g_windows[wi].class_idx = ci;
    g_windows[wi].used      = 1;

    serial_puts("USER: sending WM_CREATE\n");
    SendMessage(hwnd, WM_CREATE, 0, 0L);
    serial_puts("USER: CreateWindow done\n");

    return hwnd;
}

/* ============================================================
 * LibMain - wymagany przez wlink system windows_dll
 * ============================================================ */
int __far __pascal LibMain(unsigned hInst, unsigned wData,
                            unsigned cbHeap, const char __far *cmd)
{
    (void)hInst; (void)wData; (void)cbHeap; (void)cmd;
    return 1;
}
