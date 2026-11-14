/* paint.c - PaintCraft Studio: Modern Pixel Art & Drawing Application
 *
 * Features:
 * - Double-buffered GDI canvas (zero flicker)
 * - Real-time vector preview (Rubber-banding on hdcTemp)
 * - 14 creative tools: Pencil, Brush, Airbrush, Eraser, Color Eraser,
 *   Fill Bucket, Line, Rectangle, Rounded Rect, Ellipse, Polygon,
 *   Eyedropper, Text, Selection
 * - Contemporary 28-color palette with Dual-Color indicator (LMB: Primary, RMB: Background)
 * - Undo support (Ctrl+Z)
 * - Canvas transformations: Invert (Negative), Clear, Flip Horizontal, Flip Vertical
 * - Direct 24-bit Windows Bitmap (BMP) loading and saving
 * - Fully resizable window with maximize & minimize support
 */

#include <windows.h>
#include <commdlg.h>
#include <string.h>
#include <stdlib.h>

#define TOOL_PENCIL       1
#define TOOL_BRUSH        2
#define TOOL_AIRBRUSH     3
#define TOOL_ERASER       4
#define TOOL_COLOR_ERASER 5
#define TOOL_FILL         6
#define TOOL_LINE         7
#define TOOL_RECT         8
#define TOOL_ROUNDRECT    9
#define TOOL_ELLIPSE      10
#define TOOL_POLYGON      11
#define TOOL_EYEDROPPER   12
#define TOOL_TEXT         13
#define TOOL_SELECT       14
#define TOOL_COUNT        14

#define STYLE_OUTLINE       1
#define STYLE_FILLED_BORDER 2
#define STYLE_FILLED_SOLID  3

#define CANVAS_DEF_W 580
#define CANVAS_DEF_H 310
#define CANVAS_X     10
#define CANVAS_Y     88

#define IDM_NEW       1001
#define IDM_OPEN      1002
#define IDM_SAVE      1003
#define IDM_SAVEAS    1004
#define IDM_EXIT      1005

#define IDM_UNDO      1010
#define IDM_CLEAR     1011
#define IDM_INVERT    1012
#define IDM_FLIPH     1013
#define IDM_FLIPV     1014

#define IDM_ABOUT     1020

#pragma pack(push, 1)
typedef struct {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1;
    WORD  bfReserved2;
    DWORD bfOffBits;
} BMPFILEHDR;

typedef struct {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BMPINFOHDR;
#pragma pack(pop)

typedef struct {
    RECT r;
    int  id;
    char name[8];
} TOOLBTN;

static HINSTANCE g_instance;
static HWND      g_mainwnd;
static char      g_curfile[128];
static char      g_titlebuf[160];
static char      g_statustext[128];

/* GDI Double Buffering */
static int     g_canvas_w = CANVAS_DEF_W;
static int     g_canvas_h = CANVAS_DEF_H;
static HDC     g_hdc_canvas = NULL;
static HBITMAP g_hbmp_canvas = NULL;
static HBITMAP g_hbmp_old_canvas = NULL;

static HDC     g_hdc_undo = NULL;
static HBITMAP g_hbmp_undo = NULL;
static HBITMAP g_hbmp_old_undo = NULL;
static BOOL    g_can_undo = FALSE;

static HDC     g_hdc_temp = NULL;
static HBITMAP g_hbmp_temp = NULL;
static HBITMAP g_hbmp_old_temp = NULL;

/* Tool States */
static int      g_active_tool = TOOL_PENCIL;
static int      g_brush_size = 2;
static int      g_shape_style = STYLE_OUTLINE;
static COLORREF g_col_primary = RGB(0, 0, 0);
static COLORREF g_col_secondary = RGB(255, 255, 255);

static BOOL g_is_drawing = FALSE;
static BOOL g_is_dragging = FALSE;
static int  g_start_x, g_start_y;
static int  g_last_x, g_last_y;
static int  g_cur_x, g_cur_y;

/* Polygon */
static POINT g_poly_pts[32];
static int   g_poly_count = 0;

/* UI Elements */
static TOOLBTN  g_tools[TOOL_COUNT + 1];
static COLORREF g_palette[28];
static unsigned g_rand_seed = 12345;

static unsigned simple_rand(void)
{
    g_rand_seed = (unsigned)(g_rand_seed * 25173u + 13849u);
    return g_rand_seed;
}

static void set_status(const char *text)
{
    RECT r;
    lstrcpy(g_statustext, text);
    GetClientRect(g_mainwnd, &r);
    r.top = r.bottom - 22;
    InvalidateRect(g_mainwnd, &r, TRUE);
}

static void init_palette(void)
{
    /* Row 1 - Deep and vibrant shades */
    g_palette[0]  = RGB(0, 0, 0);
    g_palette[1]  = RGB(96, 96, 96);
    g_palette[2]  = RGB(144, 0, 0);
    g_palette[3]  = RGB(220, 20, 20);
    g_palette[4]  = RGB(240, 110, 0);
    g_palette[5]  = RGB(248, 192, 0);
    g_palette[6]  = RGB(248, 240, 32);
    g_palette[7]  = RGB(0, 112, 32);
    g_palette[8]  = RGB(16, 176, 64);
    g_palette[9]  = RGB(112, 224, 32);
    g_palette[10] = RGB(0, 128, 128);
    g_palette[11] = RGB(0, 200, 224);
    g_palette[12] = RGB(16, 32, 144);
    g_palette[13] = RGB(32, 96, 224);

    /* Row 2 - Light, pastel and accent colors */
    g_palette[14] = RGB(255, 255, 255);
    g_palette[15] = RGB(192, 192, 192);
    g_palette[16] = RGB(128, 64, 16);
    g_palette[17] = RGB(240, 128, 128);
    g_palette[18] = RGB(255, 192, 160);
    g_palette[19] = RGB(255, 255, 176);
    g_palette[20] = RGB(176, 240, 192);
    g_palette[21] = RGB(128, 208, 255);
    g_palette[22] = RGB(200, 232, 255);
    g_palette[23] = RGB(80, 48, 160);
    g_palette[24] = RGB(144, 48, 192);
    g_palette[25] = RGB(224, 40, 192);
    g_palette[26] = RGB(255, 128, 192);
    g_palette[27] = RGB(160, 120, 80);
}

static void init_buttons(void)
{
    static const char *names[] = {
        "", "Penc", "Brsh", "Spry", "Eras", "C.Er", "Fill",
        "Line", "Rect", "RndR", "Oval", "Poly", "Pick", "Text", "Sel."
    };
    int i, x, y, w, h;
    w = 38;
    h = 20;
    y = 4;
    for (i = 1; i <= TOOL_COUNT; i++) {
        x = 8 + (i - 1) * (w + 3);
        g_tools[i].r.left   = x;
        g_tools[i].r.top    = y;
        g_tools[i].r.right  = x + w;
        g_tools[i].r.bottom = y + h;
        g_tools[i].id       = i;
        lstrcpy(g_tools[i].name, names[i]);
    }
}

static BOOL tool_supports_size(int tool)
{
    switch (tool) {
    case TOOL_BRUSH:
    case TOOL_AIRBRUSH:
    case TOOL_ERASER:
    case TOOL_COLOR_ERASER:
    case TOOL_LINE:
    case TOOL_RECT:
    case TOOL_ROUNDRECT:
    case TOOL_ELLIPSE:
    case TOOL_POLYGON:
        return TRUE;
    default:
        return FALSE;
    }
}

static void save_undo_state(void)
{
    if (g_hdc_canvas && g_hdc_undo) {
        BitBlt(g_hdc_undo, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
        g_can_undo = TRUE;
    }
}

static void do_undo(void)
{
    HDC dc;
    if (!g_can_undo) {
        MessageBox(g_mainwnd, "No action available to undo.", "Undo", MB_OK | MB_ICONINFORMATION);
        return;
    }
    BitBlt(g_hdc_canvas, 0, 0, g_canvas_w, g_canvas_h, g_hdc_undo, 0, 0, SRCCOPY);
    g_can_undo = FALSE;

    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);
    set_status("Undid last action.");
}

static void clear_canvas(void)
{
    HBRUSH br;
    RECT r;
    HDC dc;
    save_undo_state();
    br = CreateSolidBrush(g_col_secondary);
    SetRect(&r, 0, 0, g_canvas_w, g_canvas_h);
    FillRect(g_hdc_canvas, &r, br);
    DeleteObject(br);

    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);
    set_status("Canvas cleared with background color.");
}

