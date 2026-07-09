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

/* HLT z IF=1: czeka na nastepne przerwanie (IRQ0 co ~55ms) */
static void do_hlt(void);
#pragma aux do_hlt = "sti" "hlt" "cli" modify [];

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

static void vesa_get_pixel(int x, int y,
                            unsigned char *pr, unsigned char *pg, unsigned char *pb)
{
    unsigned long off = (unsigned long)y * VESA_PITCH + (unsigned long)x * VESA_BPP;
    unsigned win = (unsigned)(off >> 16);
    unsigned o   = (unsigned)(off & 0xFFFFUL);
    unsigned char __far *p = USR_MK_FP_VESA(SEL_VESA_BASE + win * 8u, o);
    *pb = p[0]; *pg = p[1]; *pr = p[2];
}

static void vesa_put_pixel(int x, int y,
                            unsigned char r, unsigned char g, unsigned char b)
{
    unsigned long off = (unsigned long)y * VESA_PITCH + (unsigned long)x * VESA_BPP;
    unsigned win = (unsigned)(off >> 16);
    unsigned o   = (unsigned)(off & 0xFFFFUL);
    unsigned char __far *p = USR_MK_FP_VESA(SEL_VESA_BASE + win * 8u, o);
    p[0] = b; p[1] = g; p[2] = r;
}

/* ============================================================
 * Software mouse cursor — strzalka 12x17 pikseli
 * 0=przezroczysty, 1=czarny, 2=bialy
 * ============================================================ */
#define CURSOR_W 12
#define CURSOR_H 17

