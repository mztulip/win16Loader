/*
 * user.c - USER.EXE (STEP11)
 *
 * Eksporty pod prawdziwymi numerami ordynalow Windows 3.1.
 * Funkcje wymagane przez SKI.EXE (i win16app.c):
 *
 *   1   = MessageBox         (stub: zwroc IDOK)
 *   5   = InitApp            (stub: return 1)
 *   6   = PostQuitMessage
 *   13  = GetTickCount       (monotoniczny licznik ms)
 *   22  = SetFocus           (stub)
 *   28  = PostMessage
 *   31  = IsIconic           (stub: return 0)
 *   33  = GetClientRect      (return 640x480)
 *   37  = SetWindowText      (stub)
 *   39  = BeginPaint
 *   40  = EndPaint
 *   41  = CreateWindow
 *   42  = ShowWindow
 *   44  = OpenIcon           (stub)
 *   50  = FindWindow         (stub: return 0)
 *   53  = DestroyWindow      (stub)
 *   56  = MoveWindow         (stub)
 *   57  = RegisterClass
 *   66  = GetDC
 *   68  = ReleaseDC
 *   81  = FillRect           (stub)
 *   83  = FrameRect          (stub)
 *   107 = DefWindowProc
 *   108 = GetMessage
 *   109 = PeekMessage
 *   113 = TranslateMessage
 *   114 = DispatchMessage
 *   124 = UpdateWindow
 *   125 = InvalidateRect
 *   173 = LoadCursor         (stub: return 1)
 *   174 = LoadIcon           (stub: return 1)
 *   175 = LoadBitmap         (fake HBITMAP = 0x100|id, non-zero by ETAP 14)
 *   176 = LoadString         (stub: return 0)
 *   232 = SetWindowPos       (stub)
 *   420 = Wsprintf           (stub)
 *   500 = LibMain
 */

#include <stdarg.h>

/* Port I/O */
unsigned char io_inb(unsigned port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(unsigned port, unsigned char val);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

/* DS switch */
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

static void serial_hex16(unsigned v)
{
    int i;
    for (i = 12; i >= 0; i -= 4) {
        unsigned n = (v >> i) & 0xF;
        serial_putc(n < 10 ? '0' + n : 'A' + n - 10);
    }
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
typedef unsigned short HDC;

typedef LRESULT (__far __pascal *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#define WM_CREATE   0x0001
#define WM_DESTROY  0x0002
#define WM_SIZE     0x0005
#define WM_ACTIVATE 0x0006
#define WM_PAINT    0x000F
#define WM_QUIT     0x0012

typedef struct {
    unsigned          style;
    WNDPROC           lpfnWndProc;
    int               cbClsExtra;
    int               cbWndExtra;
    unsigned          hInstance;
    unsigned          hIcon;
    unsigned          hCursor;
    unsigned          hbrBackground;
    const char __far *lpszMenuName;
    const char __far *lpszClassName;
} WNDCLASS;

typedef struct {
    HWND          hwnd;
    UINT          message;
    WPARAM        wParam;
    LPARAM        lParam;
    unsigned long time;
    int           x, y;
} MSG;

typedef struct {
    unsigned hdc;
    unsigned fErase;
    unsigned rcPaint[4];
    unsigned fRestore;
    unsigned fIncUpdate;
} PAINTSTRUCT;

typedef struct {
    int left, top, right, bottom;
} RECT;

/* ============================================================
 * KCB - Kernel Control Block (SEL_KCB=0x98, 256 bajtow)
 * ============================================================ */
#define SEL_KCB      ((unsigned short)0x98)
#define KCB_RSC_DATA_OFF 28   /* offset surowych danych RT_STRING w KCB */

#define KCB_MK_FP(off) \
    ((unsigned char __far *)(((unsigned long)(SEL_KCB) << 16) | (unsigned short)(off)))

#pragma pack(push, 1)
typedef struct {
    unsigned short app_hinstance;
    unsigned short next_dyn_sel;
    unsigned long  heap_phys;
    unsigned long  heap_next;
    unsigned long  heap_end;
    unsigned short local_heap_off;
    unsigned char  rsc_nblocks;
    unsigned char  rsc_pad;
    unsigned short rsc_block_ids[2];
    unsigned short rsc_block_sizes[2];
    /* bajty 28..255: surowe dane RT_STRING */
} KCB_USR;
#pragma pack(pop)

/* ============================================================
 * Wewnetrzne tablice klas i okien
 * ============================================================ */
#define MAX_CLASSES 8
#define MAX_WINDOWS 8

static struct {
    char    name[32];
    WNDPROC proc;
    unsigned inst_ds;
    int     used;
} g_classes[MAX_CLASSES];

static struct {
    HWND     hwnd;
    int      class_idx;
    int      used;
    unsigned w, h;
} g_windows[MAX_WINDOWS];

static unsigned g_next_hwnd = 1;

/* ============================================================
 * Kolejka komunikatow (ring buffer, 64 slotow)
 * ============================================================ */
#define MSG_QUEUE_SIZE 64

static MSG      g_msg_queue[MSG_QUEUE_SIZE];
static unsigned g_msg_head  = 0;
static unsigned g_msg_tail  = 0;
static unsigned g_msg_count = 0;

/* Monotoniczny licznik ms (inkrementowany w PeekMessage) */
static unsigned long g_tick_ms = 0;

/* Forward declaration */
LRESULT __far __pascal SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ============================================================
 * Pomocnicze
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
    for (i = 0; i < MAX_CLASSES; i++)
        if (g_classes[i].used && far_strcmp(g_classes[i].name, name) == 0)
            return i;
    return -1;
}

static void push_msg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msg_count >= MSG_QUEUE_SIZE) return;
    g_msg_queue[g_msg_tail].hwnd    = hwnd;
    g_msg_queue[g_msg_tail].message = msg;
    g_msg_queue[g_msg_tail].wParam  = wp;
    g_msg_queue[g_msg_tail].lParam  = lp;
    g_msg_queue[g_msg_tail].time    = g_tick_ms;
    g_msg_queue[g_msg_tail].x       = 0;
    g_msg_queue[g_msg_tail].y       = 0;
    g_msg_tail = (g_msg_tail + 1) % MSG_QUEUE_SIZE;
    g_msg_count++;
}