static void invert_canvas(void)
{
    HDC dc;
    save_undo_state();
    PatBlt(g_hdc_canvas, 0, 0, g_canvas_w, g_canvas_h, DSTINVERT);
    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);
    set_status("Colors inverted.");
}

static void flip_canvas_h(void)
{
    int x, y;
    COLORREF cl, cr;
    HDC dc;
    save_undo_state();
    for (y = 0; y < g_canvas_h; y++) {
        for (x = 0; x < g_canvas_w / 2; x++) {
            cl = GetPixel(g_hdc_canvas, x, y);
            cr = GetPixel(g_hdc_canvas, g_canvas_w - 1 - x, y);
            SetPixel(g_hdc_canvas, x, y, cr);
            SetPixel(g_hdc_canvas, g_canvas_w - 1 - x, y, cl);
        }
    }
    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);
    set_status("Canvas flipped horizontally.");
}

static void flip_canvas_v(void)
{
    int x, y;
    COLORREF ct, cb;
    HDC dc;
    save_undo_state();
    for (y = 0; y < g_canvas_h / 2; y++) {
        for (x = 0; x < g_canvas_w; x++) {
            ct = GetPixel(g_hdc_canvas, x, y);
            cb = GetPixel(g_hdc_canvas, x, g_canvas_h - 1 - y);
            SetPixel(g_hdc_canvas, x, y, cb);
            SetPixel(g_hdc_canvas, x, g_canvas_h - 1 - y, ct);
        }
    }
    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);
    set_status("Canvas flipped vertically.");
}

static void init_canvas_buffers(HWND hwnd)
{
    HDC scr;
    RECT r;
    HBRUSH br;

    scr = GetDC(hwnd);
    g_hdc_canvas = CreateCompatibleDC(scr);
    g_hbmp_canvas = CreateCompatibleBitmap(scr, g_canvas_w, g_canvas_h);
    g_hbmp_old_canvas = (HBITMAP)SelectObject(g_hdc_canvas, g_hbmp_canvas);

    g_hdc_undo = CreateCompatibleDC(scr);
    g_hbmp_undo = CreateCompatibleBitmap(scr, g_canvas_w, g_canvas_h);
    g_hbmp_old_undo = (HBITMAP)SelectObject(g_hdc_undo, g_hbmp_undo);

    g_hdc_temp = CreateCompatibleDC(scr);
    g_hbmp_temp = CreateCompatibleBitmap(scr, g_canvas_w, g_canvas_h);
    g_hbmp_old_temp = (HBITMAP)SelectObject(g_hdc_temp, g_hbmp_temp);

    ReleaseDC(hwnd, scr);

    br = CreateSolidBrush(RGB(255, 255, 255));
    SetRect(&r, 0, 0, g_canvas_w, g_canvas_h);
    FillRect(g_hdc_canvas, &r, br);
    FillRect(g_hdc_undo, &r, br);
    DeleteObject(br);
    g_can_undo = FALSE;
}

static void destroy_canvas_buffers(void)
{
    if (g_hdc_canvas) {
        SelectObject(g_hdc_canvas, g_hbmp_old_canvas);
        DeleteObject(g_hbmp_canvas);
        DeleteDC(g_hdc_canvas);
    }
    if (g_hdc_undo) {
        SelectObject(g_hdc_undo, g_hbmp_old_undo);
        DeleteObject(g_hbmp_undo);
        DeleteDC(g_hdc_undo);
    }
    if (g_hdc_temp) {
        SelectObject(g_hdc_temp, g_hbmp_old_temp);
        DeleteObject(g_hbmp_temp);
        DeleteDC(g_hdc_temp);
    }
}

