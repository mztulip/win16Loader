/*
 * user.c - USER.EXE (STEP16)
 *
 * Nowe vs STEP15:
 *   - TranslateMessage (USER.113): generuje WM_CHAR z WM_KEYDOWN dla klawiszy ASCII
 *   - g_active_hwnd: globalna zmienna uzywana przez IRQ1 handler w pm_call.asm
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

/* Odczyt SP: uzywane do debugowania glebi stosu */
unsigned get_sp(void);
#pragma aux get_sp = "mov ax, sp" value [ax] modify [ax];

/* ============================================================
 * VESA LFB: 640x480x24bpp, pitch=1920
 * Huge selectors: SEL_VESA_BASE=0xA0, kazde okno = 64KB
 * ============================================================ */
#define USR_MK_FP_VESA(seg, off) \
    ((unsigned char __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

#define SEL_VESA_BASE  0xA0u
#define VESA_PITCH     1920u
#define VESA_BPP       3u

static void vesa_fill_rect(int x0, int y0, int w, int h,
                            unsigned char r, unsigned char g, unsigned char b)
{
    int row, col;
    for (row = 0; row < h; row++) {
        unsigned long base = (unsigned long)(y0 + row) * VESA_PITCH
                           + (unsigned long)x0 * VESA_BPP;
        for (col = 0; col < w; col++) {
            unsigned long off  = base + (unsigned long)col * VESA_BPP;
            unsigned      win  = (unsigned)(off >> 16);
            unsigned      o    = (unsigned)(off & 0xFFFFUL);
            unsigned      sel  = SEL_VESA_BASE + win * 8u;
            unsigned char __far *p = USR_MK_FP_VESA(sel, o);
            p[0] = b; p[1] = g; p[2] = r;
        }
    }
}

/* DS switch */
unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];
void set_ds(unsigned sel);
#pragma aux set_ds = "mov ds, ax" parm [ax] modify [];
void do_sti(void);
#pragma aux do_sti = "sti" modify [];
void do_cli(void);
#pragma aux do_cli = "cli" modify [];

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
#define WM_KEYDOWN  0x0100
#define WM_KEYUP    0x0101
#define WM_CHAR     0x0102

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
#define KCB_RSC_DATA_OFF  32  /* offset surowych danych RT_STRING w KCB (po tick_ms) */

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
    unsigned long  tick_ms;              /* 28: licznik ms (inkrementowany przez IRQ0) */
    /* bajty 32..255: surowe dane RT_STRING */
} KCB_USR;
#pragma pack(pop)

/* ============================================================
 * Kodowanie origin okna w HDC (16-bit):
 *   HDC = 1          -> screen DC, origin (0,0)
 *   HDC = 2..15      -> memory DC (CreateCompatibleDC), origin (0,0)
 *   HDC bit14=1      -> HWND-based DC (dynamiczny):
 *                        HDC = 0x4000 | hwnd
 *                        origin czytany z KCB przy kazdym uzyciu
 *   HDC bit15=1      -> (legacy) pozycja zakodowana statycznie
 *
 * Tablica pozycji okien w KCB (offset 208..255):
 *   208..223: short wnd_ox[8]  (abs x indeksowane hwnd-1)
 *   224..239: short wnd_oy[8]  (abs y indeksowane hwnd-1)
 *   240..255: short wnd_w[8]   (szerokosc child window; 0 = brak/root)
 *   256..271: short wnd_h[8]   (wysokosc child window; 0 = brak/root)
 *
 * gdi.c dekoduje origin z HDC w TextOut/PatBlt; uzywa wnd_w/wnd_h do klipowania.
 * user.c dekoduje origin z HDC w FillRect/FrameRect.
 * ============================================================ */
#define KCB_WND_OX_OFF  208
#define KCB_WND_OY_OFF  224
#define KCB_WND_W_OFF   240   /* short wnd_w[8]: szerokosc child window (0=root) */
#define KCB_WND_H_OFF   256   /* short wnd_h[8]: wysokosc child window (0=root) */