static const unsigned char g_cur_shape[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,1,1,1,0,0,0,0},
    {1,2,2,1,2,1,0,0,0,0,0,0},
    {1,2,1,0,1,2,1,0,0,0,0,0},
    {1,1,0,0,0,1,2,1,0,0,0,0},
    {0,0,0,0,0,1,2,1,0,0,0,0},
    {0,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

/* Zapisane tlo pod kursorem (r,g,b per pixel) */
static unsigned char g_cur_bg[CURSOR_H][CURSOR_W][3];
static int g_cur_drawn = 0;
static int g_cur_x = 0, g_cur_y = 0;

static void cursor_erase(void)
{
    int row, col;
    if (!g_cur_drawn) return;
    for (row = 0; row < CURSOR_H; row++) {
        int y = g_cur_y + row;
        if (y < 0 || y >= 480) continue;
        for (col = 0; col < CURSOR_W; col++) {
            int x = g_cur_x + col;
            if (x < 0 || x >= 640) continue;
            if (g_cur_shape[row][col])
                vesa_put_pixel(x, y, g_cur_bg[row][col][0],
                                     g_cur_bg[row][col][1],
                                     g_cur_bg[row][col][2]);
        }
    }
    g_cur_drawn = 0;
}

static void cursor_draw(int cx, int cy)
{
    int row, col;
    g_cur_x = cx; g_cur_y = cy;
    for (row = 0; row < CURSOR_H; row++) {
        int y = cy + row;
        if (y < 0 || y >= 480) continue;
        for (col = 0; col < CURSOR_W; col++) {
            int x = cx + col;
            unsigned char pix = g_cur_shape[row][col];
            if (x < 0 || x >= 640 || !pix) continue;
            vesa_get_pixel(x, y,
                           &g_cur_bg[row][col][0],
                           &g_cur_bg[row][col][1],
                           &g_cur_bg[row][col][2]);
            if (pix == 1) vesa_put_pixel(x, y,   0,   0,   0);
            else          vesa_put_pixel(x, y, 255, 255, 255);
        }
    }
    g_cur_drawn = 1;
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

#define WM_CREATE        0x0001
#define WM_DESTROY       0x0002
#define WM_SIZE          0x0005
#define WM_ACTIVATE      0x0006
#define WM_CLOSE         0x0010
#define WM_PAINT         0x000F
#define WM_QUIT          0x0012
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_KEYDOWN       0x0100
#define WM_KEYUP         0x0101
#define WM_CHAR          0x0102
#define WM_SYSCOMMAND    0x0112

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

/* mouse_poll: zdefiniowane w mouse_msg.c, linkowane razem z user.obj */
extern int __far mouse_poll(MSG __far *pmsg, HWND hwnd);

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
 * Stale Win16 / NC metryki
 * ============================================================ */
#define WS_CAPTION      0x00C00000UL
#define WS_THICKFRAME   0x00040000UL
#define WS_MINIMIZEBOX  0x00020000UL
#define WS_MAXIMIZEBOX  0x00010000UL
#define WS_SYSMENU      0x00080000UL

#define NC_BORDER_W   4     /* grubosc ramki (outer 1px black + 3px gray) */
#define NC_CAPTION_H  20    /* wysokosc paska tytulu */
#define NC_BTN_W      18    /* szerokosc przyciskow min/max */
#define NC_SYSMENU_W  18    /* szerokosc przycisku sysmenu */
#define MENU_BAR_H    18    /* wysokosc paska menu (0 jesli brak menu) */

#define CW_USEDEFAULT  ((int)0x8000)

/* KCB - menu bar (RT_MENU skopiowany przez rc_copy_menu_to_kcb w loader.c) */
#define KCB_MENU_N    292   /* BYTE: n_menus (0 lub 1) */
#define KCB_MENU_ID   293   /* WORD: menu ID */
#define KCB_MENU_SZ   295   /* WORD: menu_size (bajty) */
#define KCB_MENU_DATA 297   /* BYTE[]: surowe dane RT_MENU (max 215 B) */

/* NC hit test codes */
#define HTCLIENT    1
#define HTCAPTION   2
#define HTSYSMENU   3
#define HTMINBUTTON 8
#define HTMAXBUTTON 9
#define HTBORDER   18
#define HTMENU     10   /* klikniecie w menu bar (nie standard Windows - wewnetrzny) */

/* SC (SysCommand) codes */
#define SC_CLOSE    0xF060u
#define SC_MINIMIZE 0xF020u
#define SC_MAXIMIZE 0xF030u
#define SC_RESTORE  0xF120u

/* Font ROM (8x16, PSF) dostepny przez GDT selector 0x118 */
#define SEL_FONT_USR   0x118u
#define FONT_H_USR     16
#define FONT_W_USR     8

/* ============================================================
 * Menu bar — dane parsowane z RT_MENU (KCB[292..511])
 * ============================================================ */
#define MENU_MAX_TOPLEVEL 4   /* max top-level pozycji menu bar */
#define MENU_MAX_ITEMS    8   /* max itemow w jednym popup */

static struct {
    char     title[20];             /* nazwa top-level (np. "File") */
    int      n_items;
    struct {
        char     name[20];          /* nazwa itemu (np. "Exit") */
        unsigned id;                /* WM_COMMAND ID */
        int      separator;         /* 1 jesli MF_SEPARATOR */
    } items[MENU_MAX_ITEMS];
    int      x0, x1;               /* abs ekranowe x (wypelniane przy draw) */
} g_menu[MENU_MAX_TOPLEVEL];

static int      g_menu_n     = 0;  /* liczba top-level pozycji */
static int      g_menu_parsed = 0; /* 1 po udanym parse_menu_from_kcb */
static int      g_popup_idx  = -1; /* indeks otwartego popupu (-1=brak) */
static int      g_menu_bar_y = 0;  /* abs Y menu bar (ustawiane w ShowWindow) */
static int      g_menu_bar_x = 0;  /* abs X poczatku menu bar */
static int      g_menu_bar_w = 0;  /* szerokosc menu bar */

/* Parsuje RT_MENU z KCB do g_menu[]. Zwraca liczbe top-level pozycji. */
static int parse_menu_from_kcb(void)
{
    unsigned char __far *kcb = KCB_MK_FP(0);
    unsigned char __far *p;
    unsigned short menu_size;
    unsigned char n_menus;
    int ti;         /* top-level index */

    n_menus = kcb[KCB_MENU_N];
    if (!n_menus) return 0;

    menu_size = (unsigned short)kcb[KCB_MENU_SZ] | ((unsigned short)kcb[KCB_MENU_SZ+1] << 8);
    p = KCB_MK_FP(KCB_MENU_DATA);

    /* Skip MENUHEADER: 2 bajty versionNumber + 2 bajty cbHeaderSize */
    if (menu_size < 4) return 0;
    p += 4;

    g_menu_n = 0;
    ti = 0;
    while (ti < MENU_MAX_TOPLEVEL) {
        unsigned short mtOption;
        int ii;

        /* Odczyt mtOption (WORD) */
        mtOption = (unsigned short)*p | ((unsigned short)*(p+1) << 8);
        p += 2;

        /* Koniec listy (nie tu powinien byc MF_END na top-level) */
        if (mtOption == 0 && *p == 0) break;

        if (mtOption & 0x0010) {    /* MF_POPUP */
            /* Czytaj nazwe popupu */
            int k = 0;
            while (*p && k < 19) { g_menu[ti].title[k++] = (char)*p++; }
            if (*p) while (*p) p++;  /* pomiń resztkę */
            p++;  /* null terminator */
            g_menu[ti].title[k] = '\0';

            /* Czytaj sub-items az do MF_END */
            ii = 0;
            while (ii < MENU_MAX_ITEMS) {
                unsigned short subOpt;
                unsigned short subID;
                subOpt = (unsigned short)*p | ((unsigned short)*(p+1) << 8);
                p += 2;

                if (subOpt & 0x0800) {  /* MF_SEPARATOR */
                    g_menu[ti].items[ii].separator = 1;
                    g_menu[ti].items[ii].name[0] = '\0';
                    g_menu[ti].items[ii].id = 0;
                    ii++;
                    if (subOpt & 0x0080) break;
                    continue;
                }
                /* mtID (tylko dla niepopupowych) */
                subID = (unsigned short)*p | ((unsigned short)*(p+1) << 8);
                p += 2;

                /* nazwa */
                k = 0;
                while (*p && k < 19) { g_menu[ti].items[ii].name[k++] = (char)*p++; }
                if (*p) while (*p) p++;
                p++;
                g_menu[ti].items[ii].name[k] = '\0';
                g_menu[ti].items[ii].id       = subID;
                g_menu[ti].items[ii].separator = 0;
                ii++;
                if (subOpt & 0x0080) break;  /* MF_END */
            }
            g_menu[ti].n_items = ii;
            ti++;
            if (mtOption & 0x0080) break;   /* MF_END na top-level */
        } else {
            /* Top-level zwykly item (rzadki) - pomijamy */
            p += 2;  /* skip ID */
            while (*p) p++; p++;
            if (mtOption & 0x0080) break;
        }
    }
    g_menu_n = ti;
    serial_puts("MENU: parse OK n="); serial_hex16((unsigned)g_menu_n); serial_putc('\n');
    return g_menu_n;
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
    unsigned menu_name_id;  /* MAKEINTRESOURCE menu ID (0=brak) */
} g_classes[MAX_CLASSES];

static struct {
    HWND          hwnd;
    HWND          parent;
    int           class_idx;
    int           used;
    unsigned      w, h;
    int           x, y;          /* abs screen coords lewego gornego rogu okna */
    unsigned long style;          /* WS_* flagi */
    char          title[64];      /* tytul (z CreateWindow lpWindowName) */
    unsigned char state;          /* 0=normal, 1=minimized, 2=maximized */
    int           saved_x, saved_y;   /* pozycja przed maximize */
    unsigned      saved_w, saved_h;   /* rozmiar przed maximize */
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
BOOL __far __pascal DestroyWindow(HWND hwnd);
static unsigned nc_hit_test(int wi, int abs_x, int abs_y);

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
            /* lpszMenuName: MAKEINTRESOURCE(n) = far ptr z seg=0, off=n */
            g_classes[i].menu_name_id =
                (unsigned)((unsigned long)wc->lpszMenuName & 0xFFFFUL);
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
 * draw_window_chrome — rysuje NC area okna bezposrednio do LFB
 * Wywolywane po ShowWindow i po MoveWindow.
 * ============================================================ */
static void draw_chrome_char(int sx, int sy, unsigned char ch,
                              unsigned char fr, unsigned char fg, unsigned char fb,
                              unsigned char br, unsigned char bg, unsigned char bb)
{
    int row, col;
    for (row = 0; row < FONT_H_USR; row++) {
        unsigned char bits = *((unsigned char __far *)
            ((unsigned long)SEL_FONT_USR << 16 | (unsigned)((unsigned)ch * FONT_H_USR + row)));
        for (col = 0; col < FONT_W_USR; col++) {
            unsigned char r, g, b;
            if (bits & (0x80u >> col)) { r=fr; g=fg; b=fb; }
            else                        { r=br; g=bg; b=bb; }
            {
                unsigned long off = (unsigned long)(sy + row) * VESA_PITCH
                                  + (unsigned long)(sx + col) * VESA_BPP;
                unsigned sel = (unsigned)(SEL_VESA_BASE + (unsigned)(off >> 16) * 8u);
                unsigned char __far *p = USR_MK_FP_VESA(sel, (unsigned)(off & 0xFFFFUL));
                p[0]=b; p[1]=g; p[2]=r;
            }
        }
    }
}

static void draw_chrome_text(int sx, int sy, const char *s,
                              unsigned char fr, unsigned char fg, unsigned char fb,
                              unsigned char br, unsigned char bg, unsigned char bb)
{
    while (*s) {
        draw_chrome_char(sx, sy, (unsigned char)*s, fr, fg, fb, br, bg, bb);
        sx += FONT_W_USR;
        s++;
    }
}

/* Rysuje jeden przycisk NC; pressed=1 -> odwrocone 3D (wcisniety) */
static void draw_nc_button(int bx, int by, int bw, int bh, char glyph, int pressed)
{
    int tx, ty;
    vesa_fill_rect(bx, by, bw, bh, 0x60, 0xA8, 0xB4);
    if (pressed) {
        /* wcisniety: ciemna krawedz top/left, jasna bottom/right */
        vesa_fill_rect(bx,          by,          bw, 1,  0x00, 0x40, 0x48);
        vesa_fill_rect(bx,          by,          1,  bh, 0x00, 0x40, 0x48);
        vesa_fill_rect(bx,          by + bh - 1, bw, 1,  0xA0, 0xD8, 0xE0);
        vesa_fill_rect(bx + bw - 1, by,          1,  bh, 0xA0, 0xD8, 0xE0);
        tx = bx + (bw - FONT_W_USR) / 2 + 1;
        ty = by + (bh - FONT_H_USR) / 2 + 1;
    } else {
        /* normalny: jasna top/left, ciemna bottom/right */
        vesa_fill_rect(bx,          by,          bw, 1,  0xA0, 0xD8, 0xE0);
        vesa_fill_rect(bx,          by,          1,  bh, 0xA0, 0xD8, 0xE0);
        vesa_fill_rect(bx,          by + bh - 1, bw, 1,  0x00, 0x40, 0x48);
        vesa_fill_rect(bx + bw - 1, by,          1,  bh, 0x00, 0x40, 0x48);
        tx = bx + (bw - FONT_W_USR) / 2;
        ty = by + (bh - FONT_H_USR) / 2;
    }
    draw_chrome_char(tx, ty, (unsigned char)glyph,
                     0x00,0x00,0x00, 0x60,0xA8,0xB4);
}

/* Rysuje mini-ikone zminimalizowanego okna na dole ekranu.
 * wi: indeks w g_windows. Ikona: x=4+wi*124, y=462, 120x16. */
#define ICON_Y   462
#define ICON_W   120
#define ICON_H    16
static void draw_minimized_icon(int wi)
{
    int ix = 4 + wi * 124;
    int iy = ICON_Y;
    const char *src = g_windows[wi].title[0] ? g_windows[wi].title : "Window";
    static char buf[13];   /* static -> w DS, nie na stosie (SS!=DS w DLL) */
    int  i;
    for (i = 0; i < 12 && src[i]; i++) buf[i] = src[i];
    buf[i] = '\0';
    vesa_fill_rect(ix,    iy,    ICON_W, ICON_H, 0x60, 0xA8, 0xB4);
    vesa_fill_rect(ix,    iy,    ICON_W, 1,      0xA0, 0xD8, 0xE0);
    vesa_fill_rect(ix,    iy,    1,      ICON_H, 0xA0, 0xD8, 0xE0);
    vesa_fill_rect(ix,    iy+ICON_H-1, ICON_W, 1, 0x00, 0x40, 0x48);
    vesa_fill_rect(ix+ICON_W-1, iy, 1, ICON_H, 0x00, 0x40, 0x48);
    draw_chrome_text(ix + 4, iy + 2, buf, 0x00, 0x00, 0x00, 0x60, 0xA8, 0xB4);
}

/* Sledzi przycisk NC: rysuje wcisniety, czeka na mouse-up.
 * Zwraca 1 jesli puszczono nad przyciskiem, 0 jesli poza. */
static int track_nc_button(int bx, int by, int bw, int bh, char glyph, HWND target_hwnd)
{
    int released_on = 0;
    cursor_erase();
    draw_nc_button(bx, by, bw, bh, glyph, 1);
    cursor_draw(g_cur_x, g_cur_y);
    for (;;) {
        MSG tmp;
        do_hlt();
        if (mouse_poll(&tmp, target_hwnd)) {
            unsigned char __far *kcb = KCB_MK_FP(0);
            int mx = (int)((unsigned)kcb[285] | ((unsigned)kcb[286] << 8));
            int my = (int)((unsigned)kcb[287] | ((unsigned)kcb[288] << 8));
            if (mx != g_cur_x || my != g_cur_y || !g_cur_drawn) {
                cursor_erase();
                cursor_draw(mx, my);
            }
            if (tmp.message == 0x0202 /* WM_LBUTTONUP */) {
                int ax = (int)(tmp.lParam & 0xFFFFUL);
                int ay = (int)((tmp.lParam >> 16) & 0xFFFFUL);
                released_on = (ax >= bx && ax < bx+bw && ay >= by && ay < by+bh);
                break;
            }
        }
    }
    cursor_erase();
    draw_nc_button(bx, by, bw, bh, glyph, 0);
    cursor_draw(g_cur_x, g_cur_y);
    return released_on;
}

static void draw_window_chrome(int wi)
{
    int wx, wy, ww, wh;
    unsigned long style;
    int cap_x, cap_y, cap_w;
    int client_x, client_y, client_w, client_h;
    const char *title;
    int title_len, title_x, title_y;

    if (!g_windows[wi].used) return;
    wx    = g_windows[wi].x;
    wy    = g_windows[wi].y;
    ww    = (int)g_windows[wi].w;
    wh    = (int)g_windows[wi].h;
    style = g_windows[wi].style;

    if (!(style & WS_CAPTION)) return;

    /* Zewnetrzna ramka 1px czarna */
    vesa_fill_rect(wx, wy, ww, 1,  0,0,0);
    vesa_fill_rect(wx, wy + wh - 1, ww, 1, 0,0,0);
    vesa_fill_rect(wx, wy, 1, wh, 0,0,0);
    vesa_fill_rect(wx + ww - 1, wy, 1, wh, 0,0,0);
    /* Wewnetrzna gruba ramka 3px jasny teal (inset o 1) */
    vesa_fill_rect(wx+1, wy+1, ww-2, 3, 0x88,0xC8,0xD0);
    vesa_fill_rect(wx+1, wy+wh-4, ww-2, 3, 0x88,0xC8,0xD0);
    vesa_fill_rect(wx+1, wy+1, 3, wh-2, 0x88,0xC8,0xD0);
    vesa_fill_rect(wx+ww-4, wy+1, 3, wh-2, 0x88,0xC8,0xD0);

    /* Pasek tytulu: od (wx+4, wy+4) szerokosc ww-8, wysokosc NC_CAPTION_H */
    cap_x = wx + NC_BORDER_W;
    cap_y = wy + NC_BORDER_W;
    cap_w = ww - 2 * NC_BORDER_W;
    vesa_fill_rect(cap_x, cap_y, cap_w, NC_CAPTION_H, 0x0A,0x6E,0x7E); /* deep teal */

    /* Przycisk sysmenu (lewy) */
    draw_nc_button(cap_x, cap_y, NC_SYSMENU_W, NC_CAPTION_H, '=', 0);

    /* Przycisk minimize (drugi od prawej) */
    draw_nc_button(cap_x + cap_w - 2*NC_BTN_W, cap_y, NC_BTN_W, NC_CAPTION_H, '-', 0);

    /* Przycisk maximize (prawy) */
    draw_nc_button(cap_x + cap_w - NC_BTN_W, cap_y, NC_BTN_W, NC_CAPTION_H, '+', 0);

    /* Tytul: bialy tekst wycentrowany pionowo, wyrownany od lewej */
    title = g_windows[wi].title;
    title_len = 0;
    while (title[title_len]) title_len++;
    title_x = cap_x + NC_SYSMENU_W + 4;
    title_y = cap_y + (NC_CAPTION_H - FONT_H_USR) / 2;
    draw_chrome_text(title_x, title_y, title,
                     0xFF,0xFF,0xFF, 0x0A,0x6E,0x7E);
    (void)title_len;

    /* Obszar klienta: bialy (z uwzglednieniem menu bar jesli obecne) */
    {
        int menu_h = (g_menu_parsed && g_menu_n > 0) ? MENU_BAR_H : 0;
        client_x = wx + NC_BORDER_W;
        client_y = wy + NC_BORDER_W + NC_CAPTION_H + menu_h;
        client_w = ww - 2 * NC_BORDER_W;
        client_h = wh - 2 * NC_BORDER_W - NC_CAPTION_H - menu_h;
        if (client_w > 0 && client_h > 0)
            vesa_fill_rect(client_x, client_y, client_w, client_h, 0xFF,0xFF,0xFF);
    }
}

/* ============================================================
 * Menu bar drawing
 * ============================================================ */

/* Rysuje menu bar pod paskiem tytulu; wypelnia g_menu[].x0/x1 */
static void draw_menu_bar(int wi)
{
    int mx, my, mw, i, x;

    if (!g_windows[wi].used) return;
    if (!(g_windows[wi].style & WS_CAPTION)) return;
    if (!g_menu_parsed || g_menu_n == 0) return;

    mx = g_windows[wi].x + NC_BORDER_W;
    my = g_windows[wi].y + NC_BORDER_W + NC_CAPTION_H;
    mw = (int)g_windows[wi].w - 2 * NC_BORDER_W;

    /* Zapisz dla hit testingu */
    g_menu_bar_x = mx;
    g_menu_bar_y = my;
    g_menu_bar_w = mw;

    /* Tlo — jasny teal */
    vesa_fill_rect(mx, my, mw, MENU_BAR_H, 0x88, 0xC0, 0xC8);
    /* Linia u dolu */
    vesa_fill_rect(mx, my + MENU_BAR_H - 1, mw, 1, 0x40, 0x80, 0x88);

    /* Top-level pozycje */
    x = mx + 4;
    for (i = 0; i < g_menu_n; i++) {
        int tw = 0;
        const char *s = g_menu[i].title;
        while (s[tw]) tw++;
        tw *= FONT_W_USR;
        g_menu[i].x0 = x;
        g_menu[i].x1 = x + tw + 8;
        /* Podswietl aktywny popup */
        if (i == g_popup_idx)
            vesa_fill_rect(x - 2, my, tw + 12, MENU_BAR_H - 1, 0x0A,0x6E,0x7E);
        draw_chrome_text(x + 2, my + 1, g_menu[i].title,
                         (i == g_popup_idx) ? 0xFF : 0x00,
                         (i == g_popup_idx) ? 0xFF : 0x00,
                         (i == g_popup_idx) ? 0xFF : 0x00,
                         (i == g_popup_idx) ? 0x0A : 0x88,
                         (i == g_popup_idx) ? 0x6E : 0xC0,
                         (i == g_popup_idx) ? 0x7E : 0xC8);
        x = g_menu[i].x1;
    }
}

/* Rysuje popup dropdown dla g_menu[idx].
 * Zwraca ID wybranego itemu (0 jesli zadnego / ESC). */
/* Rysuje jeden item popupu (z podswietleniem lub bez) */
static void draw_popup_item(int mi, int i, int px, int py, int pw, int item_h, int highlighted)
{
    int iy = py + 1 + i * item_h;
    if (g_menu[mi].items[i].separator) {
        vesa_fill_rect(px+1, iy, pw-2, item_h,  0xC0,0xE4,0xE8);
        vesa_fill_rect(px+2, iy + item_h/2, pw-4, 1, 0x40,0x90,0x98);
    } else if (highlighted) {
        vesa_fill_rect(px+1, iy, pw-2, item_h, 0x0A,0x6E,0x7E);
        draw_chrome_text(px + 6, iy + 1, g_menu[mi].items[i].name,
                         0xFF,0xFF,0xFF, 0x0A,0x6E,0x7E);
    } else {
        vesa_fill_rect(px+1, iy, pw-2, item_h, 0xC0,0xE4,0xE8);
        draw_chrome_text(px + 6, iy + 1, g_menu[mi].items[i].name,
                         0x00,0x00,0x00, 0xC0,0xE4,0xE8);
    }
}

static unsigned draw_and_run_popup(int mi, HWND target_hwnd)
{
    int px, py, pw, ph, i, item_h, n;
    unsigned char vk;

    if (mi < 0 || mi >= g_menu_n) return 0;
    n = g_menu[mi].n_items;
    if (n == 0) return 0;

    item_h = FONT_H_USR + 2;
    px = g_menu[mi].x0;
    py = g_menu_bar_y + MENU_BAR_H;
    pw = 120;
    ph = n * item_h + 2;

    /* Ukryj kursor przed rysowaniem popupu */
    cursor_erase();

    /* Ramka + tlo — bardzo jasny teal */
    vesa_fill_rect(px, py, pw, ph, 0xC0, 0xE4, 0xE8);
    vesa_fill_rect(px, py, pw, 1, 0x00,0x40,0x48);
    vesa_fill_rect(px, py + ph - 1, pw, 1, 0x00,0x40,0x48);
    vesa_fill_rect(px, py, 1, ph, 0x00,0x40,0x48);
    vesa_fill_rect(px + pw - 1, py, 1, ph, 0x00,0x40,0x48);

    for (i = 0; i < n; i++)
        draw_popup_item(mi, i, px, py, pw, item_h, 0);

    /* Petla menu - tryb "click to open, click to close" (Windows 3.1):
     * Pierwsze WM_LBUTTONUP (puszczenie przycisku ktory otwarl menu) jest
     * ignorowane. Menu zostaje otwarte az do nastepnego klikniecia:
     *   - WM_LBUTTONDOWN poza popupem -> zamknij
     *   - WM_LBUTTONDOWN na itemie   -> wybierz
     *   - ESC                         -> zamknij */
    {
        int first_up_done = 0;  /* 0 = jeszcze nie puszczono przycisku otwierajacego */
        int hover_item = -1;    /* aktualnie podswietlony item (-1 = brak) */

        for (;;) {
            MSG tmp;
            int hit_item = -1;

            do_hlt();   /* czekaj na IRQ */

            /* Sprawdz ESC */
            do_sti(); vk = kb_dequeue(); do_cli();
            if (vk == 0x1B) { cursor_draw(g_cur_x, g_cur_y); return 0; }

            /* Sprawdz myszke */
            if (mouse_poll(&tmp, target_hwnd)) {
                /* Aktualizuj kursor + hover na biezaco */
                {
                    unsigned char __far *kcb = KCB_MK_FP(0);
                    int mx = (int)((unsigned)kcb[285] | ((unsigned)kcb[286] << 8));
                    int my = (int)((unsigned)kcb[287] | ((unsigned)kcb[288] << 8));
                    int new_hover = -1;
                    /* Oblicz nowy hover */
                    if (mx >= px && mx < px + pw && my >= py + 1 && my < py + ph - 1) {
                        int idx = (my - py - 1) / item_h;
                        if (idx >= 0 && idx < n && !g_menu[mi].items[idx].separator)
                            new_hover = idx;
                    }
                    if (mx != g_cur_x || my != g_cur_y || new_hover != hover_item || !g_cur_drawn) {
                        cursor_erase();
                        /* Przerysuj zmienione itemy bez kursora */
                        if (new_hover != hover_item) {
                            if (hover_item >= 0)
                                draw_popup_item(mi, hover_item, px, py, pw, item_h, 0);
                            if (new_hover >= 0)
                                draw_popup_item(mi, new_hover, px, py, pw, item_h, 1);
                            hover_item = new_hover;
                        }
                        cursor_draw(mx, my);
                    }
                }
                if (tmp.message == 0x0202 /* WM_LBUTTONUP */) {
                    if (!first_up_done) {
                        /* Pierwsze puszczenie: zignoruj, przelacz w tryb click */
                        first_up_done = 1;
                    } else {
                        /* Drugie puszczenie: klik na itemie lub poza */
                        int ax = (int)(tmp.lParam & 0xFFFFUL);
                        int ay = (int)((tmp.lParam >> 16) & 0xFFFFUL);
                        if (ax < px || ax >= px + pw || ay < py || ay >= py + ph) {
                            cursor_draw(g_cur_x, g_cur_y);
                            return 0;
                        }
                        i = (ay - py - 1) / item_h;
                        if (i >= 0 && i < n && !g_menu[mi].items[i].separator)
                            hit_item = i;
                    }
                } else if (tmp.message == 0x0201 /* WM_LBUTTONDOWN */) {
                    /* Po pierwszym puszczeniu: klikniecie zamyka lub wybiera */
                    if (first_up_done) {
                        int ax = (int)(tmp.lParam & 0xFFFFUL);
                        int ay = (int)((tmp.lParam >> 16) & 0xFFFFUL);
                        if (ax < px || ax >= px + pw || ay < py || ay >= py + ph) {
                            cursor_draw(g_cur_x, g_cur_y);
                            return 0;
                        }
                        /* Klik na itemie: zaznacz, poczekaj na UP */
                        i = (ay - py - 1) / item_h;
                        if (i >= 0 && i < n && !g_menu[mi].items[i].separator)
                            hit_item = i;
                    }
                }
            }
            if (hit_item >= 0) {
                cursor_draw(g_cur_x, g_cur_y);
                return (unsigned)g_menu[mi].items[hit_item].id;
            }
        }
    }
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
    int  ci, wi, ti;
    (void)hMenu; (void)hInst; (void)lpParam;

    ci = find_class(cls);
    if (ci < 0) { serial_puts("USER: class not found\n"); return 0; }

    for (wi = 0; wi < MAX_WINDOWS; wi++)
        if (!g_windows[wi].used) break;
    if (wi >= MAX_WINDOWS) return 0;

    /* CW_USEDEFAULT: uzyj domyslnej pozycji/rozmiaru */
    if (x == CW_USEDEFAULT || x == 0) x = 10;
    if (y == CW_USEDEFAULT || y == 0) y = 10;
    if (w <= 0 || w == (int)0x8000)   w = 320;
    if (h <= 0 || h == (int)0x8000)   h = 200;

    hwnd = g_next_hwnd++;
    g_windows[wi].hwnd      = hwnd;
    g_windows[wi].parent    = parent;
    g_windows[wi].class_idx = ci;
    g_windows[wi].w         = (unsigned)w;
    g_windows[wi].h         = (unsigned)h;
    g_windows[wi].x         = x;
    g_windows[wi].y         = y;
    g_windows[wi].style     = style;
    g_windows[wi].state     = 0;
    /* Kopiuj tytul */
    for (ti = 0; ti < 63 && title && title[ti]; ti++)
        g_windows[wi].title[ti] = (char)title[ti];
    g_windows[wi].title[ti] = '\0';
    g_windows[wi].used      = 1;

    /* Dla okien potomnych: x,y sa wzgledem client area rodzica. */
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

    /* Zapisz do KCB (dla GDI: HWND-based DC).
     * Dla okien potomnych: ox/oy = abs client origin.
     * Dla top-level z NC: ox/oy = client origin (window + NC offset). */
    /* Parsuj menu klasy (top-level, z WS_CAPTION) */
    if (parent == 0 && (style & WS_CAPTION) && !g_menu_parsed) {
        unsigned mid = g_classes[ci].menu_name_id;
        if (mid != 0) {
            unsigned char __far *kcb = KCB_MK_FP(0);
            unsigned short kid = (unsigned short)kcb[KCB_MENU_ID] |
                                 ((unsigned short)kcb[KCB_MENU_ID+1] << 8);
            if (kcb[KCB_MENU_N] && kid == mid) {
                parse_menu_from_kcb();
                g_menu_parsed = 1;
            }
        }
    }

    if (parent != 0) {
        kcb_set_wnd_pos((unsigned)hwnd, g_windows[wi].x, g_windows[wi].y);
        kcb_set_wnd_w((unsigned)hwnd, g_windows[wi].w);
        kcb_set_wnd_h((unsigned)hwnd, g_windows[wi].h);
    } else if (style & WS_CAPTION) {
        /* top-level z paskiem: KCB wskazuje na client area (z uwzglednieniem menu bar) */
        int menu_h = (g_menu_parsed && g_menu_n > 0) ? MENU_BAR_H : 0;
        kcb_set_wnd_pos((unsigned)hwnd,
                        g_windows[wi].x + NC_BORDER_W,
                        g_windows[wi].y + NC_BORDER_W + NC_CAPTION_H + menu_h);
        kcb_set_wnd_w((unsigned)hwnd, (unsigned)(w - 2*NC_BORDER_W));
        kcb_set_wnd_h((unsigned)hwnd, (unsigned)(h - 2*NC_BORDER_W - NC_CAPTION_H - menu_h));
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
    /* Sprawdz zdarzenia myszy (mouse_msg.c) */
    if (mouse_poll(pmsg, g_kb_hwnd ? g_kb_hwnd : 1)) {
        /* Aktualizuj pozycje kursora (KCB[285..288] = abs X/Y) */
        {
            unsigned char __far *kcb = KCB_MK_FP(0);
            int mx = (int)((unsigned)kcb[285] | ((unsigned)kcb[286] << 8));
            int my = (int)((unsigned)kcb[287] | ((unsigned)kcb[288] << 8));
            if (mx != g_cur_x || my != g_cur_y || !g_cur_drawn) {
                cursor_erase();
                cursor_draw(mx, my);
            }
        }
        /* WM_LBUTTONDOWN: sprawdz czy w NC area */
        if (pmsg->message == 0x0201 /* WM_LBUTTONDOWN */) {
            int abs_x = (int)(pmsg->lParam & 0xFFFFUL);
            int abs_y = (int)((pmsg->lParam >> 16) & 0xFFFFUL);
            unsigned ht = HTCLIENT;
            int wi;
            HWND hw = g_kb_hwnd ? g_kb_hwnd : 1;
            /* Sprawdz klikniecie na ikonie zminimalizowanego okna */
            for (wi = 0; wi < MAX_WINDOWS; wi++) {
                if (g_windows[wi].used && g_windows[wi].state == 1) {
                    int ix = 4 + wi * 124;
                    if (abs_x >= ix && abs_x < ix + ICON_W &&
                        abs_y >= ICON_Y && abs_y < ICON_Y + ICON_H) {
                        push_msg(g_windows[wi].hwnd, WM_SYSCOMMAND,
                                 (WPARAM)SC_RESTORE, 0L);
                        pmsg->message = 0; /* WM_NULL - polknij klikniecie */
                        break;
                    }
                }
            }
            for (wi = 0; wi < MAX_WINDOWS; wi++) {
                if (g_windows[wi].used && g_windows[wi].hwnd == hw
                    && g_windows[wi].state != 1) {
                    ht = nc_hit_test(wi, abs_x, abs_y);
                    if (ht != HTCLIENT) {
                        /* NC klikniecie */
                        pmsg->message = WM_NCLBUTTONDOWN;
                        pmsg->wParam  = ht;
                        /* lParam = MAKELONG(screen_x, screen_y) - zostaje */
                    } else {
                        /* Klient: lParam = wzgledem client origin */
                        int menu_h = (g_menu_parsed && g_menu_n > 0) ? MENU_BAR_H : 0;
                        int client_ox = g_windows[wi].x + NC_BORDER_W;
                        int client_oy = g_windows[wi].y + NC_BORDER_W + NC_CAPTION_H + menu_h;
                        if (g_windows[wi].style & WS_CAPTION) {
                            pmsg->lParam = (LPARAM)(((unsigned long)(unsigned)(abs_y - client_oy) << 16) |
                                                     (unsigned)(abs_x - client_ox));
                        }
                    }
                    break;
                }
            }
            serial_puts("MSE:0x"); serial_hex16(pmsg->message);
            serial_puts(" ht="); serial_hex16(ht); serial_putc('\n');
        }
        return 1;
    }

    if (g_msg_count == 0) {
        /* Kolejka pusta: czekaj na przerwanie (IRQ0/IRQ1/IRQ12) */
        do_hlt();
        pmsg->hwnd    = 0;
        pmsg->message = 0;   /* WM_NULL */
        pmsg->wParam  = 0;
        pmsg->lParam  = 0L;
        return 1;
    }
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
        serial_puts("KEY:0x"); serial_hex16(vk); serial_putc('\n');
        pmsg->hwnd    = g_kb_hwnd ? g_kb_hwnd : 1;
        pmsg->message = WM_KEYDOWN;
        pmsg->wParam  = vk;
        pmsg->lParam  = 0L;
        return 1;
    }
    /* Sprawdz zdarzenia myszy (mouse_msg.c) */
    if (mouse_poll(pmsg, g_kb_hwnd ? g_kb_hwnd : 1)) {
        serial_puts("MSE:0x"); serial_hex16(pmsg->message); serial_putc('\n');
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

/* NC hit test: zwraca HT code dla punktu (abs_x, abs_y) wzgledem okna wi */
static unsigned nc_hit_test(int wi, int abs_x, int abs_y)
{
    int wx, wy, ww, wh, cap_x, cap_y, cap_w;
    unsigned long style;

    if (!g_windows[wi].used) return HTCLIENT;
    wx    = g_windows[wi].x;
    wy    = g_windows[wi].y;
    ww    = (int)g_windows[wi].w;
    wh    = (int)g_windows[wi].h;
    style = g_windows[wi].style;

    /* Poza oknem */
    if (abs_x < wx || abs_x >= wx + ww || abs_y < wy || abs_y >= wy + wh)
        return HTCLIENT;

    if (!(style & WS_CAPTION)) return HTCLIENT;

    cap_x = wx + NC_BORDER_W;
    cap_y = wy + NC_BORDER_W;
    cap_w = ww - 2 * NC_BORDER_W;

    /* Ramka */
    if (abs_x < cap_x || abs_x >= cap_x + cap_w ||
        abs_y < cap_y || abs_y >= wy + wh - NC_BORDER_W)
        return HTBORDER;

    /* Pasek tytulu */
    if (abs_y < cap_y + NC_CAPTION_H) {
        if (abs_x < cap_x + NC_SYSMENU_W)
            return HTSYSMENU;
        if (abs_x >= cap_x + cap_w - NC_BTN_W)
            return HTMAXBUTTON;
        if (abs_x >= cap_x + cap_w - 2 * NC_BTN_W)
            return HTMINBUTTON;
        return HTCAPTION;
    }

    /* Menu bar */
    if (g_menu_parsed && g_menu_n > 0 &&
        abs_y >= cap_y + NC_CAPTION_H &&
        abs_y <  cap_y + NC_CAPTION_H + MENU_BAR_H) {
        return HTMENU;
    }

    return HTCLIENT;
}

LRESULT __far __pascal DefWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCLBUTTONDOWN) {
        /* wp = HT code, lp = MAKELONG(screen_x, screen_y) */
        if (wp == HTMENU) {
            int ax = (int)(lp & 0xFFFFUL);
            int mi, found = -1, wi;
            unsigned cmd_id;
            for (mi = 0; mi < g_menu_n; mi++) {
                if (ax >= g_menu[mi].x0 && ax < g_menu[mi].x1) { found = mi; break; }
            }
            if (found < 0) return 0;
            for (wi = 0; wi < MAX_WINDOWS; wi++)
                if (g_windows[wi].used && g_windows[wi].hwnd == hwnd) break;
            if (wi >= MAX_WINDOWS) return 0;
            g_popup_idx = found;
            /* Ukryj kursor PRZED draw_menu_bar, zeby cursor_erase w popup
             * nie przywrocila pikselii sprzed podswietlenia (slad kursora) */
            cursor_erase();
            draw_menu_bar(wi);
            cmd_id = draw_and_run_popup(found, hwnd);
            g_popup_idx = -1;
            /* Wymazanie dropdownu: ukryj kursor, wypelnij bialym, odswierz */
            cursor_erase();
            {
                int erase_x = g_menu_bar_x;
                int erase_y = g_menu_bar_y + MENU_BAR_H;
                int erase_w = g_menu_bar_w;
                int erase_h = (int)g_windows[wi].h
                              - (erase_y - g_windows[wi].y)
                              - NC_BORDER_W;
                if (erase_h > 0)
                    vesa_fill_rect(erase_x, erase_y, erase_w, erase_h,
                                   0xFF, 0xFF, 0xFF);
            }
            draw_menu_bar(wi);
            cursor_draw(g_cur_x, g_cur_y);
            push_msg(hwnd, WM_PAINT, 0, 0L);
            if (cmd_id > 0)
                push_msg(hwnd, 0x0111u /* WM_COMMAND */, (WPARAM)cmd_id, 0L);
            return 0;
        }
        if (wp == HTSYSMENU || wp == HTMINBUTTON || wp == HTMAXBUTTON) {
            /* Pobierz wspolrzedne odpowiedniego przycisku */
            int wi, bx, by, bw, bh;
            char glyph;
            for (wi = 0; wi < MAX_WINDOWS; wi++)
                if (g_windows[wi].used && g_windows[wi].hwnd == hwnd) break;
            if (wi >= MAX_WINDOWS) return 0;
            {
                int cap_x = g_windows[wi].x + NC_BORDER_W;
                int cap_y = g_windows[wi].y + NC_BORDER_W;
                int cap_w = (int)g_windows[wi].w - 2 * NC_BORDER_W;
                if (wp == HTSYSMENU) {
                    bx=cap_x; by=cap_y; bw=NC_SYSMENU_W; bh=NC_CAPTION_H; glyph='=';
                } else if (wp == HTMINBUTTON) {
                    bx=cap_x+cap_w-2*NC_BTN_W; by=cap_y; bw=NC_BTN_W; bh=NC_CAPTION_H; glyph='-';
                } else {
                    bx=cap_x+cap_w-NC_BTN_W; by=cap_y; bw=NC_BTN_W; bh=NC_CAPTION_H; glyph='+';
                }
            }
            if (track_nc_button(bx, by, bw, bh, glyph, hwnd)) {
                if (wp == HTSYSMENU)
                    push_msg(hwnd, WM_SYSCOMMAND, (WPARAM)SC_CLOSE, lp);
                else if (wp == HTMINBUTTON)
                    push_msg(hwnd, WM_SYSCOMMAND, (WPARAM)SC_MINIMIZE, lp);
                else
                    push_msg(hwnd, WM_SYSCOMMAND, (WPARAM)SC_MAXIMIZE, lp);
            }
        }
        /* HTCAPTION: brak akcji w tej wersji */
        return 0;
    }
    if (msg == WM_SYSCOMMAND) {
        unsigned cmd = wp & 0xFFF0u;
        if (cmd == (SC_CLOSE & 0xFFF0u)) {
            /* SC_CLOSE: wyslij WM_CLOSE do okna */
            SendMessage(hwnd, WM_CLOSE, 0, 0L);
        } else if (cmd == (SC_MINIMIZE & 0xFFF0u)) {
            int wi;
            for (wi = 0; wi < MAX_WINDOWS; wi++)
                if (g_windows[wi].used && g_windows[wi].hwnd == hwnd) break;
            if (wi < MAX_WINDOWS && g_windows[wi].state != 1) {
                g_windows[wi].state = 1;
                cursor_erase();
                vesa_fill_rect(0, 0, 640, 480, 0x1C, 0x50, 0x58);
                draw_minimized_icon(wi);
                cursor_draw(g_cur_x, g_cur_y);
            }
        } else if (cmd == (SC_MAXIMIZE & 0xFFF0u)) {
            int wi;
            for (wi = 0; wi < MAX_WINDOWS; wi++)
                if (g_windows[wi].used && g_windows[wi].hwnd == hwnd) break;
            if (wi < MAX_WINDOWS) {
                cursor_erase();
                if (g_windows[wi].state != 2) {
                    /* Normalny -> zmaksymalizuj */
                    g_windows[wi].saved_x = g_windows[wi].x;
                    g_windows[wi].saved_y = g_windows[wi].y;
                    g_windows[wi].saved_w = g_windows[wi].w;
                    g_windows[wi].saved_h = g_windows[wi].h;
                    g_windows[wi].x = 0; g_windows[wi].y = 0;
                    g_windows[wi].w = 640; g_windows[wi].h = 480;
                    g_windows[wi].state = 2;
                } else {
                    /* Zmaksymalizowany -> przywroc */
                    g_windows[wi].x = g_windows[wi].saved_x;
                    g_windows[wi].y = g_windows[wi].saved_y;
                    g_windows[wi].w = g_windows[wi].saved_w;
                    g_windows[wi].h = g_windows[wi].saved_h;
                    g_windows[wi].state = 0;
                }
                /* Zaktualizuj KCB (client origin) */
                {
                    int menu_h = (g_menu_parsed && g_menu_n > 0) ? MENU_BAR_H : 0;
                    kcb_set_wnd_pos((unsigned)hwnd,
                        g_windows[wi].x + NC_BORDER_W,
                        g_windows[wi].y + NC_BORDER_W + NC_CAPTION_H + menu_h);
                    kcb_set_wnd_w((unsigned)hwnd,
                        (unsigned)(g_windows[wi].w - 2*NC_BORDER_W));
                    kcb_set_wnd_h((unsigned)hwnd,
                        (unsigned)(g_windows[wi].h - 2*NC_BORDER_W - NC_CAPTION_H - menu_h));
                }
                /* Przerysuj caly ekran */
                vesa_fill_rect(0, 0, 640, 480, 0x1C, 0x50, 0x58);
                draw_window_chrome(wi);
                draw_menu_bar(wi);
                cursor_draw(g_cur_x, g_cur_y);
                push_msg(hwnd, WM_SIZE, 0,
                    ((unsigned long)(g_windows[wi].h - 2*NC_BORDER_W - NC_CAPTION_H
                                     - ((g_menu_parsed && g_menu_n > 0) ? MENU_BAR_H : 0)) << 16)
                    | (unsigned long)(g_windows[wi].w - 2*NC_BORDER_W));
                push_msg(hwnd, WM_PAINT, 0, 0L);
            }
        } else if (cmd == (SC_RESTORE & 0xFFF0u)) {
            int wi;
            for (wi = 0; wi < MAX_WINDOWS; wi++)
                if (g_windows[wi].used && g_windows[wi].hwnd == hwnd) break;
            if (wi < MAX_WINDOWS && g_windows[wi].state == 1) {
                g_windows[wi].state = 0;
                cursor_erase();
                vesa_fill_rect(0, 0, 640, 480, 0x1C, 0x50, 0x58);
                draw_window_chrome(wi);
                draw_menu_bar(wi);
                cursor_draw(g_cur_x, g_cur_y);
                push_msg(hwnd, WM_PAINT, 0, 0L);
            }
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        /* Domyslna obsluga WM_CLOSE: zniszcz okno */
        DestroyWindow(hwnd);
        return 0;
    }
    (void)lp;
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
    /* Wypelnij ekran tlem (bialy lub czarny) */
    cursor_erase();
    vesa_fill_rect(0, 0, 640, 480, 0x1C, 0x50, 0x58); /* desktop: dark teal */
    /* Narysuj chrome okna (ramka, pasek tytulu, przyciski) i menu bar */
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].hwnd == hwnd) {
            draw_window_chrome(i);
            draw_menu_bar(i);
            break;
        }
    }
    /* Narysuj kursor w poczatkowej pozycji (srodek ekranu) */
    cursor_draw(320, 240);
    /* WM_SIZE / WM_ACTIVATE (dla SKI.EXE game loop) */
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
    int i;
    (void)lpRect; (void)bErase;
    push_msg(hwnd, WM_PAINT, 0, 0L);
    /* Jesli lpRect==NULL (cale okno): przekaz WM_PAINT do wszystkich dzieci.
     * Windows 3.1: InvalidateRect rodzica invaliduje zachodzace okna potomne. */
    if (!lpRect) {
        for (i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].used && g_windows[i].parent == hwnd)
                push_msg(g_windows[i].hwnd, WM_PAINT, 0, 0L);
        }
    }
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