/* ============================================================
 * ordinal 57: RegisterClass
 * ============================================================ */
BOOL __far __pascal RegisterClass(const WNDCLASS __far *wc)
{
    int i;
    for (i = 0; i < MAX_CLASSES; i++) {
        if (!g_classes[i].used) {
            far_strncpy(g_classes[i].name, wc->lpszClassName, 32);
            g_classes[i].proc    = wc->lpfnWndProc;
            g_classes[i].inst_ds = wc->hInstance;
            g_classes[i].used    = 1;
            /* Fix: 0xFFFF jako segment far pointera = nierozwiazany marker movable
             * segmentu Win16 (brakujacy SEL fixup w SKI.EXE). Zastepujemy SEL_APP_CODE. */
            if (((unsigned *)&g_classes[i].proc)[1] == 0xFFFF)
                ((unsigned *)&g_classes[i].proc)[1] = 0x0030;
            serial_puts("USER: RegisterClass OK proc=");
            serial_hex16(((unsigned *)&g_classes[i].proc)[1]);
            serial_putc(':');
            serial_hex16(((unsigned *)&g_classes[i].proc)[0]);
            serial_putc('\n');
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 * SendMessage - synchroniczne wywolanie WndProc
 * ============================================================ */
LRESULT __far __pascal SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC  proc   = 0;
    unsigned app_ds = 0;
    unsigned save_ds;
    LRESULT  result = 0;
    int i;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            int ci = g_windows[i].class_idx;
            proc   = g_classes[ci].proc;
            app_ds = g_classes[ci].inst_ds;
            break;
        }
    }
    if (!proc) return 0;

    save_ds = get_ds();
    {
        unsigned *pp = (unsigned *)&proc;
        serial_puts("SM: proc=");
        serial_hex16(pp[1]);
        serial_putc(':');
        serial_hex16(pp[0]);
        serial_puts(" msg=");
        serial_hex16(msg);
        serial_putc('\n');
    }
    set_ds(app_ds);
    result = proc(hwnd, msg, wp, lp);
    set_ds(save_ds);
    return result;
}

/* ============================================================
 * ordinal 41: CreateWindow
 * ============================================================ */