/* Keyboard ring buffer w KCB (zapisywany przez IRQ1 handler w pm_call.asm) */
#define KCB_KB_HEAD  272   /* BYTE: indeks odczytu */
#define KCB_KB_TAIL  273   /* BYTE: indeks zapisu */
#define KCB_KB_BUF   274   /* BYTE[8]: cykliczny bufor VK */
#define KCB_KB_SZ    8

static HWND g_kb_hwnd = 0;  /* HWND aktywnego okna (ustawiany przez CreateWindow) */

/* Zwraca VK code (0 jesli bufor pusty) */
static unsigned char kb_dequeue(void)
{
    unsigned char __far *kcb = KCB_MK_FP(0);
    unsigned char head = kcb[KCB_KB_HEAD];
    unsigned char tail = kcb[KCB_KB_TAIL];
    if (head == tail) return 0;   /* bufor pusty */
    kcb[KCB_KB_HEAD] = (unsigned char)((head + 1u) % KCB_KB_SZ);
    return kcb[KCB_KB_BUF + head];
}

static HDC make_hwnd_dc(unsigned hwnd)
{
    return (HDC)(0x4000u | (hwnd & 0x3FFFu));
}

static void kcb_set_wnd_pos(unsigned hwnd, int ox, int oy)
{
    short __far *kox = (short __far *)KCB_MK_FP(KCB_WND_OX_OFF + (hwnd - 1u) * 2u);
    short __far *koy = (short __far *)KCB_MK_FP(KCB_WND_OY_OFF + (hwnd - 1u) * 2u);
    *kox = (short)ox;
    *koy = (short)oy;
}

static void kcb_set_wnd_w(unsigned hwnd, unsigned ww)
{
    short __far *kw = (short __far *)KCB_MK_FP(KCB_WND_W_OFF + (hwnd - 1u) * 2u);
    *kw = (short)ww;
}

static void kcb_set_wnd_h(unsigned hwnd, unsigned wh)
{
    short __far *kh = (short __far *)KCB_MK_FP(KCB_WND_H_OFF + (hwnd - 1u) * 2u);
    *kh = (short)wh;
}

static void decode_hwnd_dc(unsigned hwnd, int *ox, int *oy)
{
    short __far *kox = (short __far *)KCB_MK_FP(KCB_WND_OX_OFF + (hwnd - 1u) * 2u);
    short __far *koy = (short __far *)KCB_MK_FP(KCB_WND_OY_OFF + (hwnd - 1u) * 2u);
    *ox = *kox;
    *oy = *koy;
}

/* Stary schemat (legacy, zachowany dla zgodnosci) */
static HDC make_dc(int x, int y)
{
    if (x == 0 && y == 0) return (HDC)1;
    return (HDC)(0x8000u | (unsigned)(((unsigned)x / 4u) << 7) | (unsigned)((unsigned)y / 4u));
}