int __far __pascal InitApp(unsigned hInstance, unsigned hPrevInstance)
{
    (void)hInstance; (void)hPrevInstance;
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
    serial_puts("USER: DestroyWindow\n");
    /* Wyslij WM_DESTROY - apka powinna wywolac PostQuitMessage */
    SendMessage(hwnd, WM_DESTROY, 0, 0L);
    /* Oznacz okno jako nieuzywane i wymazaj z ekranu */
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].hwnd == hwnd) {
            if (g_windows[i].parent == 0) {
                /* Okno glowne: wymazanie calego ekranu kolorem desktopu */
                cursor_erase();
                vesa_fill_rect(0, 0, 640, 480, 0x1C, 0x50, 0x58);
            }
            g_windows[i].used = 0;
            break;
        }
    }
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
 * ETAP 19: nowe API Win16 (19a stubs, czesc zastapiona w 19b-19h)
 *
 * ordinal 156: GetSystemMenu   - zwroc fake HMENU
 * ordinal 411: AppendMenu      - no-op (TRUE)
 * ordinal 177: LoadAccelerators- zwroc fake HACCEL
 * ordinal 178: TranslateAccelerator - zawsze FALSE
 * ordinal 87:  DialogBox       - stub (bez dialogu)
 * ordinal 88:  EndDialog       - stub
 * ============================================================ */