static void apply_draw_shape(HDC target, int tool, int style, int x1, int y1, int x2, int y2,
                             COLORREF col_pen, COLORREF col_fill, int bsize)
{
    HPEN hp, old_pen;
    HBRUSH hbr, old_br;
    int left, top, right, bottom;

    left = (x1 < x2) ? x1 : x2;
    right = (x1 < x2) ? x2 : x1;
    top = (y1 < y2) ? y1 : y2;
    bottom = (y1 < y2) ? y2 : y1;

    switch (tool) {
    case TOOL_LINE:
        hp = CreatePen(PS_SOLID, bsize, col_pen);
        old_pen = (HPEN)SelectObject(target, hp);
        MoveTo(target, x1, y1);
        LineTo(target, x2, y2);
        SelectObject(target, old_pen);
        DeleteObject(hp);
        break;

    case TOOL_RECT:
        if (style == STYLE_OUTLINE) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = (HBRUSH)GetStockObject(NULL_BRUSH);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Rectangle(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hp);
        } else if (style == STYLE_FILLED_BORDER) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = CreateSolidBrush(col_fill);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Rectangle(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
            DeleteObject(hp);
        } else {
            hp = (HPEN)GetStockObject(NULL_PEN);
            hbr = CreateSolidBrush(col_pen);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Rectangle(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
        }
        break;

    case TOOL_ROUNDRECT:
        if (style == STYLE_OUTLINE) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = (HBRUSH)GetStockObject(NULL_BRUSH);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            RoundRect(target, left, top, right, bottom, 16, 16);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hp);
        } else if (style == STYLE_FILLED_BORDER) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = CreateSolidBrush(col_fill);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            RoundRect(target, left, top, right, bottom, 16, 16);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
            DeleteObject(hp);
        } else {
            hp = (HPEN)GetStockObject(NULL_PEN);
            hbr = CreateSolidBrush(col_pen);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            RoundRect(target, left, top, right, bottom, 16, 16);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
        }
        break;

    case TOOL_ELLIPSE:
        if (style == STYLE_OUTLINE) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = (HBRUSH)GetStockObject(NULL_BRUSH);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Ellipse(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hp);
        } else if (style == STYLE_FILLED_BORDER) {
            hp = CreatePen(PS_SOLID, bsize, col_pen);
            hbr = CreateSolidBrush(col_fill);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Ellipse(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
            DeleteObject(hp);
        } else {
            hp = (HPEN)GetStockObject(NULL_PEN);
            hbr = CreateSolidBrush(col_pen);
            old_pen = (HPEN)SelectObject(target, hp);
            old_br = (HBRUSH)SelectObject(target, hbr);
            Ellipse(target, left, top, right, bottom);
            SelectObject(target, old_br);
            SelectObject(target, old_pen);
            DeleteObject(hbr);
        }
        break;

    case TOOL_SELECT:
        hp = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
        hbr = (HBRUSH)GetStockObject(NULL_BRUSH);
        old_pen = (HPEN)SelectObject(target, hp);
        old_br = (HBRUSH)SelectObject(target, hbr);
        Rectangle(target, left, top, right, bottom);
        SelectObject(target, old_br);
        SelectObject(target, old_pen);
        DeleteObject(hp);
        break;
    }
}

static void paint_brush_disc(HDC target, int x, int y, int radius, COLORREF col)
{
    HBRUSH br, old_br;
    HPEN hp, old_pen;
    if (radius <= 1) {
        SetPixel(target, x, y, col);
        return;
    }
    br = CreateSolidBrush(col);
    hp = CreatePen(PS_SOLID, 1, col);
    old_br = (HBRUSH)SelectObject(target, br);
    old_pen = (HPEN)SelectObject(target, hp);
    Ellipse(target, x - radius, y - radius, x + radius + 1, y + radius + 1);
    SelectObject(target, old_pen);
    SelectObject(target, old_br);
    DeleteObject(hp);
    DeleteObject(br);
}

static void draw_continuous_brush(HDC target, int x1, int y1, int x2, int y2, int size, COLORREF col)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    int i, cx, cy;
    if (dist < 1) dist = 1;
    for (i = 0; i <= dist; i++) {
        cx = x1 + (int)(((long)dx * i) / dist);
        cy = y1 + (int)(((long)dy * i) / dist);
        paint_brush_disc(target, cx, cy, size / 2, col);
    }
}

static void draw_spray(HDC target, int cx, int cy, int radius, COLORREF col)
{
    int i, r, angle, px, py;
    if (radius < 3) radius = 3;
    for (i = 0; i < 14; i++) {
        r = (int)(simple_rand() % (unsigned)radius);
        angle = (int)(simple_rand() % 360u);
        px = cx + (r * (180 - abs(angle - 180))) / 120 - (r / 2);
        py = cy + (r * (180 - abs(((angle + 90) % 360) - 180))) / 120 - (r / 2);
        if (px >= 0 && px < g_canvas_w && py >= 0 && py < g_canvas_h)
            SetPixel(target, px, py, col);
    }
}

static void draw_color_eraser(HDC target, int cx, int cy, int half, COLORREF find_col, COLORREF replace_col)
{
    int x, y;
    for (y = cy - half; y <= cy + half; y++) {
        if (y >= 0 && y < g_canvas_h) {
            for (x = cx - half; x <= cx + half; x++) {
                if (x >= 0 && x < g_canvas_w) {
                    if (GetPixel(target, x, y) == find_col)
                        SetPixel(target, x, y, replace_col);
                }
            }
        }
    }
}

static void draw_normal_eraser(HDC target, int cx, int cy, int half, COLORREF bg_col)
{
    RECT r;
    HBRUSH br;
    SetRect(&r, cx - half, cy - half, cx + half + 1, cy + half + 1);
    br = CreateSolidBrush(bg_col);
    FillRect(target, &r, br);
    DeleteObject(br);
}