static void decode_dc(HDC hdc, int *ox, int *oy)
{
    if ((unsigned)hdc & 0x4000u) {
        decode_hwnd_dc((unsigned)hdc & 0x3FFFu, ox, oy);
    } else if ((unsigned)hdc & 0x8000u) {
        *ox = (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        *oy = (int)((unsigned)hdc & 0x7Fu) * 4;
    } else {
        *ox = 0; *oy = 0;
    }
}

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
    HWND     parent;
    int      class_idx;
    int      used;
    unsigned w, h;
    int      x, y;   /* pozycja na ekranie (absolute screen coords) */
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

/* GetTickCount czyta KCB->tick_ms; IRQ0 (IDT[0x20], 18.2Hz) inkrementuje go o 55ms/tick */

/* Forward declarations */
LRESULT __far __pascal SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
unsigned long __far __pascal GetTickCount(void);

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
    g_msg_queue[g_msg_tail].time    = GetTickCount();
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
            serial_puts("USER: RegisterClass proc=");
            serial_hex16(((unsigned *)&g_classes[i].proc)[1]);
            serial_putc(':');
            serial_hex16(((unsigned *)&g_classes[i].proc)[0]);
            serial_puts(" inst_ds=");
            serial_hex16(g_classes[i].inst_ds);
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
        unsigned short __far *dp = (unsigned short __far *)((unsigned long)app_ds << 16);
        serial_puts("SM: proc=");
        serial_hex16(pp[1]);
        serial_putc(':');
        serial_hex16(pp[0]);
        serial_puts(" ds=");
        serial_hex16(app_ds);
        serial_puts(" msg=");
        serial_hex16(msg);
        serial_puts(" sp=");
        serial_hex16(get_sp());
        serial_puts(" [0]=");
        serial_hex16(dp[0]);
        serial_puts(" [2]=");
        serial_hex16(dp[1]);
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
    (void)title; (void)style; (void)hMenu; (void)hInst; (void)lpParam;

    ci = find_class(cls);
    if (ci < 0) { serial_puts("USER: class not found\n"); return 0; }

    for (wi = 0; wi < MAX_WINDOWS; wi++)
        if (!g_windows[wi].used) break;
    if (wi >= MAX_WINDOWS) return 0;

    hwnd = g_next_hwnd++;
    g_windows[wi].hwnd      = hwnd;
    g_windows[wi].parent    = parent;
    g_windows[wi].class_idx = ci;
    g_windows[wi].w         = (unsigned)(w > 0 ? w : 640);
    g_windows[wi].h         = (unsigned)(h > 0 ? h : 480);
    g_windows[wi].x         = x;
    g_windows[wi].y         = y;
    g_windows[wi].used      = 1;

    /* Dla okien potomnych: x,y sa wzgledem client area rodzica.
     * Zawsze dodajemy abs pozycje rodzica (nawet jesli top-level). */
    if (parent != 0) {
        int pi;
        for (pi = 0; pi < MAX_WINDOWS; pi++) {
            if (g_windows[pi].used && g_windows[pi].hwnd == parent) {
                g_windows[wi].x += g_windows[pi].x;
                g_windows[wi].y += g_windows[pi].y;
                break;
            }
        }
    }

    /* Zapisz pozycje i rozmiar okna do KCB (dla GDI - HWND-based DC i klipowanie) */
    if (parent != 0) {
        kcb_set_wnd_pos((unsigned)hwnd, g_windows[wi].x, g_windows[wi].y);
        kcb_set_wnd_w((unsigned)hwnd, g_windows[wi].w);
        kcb_set_wnd_h((unsigned)hwnd, g_windows[wi].h);
    }

    serial_puts("USER: CW raw_x="); serial_hex16((unsigned short)(short)x);
    serial_puts(" raw_y=");          serial_hex16((unsigned short)(short)y);
    serial_puts(" w=");              serial_hex16((unsigned short)(short)w);
    serial_puts(" h=");              serial_hex16((unsigned short)(short)h);
    serial_puts(" parent=");         serial_hex16((unsigned short)parent);
    serial_puts(" hwnd=");           serial_hex16((unsigned short)hwnd);
    serial_puts(" abs_x=");          serial_hex16((unsigned short)(short)g_windows[wi].x);
    serial_puts(" abs_y=");          serial_hex16((unsigned short)(short)g_windows[wi].y);
    serial_putc('\n');
    if (parent == 0 && g_kb_hwnd == 0)
        g_kb_hwnd = hwnd;  /* pierwsze top-level okno = aktywne dla klawiatury */

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
    unsigned char vk;
    (void)hwnd; (void)msgMin; (void)msgMax;

    /* Sprawdz bufor klawiatury przed kolejka komunikatow */
    do_sti();
    vk = kb_dequeue();
    do_cli();
    if (vk) {
        pmsg->hwnd    = g_kb_hwnd ? g_kb_hwnd : 1;
        pmsg->message = WM_KEYDOWN;
        pmsg->wParam  = vk;
        pmsg->lParam  = 0L;
        return 1;
    }

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
    unsigned char vk;
    (void)hwnd; (void)msgMin; (void)msgMax;

    /* Najpierw sprawdz bufor klawiatury (IRQ1 zapisuje VK do KCB) */
    do_sti();
    vk = kb_dequeue();
    do_cli();
    if (vk) {
        pmsg->hwnd    = g_kb_hwnd ? g_kb_hwnd : 1;
        pmsg->message = WM_KEYDOWN;
        pmsg->wParam  = vk;
        pmsg->lParam  = 0L;
        return 1;
    }

    /* Sprawdz kolejke komunikatow */
    if (g_msg_count > 0) {
        *pmsg = g_msg_queue[g_msg_head];
        if (wRemoveMsg) {
            g_msg_head = (g_msg_head + 1) % MSG_QUEUE_SIZE;
            g_msg_count--;
        }
        return 1;
    }

    /* Kolejka pusta i brak klawiszy: czekaj az IRQ0 zmieni tick_ms.
     * STI/CLI potrzebne: thunk INT 3F to interrupt gate (IF=0 przy wejsciu).
     * IRQ0 bezpieczny: PIC przemapowany na INT 0x20. */
    {
        unsigned long __far *tick_p =
            (unsigned long __far *)KCB_MK_FP(28);
        unsigned long t0 = *tick_p;
        do_sti();
        while (*tick_p == t0) {}   /* czekaj na jeden tik IRQ0 (~55ms) */
        do_cli();
    }
    return 0;
}