unsigned __far __pascal GetSystemMenu(HWND hwnd, BOOL bRevert)
{
    (void)bRevert;
    return (unsigned)(0xF000u | (hwnd & 0xFFu));
}

BOOL __far __pascal AppendMenu(unsigned hMenu, unsigned uFlags,
                                unsigned uIDNewItem, const char __far *lpNewItem)
{
    (void)hMenu; (void)uFlags; (void)uIDNewItem; (void)lpNewItem;
    return 1;
}

unsigned __far __pascal LoadAccelerators(unsigned hInst,
                                          const char __far *lpTableName)
{
    unsigned id = (unsigned)((unsigned long)lpTableName & 0xFFFFUL);
    (void)hInst;
    return id ? (unsigned)(0xACE0u | (id & 0x1Fu)) : 0;
}

BOOL __far __pascal TranslateAccelerator(HWND hwnd, unsigned hAccel,
                                          MSG __far *pmsg)
{
    (void)hwnd; (void)hAccel; (void)pmsg;
    return 0;
}

int __far __pascal DialogBox(unsigned hInst, const char __far *lpTemplate,
                               HWND hwndParent, void __far *lpDialogFunc)
{
    (void)hInst; (void)lpTemplate; (void)hwndParent; (void)lpDialogFunc;
    serial_puts("USER: DialogBox stub\n");
    return 0;
}

void __far __pascal EndDialog(HWND hwndDlg, int nResult)
{
    (void)hwndDlg; (void)nResult;
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