/* BMP File I/O */
static BOOL save_canvas_to_bmp(const char *path)
{
    HFILE fh;
    BMPFILEHDR fhdr;
    BMPINFOHDR ihdr;
    int x, y, row_bytes, pad_bytes, idx;
    COLORREF col;
    BYTE row_buf[2048];
    DWORD zero = 0;

    row_bytes = g_canvas_w * 3;
    pad_bytes = (4 - (row_bytes % 4)) % 4;

    memset(&fhdr, 0, sizeof(fhdr));
    memset(&ihdr, 0, sizeof(ihdr));

    fhdr.bfType = 0x4D42; /* 'BM' */
    fhdr.bfOffBits = sizeof(BMPFILEHDR) + sizeof(BMPINFOHDR);
    fhdr.bfSize = fhdr.bfOffBits + (DWORD)(row_bytes + pad_bytes) * g_canvas_h;

    ihdr.biSize = sizeof(BMPINFOHDR);
    ihdr.biWidth = g_canvas_w;
    ihdr.biHeight = g_canvas_h;
    ihdr.biPlanes = 1;
    ihdr.biBitCount = 24;
    ihdr.biCompression = 0;
    ihdr.biSizeImage = (DWORD)(row_bytes + pad_bytes) * g_canvas_h;

    fh = _lcreat(path, 0);
    if (fh == HFILE_ERROR) {
        MessageBox(g_mainwnd, "Unable to create BMP file for saving.", "Save Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    _lwrite(fh, (LPCSTR)&fhdr, sizeof(fhdr));
    _lwrite(fh, (LPCSTR)&ihdr, sizeof(ihdr));

    for (y = g_canvas_h - 1; y >= 0; y--) {
        idx = 0;
        for (x = 0; x < g_canvas_w; x++) {
            col = GetPixel(g_hdc_canvas, x, y);
            row_buf[idx]     = (BYTE)((col >> 16) & 0xFF); /* Blue */
            row_buf[idx + 1] = (BYTE)((col >> 8) & 0xFF);  /* Green */
            row_buf[idx + 2] = (BYTE)(col & 0xFF);         /* Red */
            idx += 3;
        }
        _lwrite(fh, (LPCSTR)row_buf, row_bytes);
        if (pad_bytes > 0)
            _lwrite(fh, (LPCSTR)&zero, pad_bytes);
    }
    _lclose(fh);

    lstrcpy(g_curfile, path);
    wsprintf(g_titlebuf, "PaintCraft - %s", g_curfile);
    SetWindowText(g_mainwnd, g_titlebuf);
    set_status("File saved successfully.");
    return TRUE;
}

static BOOL load_canvas_from_bmp(const char *path)
{
    HFILE fh;
    BMPFILEHDR fhdr;
    BMPINFOHDR ihdr;
    int x, y, row_bytes, pad_bytes, idx, dw, dh, bpp_step;
    COLORREF col;
    BYTE row_buf[2048];
    HDC dc;

    fh = _lopen(path, 0);
    if (fh == HFILE_ERROR) {
        MessageBox(g_mainwnd, "Unable to open selected file.", "Open Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    _lread(fh, (LPSTR)&fhdr, sizeof(fhdr));
    if (fhdr.bfType != 0x4D42) {
        _lclose(fh);
        MessageBox(g_mainwnd, "Selected file is not a valid BMP bitmap.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    _lread(fh, (LPSTR)&ihdr, sizeof(ihdr));
    if (ihdr.biBitCount != 24 && ihdr.biBitCount != 32) {
        _lclose(fh);
        MessageBox(g_mainwnd, "PaintCraft supports 24-bit and 32-bit BMP files.", "File Format", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    save_undo_state();
    _llseek(fh, fhdr.bfOffBits, 0);

    dw = (int)ihdr.biWidth;
    if (dw > g_canvas_w) dw = g_canvas_w;
    dh = (int)ihdr.biHeight;
    if (dh > g_canvas_h) dh = g_canvas_h;

    bpp_step = ihdr.biBitCount / 8;
    row_bytes = (int)ihdr.biWidth * bpp_step;
    pad_bytes = (4 - (row_bytes % 4)) % 4;

    for (y = (int)ihdr.biHeight - 1; y >= 0; y--) {
        _lread(fh, (LPSTR)row_buf, row_bytes + pad_bytes);
        if (y < dh) {
            idx = 0;
            for (x = 0; x < dw; x++) {
                col = RGB(row_buf[idx + 2], row_buf[idx + 1], row_buf[idx]);
                SetPixel(g_hdc_canvas, x, y, col);
                idx += bpp_step;
            }
        }
    }
    _lclose(fh);

    dc = GetDC(g_mainwnd);
    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
    ReleaseDC(g_mainwnd, dc);

    lstrcpy(g_curfile, path);
    wsprintf(g_titlebuf, "PaintCraft - %s", g_curfile);
    SetWindowText(g_mainwnd, g_titlebuf);
    set_status("Loaded BMP image.");
    return TRUE;
}

static void do_file_open(void)
{
    OPENFILENAME ofn;
    char file_buf[128];
    static const char filter[] = "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0\0";

    memset(&ofn, 0, sizeof(ofn));
    file_buf[0] = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_mainwnd;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf);
    ofn.lpstrDefExt = "bmp";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn)) {
        if (file_buf[0])
            load_canvas_from_bmp(file_buf);
    }
}

static void do_file_save_as(void)
{
    OPENFILENAME ofn;
    char file_buf[128];
    static const char filter[] = "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0\0";

    memset(&ofn, 0, sizeof(ofn));
    lstrcpy(file_buf, g_curfile);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_mainwnd;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf);
    ofn.lpstrDefExt = "bmp";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileName(&ofn)) {
        if (file_buf[0])
            save_canvas_to_bmp(file_buf);
    }
}

static void do_file_save(void)
{
    if (g_curfile[0])
        save_canvas_to_bmp(g_curfile);
    else
        do_file_save_as();
}

static void draw_modern_ui(HDC dc)
{
    RECT client_rc, r;
    HBRUSH br, old_br;
    HPEN hp, old_pen;
    int i, x, y, w;
    char buf[16];
    BOOL has_size;

    GetClientRect(g_mainwnd, &client_rc);

    /* Fill background outside control panel and canvas with neutral background */
    SetRect(&r, 0, 0, client_rc.right, client_rc.bottom);
    br = (HBRUSH)(COLOR_BTNFACE + 1);
    FillRect(dc, &r, br);

    /* 1. Control Panel Background */
    SetRect(&r, 0, 0, client_rc.right, CANVAS_Y - 4);
    br = CreateSolidBrush(RGB(236, 238, 241));
    FillRect(dc, &r, br);
    DeleteObject(br);

    hp = CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
    old_pen = (HPEN)SelectObject(dc, hp);
    MoveTo(dc, 0, CANVAS_Y - 5);
    LineTo(dc, client_rc.right, CANVAS_Y - 5);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    /* 2. Tool Buttons (Row 1) */
    SetBkMode(dc, TRANSPARENT);
    for (i = 1; i <= TOOL_COUNT; i++) {
        r = g_tools[i].r;
        if (g_active_tool == i) {
            br = CreateSolidBrush(RGB(196, 220, 250));
            FillRect(dc, &r, br);
            DeleteObject(br);
            hp = CreatePen(PS_SOLID, 1, RGB(32, 96, 200));
            old_pen = (HPEN)SelectObject(dc, hp);
            old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, old_br);
            SelectObject(dc, old_pen);
            DeleteObject(hp);
            SetTextColor(dc, RGB(16, 48, 128));
        } else {
            br = CreateSolidBrush(RGB(248, 249, 250));
            FillRect(dc, &r, br);
            DeleteObject(br);
            hp = CreatePen(PS_SOLID, 1, RGB(190, 195, 205));
            old_pen = (HPEN)SelectObject(dc, hp);
            old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, old_br);
            SelectObject(dc, old_pen);
            DeleteObject(hp);
            SetTextColor(dc, RGB(40, 44, 52));
        }
        DrawText(dc, g_tools[i].name, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 3. Brush Width Options */
    has_size = tool_supports_size(g_active_tool);
    SetTextColor(dc, has_size ? RGB(60, 64, 72) : RGB(150, 155, 165));
    TextOut(dc, 8, 28, "Size:", 5);
    for (i = 1; i <= 5; i++) {
        x = 52 + (i - 1) * 26;
        y = 26;
        SetRect(&r, x, y, x + 22, y + 18);
        switch (i) {
        case 1: w = 1; lstrcpy(buf, "1"); break;
        case 2: w = 2; lstrcpy(buf, "2"); break;
        case 3: w = 4; lstrcpy(buf, "4"); break;
        case 4: w = 8; lstrcpy(buf, "8"); break;
        default: w = 12; lstrcpy(buf, "12"); break;
        }
        if (has_size) {
            br = CreateSolidBrush(g_brush_size == w ? RGB(196, 220, 250) : RGB(255, 255, 255));
            hp = CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
            SetTextColor(dc, g_brush_size == w ? RGB(16, 48, 128) : RGB(40, 44, 52));
        } else {
            if (g_active_tool == TOOL_PENCIL && w == 1) {
                br = CreateSolidBrush(RGB(226, 230, 238));
                hp = CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
                SetTextColor(dc, RGB(100, 110, 130));
            } else {
                br = CreateSolidBrush(RGB(240, 241, 243));
                hp = CreatePen(PS_SOLID, 1, RGB(215, 218, 224));
                SetTextColor(dc, RGB(170, 175, 185));
            }
        }
        FillRect(dc, &r, br);
        DeleteObject(br);

        old_pen = (HPEN)SelectObject(dc, hp);
        old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(hp);
        DrawText(dc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Shape Style Options */
    TextOut(dc, 194, 28, "Style:", 6);
    for (i = 1; i <= 3; i++) {
        x = 240 + (i - 1) * 44;
        y = 26;
        SetRect(&r, x, y, x + 40, y + 18);
        switch (i) {
        case 1: lstrcpy(buf, "Frame"); break;
        case 2: lstrcpy(buf, "Fill+"); break;
        default: lstrcpy(buf, "Solid"); break;
        }
        br = CreateSolidBrush(g_shape_style == i ? RGB(196, 220, 250) : RGB(255, 255, 255));
        FillRect(dc, &r, br);
        DeleteObject(br);

        hp = CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
        old_pen = (HPEN)SelectObject(dc, hp);
        old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(hp);
        DrawText(dc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Quick Action Buttons */
    for (i = 1; i <= 5; i++) {
        x = 384 + (i - 1) * 44;
        y = 26;
        SetRect(&r, x, y, x + 41, y + 18);
        switch (i) {
        case 1: lstrcpy(buf, "Undo"); break;
        case 2: lstrcpy(buf, "Clear"); break;
        case 3: lstrcpy(buf, "Invert"); break;
        case 4: lstrcpy(buf, "FlipX"); break;
        default: lstrcpy(buf, "FlipY"); break;
        }
        br = CreateSolidBrush(RGB(244, 245, 247));
        FillRect(dc, &r, br);
        DeleteObject(br);

        hp = CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
        old_pen = (HPEN)SelectObject(dc, hp);
        old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(hp);
        DrawText(dc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 4. Dual-color Indicator */
    SetRect(&r, 22, 58, 48, 80);
    br = CreateSolidBrush(g_col_secondary);
    FillRect(dc, &r, br);
    DeleteObject(br);
    hp = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    old_pen = (HPEN)SelectObject(dc, hp);
    old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);

    SetRect(&r, 10, 50, 36, 72);
    br = CreateSolidBrush(g_col_primary);
    FillRect(dc, &r, br);
    DeleteObject(br);
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    /* 5. 28-Color Swatches */
    for (i = 0; i < 28; i++) {
        if (i < 14) {
            x = 56 + i * 19;
            y = 50;
        } else {
            x = 56 + (i - 14) * 19;
            y = 66;
        }
        SetRect(&r, x, y, x + 16, y + 13);
        br = CreateSolidBrush(g_palette[i]);
        FillRect(dc, &r, br);
        DeleteObject(br);

        hp = CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
        old_pen = (HPEN)SelectObject(dc, hp);
        old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(hp);
    }

    /* 6. Border around Canvas */
    hp = CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
    old_pen = (HPEN)SelectObject(dc, hp);
    old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, CANVAS_X - 1, CANVAS_Y - 1, CANVAS_X + g_canvas_w + 1, CANVAS_Y + g_canvas_h + 1);
    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    /* Canvas Drop Shadow */
    hp = CreatePen(PS_SOLID, 1, RGB(200, 205, 212));
    old_pen = (HPEN)SelectObject(dc, hp);
    MoveTo(dc, CANVAS_X + g_canvas_w + 1, CANVAS_Y);
    LineTo(dc, CANVAS_X + g_canvas_w + 1, CANVAS_Y + g_canvas_h + 2);
    LineTo(dc, CANVAS_X, CANVAS_Y + g_canvas_h + 1);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    /* 7. Bottom Status Bar */
    SetRect(&r, 0, client_rc.bottom - 22, client_rc.right, client_rc.bottom);
    br = CreateSolidBrush(RGB(240, 242, 245));
    FillRect(dc, &r, br);
    DeleteObject(br);

    hp = CreatePen(PS_SOLID, 1, RGB(190, 195, 205));
    old_pen = (HPEN)SelectObject(dc, hp);
    MoveTo(dc, 0, r.top);
    LineTo(dc, client_rc.right, r.top);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(40, 44, 52));
    TextOut(dc, 8, r.top + 3, g_statustext, lstrlen(g_statustext));
}

LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT ps;
    HDC dc;
    int x, y, i, swatch_idx;
    char coord_buf[80];
    COLORREF target_col;
    HBRUSH fill_br, old_fill_br;

    switch (msg) {
    case WM_CREATE:
        init_canvas_buffers(hwnd);
        lstrcpy(g_statustext, "PaintCraft - Ready. LMB: Primary Color, RMB: Background Color.");
        return 0;

    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        draw_modern_ui(dc);
        if (g_hdc_canvas)
            BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_COMMAND:
        switch (wparam) {
        case IDM_NEW:    clear_canvas(); break;
        case IDM_OPEN:   do_file_open(); break;
        case IDM_SAVE:   do_file_save(); break;
        case IDM_SAVEAS: do_file_save_as(); break;
        case IDM_UNDO:   do_undo(); break;
        case IDM_CLEAR:  clear_canvas(); break;
        case IDM_INVERT: invert_canvas(); break;
        case IDM_FLIPH:  flip_canvas_h(); break;
        case IDM_FLIPV:  flip_canvas_v(); break;
        case IDM_EXIT:   SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        case IDM_ABOUT:
            MessageBox(
                hwnd,
                "PaintCraft Studio for Windows & DOS\r\n"
                "A modern pixel art & graphics editor.\r\n\r\n"
                "Features:\r\n"
                "- Double-buffered GDI canvas\r\n"
                "- Real-time interactive shape preview\r\n"
                "- 14 drawing tools & 28-color palette\r\n"
                "- Direct 24-bit BMP loading and saving\r\n"
                "- Multi-level Undo & canvas transformations\r\n\r\n"
                "Win16Loader Project",
                "About PaintCraft",
                MB_OK | 0x0040
            );
            break;
        }
        return 0;

    case WM_LBUTTONDOWN:
        x = LOWORD(lparam);
        y = HIWORD(lparam);

        /* 1. Tools */
        for (i = 1; i <= TOOL_COUNT; i++) {
            if (x >= g_tools[i].r.left && x <= g_tools[i].r.right &&
                y >= g_tools[i].r.top && y <= g_tools[i].r.bottom) {
                g_active_tool = i;
                g_poly_count = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                set_status("Active tool changed.");
                return 0;
            }
        }

        /* 2. Brush Size */
        if (y >= 26 && y <= 44 && x >= 52 && x <= 182) {
            if (!tool_supports_size(g_active_tool)) {
                if (g_active_tool == TOOL_PENCIL)
                    set_status("Pencil is fixed at 1 px (hairline).");
                else
                    set_status("Size option is not available for this tool.");
                return 0;
            }
            i = ((x - 52) / 26) + 1;
            switch (i) {
            case 1: g_brush_size = 1; break;
            case 2: g_brush_size = 2; break;
            case 3: g_brush_size = 4; break;
            case 4: g_brush_size = 8; break;
            default: g_brush_size = 12; break;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            set_status("Brush size changed.");
            return 0;
        }

        /* 3. Shape Style */
        if (y >= 26 && y <= 44 && x >= 240 && x <= 372) {
            i = ((x - 240) / 44) + 1;
            if (i >= 1 && i <= 3) g_shape_style = i;
            InvalidateRect(hwnd, NULL, FALSE);
            set_status("Shape style changed.");
            return 0;
        }

        /* 4. Quick Actions */
        if (y >= 26 && y <= 44 && x >= 384 && x <= 604) {
            i = ((x - 384) / 44) + 1;
            switch (i) {
            case 1: do_undo(); break;
            case 2: clear_canvas(); break;
            case 3: invert_canvas(); break;
            case 4: flip_canvas_h(); break;
            default: flip_canvas_v(); break;
            }
            return 0;
        }

        /* 5. Palette LMB */
        if (y >= 50 && y <= 80 && x >= 56 && x <= 56 + 14 * 19) {
            swatch_idx = (x - 56) / 19;
            if (swatch_idx >= 0 && swatch_idx < 14) {
                if (y >= 66) swatch_idx += 14;
                g_col_primary = g_palette[swatch_idx];
                InvalidateRect(hwnd, NULL, FALSE);
                set_status("Primary color selected (LMB).");
                return 0;
            }
        }

        /* 6. Canvas Area */
        if (x >= CANVAS_X && x < CANVAS_X + g_canvas_w &&
            y >= CANVAS_Y && y < CANVAS_Y + g_canvas_h) {
            SetCapture(hwnd);
            x -= CANVAS_X;
            y -= CANVAS_Y;

            switch (g_active_tool) {
            case TOOL_PENCIL:
            case TOOL_BRUSH:
            case TOOL_AIRBRUSH:
            case TOOL_ERASER:
            case TOOL_COLOR_ERASER:
                save_undo_state();
                g_is_drawing = TRUE;
                g_start_x = g_last_x = x;
                g_start_y = g_last_y = y;

                dc = GetDC(hwnd);
                if (g_active_tool == TOOL_PENCIL) {
                    SetPixel(g_hdc_canvas, x, y, g_col_primary);
                    BitBlt(dc, CANVAS_X + x, CANVAS_Y + y, 1, 1, g_hdc_canvas, x, y, SRCCOPY);
                } else if (g_active_tool == TOOL_BRUSH) {
                    paint_brush_disc(g_hdc_canvas, x, y, g_brush_size / 2, g_col_primary);
                    BitBlt(dc, CANVAS_X + x - g_brush_size, CANVAS_Y + y - g_brush_size,
                           g_brush_size * 2 + 1, g_brush_size * 2 + 1,
                           g_hdc_canvas, x - g_brush_size, y - g_brush_size, SRCCOPY);
                } else if (g_active_tool == TOOL_AIRBRUSH) {
                    draw_spray(g_hdc_canvas, x, y, g_brush_size * 3 + 4, g_col_primary);
                    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                } else if (g_active_tool == TOOL_ERASER) {
                    draw_normal_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_secondary);
                    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                } else if (g_active_tool == TOOL_COLOR_ERASER) {
                    draw_color_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_primary, g_col_secondary);
                    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                }
                ReleaseDC(hwnd, dc);
                break;

            case TOOL_FILL:
                target_col = GetPixel(g_hdc_canvas, x, y);
                if (target_col != g_col_primary) {
                    save_undo_state();
                    fill_br = CreateSolidBrush(g_col_primary);
                    old_fill_br = (HBRUSH)SelectObject(g_hdc_canvas, fill_br);
                    ExtFloodFill(g_hdc_canvas, x, y, target_col, 1 /* FLOODFILLSURFACE */);
                    SelectObject(g_hdc_canvas, old_fill_br);
                    DeleteObject(fill_br);
                    dc = GetDC(hwnd);
                    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                    ReleaseDC(hwnd, dc);
                    set_status("Surface filled with primary color.");
                }
                break;

            case TOOL_EYEDROPPER:
                g_col_primary = GetPixel(g_hdc_canvas, x, y);
                InvalidateRect(hwnd, NULL, FALSE);
                set_status("Primary color picked from canvas.");
                break;

            case TOOL_TEXT:
                save_undo_state();
                SetBkMode(g_hdc_canvas, TRANSPARENT);
                SetTextColor(g_hdc_canvas, g_col_primary);
                TextOut(g_hdc_canvas, x, y, "PaintCraft", 10);
                dc = GetDC(hwnd);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                ReleaseDC(hwnd, dc);
                set_status("Text stamped onto canvas.");
                break;

            case TOOL_POLYGON:
                if (g_poly_count == 0) {
                    save_undo_state();
                    g_poly_pts[0].x = x;
                    g_poly_pts[0].y = y;
                    g_poly_count = 1;
                    g_last_x = x; g_last_y = y;
                    set_status("Polygon: Click points. Double-click to close.");
                } else if (g_poly_count < 30) {
                    if (abs(x - g_poly_pts[0].x) < 6 && abs(y - g_poly_pts[0].y) < 6 && g_poly_count >= 2) {
                        Polygon(g_hdc_canvas, g_poly_pts, g_poly_count);
                        g_poly_count = 0;
                        dc = GetDC(hwnd);
                        BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                        ReleaseDC(hwnd, dc);
                        set_status("Polygon closed and filled.");
                    } else {
                        g_poly_pts[g_poly_count].x = x;
                        g_poly_pts[g_poly_count].y = y;
                        g_poly_count++;
                        apply_draw_shape(g_hdc_canvas, TOOL_LINE, 0, g_last_x, g_last_y, x, y, g_col_primary, 0, g_brush_size);
                        g_last_x = x; g_last_y = y;
                        dc = GetDC(hwnd);
                        BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                        ReleaseDC(hwnd, dc);
                    }
                }
                break;

            case TOOL_LINE:
            case TOOL_RECT:
            case TOOL_ROUNDRECT:
            case TOOL_ELLIPSE:
            case TOOL_SELECT:
                save_undo_state();
                g_is_dragging = TRUE;
                g_start_x = g_cur_x = x;
                g_start_y = g_cur_y = y;
                break;
            }
        }
        return 0;

    case WM_RBUTTONDOWN:
        x = LOWORD(lparam);
        y = HIWORD(lparam);

        /* Palette RMB -> Background Color */
        if (y >= 50 && y <= 80 && x >= 56 && x <= 56 + 14 * 19) {
            swatch_idx = (x - 56) / 19;
            if (swatch_idx >= 0 && swatch_idx < 14) {
                if (y >= 66) swatch_idx += 14;
                g_col_secondary = g_palette[swatch_idx];
                InvalidateRect(hwnd, NULL, FALSE);
                set_status("Background color selected (RMB).");
                return 0;
            }
        }

        if (x >= CANVAS_X && x < CANVAS_X + g_canvas_w &&
            y >= CANVAS_Y && y < CANVAS_Y + g_canvas_h) {
            x -= CANVAS_X;
            y -= CANVAS_Y;
            if (g_active_tool == TOOL_EYEDROPPER) {
                g_col_secondary = GetPixel(g_hdc_canvas, x, y);
                InvalidateRect(hwnd, NULL, FALSE);
                set_status("Background color picked from canvas.");
                return 0;
            } else if (g_active_tool == TOOL_FILL) {
                target_col = GetPixel(g_hdc_canvas, x, y);
                if (target_col != g_col_secondary) {
                    save_undo_state();
                    fill_br = CreateSolidBrush(g_col_secondary);
                    old_fill_br = (HBRUSH)SelectObject(g_hdc_canvas, fill_br);
                    ExtFloodFill(g_hdc_canvas, x, y, target_col, 1 /* FLOODFILLSURFACE */);
                    SelectObject(g_hdc_canvas, old_fill_br);
                    DeleteObject(fill_br);
                    dc = GetDC(hwnd);
                    BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                    ReleaseDC(hwnd, dc);
                    set_status("Surface filled with background color (RMB).");
                }
                return 0;
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        x = LOWORD(lparam);
        y = HIWORD(lparam);

        if (x >= CANVAS_X && x < CANVAS_X + g_canvas_w &&
            y >= CANVAS_Y && y < CANVAS_Y + g_canvas_h) {
            wsprintf(coord_buf, "Cursor: X=%d Y=%d px  |  Canvas: 580 x 310", x - CANVAS_X, y - CANVAS_Y);
            set_status(coord_buf);
        }

        if (g_is_drawing && x >= CANVAS_X && x < CANVAS_X + g_canvas_w &&
            y >= CANVAS_Y && y < CANVAS_Y + g_canvas_h) {
            x -= CANVAS_X;
            y -= CANVAS_Y;
            dc = GetDC(hwnd);

            switch (g_active_tool) {
            case TOOL_PENCIL:
                apply_draw_shape(g_hdc_canvas, TOOL_LINE, 0, g_last_x, g_last_y, x, y, g_col_primary, 0, 1);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                break;
            case TOOL_BRUSH:
                draw_continuous_brush(g_hdc_canvas, g_last_x, g_last_y, x, y, g_brush_size, g_col_primary);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                break;
            case TOOL_AIRBRUSH:
                draw_spray(g_hdc_canvas, x, y, g_brush_size * 3 + 4, g_col_primary);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                break;
            case TOOL_ERASER:
                draw_normal_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_secondary);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                break;
            case TOOL_COLOR_ERASER:
                draw_color_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_primary, g_col_secondary);
                BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
                break;
            }
            g_last_x = x;
            g_last_y = y;
            ReleaseDC(hwnd, dc);
        }

        if (g_is_dragging) {
            x -= CANVAS_X;
            y -= CANVAS_Y;
            if (x < 0) x = 0;
            if (x >= g_canvas_w) x = g_canvas_w - 1;
            if (y < 0) y = 0;
            if (y >= g_canvas_h) y = g_canvas_h - 1;
            g_cur_x = x;
            g_cur_y = y;

            BitBlt(g_hdc_temp, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
            apply_draw_shape(g_hdc_temp, g_active_tool, g_shape_style,
                             g_start_x, g_start_y, g_cur_x, g_cur_y,
                             g_col_primary, g_col_secondary, g_brush_size);

            dc = GetDC(hwnd);
            BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_temp, 0, 0, SRCCOPY);
            ReleaseDC(hwnd, dc);
        }
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_is_drawing)
            g_is_drawing = FALSE;

        if (g_is_dragging) {
            g_is_dragging = FALSE;
            apply_draw_shape(g_hdc_canvas, g_active_tool, g_shape_style,
                             g_start_x, g_start_y, g_cur_x, g_cur_y,
                             g_col_primary, g_col_secondary, g_brush_size);
            dc = GetDC(hwnd);
            BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
            ReleaseDC(hwnd, dc);
            set_status("Shape drawn.");
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (g_active_tool == TOOL_POLYGON && g_poly_count >= 2) {
            Polygon(g_hdc_canvas, g_poly_pts, g_poly_count);
            g_poly_count = 0;
            dc = GetDC(hwnd);
            BitBlt(dc, CANVAS_X, CANVAS_Y, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
            ReleaseDC(hwnd, dc);
            set_status("Polygon completed.");
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        destroy_canvas_buffers();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void create_main_menu(HWND hwnd)
{
    HMENU hmenu, hfile, hedit, hhelp;
    hmenu = CreateMenu();

    hfile = CreatePopupMenu();
    AppendMenu(hfile, MF_STRING, IDM_NEW,    "&New\tCtrl+N");
    AppendMenu(hfile, MF_STRING, IDM_OPEN,   "&Open...\tCtrl+O");
    AppendMenu(hfile, MF_STRING, IDM_SAVE,   "&Save\tCtrl+S");
    AppendMenu(hfile, MF_STRING, IDM_SAVEAS, "Save &As...");
    AppendMenu(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hfile, MF_STRING, IDM_EXIT,   "E&xit\tAlt+F4");
    AppendMenu(hmenu, MF_POPUP, hfile, "&File");

    hedit = CreatePopupMenu();
    AppendMenu(hedit, MF_STRING, IDM_UNDO,   "&Undo\tCtrl+Z");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, IDM_CLEAR,  "&Clear Canvas");
    AppendMenu(hedit, MF_STRING, IDM_INVERT, "&Invert Colors");
    AppendMenu(hedit, MF_STRING, IDM_FLIPH,  "Flip &Horizontal");
    AppendMenu(hedit, MF_STRING, IDM_FLIPV,  "Flip &Vertical");
    AppendMenu(hmenu, MF_POPUP, hedit, "&Edit");

    hhelp = CreatePopupMenu();
    AppendMenu(hhelp, MF_STRING, IDM_ABOUT,  "&About PaintCraft...");
    AppendMenu(hmenu, MF_POPUP, hhelp, "&Help");

    SetMenu(hwnd, hmenu);
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE prev_inst, LPSTR cmdline, int show)
{
    WNDCLASS wc;
    MSG msg;
    (void)prev_inst;
    (void)cmdline;

    g_instance = hinst;
    init_palette();
    init_buttons();

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "W16PaintCraftClass";

    if (!RegisterClass(&wc)) return 1;

    /* Fully resizable window with Maximize, Minimize, System Menu, Caption, Thick Frame */
    g_mainwnd = CreateWindow(
        "W16PaintCraftClass",
        "PaintCraft - [Untitled]",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        624, 468,
        NULL, NULL, hinst, NULL
    );

    if (!g_mainwnd) return 1;

    create_main_menu(g_mainwnd);
    ShowWindow(g_mainwnd, show);
    UpdateWindow(g_mainwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