/* ============================================================
 * ordinal 113: TranslateMessage
 * ordinal 114: DispatchMessage
 * ordinal 107: DefWindowProc
 * ============================================================ */
BOOL __far __pascal TranslateMessage(const MSG __far *pmsg)
{
    /* Generuj WM_CHAR dla klawiszy ASCII (Enter, Space) */
    if (pmsg->message == WM_KEYDOWN) {
        WPARAM vk = pmsg->wParam;
        if (vk == 0x0D || vk == 0x20) {  /* VK_RETURN, VK_SPACE */
            push_msg(pmsg->hwnd, WM_CHAR, vk, 0L);
            return 1;
        }
    }
    return 0;
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
    int i;
    (void)nCmdShow;
    /* Dla okien potomnych nie wysylamy WM_SIZE/WM_ACTIVATE - moglby
     * skasowac [0xb3e] ustawiony przez ShowWindow glownego okna. */
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            if (g_windows[i].parent != 0) return 1;  /* child: brak */
            break;
        }
    }
    /* Wypelnij ekran bialym (tlo sniegu) przed pierwszym WM_PAINT */
    vesa_fill_rect(0, 0, 640, 480, 0xFF, 0xFF, 0xFF);
    /* WM_SIZE(wParam=0=SIZE_RESTORED) -> SKI: [0xb68]=0
     * WM_ACTIVATE(wParam=1=WA_ACTIVE) -> SKI: [0xae2]=1, [0xb1e]=1 -> 0x35C2 -> [0xb3e]=1
     * Po WM_ACTIVATE game loop ma obie flagi != 0 i zaczyna wykonywac game_tick(). */
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
    int i;
    HDC hdc = 1;
    ps->fErase = 0;
    ps->rcPaint[0] = 0;    /* left */
    ps->rcPaint[1] = 0;    /* top */
    ps->rcPaint[2] = 640;  /* right */
    ps->rcPaint[3] = 480;  /* bottom */
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            /* Okna potomne dostaja HWND-based DC (dynamiczny - origin z KCB) */
            if (g_windows[i].parent != 0) {
                hdc = make_hwnd_dc((unsigned)hwnd);
                /* rcPaint = client area okna (wspolrzedne wzgledem origin okna) */
                ps->rcPaint[2] = (unsigned)g_windows[i].w;   /* right */
                ps->rcPaint[3] = (unsigned)g_windows[i].h;   /* bottom */
            }
            break;
        }
    }
    ps->hdc = hdc;
    return hdc;
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
    int i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            /* Okna potomne dostaja HWND-based DC (dynamiczny - origin z KCB) */
            if (g_windows[i].parent != 0)
                return make_hwnd_dc((unsigned)hwnd);
            return 1;
        }
    }
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
    /* Czyta KCB->tick_ms (offset 28) inkrementowane przez IRQ0 (~55ms/tick przy 18.2Hz). */
    unsigned long __far *p = (unsigned long __far *)KCB_MK_FP(28);
    unsigned long val = *p;
    return val;
}

/* ============================================================
 * ordinal 33: GetClientRect
 * ============================================================ */