HWND __far __pascal CreateWindow(
    const char __far *cls, const char __far *title,
    unsigned long style,
    int x, int y, int w, int h,
    HWND parent, unsigned hMenu, unsigned hInst, void __far *lpParam)
{
    HWND hwnd;
    int  ci, wi;
    (void)title; (void)style; (void)x; (void)y;
    (void)parent; (void)hMenu; (void)hInst; (void)lpParam;

    ci = find_class(cls);
    if (ci < 0) { serial_puts("USER: class not found\n"); return 0; }

    for (wi = 0; wi < MAX_WINDOWS; wi++)
        if (!g_windows[wi].used) break;
    if (wi >= MAX_WINDOWS) return 0;

    hwnd = g_next_hwnd++;
    g_windows[wi].hwnd      = hwnd;
    g_windows[wi].class_idx = ci;
    g_windows[wi].w         = (unsigned)(w > 0 ? w : 640);
    g_windows[wi].h         = (unsigned)(h > 0 ? h : 480);
    g_windows[wi].used      = 1;

    serial_puts("USER: CreateWindow -> WM_CREATE\n");
    SendMessage(hwnd, WM_CREATE, 0, 0L);
    return hwnd;
}

/* ============================================================
 * ordinal 28: PostMessage
 * ordinal 6:  PostQuitMessage
 * ============================================================ */
BOOL __far __pascal PostMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    push_msg(hwnd, msg, wp, lp);
    return 1;
}

void __far __pascal PostQuitMessage(int exitCode)
{
    serial_puts("USER: PostQuitMessage\n");
    push_msg(0, WM_QUIT, (WPARAM)exitCode, 0L);
}

/* ============================================================
 * ordinal 108: GetMessage
 * ============================================================ */
BOOL __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                UINT msgMin, UINT msgMax)
{
    (void)hwnd; (void)msgMin; (void)msgMax;
    if (g_msg_count == 0) return 1;   /* kolejka pusta: brak WM_QUIT */
    *pmsg = g_msg_queue[g_msg_head];
    g_msg_head = (g_msg_head + 1) % MSG_QUEUE_SIZE;
    g_msg_count--;
    return (pmsg->message != WM_QUIT) ? 1 : 0;
}

/* ============================================================
 * ordinal 109: PeekMessage (nieblokujacy - game loop SKI)
 * ============================================================ */
BOOL __far __pascal PeekMessage(MSG __far *pmsg, HWND hwnd,
                                 UINT msgMin, UINT msgMax, UINT wRemoveMsg)
{
    (void)hwnd; (void)msgMin; (void)msgMax;
    g_tick_ms += 55;   /* ~18.2 Hz: 55ms per tick */
    if (g_msg_count == 0) return 0;
    *pmsg = g_msg_queue[g_msg_head];
    if (wRemoveMsg) {
        g_msg_head = (g_msg_head + 1) % MSG_QUEUE_SIZE;
        g_msg_count--;
    }
    return 1;
}

/* ============================================================
 * ordinal 113: TranslateMessage
 * ordinal 114: DispatchMessage
 * ordinal 107: DefWindowProc
 * ============================================================ */
BOOL __far __pascal TranslateMessage(const MSG __far *pmsg)
{
    (void)pmsg;
    return 1;
}

LRESULT __far __pascal DispatchMessage(const MSG __far *pmsg)
{
    return SendMessage(pmsg->hwnd, pmsg->message, pmsg->wParam, pmsg->lParam);
}

LRESULT __far __pascal DefWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}

/* ============================================================
 * ordinal 42: ShowWindow
 * ordinal 124: UpdateWindow
 * ordinal 125: InvalidateRect
 * ============================================================ */
BOOL __far __pascal ShowWindow(HWND hwnd, int nCmdShow)
{
    (void)nCmdShow;
    /* Dispatchujemy WM_SIZE i WM_ACTIVATE do WndProca apki.
     * WM_SIZE(wParam=0=SIZE_RESTORED) -> SKI: [0xb68]=0
     * WM_ACTIVATE(wParam=1=WA_ACTIVE) -> SKI: [0xae2]=1
     * Dopiero po obu flagach [0xb1e]=1 i petla gry zaczyna renderowac. */
    SendMessage(hwnd, WM_SIZE,     0, ((unsigned long)480 << 16) | 640UL);
    SendMessage(hwnd, WM_ACTIVATE, 1, 0L);
    return 1;
}

