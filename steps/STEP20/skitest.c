/*
 * skitest.c - Test renderowania sprite'ow SKI.EXE
 *
 * Biale tlo, sprite #53 (ekran tytulowy, 105x55) w centrum,
 * sprite #27 (gondola, 64x32) poruszajacy sie poziomo.
 *
 * Pipeline jak w SKI.EXE:
 *   WM_CREATE: SelectObject(dc_tmp, hbm) + BitBlt(dc_sprite, ..., dc_tmp) -> Case 1
 *   Petla:     BitBlt(dc_mem, ..., dc_sprite)  -> Case 2B
 *              BitBlt(screen, ..., dc_mem)      -> Case C
 *              PatBlt(dc_mem, WHITENESS)        -> reset dc_mem (jak w SKI.EXE)
 *
 * Rysujemy: najpierw ekran tytulowy, potem gondole.
 * Gondola powinna byc widoczna na wierzchu ekranu tytulowego.
 */

typedef unsigned short HWND;
typedef unsigned short UINT;
typedef unsigned short WPARAM;
typedef unsigned long  LPARAM;
typedef unsigned short BOOL;
typedef unsigned short HDC;
typedef unsigned short HBITMAP;
typedef long           LRESULT;
typedef const char __far * LPCSTR;

typedef LRESULT (__far __pascal *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#define WM_CREATE   0x0001
#define WM_DESTROY  0x0002
#define WM_PAINT    0x000F
#define WM_QUIT     0x0012

#define SRCCOPY     0x00CC0020UL
#define WHITENESS   0x00FF0062UL
#define PM_NOREMOVE 0
#define WHITE_BRUSH 0

/* Wymiary sprite'ow */
#define TITLE_W     105
#define TITLE_H     55
#define GOND_W      26
#define GOND_H      32
#define MEM_W       128
#define MEM_H       128

/* Pozycje z oryginalnej gry (trace: BB DC6->screen D=176,105, gondola x=195) */
#define TITLE_X     176
#define TITLE_Y     105
#define GOND_X      195

/* ID sprite'ow (= HBITMAP w naszej implementacji) */
#define HBM_TITLE    53
#define HBM_GONDOLA  65   /* gondola jedzie w gore (ySrc=1415 w atlasie DC2) */
#define HBM_GONDOLA2 67   /* gondola jedzie w dol  (ySrc=1479 w atlasie DC2) */

/* x gondoli 2 (z trace: linia kolejki) */
#define GOND2_X     163

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
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
} MSG;

typedef struct {
    int left, top, right, bottom;
} RECT;

typedef struct {
    HDC    hdc;
    BOOL   fErase;
    RECT   rcPaint;
    BOOL   fRestore;
    BOOL   fIncUpdate;
    char   rgbReserved[16];
} PAINTSTRUCT;

/* USER imports */
extern void     __far __pascal OutputDebugString(const char __far *s);
extern BOOL     __far __pascal RegisterClass(const WNDCLASS __far *wc);
extern HWND     __far __pascal CreateWindow(
    const char __far *, const char __far *,
    unsigned long, int, int, int, int,
    HWND, unsigned, unsigned, void __far *);
extern void     __far __pascal PostQuitMessage(int exitCode);
extern BOOL     __far __pascal GetMessage(MSG __far *pmsg, HWND hwnd,
                                           UINT msgMin, UINT msgMax);
extern BOOL     __far __pascal PeekMessage(MSG __far *pmsg, HWND hwnd,
                                            UINT min, UINT max, UINT remove);
extern BOOL     __far __pascal TranslateMessage(const MSG __far *pmsg);
extern LRESULT  __far __pascal DispatchMessage(const MSG __far *pmsg);
extern BOOL     __far __pascal ShowWindow(HWND hwnd, int nCmdShow);
extern BOOL     __far __pascal UpdateWindow(HWND hwnd);
extern HDC      __far __pascal GetDC(HWND hwnd);
extern int      __far __pascal ReleaseDC(HWND hwnd, HDC hdc);
extern BOOL     __far __pascal FillRect(HDC hdc, const RECT __far *lpr, unsigned hbr);
extern LRESULT  __far __pascal DefWindowProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp);
extern HBITMAP  __far __pascal LoadBitmap(unsigned hInst, LPCSTR name);

/* GDI imports */
extern HDC      __far __pascal CreateCompatibleDC(HDC hdc);
extern HBITMAP  __far __pascal CreateCompatibleBitmap(HDC hdc, int w, int h);
extern unsigned __far __pascal SelectObject(HDC hdc, unsigned hobj);
extern BOOL     __far __pascal BitBlt(HDC dst, int xD, int yD, int w, int h,
                                       HDC src, int xS, int yS, unsigned long rop);
extern BOOL     __far __pascal PatBlt(HDC hdc, int x, int y, int w, int h,
                                       unsigned long rop);
extern unsigned __far __pascal GetStockObject(int obj);
extern int      __far __pascal DeleteObject(unsigned hobj);
extern int      __far __pascal DeleteDC(HDC hdc);

unsigned get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];

/* ---- Stan globalny ---- */
static char g_classname[] = "SkiTest";