BOOL __far __pascal GetClientRect(HWND hwnd, RECT __far *lpRect)
{
    int i, j;
    lpRect->left = 0;
    lpRect->top  = 0;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            lpRect->right  = (int)g_windows[i].w;
            lpRect->bottom = (int)g_windows[i].h;
            /* Okno glowne: przytnij do wysokosci fizycznego ekranu */
            if (g_windows[i].parent == 0 && lpRect->bottom > 480)
                lpRect->bottom = 480;
            /* Dla okien potomnych: przytnij do granic klienta rodzica */
            if (g_windows[i].parent != 0) {
                for (j = 0; j < MAX_WINDOWS; j++) {
                    if (g_windows[j].used && g_windows[j].hwnd == g_windows[i].parent) {
                        int parent_r = g_windows[j].x + (int)g_windows[j].w - g_windows[i].x;
                        int parent_b = g_windows[j].y + (int)g_windows[j].h - g_windows[i].y;
                        if (lpRect->right  > parent_r) lpRect->right  = parent_r;
                        if (lpRect->bottom > parent_b) lpRect->bottom = parent_b;
                        break;
                    }
                }
            }
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
    int i;
    (void)repaint;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            /* Dla okien potomnych x,y sa wzgledem client area rodzica.
             * Zawsze dodajemy abs pozycje rodzica. */
            if (g_windows[i].parent != 0) {
                int pi;
                for (pi = 0; pi < MAX_WINDOWS; pi++) {
                    if (g_windows[pi].used && g_windows[pi].hwnd == g_windows[i].parent) {
                        x += g_windows[pi].x;
                        y += g_windows[pi].y;
                        break;
                    }
                }
            }
            g_windows[i].x = x;
            g_windows[i].y = y;
            if (w > 0) g_windows[i].w = (unsigned)w;
            if (h > 0) g_windows[i].h = (unsigned)h;
            /* Zaktualizuj tablice pozycji i rozmiaru w KCB (dla GDI) */
            if (g_windows[i].parent != 0) {
                kcb_set_wnd_pos((unsigned)hwnd, x, y);
                kcb_set_wnd_w((unsigned)hwnd, g_windows[i].w);
                kcb_set_wnd_h((unsigned)hwnd, g_windows[i].h);
            }
            serial_puts("USER: MoveWindow hwnd="); serial_hex16(hwnd);
            serial_puts(" abs_x="); serial_hex16((unsigned short)(short)g_windows[i].x);
            serial_puts(" abs_y="); serial_hex16((unsigned short)(short)g_windows[i].y);
            serial_puts(" w=");     serial_hex16((unsigned short)g_windows[i].w);
            serial_puts(" h=");     serial_hex16((unsigned short)g_windows[i].h);
            serial_putc('\n');
            break;
        }
    }
    return 1;
}