BOOL __far __pascal UpdateWindow(HWND hwnd)
{
    SendMessage(hwnd, WM_PAINT, 0, 0L);
    return 1;
}

BOOL __far __pascal InvalidateRect(HWND hwnd, const RECT __far *lpRect, BOOL bErase)
{
    (void)lpRect; (void)bErase;
    push_msg(hwnd, WM_PAINT, 0, 0L);
    return 1;
}

/* ============================================================
 * ordinal 39: BeginPaint
 * ordinal 40: EndPaint
 * ============================================================ */
HDC __far __pascal BeginPaint(HWND hwnd, PAINTSTRUCT __far *ps)
{
    (void)hwnd;
    ps->hdc    = 1;
    ps->fErase = 0;
    return 1;
}

BOOL __far __pascal EndPaint(HWND hwnd, const PAINTSTRUCT __far *ps)
{
    (void)hwnd; (void)ps;
    return 1;
}

/* ============================================================
 * ordinal 66: GetDC
 * ordinal 68: ReleaseDC
 * (w Win16 SDK: GetDC/ReleaseDC sa w USER, nie GDI)
 * ============================================================ */
HDC __far __pascal GetDC(HWND hwnd)
{
    (void)hwnd;
    return 1;
}

int __far __pascal ReleaseDC(HWND hwnd, HDC hdc)
{
    (void)hwnd; (void)hdc;
    return 1;
}

/* ============================================================
 * ordinal 13: GetTickCount
 * ============================================================ */
unsigned long __far __pascal GetTickCount(void)
{
    return g_tick_ms;
}

/* ============================================================
 * ordinal 33: GetClientRect
 * ============================================================ */
BOOL __far __pascal GetClientRect(HWND hwnd, RECT __far *lpRect)
{
    int i;
    lpRect->left = 0;
    lpRect->top  = 0;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            lpRect->right  = (int)g_windows[i].w;
            lpRect->bottom = (int)g_windows[i].h;
            return 1;
        }
    }
    lpRect->right  = 640;
    lpRect->bottom = 480;
    return 1;
}

/* ============================================================
 * Stubs
 * ============================================================ */
int __far __pascal MessageBox(HWND hwnd, const char __far *text,
                               const char __far *caption, unsigned uType)
{
    (void)hwnd; (void)text; (void)caption; (void)uType;
    serial_puts("USER: MessageBox\n");
    return 1;   /* IDOK */
}

int __far __pascal InitApp(unsigned hInstance)
{
    (void)hInstance;
    return 1;
}

int __far __pascal SetFocus(HWND hwnd)
{
    (void)hwnd;
    return 0;
}

BOOL __far __pascal IsIconic(HWND hwnd)
{
    (void)hwnd;
    return 0;
}

BOOL __far __pascal SetWindowText(HWND hwnd, const char __far *s)
{
    (void)hwnd; (void)s;
    return 1;
}

HWND __far __pascal OpenIcon(HWND hwnd)
{
    (void)hwnd;
    return 0;
}

HWND __far __pascal FindWindow(const char __far *cls, const char __far *title)
{
    (void)cls; (void)title;
    return 0;
}

BOOL __far __pascal DestroyWindow(HWND hwnd)
{
    int i;
    for (i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].hwnd == hwnd) { g_windows[i].used = 0; break; }
    return 1;
}

BOOL __far __pascal MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint)
{
    (void)hwnd; (void)x; (void)y; (void)w; (void)h; (void)repaint;
    return 1;
}

BOOL __far __pascal FillRect(HDC hdc, const RECT __far *lpRect, unsigned hBrush)
{
    (void)hdc; (void)lpRect; (void)hBrush;
    return 1;
}

BOOL __far __pascal FrameRect(HDC hdc, const RECT __far *lpRect, unsigned hBrush)
{
    (void)hdc; (void)lpRect; (void)hBrush;
    return 1;
}

unsigned __far __pascal LoadCursor(unsigned hInstance, const char __far *lpCursorName)
{
    (void)hInstance; (void)lpCursorName;
    return 1;
}

unsigned __far __pascal LoadIcon(unsigned hInstance, const char __far *lpIconName)
{
    (void)hInstance; (void)lpIconName;
    return 1;
}