/* DCs: dc_screen = 1 (screen), dc_tmp = scratch dla Case1,
 * dc_title/dc_gond = pre-zbudowane bufory sprite'ow,
 * dc_mem = kompozytowy (jak DC6 w SKI.EXE) */
static HDC    g_dc_screen;
static HDC    g_dc_tmp;
static HDC    g_dc_title;
static HDC    g_dc_gond;
static HDC    g_dc_mem;

/* Bufory pixelowe (z CreateCompatibleBitmap) */
static HBITMAP g_hbm_title;
static HBITMAP g_hbm_gond;
static HBITMAP g_hbm_mem;

/* Animacja gondoli */
static int g_gond_x;
static int g_gond_y;

/* Gondola 2 (jedzie w dol) */
static HDC    g_dc_gond2;
static HBITMAP g_hbm_gond2;
static int g_gond2_y;

static RECT g_screen_rect;

/* ---- Inicjalizacja DC i sprite'ow w WM_CREATE ---- */
static void setup_dcs(HWND hwnd)
{
    HBITMAP prev_title, prev_gond, prev_hbm_title, prev_hbm_gond;

    g_dc_screen = GetDC(hwnd);  /* zwraca 1 dla glownego okna */

    /* Scratch DC (bez wlasnego bufora - tylko do SelectObject + Case1) */
    g_dc_tmp = CreateCompatibleDC(g_dc_screen);

    /* Dedykowane bufory dla sprite'ow */
    g_dc_title = CreateCompatibleDC(g_dc_screen);
    g_dc_gond  = CreateCompatibleDC(g_dc_screen);
    g_dc_gond2 = CreateCompatibleDC(g_dc_screen);
    g_dc_mem   = CreateCompatibleDC(g_dc_screen);  /* jak DC6 */

    /* Alokuj pixelowe bufory (z XMS) */
    g_hbm_title = CreateCompatibleBitmap(g_dc_screen, TITLE_W, TITLE_H);
    g_hbm_gond  = CreateCompatibleBitmap(g_dc_screen, GOND_W,  GOND_H);
    g_hbm_gond2 = CreateCompatibleBitmap(g_dc_screen, GOND_W,  GOND_H);
    g_hbm_mem   = CreateCompatibleBitmap(g_dc_screen, MEM_W,   MEM_H);

    /* Przypisz bufory do DC przez SelectObject */
    prev_title     = (HBITMAP)SelectObject(g_dc_title, g_hbm_title);
    prev_gond      = (HBITMAP)SelectObject(g_dc_gond,  g_hbm_gond);
    prev_hbm_gond  = (HBITMAP)SelectObject(g_dc_gond2, g_hbm_gond2);
    prev_hbm_title = (HBITMAP)SelectObject(g_dc_mem,   g_hbm_mem);

    (void)prev_title; (void)prev_gond; (void)prev_hbm_gond; (void)prev_hbm_title;

    /* Zaladuj sprite'y do dc_title i dc_gond przez Case1 */
    prev_hbm_gond = (HBITMAP)SelectObject(g_dc_tmp, (unsigned)HBM_TITLE);
    BitBlt(g_dc_title, 0, 0, TITLE_W, TITLE_H, g_dc_tmp, 0, 0, SRCCOPY);

    prev_hbm_gond = (HBITMAP)SelectObject(g_dc_tmp, (unsigned)HBM_GONDOLA);
    BitBlt(g_dc_gond,  0, 0, GOND_W,  GOND_H,  g_dc_tmp, 0, 0, SRCCOPY);

    prev_hbm_gond = (HBITMAP)SelectObject(g_dc_tmp, (unsigned)HBM_GONDOLA2);
    BitBlt(g_dc_gond2, 0, 0, GOND_W,  GOND_H,  g_dc_tmp, 0, 0, SRCCOPY);

    (void)prev_hbm_gond;

    /* Gondola 1: jedzie w gore (z dolu), x stale jak w SKI.EXE */
    g_gond_x = GOND_X;
    g_gond_y = 480;

    /* Gondola 2: jedzie w dol (z gory), x stale, przesunieta w fazie */
    g_gond2_y = -GOND_H;

    g_screen_rect.left   = 0;
    g_screen_rect.top    = 0;
    g_screen_rect.right  = 640;
    g_screen_rect.bottom = 480;

    /* Jednorazowe czyszczenie ekranu (ShowWindow i tak wypelni bielą przed WM_PAINT) */
    PatBlt(g_dc_screen, 0, 0, 640, 480, WHITENESS);
}


/* Krok gondoli [px/klatke]. */
#define GOND_STEP 2

/* Skopiuj fragment dc_title do dc_mem dla bloku ekranowego (bx,by,bw,bh).
 * dc_mem reprezentuje ten blok: offset (0,0) = ekran (bx,by). */