BOOL __far __pascal FillRect(HDC hdc, const RECT __far *lpRect, unsigned hBrush)
{
    unsigned char r = 0xFF, g = 0xFF, b = 0xFF;   /* domyslnie bialy */
    int x, y, w, h, ox = 0, oy = 0;
    /* Rysuj tylko dla screen DC (1), window DC (bit15=1) lub HWND DC (bit14=1) */
    if (hdc != 1 && !((unsigned)hdc & 0xC000u)) return 1;
    /* BLACK_BRUSH=0x8004, NULL_BRUSH/NULL_PEN=0x8005 -> pomijamy */
    if (hBrush == 0x8004u) { r = 0; g = 0; b = 0; }
    else if (hBrush == 0x8005u) return 1;
    /* Inline decode (DS!=SS w DLL, near ptr na stos nie dziala) */
    if ((unsigned)hdc & 0x4000u) {
        unsigned hwnd_v = (unsigned)hdc & 0x3FFFu;
        short __far *kox = (short __far *)KCB_MK_FP(KCB_WND_OX_OFF + (hwnd_v-1u)*2u);
        short __far *koy = (short __far *)KCB_MK_FP(KCB_WND_OY_OFF + (hwnd_v-1u)*2u);
        ox = *kox;
        oy = *koy;
    } else if ((unsigned)hdc & 0x8000u) {
        ox = (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        oy = (int)((unsigned)hdc & 0x7Fu) * 4;
    }
    x = lpRect->left  + ox;
    y = lpRect->top   + oy;
    w = lpRect->right  - lpRect->left;
    h = lpRect->bottom - lpRect->top;
    if (w <= 0 || h <= 0) return 1;
    vesa_fill_rect(x, y, w, h, r, g, b);
    return 1;
}

BOOL __far __pascal FrameRect(HDC hdc, const RECT __far *lpRect, unsigned hBrush)
{
    unsigned char r = 0, g = 0, b = 0;   /* domyslnie czarny */
    int x, y, w, h, ox = 0, oy = 0;
    int wnd_w = 640, wnd_h = 480;
    if (hdc != 1 && !((unsigned)hdc & 0xC000u)) return 1;
    if (hBrush == 0x8005u) return 1;  /* NULL_BRUSH */
    if (hBrush == 0x8000u) { r = 0xFF; g = 0xFF; b = 0xFF; }  /* WHITE_BRUSH */
    if ((unsigned)hdc & 0x4000u) {
        /* HWND-based DC: czytaj pozycje z KCB (inline - DS!=SS w DLL) */
        unsigned hwnd_v = (unsigned)hdc & 0x3FFFu;
        short __far *kox = (short __far *)KCB_MK_FP(KCB_WND_OX_OFF + (hwnd_v-1u)*2u);
        short __far *koy = (short __far *)KCB_MK_FP(KCB_WND_OY_OFF + (hwnd_v-1u)*2u);
        int i;
        ox = *kox;
        oy = *koy;
        /* Rozmiar okna z g_windows (nie ma w KCB) */
        for (i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].used && (unsigned)g_windows[i].hwnd == hwnd_v) {
                wnd_w = (int)g_windows[i].w;
                wnd_h = (int)g_windows[i].h;
                break;
            }
        }
    } else if ((unsigned)hdc & 0x8000u) {
        int i;
        ox = (int)(((unsigned)hdc >> 7) & 0xFFu) * 4;
        oy = (int)((unsigned)hdc & 0x7Fu) * 4;
        /* Znajdz okno po pasujacych wspolrzednych */
        for (i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].used && g_windows[i].parent != 0 &&
                g_windows[i].x == ox && g_windows[i].y == oy) {
                wnd_w = (int)g_windows[i].w;
                wnd_h = (int)g_windows[i].h;
                break;
            }
        }
    }
    x = lpRect->left  + ox;
    y = lpRect->top   + oy;
    w = lpRect->right  - lpRect->left;
    h = lpRect->bottom - lpRect->top;
    if ((unsigned)hdc & 0xC000u) {
        /* Przytnij do granic okna potomnego */
        int x2 = x + w, y2 = y + h;
        if (x  < ox)          x  = ox;
        if (y  < oy)          y  = oy;
        if (x2 > ox + wnd_w)  x2 = ox + wnd_w;
        if (y2 > oy + wnd_h)  y2 = oy + wnd_h;
        w = x2 - x;
        h = y2 - y;
    }
    if (w <= 0 || h <= 0) return 1;
    vesa_fill_rect(x,         y,         w, 1, r, g, b);  /* top */
    vesa_fill_rect(x,         y + h - 1, w, 1, r, g, b);  /* bottom */
    vesa_fill_rect(x,         y,         1, h, r, g, b);  /* left */
    vesa_fill_rect(x + w - 1, y,         1, h, r, g, b);  /* right */
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
    /* MAKEINTRESOURCE(n): far ptr z segment=0, offset=n. Wyciagamy 16-bit offset = ID. */
    unsigned id = (unsigned)((unsigned long)lpBitmapName & 0xFFFFUL);
    (void)hInstance;
    /* HBITMAP = id (1..86); gdi.c uzywa tego do indeksowania bufora SEL_BITMAPS. */
    return id;
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

    block_id     = uID / 16 + 1;
    str_in_block = uID % 16;

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
    unsigned i;
    (void)hInst; (void)wData; (void)cbHeap; (void)cmd;
    /* Zero-init wnd_w[8] i wnd_h[8] - KCB nie jest zerowane przez DOS */
    for (i = 0; i < 8u; i++) {
        short __far *kw = (short __far *)KCB_MK_FP(KCB_WND_W_OFF + i * 2u);
        short __far *kh = (short __far *)KCB_MK_FP(KCB_WND_H_OFF + i * 2u);
        *kw = 0;
        *kh = 0;
    }
    return 1;
}