unsigned __far __pascal LoadBitmap(unsigned hInstance, const char __far *lpBitmapName)
{
    /* MAKERESOURCE(id) = (const char*)(id & 0xFFFF): segment=0, offset=id */
    unsigned id = (unsigned)((unsigned long)lpBitmapName & 0x00FFUL);
    (void)hInstance;
    /* Zwracamy fake HBITMAP = 0x100|id, zawsze non-zero.
     * Dzieki temu SKI nie przerywa petli ladowania sprite'ow po pierwszym NULL. */
    return (unsigned)(0x0100U | id);
}

int __far __pascal LoadString(unsigned hInstance, unsigned uID,
                               char __far *lpBuffer, int nBufferMax)
{
    KCB_USR __far *kcb;
    unsigned char __far *kp;
    unsigned block_id, str_in_block;
    unsigned nb, i;
    unsigned data_off;
    unsigned char __far *data;
    unsigned length;

    (void)hInstance;
    if (nBufferMax <= 0 || uID == 0) { if (nBufferMax > 0) lpBuffer[0] = 0; return 0; }

    kcb = (KCB_USR __far *)KCB_MK_FP(0);
    kp  = (unsigned char __far *)KCB_MK_FP(0);
    nb  = kcb->rsc_nblocks;

    block_id     = (uID - 1) / 16 + 1;
    str_in_block = (uID - 1) % 16;

    /* Znajdz blok RT_STRING o danym block_id */
    data_off = KCB_RSC_DATA_OFF;
    for (i = 0; i < nb; i++) {
        if (kcb->rsc_block_ids[i] == (unsigned short)block_id) {
            /* Przejdz przez 16 napisow w bloku */
            unsigned si;
            data = kp + data_off;
            for (si = 0; si < 16; si++) {
                length = *data++;
                if (si == str_in_block) {
                    unsigned n, j;
                    n = (length < (unsigned)(nBufferMax - 1))
                        ? length : (unsigned)(nBufferMax - 1);
                    for (j = 0; j < n; j++) lpBuffer[j] = (char)data[j];
                    lpBuffer[n] = 0;
                    { unsigned v = uID; int k;
                      serial_puts("USR:LoadStr(");
                      for (k=12; k>=0; k-=4) {
                          unsigned char nib=(unsigned char)((v>>k)&0xF);
                          serial_putc(nib<10?'0'+nib:'A'+nib-10);
                      }
                      serial_putc(')');
                      serial_putc('=');
                      { const char __far *fp = lpBuffer; while(*fp) serial_putc(*fp++); }
                      serial_putc('\n'); }
                    return (int)n;
                }
                data += length;
            }
            lpBuffer[0] = 0;
            return 0;
        }
        data_off += kcb->rsc_block_sizes[i];
    }

    lpBuffer[0] = 0;
    return 0;
}

BOOL __far __pascal SetWindowPos(HWND hwnd, HWND hwndInsertAfter,
                                  int x, int y, int cx, int cy, unsigned uFlags)
{
    (void)hwnd; (void)hwndInsertAfter;
    (void)x; (void)y; (void)cx; (void)cy; (void)uFlags;
    return 1;
}

/* ============================================================
 * ordinal 420: Wsprintf
 * Win16 __cdecl varargs. Obsługuje: %d %u %ld %lu %s %c %%
 * Win16 konwencja: %s = LPSTR (far pointer, 4 bajty na stosie).
 * Konwersja decimal bez __U4D: iterowane odejmowanie poteg 10.
 * ============================================================ */