static void copy_title_fragment(int bx, int by, int bw, int bh)
{
    int ix0 = (bx > TITLE_X) ? bx : TITLE_X;
    int iy0 = (by > TITLE_Y) ? by : TITLE_Y;
    int ix1 = (bx+bw < TITLE_X+TITLE_W) ? bx+bw : TITLE_X+TITLE_W;
    int iy1 = (by+bh < TITLE_Y+TITLE_H) ? by+bh : TITLE_Y+TITLE_H;
    int cw = ix1 - ix0;
    int ch = iy1 - iy0;
    if (cw > 0 && ch > 0)
        BitBlt(g_dc_mem, ix0-bx, iy0-by, cw, ch,
               g_dc_title, ix0-TITLE_X, iy0-TITLE_Y, SRCCOPY);
}

/* ---- Renderowanie jednej klatki ---- */
static void render_frame(void)
{
    /* bh = GOND_H + GOND_STEP jak w SKI.EXE (h+2): jeden blitblock pokrywa
     * sprite ORAZ 2 wiersze ogona — jeden zapis na ekran na gondole = brak migania. */
    int bh = GOND_H + GOND_STEP;  /* 34 */
    static int s_init = 0;

    /* Pierwsze wywolanie: ShowWindow() wyczyścil ekran bielą po WM_CREATE,
     * wiec tytul trzeba narysowac tutaj (przed gondolami). */
    if (!s_init) {
        s_init = 1;
        BitBlt(g_dc_mem, 0, 0, TITLE_W, TITLE_H, g_dc_title, 0, 0, SRCCOPY);
        BitBlt(g_dc_screen, TITLE_X, TITLE_Y, TITLE_W, TITLE_H, g_dc_mem, 0, 0, SRCCOPY);
        PatBlt(g_dc_mem, 0, 0, TITLE_W, TITLE_H, WHITENESS);
    }

    /* Gondola 1 (jedzie w gore): blok (g_gond_x, g_gond_y, GOND_W, bh).
     * Gondola na offset dc_mem (0,0), wiersze 32..33 biale — kasuja ogon. */
    PatBlt(g_dc_mem, 0, 0, GOND_W, bh, WHITENESS);
    copy_title_fragment(g_gond_x, g_gond_y, GOND_W, bh);
    BitBlt(g_dc_mem, 0, 0, GOND_W, GOND_H, g_dc_gond, 0, 0, SRCCOPY);
    BitBlt(g_dc_screen, g_gond_x, g_gond_y, GOND_W, bh, g_dc_mem, 0, 0, SRCCOPY);
    PatBlt(g_dc_mem, 0, 0, GOND_W, bh, WHITENESS);

    /* Gondola 2 (jedzie w dol): blok (GOND2_X, g_gond2_y-GOND_STEP, GOND_W, bh).
     * Gondola na offset dc_mem (0, GOND_STEP), wiersze 0..1 biale — kasuja ogon. */
    {
        int by2 = g_gond2_y - GOND_STEP;
        PatBlt(g_dc_mem, 0, 0, GOND_W, bh, WHITENESS);
        copy_title_fragment(GOND2_X, by2, GOND_W, bh);
        BitBlt(g_dc_mem, 0, GOND_STEP, GOND_W, GOND_H, g_dc_gond2, 0, 0, SRCCOPY);
        BitBlt(g_dc_screen, GOND2_X, by2, GOND_W, bh, g_dc_mem, 0, 0, SRCCOPY);
        PatBlt(g_dc_mem, 0, 0, GOND_W, bh, WHITENESS);
    }
}

/* ---- WndProc ---- */
LRESULT __far __pascal WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;

    if (msg == WM_CREATE) {
        setup_dcs(hwnd);
        return 0;
    }

    if (msg == WM_PAINT) {
        /* Petla gry: PeekMessage blokuje na 1 tick IRQ0 (~55ms, 18Hz) */
        for (;;) {
            MSG m;
            if (PeekMessage(&m, 0, 0, 0, PM_NOREMOVE)) {
                if (m.message == WM_QUIT) {
                    PostQuitMessage(0);
                    return 0;
                }
                GetMessage(&m, 0, 0, 0);
                TranslateMessage(&m);
                DispatchMessage(&m);
            } else {
                render_frame();

                /* Gondola 1 jedzie w gore (-2px/klatke jak w SKI.EXE) */
                g_gond_y -= 2;
                if (g_gond_y < -GOND_H) g_gond_y = 480;

                /* Gondola 2 jedzie w dol (+2px/klatke), przesunieta w fazie */
                g_gond2_y += 2;
                if (g_gond2_y > 480) g_gond2_y = -GOND_H;
            }
        }
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ---- Punkt wejscia ---- */
void __far app_entry(void)
{
    HWND  hwnd;
    MSG   msg;
    WNDCLASS wc;

    wc.style          = 0;
    wc.lpfnWndProc    = WndProc;
    wc.cbClsExtra     = 0;
    wc.cbWndExtra     = 0;
    wc.hInstance      = get_ds();
    wc.hIcon          = 0;
    wc.hCursor        = 0;
    wc.hbrBackground  = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName   = 0;
    wc.lpszClassName  = g_classname;

    RegisterClass(&wc);

    hwnd = CreateWindow(
        g_classname, "SkiFree Sprite Test",
        0UL, 0, 0, 640, 480,
        0, 0, get_ds(), 0);

    ShowWindow(hwnd, 1);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
