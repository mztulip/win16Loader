/*
 * i486ss.c - Screensaver with our system flag: the i486 DX microprocessor.
 *
 * Features:
 *   - Silk cloth flag waving in the digital wind with sinusoidal wave dynamics
 *   - Majestic i486 DX CPU emblem:
 *       * Gold Pin Grid Array (PGA) pins on all 4 sides
 *       * Ceramic package with 3D beveled edges & pin 1 notch
 *       * Metallic gold heat spreader lid
 *       * Stylized Intel-style i486 DX markings with custom pixel micro-font
 *       * Gold printed circuit traces radiating across royal navy cloth
 *       * Golden ceremonial embroidered borders, fringe, and corner stars
 *       * "WIN16 LOADER" & "i486 DX CPU" banner lettering
 *   - Golden flagpole with 3D sphere finial and halyard grommets
 *   - Twinkling parallax starfield background
 *   - Smooth bouncing kinematics across screen
 *   - Double-buffered, dirty-rect optimized GDI rendering
 *   - Full screensaver contract: /s (fullscreen), /c (config), /p (preview), /a (password)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "resource.h"

#define APP_NAME            "i486SS"
#define WND_CLASS_NAME      "i486SSWndClass"
#define INI_SECTION         "ScreenSaver.i486SS"
#define INI_FILE            "CONTROL.INI"

#define KEY_SPEED           "Speed"
#define DEFAULT_SPEED       3
#define MIN_SPEED           1
#define MAX_SPEED           10

#define KEY_STARS           "Stars"
#define DEFAULT_STARS       0       /* 0 = czyste czarne tlo (ochrona kineskopu CRT) */
#define MIN_STARS           0
#define MAX_STARS           80

#define KEY_WAVE            "Wave"
#define DEFAULT_WAVE        2
#define MIN_WAVE            1
#define MAX_WAVE            5

#define TIMER_ID            1
#define TIMER_INTERVAL      40      /* ~25 fps smooth animation */
#define PI                  3.14159265358979323846

/* Dimensions of the flag cloth */
#define FLAG_W              168     /* cloth width */
#define FLAG_H              120     /* cloth height */
#define SLICE_W             4       /* width of each waving vertical slice */
#define NUM_SLICES          (FLAG_W / SLICE_W) /* 42 slices */
#define NUM_PHASES          16      /* animation wave cycles */
#define FLAG_TOTAL_W        182     /* flagpole + cloth */
#define FLAG_TOTAL_H        132     /* height including pole overhang */

typedef enum {
    MODE_FULLSCREEN,
    MODE_CONFIG,
    MODE_PREVIEW,
    MODE_PASSWORD
} SS_MODE;

typedef struct {
    int x;
    int y;
    int speed;                      /* 1..3 px/tick - dynamic movement prevents CRT burn-in */
    int type;                       /* 0: faint, 1: bright, 2: cyan, 3: golden star */
} STAR;

/* Global state */
static BOOL     gbFullscreen        = FALSE;
static BOOL     gbIgnoreFirstMove   = TRUE;
static int      gOrigMouseX         = 0;
static int      gOrigMouseY         = 0;
static DWORD    gStartTime          = 0;

static int      gSpeed              = DEFAULT_SPEED;
static int      gNumStars           = DEFAULT_STARS;
static int      gWaveLevel          = DEFAULT_WAVE;

static int      gX = 30, gY = 30;
static int      gOldX = 30, gOldY = 30;
static int      gDX = DEFAULT_SPEED;
static int      gDY = DEFAULT_SPEED;
static int      gWavePhase = 0;

static STAR     gStars[MAX_STARS];

/* Double buffering: screen backbuffer */
static HDC      gHdcMem             = NULL;
static HBITMAP  gHbmMem             = NULL;
static HBITMAP  gHbmMemOld          = NULL;

/* Pre-rendered base flag DC */
static HDC      gHdcFlag            = NULL;
static HBITMAP  gHbmFlag            = NULL;
static HBITMAP  gHbmFlagOld         = NULL;

/* Precalculated integer wave displacement table: [phase][slice] */
static int      s_wave[NUM_PHASES][NUM_SLICES];

/* Function prototypes */
LONG FAR PASCAL WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL ConfigDlgProc(HWND, UINT, WPARAM, LPARAM);

static SS_MODE  ParseCmdLine(LPSTR lpCmdLine, HWND FAR *phwndArg);
static void     LoadSettings(void);
static void     SaveSettings(void);
static void     InitStars(int width, int height);
static void     InitWaveTable(void);
static void     RenderBaseFlag(HDC hdcScreen);
static void     FreeBaseFlag(void);
static void     DrawFlagAt(HDC hdc, int x, int y);
static void     GetFlagRect(int x, int y, RECT FAR *rc);
static void     DrawMiniChar(HDC hdc, int x, int y, char c, COLORREF clr);
static void     DrawMiniString(HDC hdc, int x, int y, const char *s, COLORREF clr);
static void     DrawDigit4(HDC hdc, int x, int y, COLORREF color);
static void     DrawDigit8(HDC hdc, int x, int y, COLORREF color);
static void     DrawDigit6(HDC hdc, int x, int y, COLORREF color);
static void     DrawLetterI(HDC hdc, int x, int y, COLORREF color);
static void     DrawLetterD(HDC hdc, int x, int y, COLORREF color);
static void     DrawLetterX(HDC hdc, int x, int y, COLORREF color);