#define USR_MK_FP(seg, off) \
    ((char __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

static const unsigned long wsp_pow10[10] = {
    1000000000UL, 100000000UL, 10000000UL, 1000000UL,
    100000UL,     10000UL,     1000UL,     100UL, 10UL, 1UL
};

static int wsp_u32(unsigned long v, char __far *buf)
{
    int i, len, started, digit;
    len = 0; started = 0;
    for (i = 0; i < 10; i++) {
        digit = 0;
        while (v >= wsp_pow10[i]) { v -= wsp_pow10[i]; digit++; }
        if (digit || started || i == 9) { buf[len++] = '0' + digit; started = 1; }
    }
    return len;
}

int __far __cdecl Wsprintf(char __far *lpOut, const char __far *lpFmt, ...)
{
    va_list ap;
    const char __far *fmt;
    char __far *out;
    char __far *s;
    int count, is_long, n;
    long lval;
    unsigned long uval;
    unsigned soff, sseg;

    fmt = lpFmt; out = lpOut; count = 0;
    va_start(ap, lpFmt);

    /* DEBUG: print '!' to confirm entry, then lpFmt bytes */
    serial_putc('!');
    { unsigned long addr = (unsigned long)lpFmt;
      int i; unsigned char nibble;
      for (i = 28; i >= 0; i -= 4) {
          nibble = (unsigned char)((addr >> i) & 0xF);
          serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
          if (i == 16) serial_putc(':');
      }
      serial_putc('[');
      { const char __far *p = lpFmt; int n = 0;
        while (*p && n < 8) { serial_putc(*p++); n++; } }
      serial_putc(']');
      serial_putc('\n'); }

    {
        /* Zmienne dla parsowania formatu - zadeklarowane raz dla calej petli */
        int flag_left, flag_zero, width, prec, neg, tlen, k, total, pad;
        static char tmp[12];  /* static: w DS, bo SS != DS w DLL (thunk) */
        char pc;

        while (*fmt) {
            if (*fmt != '%') { *out++ = *fmt++; count++; continue; }
            fmt++;
            if (*fmt == '%') { *out++ = '%'; count++; fmt++; continue; }

            /* Parsuj flagi */
            flag_left = 0; flag_zero = 0;
            while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0') {
                if (*fmt == '-') flag_left = 1;
                if (*fmt == '0') flag_zero = 1;
                fmt++;
            }
            /* Parsuj szerokosc pola */
            width = 0;
            while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt - '0'); fmt++; }
            /* Parsuj precyzje */
            prec = -1;
            if (*fmt == '.') {
                fmt++; prec = 0;
                while (*fmt >= '0' && *fmt <= '9') { prec = prec*10 + (*fmt - '0'); fmt++; }
            }
            is_long = 0;
            if (*fmt == 'l') { is_long = 1; fmt++; }

            switch (*fmt++) {
            case 'd': case 'i':
                lval = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
                neg = (lval < 0L) ? 1 : 0;
                uval = neg ? (unsigned long)(-lval) : (unsigned long)lval;
                goto fmt_num;
            case 'u':
                uval = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
                neg = 0;
            fmt_num:
                tlen = wsp_u32(uval, tmp); tmp[tlen] = 0;
                /* Precyzja: minimum cyfr (dopelnij zerami z lewej) */
                if (prec >= 0 && tlen < prec) {
                    pad = prec - tlen;
                    for (k = tlen; k >= 0; k--) tmp[k + pad] = tmp[k];
                    for (k = 0; k < pad; k++) tmp[k] = '0';
                    tlen += pad;
                }
                total = tlen + (neg ? 1 : 0);
                pad   = (width > total) ? width - total : 0;
                pc    = (flag_zero && prec < 0) ? '0' : ' ';
                if (!flag_left) {
                    if (neg && pc == '0') { *out++ = '-'; count++; neg = 0; }
                    for (k = 0; k < pad; k++) { *out++ = pc; count++; }
                }
                if (neg) { *out++ = '-'; count++; }
                for (k = 0; k < tlen; k++) { *out++ = tmp[k]; count++; }
                if (flag_left) { for (k = 0; k < pad; k++) { *out++ = ' '; count++; } }
                break;
            case 's':
                /* Win16: %s = LPSTR = far ptr (4 bajty: off, seg) */
                soff = va_arg(ap, unsigned);
                sseg = va_arg(ap, unsigned);
                s = USR_MK_FP(sseg, soff);
                while (s && *s) { *out++ = *s++; count++; }
                break;
            case 'c':
                *out++ = (char)va_arg(ap, int); count++;
                break;
            default:
                break;
            }
        }
    }
    *out = '\0';
    va_end(ap);
    return count;
}

/* ============================================================
 * ordinal 500: LibMain
 * ============================================================ */
int __far __pascal LibMain(unsigned hInst, unsigned wData,
                            unsigned cbHeap, const char __far *cmd)
{
    (void)hInst; (void)wData; (void)cbHeap; (void)cmd;
    return 1;
}