/* Entry point */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc;
    MSG msg;
    HWND hwnd = NULL;
    HWND hwndArg = NULL;
    SS_MODE mode;

    mode = ParseCmdLine(lpCmdLine, &hwndArg);

    if (mode == MODE_PASSWORD) {
        return 0;
    }

    if (mode == MODE_PREVIEW && hwndArg == NULL) {
        mode = MODE_FULLSCREEN;
    }

    srand((unsigned)GetTickCount());
    LoadSettings();

    if (mode == MODE_CONFIG) {
        FARPROC lpfnDlgProc = MakeProcInstance((FARPROC)ConfigDlgProc, hInstance);
        DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONFIG), hwndArg, (DLGPROC)lpfnDlgProc);
        FreeProcInstance(lpfnDlgProc);
        return 0;
    }

    if (mode == MODE_FULLSCREEN && hPrevInstance != NULL) {
        return 0;
    }

    if (!hPrevInstance) {
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = (WNDPROC)WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
        wc.hCursor       = (mode == MODE_FULLSCREEN) ? NULL : LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = WND_CLASS_NAME;

        if (!RegisterClass(&wc)) {
            return 0;
        }
    }

    if (mode == MODE_PREVIEW) {
        RECT rc;
        gbFullscreen = FALSE;
        GetClientRect(hwndArg, &rc);
        hwnd = CreateWindow(WND_CLASS_NAME, APP_NAME,
                            WS_CHILD | WS_VISIBLE,
                            0, 0, rc.right - rc.left, rc.bottom - rc.top,
                            hwndArg, NULL, hInstance, NULL);
    } else {
        gbFullscreen = TRUE;
        hwnd = CreateWindowEx(WS_EX_TOPMOST,
                              WND_CLASS_NAME, APP_NAME,
                              WS_POPUP,
                              0, 0,
                              GetSystemMetrics(SM_CXSCREEN),
                              GetSystemMetrics(SM_CYSCREEN),
                              NULL, NULL, hInstance, NULL);
    }

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, gbFullscreen ? SW_SHOWNORMAL : nCmdShow);
    if (gbFullscreen) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN),
                     SWP_SHOWWINDOW);
        UpdateWindow(hwnd);
        SetCursor(NULL);
        ShowCursor(FALSE);
    } else {
        UpdateWindow(hwnd);
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

LONG FAR PASCAL WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    PAINTSTRUCT ps;
    RECT rc;

    switch (message) {
    case WM_CREATE:
        {
            HDC hdcScreen = GetDC(hwnd);
            GetClientRect(hwnd, &rc);

            InitStars(rc.right - rc.left, rc.bottom - rc.top);
            InitWaveTable();
            RenderBaseFlag(hdcScreen);

            gHdcMem = CreateCompatibleDC(hdcScreen);
            if (gHdcMem != NULL) {
                gHbmMem = CreateCompatibleBitmap(hdcScreen, rc.right - rc.left, rc.bottom - rc.top);
                if (gHbmMem != NULL) {
                    gHbmMemOld = SelectObject(gHdcMem, gHbmMem);
                    PatBlt(gHdcMem, 0, 0, rc.right - rc.left, rc.bottom - rc.top, BLACKNESS);
                } else {
                    DeleteDC(gHdcMem);
                    gHdcMem = NULL;
                }
            }
            ReleaseDC(hwnd, hdcScreen);
        }
        gStartTime = GetTickCount();
        SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
        return 0;

    case WM_TIMER:
        {
            RECT rcClient;
            int width, height;
            RECT rcOld, rcNew, rcDirty;

            GetClientRect(hwnd, &rcClient);
            width = rcClient.right - rcClient.left;
            height = rcClient.bottom - rcClient.top;

            if (width > FLAG_TOTAL_W + 20 && height > FLAG_TOTAL_H + 20) {
                gX += gDX;
                gY += gDY;

                if (gX <= 5) {
                    gX = 5;
                    gDX = gSpeed;
                } else if (gX + FLAG_TOTAL_W >= width - 5) {
                    gX = width - 5 - FLAG_TOTAL_W;
                    gDX = -gSpeed;
                }

                if (gY <= 15) {
                    gY = 15;
                    gDY = gSpeed;
                } else if (gY + FLAG_TOTAL_H >= height - 15) {
                    gY = height - 15 - FLAG_TOTAL_H;
                    gDY = -gSpeed;
                }
            }

            gWavePhase = (gWavePhase + 1) % NUM_PHASES;

            if (gNumStars > 0) {
                int i;
                for (i = 0; i < gNumStars; i++) {
                    gStars[i].x -= gStars[i].speed;
                    if (gStars[i].x < 0) {
                        gStars[i].x = width + (rand() % 16);
                        gStars[i].y = rand() % height;
                        gStars[i].speed = (rand() % 3) + 1;
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                /* Invalidate dirty rect covering old and new flag positions */
                GetFlagRect(gOldX, gOldY, &rcOld);
                GetFlagRect(gX, gY, &rcNew);
                UnionRect(&rcDirty, &rcOld, &rcNew);

                gOldX = gX;
                gOldY = gY;

                InvalidateRect(hwnd, &rcDirty, FALSE);
            }
        }
        return 0;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);

        if (gHdcMem != NULL && gHdcFlag != NULL) {
            HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);

            if (gNumStars == 0) {
                /* Pure black void - zero CRT phosphor burn-in */
                FillRect(gHdcMem, &ps.rcPaint, hbrBlack);
                DrawFlagAt(gHdcMem, gX, gY);
                BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
                       ps.rcPaint.right - ps.rcPaint.left,
                       ps.rcPaint.bottom - ps.rcPaint.top,
                       gHdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            } else {
                /* Dynamic moving stars: all stars move continuously, preventing burn-in */
                int i;
                FillRect(gHdcMem, &rc, hbrBlack);

                for (i = 0; i < gNumStars; i++) {
                    int sx = gStars[i].x;
                    int sy = gStars[i].y;
                    COLORREF clr;
                    switch (gStars[i].type) {
                    case 1:  clr = RGB(240, 245, 255); break;
                    case 2:  clr = RGB(140, 200, 255); break;
                    case 3:  clr = RGB(255, 220, 110); break;
                    default: clr = RGB(110, 120, 140); break;
                    }
                    SetPixel(gHdcMem, sx, sy, clr);
                    if (gStars[i].speed >= 2) {
                        /* Motion blur tail */
                        SetPixel(gHdcMem, sx + 1, sy, RGB(70, 80, 95));
                    }
                }

                DrawFlagAt(gHdcMem, gX, gY);
                BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                       gHdcMem, 0, 0, SRCCOPY);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETCURSOR:
        if (gbFullscreen) {
            SetCursor(NULL);
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
        if (gbFullscreen) {
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);
            if (gbIgnoreFirstMove) {
                gOrigMouseX = mx;
                gOrigMouseY = my;
                gbIgnoreFirstMove = FALSE;
            } else if (abs(mx - gOrigMouseX) > 8 || abs(my - gOrigMouseY) > 8) {
                DestroyWindow(hwnd);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (gbFullscreen) {
            if (GetTickCount() - gStartTime > 500) {
                DestroyWindow(hwnd);
            }
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (gbFullscreen) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1L;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        if (gHdcMem != NULL) {
            SelectObject(gHdcMem, gHbmMemOld);
            DeleteObject(gHbmMem);
            DeleteDC(gHdcMem);
            gHdcMem = NULL;
        }
        FreeBaseFlag();
        if (gbFullscreen) {
            ShowCursor(TRUE);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/* Calculate flag bounding rectangle for dirty invalidation */
static void GetFlagRect(int x, int y, RECT FAR *rc)
{
    int maxAmp = gWaveLevel * 3;
    rc->left   = x - 4;
    rc->top    = y - maxAmp - 8;
    rc->right  = x + FLAG_TOTAL_W + 8;
    rc->bottom = y + FLAG_TOTAL_H + maxAmp + 10;
}

/* Initialize wave displacement table */
static void InitWaveTable(void)
{
    int p, s;
    double maxAmp = (double)(gWaveLevel * 3); /* 3 to 15 px */

    for (p = 0; p < NUM_PHASES; p++) {
        double phaseRad = (double)p * (2.0 * PI / (double)NUM_PHASES);
        for (s = 0; s < NUM_SLICES; s++) {
            /* Left end attached to flagpole (minimal amplitude), right fly end waves fully */
            double amp = maxAmp * (double)(s + 2) / (double)NUM_SLICES;
            double angle = (double)s * 0.28 - phaseRad;
            s_wave[p][s] = (int)(sin(angle) * amp);
        }
    }
}

/* Initialize starry space background */
static void InitStars(int width, int height)
{
    int i;
    if (width <= 0) width = 640;
    if (height <= 0) height = 480;

    for (i = 0; i < MAX_STARS; i++) {
        gStars[i].x = rand() % width;
        gStars[i].y = rand() % height;
        gStars[i].speed = (rand() % 3) + 1; /* 1..3 px/tick dynamic speed */
        if (i % 8 == 0) {
            gStars[i].type = 3; /* golden sparkle */
        } else if (i % 4 == 0) {
            gStars[i].type = 2; /* cyan */
        } else if (i % 2 == 0) {
            gStars[i].type = 1; /* bright white */
        } else {
            gStars[i].type = 0; /* dim */
        }
    }
}

/* 3x5 Pixel Micro-Font Renderer for vintage silicon laser markings */
static void DrawMiniChar(HDC hdc, int x, int y, char c, COLORREF clr)
{
    WORD glyph = 0;
    int r, col;

    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
    case '0': glyph = 0x7B6F; break;
    case '1': glyph = 0x2492; break;
    case '2': glyph = 0x73E7; break;
    case '3': glyph = 0x73CF; break;
    case '4': glyph = 0x5BC9; break;
    case '5': glyph = 0x79CF; break;
    case '6': glyph = 0x79EF; break;
    case '7': glyph = 0x7249; break;
    case '8': glyph = 0x7BEF; break;
    case '9': glyph = 0x7BCF; break;
    case 'A': glyph = 0x2BEB; break;
    case 'B': glyph = 0x7AEB; break;
    case 'C': glyph = 0x7927; break;
    case 'D': glyph = 0x76EB; break;
    case 'E': glyph = 0x79E7; break;
    case 'H': glyph = 0x5BE9; break;
    case 'I': glyph = 0x7497; break;
    case 'L': glyph = 0x4927; break;
    case 'M': glyph = 0x5F69; break;
    case 'N': glyph = 0x5B5D; break;
    case 'O': glyph = 0x7B6F; break;
    case 'P': glyph = 0x7BE4; break;
    case 'R': glyph = 0x7BEB; break;
    case 'S': glyph = 0x79CF; break;
    case 'T': glyph = 0x7492; break;
    case 'U': glyph = 0x5B6F; break;
    case 'W': glyph = 0x5B75; break;
    case 'X': glyph = 0x5A8B; break;
    case 'Z': glyph = 0x72A7; break;
    case '-': glyph = 0x01C0; break;
    default:  return;
    }

    for (r = 0; r < 5; r++) {
        for (col = 0; col < 3; col++) {
            int shift = (4 - r) * 3 + (2 - col);
            if ((glyph >> shift) & 1) {
                SetPixel(hdc, x + col, y + r, clr);
            }
        }
    }
}

static void DrawMiniString(HDC hdc, int x, int y, const char *s, COLORREF clr)
{
    while (*s) {
        if (*s != ' ') {
            DrawMiniChar(hdc, x, y, *s, clr);
        }
        x += 4;
        s++;
    }
}

/* Large stylized i486 typography */
static void DrawLetterI(HDC hdc, int x, int y, COLORREF color)
{
    int cy;
    SetPixel(hdc, x + 1, y, color);
    for (cy = y + 2; cy <= y + 7; cy++) {
        SetPixel(hdc, x + 1, cy, color);
    }
    SetPixel(hdc, x, y + 2, color);
    SetPixel(hdc, x, y + 7, color);
    SetPixel(hdc, x + 2, y + 7, color);
}

static void DrawDigit4(HDC hdc, int x, int y, COLORREF color)
{
    int cy, cx;
    for (cy = y; cy <= y + 4; cy++) {
        SetPixel(hdc, x, cy, color);
    }
    for (cx = x; cx <= x + 5; cx++) {
        SetPixel(hdc, cx, y + 4, color);
    }
    for (cy = y; cy <= y + 7; cy++) {
        SetPixel(hdc, x + 4, cy, color);
    }
}

static void DrawDigit8(HDC hdc, int x, int y, COLORREF color)
{
    int cy;
    SetPixel(hdc, x + 1, y, color);
    SetPixel(hdc, x + 2, y, color);
    SetPixel(hdc, x + 3, y, color);
    SetPixel(hdc, x + 1, y + 3, color);
    SetPixel(hdc, x + 2, y + 3, color);
    SetPixel(hdc, x + 3, y + 3, color);
    SetPixel(hdc, x + 1, y + 7, color);
    SetPixel(hdc, x + 2, y + 7, color);
    SetPixel(hdc, x + 3, y + 7, color);
    for (cy = y + 1; cy <= y + 2; cy++) SetPixel(hdc, x, cy, color);
    for (cy = y + 4; cy <= y + 6; cy++) SetPixel(hdc, x, cy, color);
    for (cy = y + 1; cy <= y + 2; cy++) SetPixel(hdc, x + 4, cy, color);
    for (cy = y + 4; cy <= y + 6; cy++) SetPixel(hdc, x + 4, cy, color);
}

static void DrawDigit6(HDC hdc, int x, int y, COLORREF color)
{
    int cy;
    SetPixel(hdc, x + 1, y, color);
    SetPixel(hdc, x + 2, y, color);
    SetPixel(hdc, x + 3, y, color);
    SetPixel(hdc, x + 1, y + 3, color);
    SetPixel(hdc, x + 2, y + 3, color);
    SetPixel(hdc, x + 3, y + 3, color);
    SetPixel(hdc, x + 1, y + 7, color);
    SetPixel(hdc, x + 2, y + 7, color);
    SetPixel(hdc, x + 3, y + 7, color);
    for (cy = y + 1; cy <= y + 6; cy++) SetPixel(hdc, x, cy, color);
    for (cy = y + 4; cy <= y + 6; cy++) SetPixel(hdc, x + 4, cy, color);
}

static void DrawLetterD(HDC hdc, int x, int y, COLORREF color)
{
    int cy;
    for (cy = y; cy <= y + 6; cy++) SetPixel(hdc, x, cy, color);
    SetPixel(hdc, x + 1, y, color);
    SetPixel(hdc, x + 2, y, color);
    SetPixel(hdc, x + 1, y + 6, color);
    SetPixel(hdc, x + 2, y + 6, color);
    for (cy = y + 1; cy <= y + 5; cy++) SetPixel(hdc, x + 3, cy, color);
}

static void DrawLetterX(HDC hdc, int x, int y, COLORREF color)
{
    int cy;
    for (cy = 0; cy <= 6; cy++) {
        int x1 = x + (cy * 4) / 6;
        int x2 = x + 4 - (cy * 4) / 6;
        SetPixel(hdc, x1, y + cy, color);
        SetPixel(hdc, x2, y + cy, color);
    }
}

/* Pre-render the entire base flag once onto an offscreen compatible bitmap */
static void RenderBaseFlag(HDC hdcScreen)
{
    HDC hdc;
    HBRUSH hbrNavy, hbrCeramic, hbrLid, hbrOld;
    HPEN hpenGold, hpenLightGold, hpenTrace, hpenOld;
    int p, i;

    gHdcFlag = CreateCompatibleDC(hdcScreen);
    if (!gHdcFlag) return;

    gHbmFlag = CreateCompatibleBitmap(hdcScreen, FLAG_TOTAL_W + 10, FLAG_H + 20);
    if (!gHbmFlag) {
        DeleteDC(gHdcFlag);
        gHdcFlag = NULL;
        return;
    }
    gHbmFlagOld = SelectObject(gHdcFlag, gHbmFlag);
    hdc = gHdcFlag;

    /* Fill background with transparent black */
    PatBlt(hdc, 0, 0, FLAG_TOTAL_W + 10, FLAG_H + 20, BLACKNESS);

    /* 1. Base cloth: Royal Navy Blue */
    hbrNavy = CreateSolidBrush(RGB(12, 26, 68));
    hpenGold = CreatePen(PS_SOLID, 1, RGB(220, 180, 40));
    hbrOld = SelectObject(hdc, hbrNavy);
    hpenOld = SelectObject(hdc, hpenGold);

    Rectangle(hdc, 8, 4, 8 + FLAG_W, 4 + FLAG_H);

    /* 2. Double gold embroidered borders */
    hpenLightGold = CreatePen(PS_SOLID, 1, RGB(255, 220, 80));
    SelectObject(hdc, hpenLightGold);
    Rectangle(hdc, 12, 8, 8 + FLAG_W - 4, 4 + FLAG_H - 4);

    /* Fringe along the top, bottom, and right edge */
    for (i = 14; i < 8 + FLAG_W - 4; i += 3) {
        SetPixel(hdc, i, 5, RGB(240, 200, 60));
        SetPixel(hdc, i, 4 + FLAG_H - 2, RGB(240, 200, 60));
    }
    for (i = 10; i < 4 + FLAG_H - 4; i += 3) {
        SetPixel(hdc, 8 + FLAG_W - 2, i, RGB(240, 200, 60));
    }

    /* 3. Corner star ornaments */
    {
        int cx[4], cy[4];
        cx[0] = 16; cx[1] = 8 + FLAG_W - 8; cx[2] = 16; cx[3] = 8 + FLAG_W - 8;
        cy[0] = 12; cy[1] = 12; cy[2] = 4 + FLAG_H - 8; cy[3] = 4 + FLAG_H - 8;
        for (i = 0; i < 4; i++) {
            SetPixel(hdc, cx[i], cy[i], RGB(255, 235, 120));
            SetPixel(hdc, cx[i] - 1, cy[i], RGB(210, 170, 40));
            SetPixel(hdc, cx[i] + 1, cy[i], RGB(210, 170, 40));
            SetPixel(hdc, cx[i], cy[i] - 1, RGB(210, 170, 40));
            SetPixel(hdc, cx[i], cy[i] + 1, RGB(210, 170, 40));
        }
    }

    /* 4. Golden printed circuit board traces radiating from CPU */
    hpenTrace = CreatePen(PS_SOLID, 1, RGB(160, 130, 30));
    SelectObject(hdc, hpenTrace);

    /* Top-left traces */
    MoveTo(hdc, 60, 42); LineTo(hdc, 38, 42); LineTo(hdc, 26, 30);
    Ellipse(hdc, 24, 28, 28, 32);
    MoveTo(hdc, 75, 24); LineTo(hdc, 75, 16);
    Ellipse(hdc, 73, 14, 77, 18);

    /* Bottom-left traces */
    MoveTo(hdc, 60, 84); LineTo(hdc, 38, 84); LineTo(hdc, 26, 96);
    Ellipse(hdc, 24, 94, 28, 98);
    MoveTo(hdc, 75, 96); LineTo(hdc, 75, 104);
    Ellipse(hdc, 73, 102, 77, 106);

    /* Top-right traces */
    MoveTo(hdc, 122, 42); LineTo(hdc, 144, 42); LineTo(hdc, 156, 30);
    Ellipse(hdc, 154, 28, 158, 32);
    MoveTo(hdc, 107, 24); LineTo(hdc, 107, 16);
    Ellipse(hdc, 105, 14, 109, 18);

    /* Bottom-right traces */
    MoveTo(hdc, 122, 84); LineTo(hdc, 144, 84); LineTo(hdc, 156, 96);
    Ellipse(hdc, 154, 94, 158, 98);
    MoveTo(hdc, 107, 96); LineTo(hdc, 107, 104);
    Ellipse(hdc, 105, 102, 109, 106);

    /* 5. Center i486 DX Microprocessor */
    /* CPU center is at (91, 62) */
    /* Outer PGA Gold Pins (8 pins per side) */
    hpenGold = CreatePen(PS_SOLID, 1, RGB(255, 215, 60));
    SelectObject(hdc, hpenGold);

    /* Top pins */
    for (p = 68; p <= 114; p += 6) {
        MoveTo(hdc, p, 24); LineTo(hdc, p, 30);
        SetPixel(hdc, p, 23, RGB(255, 240, 140));
    }
    /* Bottom pins */
    for (p = 68; p <= 114; p += 6) {
        MoveTo(hdc, p, 94); LineTo(hdc, p, 100);
        SetPixel(hdc, p, 100, RGB(255, 240, 140));
    }
    /* Left pins */
    for (p = 39; p <= 85; p += 6) {
        MoveTo(hdc, 55, p); LineTo(hdc, 61, p);
        SetPixel(hdc, 54, p, RGB(255, 240, 140));
    }
    /* Right pins */
    for (p = 39; p <= 85; p += 6) {
        MoveTo(hdc, 121, p); LineTo(hdc, 127, p);
        SetPixel(hdc, 127, p, RGB(255, 240, 140));
    }

    /* Ceramic Body: Dark slate ceramic (60x64 pixels from 61 to 121, 30 to 94) */
    hbrCeramic = CreateSolidBrush(RGB(35, 36, 44));
    SelectObject(hdc, hbrCeramic);
    SelectObject(hdc, GetStockObject(NULL_PEN));
    Rectangle(hdc, 61, 30, 121, 94);

    /* Ceramic 3D Bevel: top/left highlight, bottom/right shadow */
    {
        HPEN hpenBevelHi = CreatePen(PS_SOLID, 1, RGB(90, 95, 110));
        HPEN hpenBevelLo = CreatePen(PS_SOLID, 1, RGB(16, 17, 22));
        SelectObject(hdc, hpenBevelHi);
        MoveTo(hdc, 61, 93); LineTo(hdc, 61, 30); LineTo(hdc, 120, 30);
        SelectObject(hdc, hpenBevelLo);
        MoveTo(hdc, 120, 30); LineTo(hdc, 120, 93); LineTo(hdc, 60, 93);
        SelectObject(hdc, hpenOld);
        DeleteObject(hpenBevelHi);
        DeleteObject(hpenBevelLo);
    }

    /* Pin 1 orientation notch: gold triangle in top-left */
    {
        POINT tri[3];
        HBRUSH hbrGoldPin = CreateSolidBrush(RGB(255, 215, 50));
        SelectObject(hdc, hbrGoldPin);
        tri[0].x = 64; tri[0].y = 33;
        tri[1].x = 70; tri[1].y = 33;
        tri[2].x = 64; tri[2].y = 39;
        Polygon(hdc, tri, 3);
        DeleteObject(hbrGoldPin);
    }

    /* Metallic Gold Die Lid / Heat Spreader (36x36 pixels: 73 to 109, 44 to 80) */
    hbrLid = CreateSolidBrush(RGB(208, 172, 54));
    SelectObject(hdc, hbrLid);
    Rectangle(hdc, 73, 44, 109, 80);

    /* Inset 3D border of lid */
    {
        HPEN hpenLidHi = CreatePen(PS_SOLID, 1, RGB(255, 235, 120));
        HPEN hpenLidLo = CreatePen(PS_SOLID, 1, RGB(130, 105, 25));
        SelectObject(hdc, hpenLidLo);
        MoveTo(hdc, 73, 79); LineTo(hdc, 73, 44); LineTo(hdc, 108, 44);
        SelectObject(hdc, hpenLidHi);
        MoveTo(hdc, 108, 44); LineTo(hdc, 108, 79); LineTo(hdc, 72, 79);
        DeleteObject(hpenLidHi);
        DeleteObject(hpenLidLo);
    }

    /* Laser etched markings on ceramic and gold lid */
    /* "intel" above lid on ceramic in crisp silver micro-font */
    DrawMiniString(hdc, 81, 35, "INTEL", RGB(225, 230, 240));

    /* Microprocessor label on gold lid: i486 */
    DrawLetterI(hdc, 77, 47, RGB(20, 24, 35));
    DrawDigit4(hdc, 82, 47, RGB(20, 24, 35));
    DrawDigit8(hdc, 89, 47, RGB(20, 24, 35));
    DrawDigit6(hdc, 96, 47, RGB(20, 24, 35));

    /* "DX" in royal blue badge */
    {
        HBRUSH hbrBadge = CreateSolidBrush(RGB(18, 50, 110));
        SelectObject(hdc, hbrBadge);
        Rectangle(hdc, 83, 57, 99, 66);
        DrawLetterD(hdc, 85, 58, RGB(255, 235, 100));
        DrawLetterX(hdc, 91, 58, RGB(255, 235, 100));
        DeleteObject(hbrBadge);
    }

    /* "33 MHz" micro-font at bottom of gold lid */
    DrawMiniString(hdc, 78, 70, "33 MHZ", RGB(40, 32, 16));

    /* "LOADER 16" on ceramic below lid */
    DrawMiniString(hdc, 75, 84, "WIN-16", RGB(220, 225, 240));

    /* 6. Gold banner inscriptions on cloth */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 220, 80));
    TextOut(hdc, 38, 10, "* WIN16 LOADER *", 16);
    TextOut(hdc, 38, 104, "* i486 DX CPU  *", 16);

    /* Cleanup GDI objects */
    SelectObject(hdc, hbrOld);
    SelectObject(hdc, hpenOld);
    DeleteObject(hbrNavy);
    DeleteObject(hbrCeramic);
    DeleteObject(hbrLid);
    DeleteObject(hpenGold);
    DeleteObject(hpenLightGold);
    DeleteObject(hpenTrace);
}

static void FreeBaseFlag(void)
{
    if (gHdcFlag != NULL) {
        SelectObject(gHdcFlag, gHbmFlagOld);
        DeleteObject(gHbmFlag);
        DeleteDC(gHdcFlag);
        gHdcFlag = NULL;
    }
}

/* Draw waving flag with flagpole at destination coordinates */
static void DrawFlagAt(HDC hdc, int x, int y)
{
    int s;
    HBRUSH hbrGoldBall, hbrOld;
    HPEN hpenPole, hpenOld;

    if (!gHdcFlag) return;

    /* 1. Blit each vertical slice with sinusoidal wave displacement */
    for (s = 0; s < NUM_SLICES; s++) {
        int srcX = 8 + s * SLICE_W;
        int dstX = x + srcX;
        int dy   = s_wave[gWavePhase][s];

        BitBlt(hdc, dstX, y + dy, SLICE_W, FLAG_H + 10,
               gHdcFlag, srcX, 4, SRCCOPY);
    }

    /* 2. Draw Golden Flagpole */
    /* Flagpole shaft: 4 pixels wide at x + 4 .. x + 7 */
    for (s = y - 2; s <= y + FLAG_H + 12; s++) {
        SetPixel(hdc, x + 4, s, RGB(140, 110, 25));  /* shadow edge */
        SetPixel(hdc, x + 5, s, RGB(255, 235, 110)); /* shiny brass highlight */
        SetPixel(hdc, x + 6, s, RGB(215, 175, 45));  /* mid gold */
        SetPixel(hdc, x + 7, s, RGB(120, 95, 20));   /* shadow edge */
    }

    /* Top Finial: Polished golden ball with specular highlight */
    hbrGoldBall = CreateSolidBrush(RGB(235, 195, 50));
    hpenPole    = CreatePen(PS_SOLID, 1, RGB(130, 100, 20));
    hbrOld  = SelectObject(hdc, hbrGoldBall);
    hpenOld = SelectObject(hdc, hpenPole);

    Ellipse(hdc, x + 1, y - 10, x + 11, y);
    /* Specular white reflection on the finial ball */
    SetPixel(hdc, x + 4, y - 7, RGB(255, 255, 255));
    SetPixel(hdc, x + 5, y - 7, RGB(255, 255, 255));
    SetPixel(hdc, x + 4, y - 6, RGB(255, 255, 255));

    /* Bottom flagpole cap */
    Rectangle(hdc, x + 2, y + FLAG_H + 12, x + 10, y + FLAG_H + 15);

    /* Brass mounting rings (grommets) connecting cloth to pole */
    hpenPole = CreatePen(PS_SOLID, 1, RGB(255, 220, 80));
    SelectObject(hdc, hpenPole);
    MoveTo(hdc, x + 6, y + 8);  LineTo(hdc, x + 9, y + 8);
    MoveTo(hdc, x + 6, y + FLAG_H); LineTo(hdc, x + 9, y + FLAG_H);

    SelectObject(hdc, hbrOld);
    SelectObject(hdc, hpenOld);
    DeleteObject(hbrGoldBall);
    DeleteObject(hpenPole);
}

/* Parse screensaver command line */
static WORD ParseWord(LPSTR p)
{
    WORD val = 0;
    while (*p >= '0' && *p <= '9') {
        val = (WORD)(val * 10 + (*p - '0'));
        p++;
    }
    return val;
}

static SS_MODE ParseCmdLine(LPSTR lpCmdLine, HWND FAR *phwndArg)
{
    LPSTR p = lpCmdLine;
    char sw;

    *phwndArg = NULL;

    while (*p == ' ' || *p == '\t') p++;

    if (*p == '\0' || (*p != '/' && *p != '-')) {
        return MODE_FULLSCREEN;
    }

    p++;
    sw = *p;
    if (sw >= 'A' && sw <= 'Z') {
        sw = (char)(sw - 'A' + 'a');
    }
    if (*p != '\0') p++;

    while (*p == ':' || *p == ' ' || *p == '\t') p++;

    if (*p != '\0') {
        *phwndArg = (HWND)ParseWord(p);
    }

    switch (sw) {
    case 'c': return MODE_CONFIG;
    case 'p': return MODE_PREVIEW;
    case 'a': return MODE_PASSWORD;
    case 's':
    default:  return MODE_FULLSCREEN;
    }
}

static void LoadSettings(void)
{
    gSpeed = GetPrivateProfileInt(INI_SECTION, KEY_SPEED, DEFAULT_SPEED, INI_FILE);
    if (gSpeed < MIN_SPEED) gSpeed = MIN_SPEED;
    if (gSpeed > MAX_SPEED) gSpeed = MAX_SPEED;
    gDX = gSpeed;
    gDY = gSpeed;

    gNumStars = GetPrivateProfileInt(INI_SECTION, KEY_STARS, DEFAULT_STARS, INI_FILE);
    if (gNumStars < MIN_STARS) gNumStars = MIN_STARS;
    if (gNumStars > MAX_STARS) gNumStars = MAX_STARS;

    gWaveLevel = GetPrivateProfileInt(INI_SECTION, KEY_WAVE, DEFAULT_WAVE, INI_FILE);
    if (gWaveLevel < MIN_WAVE) gWaveLevel = MIN_WAVE;
    if (gWaveLevel > MAX_WAVE) gWaveLevel = MAX_WAVE;
}

static void SaveSettings(void)
{
    char buf[16];
    sprintf(buf, "%d", gSpeed);
    WritePrivateProfileString(INI_SECTION, KEY_SPEED, buf, INI_FILE);
    sprintf(buf, "%d", gNumStars);
    WritePrivateProfileString(INI_SECTION, KEY_STARS, buf, INI_FILE);
    sprintf(buf, "%d", gWaveLevel);
    WritePrivateProfileString(INI_SECTION, KEY_WAVE, buf, INI_FILE);
}

static void UpdateStarsLabel(HWND hDlg, int stars)
{
    char buf[24];
    if (stars <= 0) {
        SetDlgItemText(hDlg, IDC_STARS_VAL, "Brak");
    } else {
        sprintf(buf, "%d (ruch)", stars);
        SetDlgItemText(hDlg, IDC_STARS_VAL, buf);
    }
}

BOOL FAR PASCAL ConfigDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        SetScrollRange(GetDlgItem(hDlg, IDC_SPEED), SB_CTL, MIN_SPEED, MAX_SPEED, FALSE);
        SetScrollPos(GetDlgItem(hDlg, IDC_SPEED), SB_CTL, gSpeed, TRUE);
        SetDlgItemInt(hDlg, IDC_SPEED_VAL, (UINT)gSpeed, FALSE);

        SetScrollRange(GetDlgItem(hDlg, IDC_STARS), SB_CTL, MIN_STARS, MAX_STARS, FALSE);
        SetScrollPos(GetDlgItem(hDlg, IDC_STARS), SB_CTL, gNumStars, TRUE);
        UpdateStarsLabel(hDlg, gNumStars);

        SetScrollRange(GetDlgItem(hDlg, IDC_WAVE), SB_CTL, MIN_WAVE, MAX_WAVE, FALSE);
        SetScrollPos(GetDlgItem(hDlg, IDC_WAVE), SB_CTL, gWaveLevel, TRUE);
        SetDlgItemInt(hDlg, IDC_WAVE_VAL, (UINT)gWaveLevel, FALSE);
        return TRUE;

    case WM_HSCROLL:
        {
            HWND hwndScroll = (HWND)HIWORD(lParam);
            int pos = GetScrollPos(hwndScroll, SB_CTL);
            int minVal = 1, maxVal = 10, valId = 0;

            if (hwndScroll == GetDlgItem(hDlg, IDC_SPEED)) {
                minVal = MIN_SPEED; maxVal = MAX_SPEED; valId = IDC_SPEED_VAL;
            } else if (hwndScroll == GetDlgItem(hDlg, IDC_STARS)) {
                minVal = MIN_STARS; maxVal = MAX_STARS; valId = IDC_STARS_VAL;
            } else if (hwndScroll == GetDlgItem(hDlg, IDC_WAVE)) {
                minVal = MIN_WAVE; maxVal = MAX_WAVE; valId = IDC_WAVE_VAL;
            }

            switch (wParam) {
            case SB_LINELEFT:  pos--; break;
            case SB_LINERIGHT: pos++; break;
            case SB_PAGELEFT:  pos -= 5; break;
            case SB_PAGERIGHT: pos += 5; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                pos = (int)LOWORD(lParam);
                break;
            }

            if (pos < minVal) pos = minVal;
            if (pos > maxVal) pos = maxVal;

            SetScrollPos(hwndScroll, SB_CTL, pos, TRUE);
            if (valId == IDC_STARS_VAL) {
                UpdateStarsLabel(hDlg, pos);
            } else if (valId) {
                SetDlgItemInt(hDlg, valId, (UINT)pos, FALSE);
            }
        }
        return 0;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            gSpeed = GetScrollPos(GetDlgItem(hDlg, IDC_SPEED), SB_CTL);
            gNumStars = GetScrollPos(GetDlgItem(hDlg, IDC_STARS), SB_CTL);
            gWaveLevel = GetScrollPos(GetDlgItem(hDlg, IDC_WAVE), SB_CTL);
            SaveSettings();
            EndDialog(hDlg, TRUE);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
