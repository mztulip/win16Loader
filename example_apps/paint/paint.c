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
#include "paint.h"

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

#define IDM_CUT       1006
#define IDM_COPY      1007
#define IDM_PASTE     1008
#define IDM_DELETE    1009

#define IDM_UNDO      1010
#define IDM_CLEAR     1011
#define IDM_INVERT    1012
#define IDM_FLIPH     1013
#define IDM_FLIPV     1014
#define IDM_SELECTALL 1015
#define IDM_CANVASSIZE 1016

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

typedef struct {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} BMPRGBQUAD;
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

/* Viewport and Scrolling */
static int     g_view_w = CANVAS_DEF_W;
static int     g_view_h = CANVAS_DEF_H;
static int     g_scroll_x = 0;
static int     g_scroll_y = 0;
static HWND    g_hwnd_sb_vert = NULL;
static HWND    g_hwnd_sb_horz = NULL;

/* Selection State */
static BOOL    g_has_sel = FALSE;
static RECT    g_sel_rc;
static int     g_sel_w = 0, g_sel_h = 0;
static HDC     g_hdc_sel = NULL;
static HBITMAP g_hbmp_sel = NULL;
static HBITMAP g_hbmp_old_sel = NULL;
static BOOL    g_sel_floating = FALSE;
static BOOL    g_sel_dragging = FALSE;
static int     g_sel_anchor_x = 0, g_sel_anchor_y = 0;

/* Clipboard Buffer */
static HDC     g_hdc_clip = NULL;
static HBITMAP g_hbmp_clip = NULL;
static HBITMAP g_hbmp_old_clip = NULL;
static int     g_clip_w = 0, g_clip_h = 0;
static BOOL    g_has_clip = FALSE;

/* Text Tool State */
static HWND    g_hwnd_text_edit = NULL;
static int     g_text_cx = 0;
static int     g_text_cy = 0;
static BOOL    g_text_active = FALSE;
static FARPROC g_old_text_edit_proc = NULL;
static void    commit_text(void);
static void    cancel_text(void);
static void    update_text_edit_pos(void);

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
    RECT r, client_rc;
    HDC dc;
    HBRUSH br;
    HPEN hp, old_pen;

    lstrcpy(g_statustext, text);
    if (!g_mainwnd) return;
    GetClientRect(g_mainwnd, &client_rc);
    SetRect(&r, 0, client_rc.bottom - 22, client_rc.right, client_rc.bottom);
    dc = GetDC(g_mainwnd);
    if (dc) {
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
        ReleaseDC(g_mainwnd, dc);
    }
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

static void update_scrollbars(void)
{
    RECT client_rc;
    int max_view_w, max_view_h;
    int view_w, view_h;
    BOOL need_h = FALSE, need_v = FALSE;
    int max_scroll_x, max_scroll_y;

    if (!g_mainwnd) return;
    GetClientRect(g_mainwnd, &client_rc);

    max_view_w = client_rc.right - CANVAS_X - 10;
    max_view_h = client_rc.bottom - 22 - 4 - CANVAS_Y;
    if (max_view_w < 50) max_view_w = 50;
    if (max_view_h < 50) max_view_h = 50;

    view_w = max_view_w;
    view_h = max_view_h;

    if (g_canvas_w > view_w) {
        need_h = TRUE;
        view_h -= 16;
    }
    if (g_canvas_h > view_h) {
        need_v = TRUE;
        view_w -= 16;
        if (!need_h && g_canvas_w > view_w) {
            need_h = TRUE;
            view_h -= 16;
        }
    }
    if (view_w < 50) view_w = 50;
    if (view_h < 50) view_h = 50;

    g_view_w = view_w;
    g_view_h = view_h;

    max_scroll_x = (g_canvas_w > g_view_w) ? (g_canvas_w - g_view_w) : 0;
    max_scroll_y = (g_canvas_h > g_view_h) ? (g_canvas_h - g_view_h) : 0;

    if (g_scroll_x > max_scroll_x) g_scroll_x = max_scroll_x;
    if (g_scroll_y > max_scroll_y) g_scroll_y = max_scroll_y;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_y < 0) g_scroll_y = 0;

    if (g_hwnd_sb_vert) {
        if (need_v) {
            MoveWindow(g_hwnd_sb_vert, CANVAS_X + g_view_w, CANVAS_Y, 16, g_view_h, TRUE);
            SetScrollRange(g_hwnd_sb_vert, SB_CTL, 0, max_scroll_y, FALSE);
            SetScrollPos(g_hwnd_sb_vert, SB_CTL, g_scroll_y, TRUE);
            ShowWindow(g_hwnd_sb_vert, SW_SHOW);
        } else {
            ShowWindow(g_hwnd_sb_vert, SW_HIDE);
        }
    }

    if (g_hwnd_sb_horz) {
        if (need_h) {
            MoveWindow(g_hwnd_sb_horz, CANVAS_X, CANVAS_Y + g_view_h, g_view_w, 16, TRUE);
            SetScrollRange(g_hwnd_sb_horz, SB_CTL, 0, max_scroll_x, FALSE);
            SetScrollPos(g_hwnd_sb_horz, SB_CTL, g_scroll_x, TRUE);
            ShowWindow(g_hwnd_sb_horz, SW_SHOW);
        } else {
            ShowWindow(g_hwnd_sb_horz, SW_HIDE);
        }
    }
    update_text_edit_pos();
}

static void blit_canvas_viewport(HDC target_dc, HDC src_dc)
{
    int disp_w, disp_h;
    if (!target_dc || !src_dc) return;

    disp_w = (g_canvas_w - g_scroll_x < g_view_w) ? (g_canvas_w - g_scroll_x) : g_view_w;
    disp_h = (g_canvas_h - g_scroll_y < g_view_h) ? (g_canvas_h - g_scroll_y) : g_view_h;

    if (disp_w > 0 && disp_h > 0) {
        BitBlt(target_dc, CANVAS_X, CANVAS_Y, disp_w, disp_h, src_dc, g_scroll_x, g_scroll_y, SRCCOPY);
    }
}

static BOOL screen_to_canvas(int sx, int sy, int *cx, int *cy)
{
    int disp_w = (g_canvas_w - g_scroll_x < g_view_w) ? (g_canvas_w - g_scroll_x) : g_view_w;
    int disp_h = (g_canvas_h - g_scroll_y < g_view_h) ? (g_canvas_h - g_scroll_y) : g_view_h;

    if (sx >= CANVAS_X && sx < CANVAS_X + disp_w &&
        sy >= CANVAS_Y && sy < CANVAS_Y + disp_h) {
        int x = sx - CANVAS_X + g_scroll_x;
        int y = sy - CANVAS_Y + g_scroll_y;
        if (x < 0) x = 0;
        if (x >= g_canvas_w) x = g_canvas_w - 1;
        if (y < 0) y = 0;
        if (y >= g_canvas_h) y = g_canvas_h - 1;
        *cx = x;
        *cy = y;
        return TRUE;
    }
    return FALSE;
}

static void render_canvas_to_dc(HDC target_dc)
{
    HPEN hp, old_pen;
    HBRUSH old_br, br;
    RECT r;
    int disp_w, disp_h;

    if (!g_hdc_canvas || !g_hdc_temp) return;

    disp_w = (g_canvas_w - g_scroll_x < g_view_w) ? (g_canvas_w - g_scroll_x) : g_view_w;
    disp_h = (g_canvas_h - g_scroll_y < g_view_h) ? (g_canvas_h - g_scroll_y) : g_view_h;

    /* Copy base canvas to temp composite buffer */
    BitBlt(g_hdc_temp, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);

    /* If selection is floating, blit it on top */
    if (g_has_sel && g_sel_floating && g_hdc_sel && g_hbmp_sel) {
        BitBlt(g_hdc_temp, g_sel_rc.left, g_sel_rc.top, g_sel_w, g_sel_h, g_hdc_sel, 0, 0, SRCCOPY);
    }

    /* If selection is active, draw dotted focus border */
    if (g_has_sel) {
        hp = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
        old_pen = (HPEN)SelectObject(g_hdc_temp, hp);
        old_br = (HBRUSH)SelectObject(g_hdc_temp, GetStockObject(NULL_BRUSH));
        Rectangle(g_hdc_temp, g_sel_rc.left, g_sel_rc.top, g_sel_rc.right, g_sel_rc.bottom);
        SelectObject(g_hdc_temp, old_br);
        SelectObject(g_hdc_temp, old_pen);
        DeleteObject(hp);
    }

    /* Blit composite temp buffer to screen window inside viewport bounds */
    if (disp_w > 0 && disp_h > 0) {
        BitBlt(target_dc, CANVAS_X, CANVAS_Y, disp_w, disp_h, g_hdc_temp, g_scroll_x, g_scroll_y, SRCCOPY);
    }

    /* If canvas is smaller than viewport, clear margins */
    if (disp_w < g_view_w) {
        SetRect(&r, CANVAS_X + disp_w, CANVAS_Y, CANVAS_X + g_view_w, CANVAS_Y + g_view_h);
        br = (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(target_dc, &r, br);
    }
    if (disp_h < g_view_h) {
        SetRect(&r, CANVAS_X, CANVAS_Y + disp_h, CANVAS_X + g_view_w, CANVAS_Y + g_view_h);
        br = (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(target_dc, &r, br);
    }
}

static void refresh_canvas(void)
{
    if (g_mainwnd) {
        HDC dc = GetDC(g_mainwnd);
        if (dc) {
            render_canvas_to_dc(dc);
            ReleaseDC(g_mainwnd, dc);
        }
    }
}

static void commit_selection(void)
{
    if (!g_has_sel) return;

    if (g_sel_floating && g_hdc_sel && g_hbmp_sel && g_hdc_canvas) {
        BitBlt(g_hdc_canvas, g_sel_rc.left, g_sel_rc.top, g_sel_w, g_sel_h, g_hdc_sel, 0, 0, SRCCOPY);
        SelectObject(g_hdc_sel, g_hbmp_old_sel);
        DeleteObject(g_hbmp_sel);
        g_hbmp_sel = NULL;
        g_sel_floating = FALSE;
    }
    g_has_sel = FALSE;
    g_sel_dragging = FALSE;
    refresh_canvas();
}

static void commit_text(void)
{
    char buf[256];
    int len;

    if (!g_text_active || !g_hwnd_text_edit) return;

    g_text_active = FALSE;
    len = GetWindowText(g_hwnd_text_edit, buf, sizeof(buf));
    ShowWindow(g_hwnd_text_edit, SW_HIDE);

    if (len > 0) {
        save_undo_state();
        SetBkMode(g_hdc_canvas, TRANSPARENT);
        SetTextColor(g_hdc_canvas, g_col_primary);
        TextOut(g_hdc_canvas, g_text_cx, g_text_cy, buf, len);
        refresh_canvas();
        set_status("Text added to canvas.");
    }
}

static void cancel_text(void)
{
    if (!g_text_active || !g_hwnd_text_edit) return;
    g_text_active = FALSE;
    ShowWindow(g_hwnd_text_edit, SW_HIDE);
    set_status("Text input cancelled.");
}

static void update_text_edit_pos(void)
{
    int sx, sy, max_w, edit_w;

    if (!g_text_active || !g_hwnd_text_edit) return;

    sx = CANVAS_X + g_text_cx - g_scroll_x;
    sy = CANVAS_Y + g_text_cy - g_scroll_y;
    max_w = (CANVAS_X + g_view_w) - sx;
    edit_w = 200;
    if (edit_w > max_w) edit_w = max_w;
    if (edit_w < 60) edit_w = 60;

    if (sx >= CANVAS_X - 100 && sx < CANVAS_X + g_view_w &&
        sy >= CANVAS_Y - 20 && sy < CANVAS_Y + g_view_h) {
        MoveWindow(g_hwnd_text_edit, sx, sy, edit_w, 20, TRUE);
        ShowWindow(g_hwnd_text_edit, SW_SHOW);
    } else {
        ShowWindow(g_hwnd_text_edit, SW_HIDE);
    }
}

LRESULT CALLBACK _export TextEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            commit_text();
            if (g_mainwnd) SetFocus(g_mainwnd);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            cancel_text();
            if (g_mainwnd) SetFocus(g_mainwnd);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 0x0D)) {
        commit_text();
        if (g_mainwnd) SetFocus(g_mainwnd);
        return 0;
    }
    return CallWindowProc((FARPROC)g_old_text_edit_proc, hwnd, msg, wp, lp);
}

static void do_copy(void)
{
    HDC scr;
    HBITMAP hbmp;

    if (!g_mainwnd) return;
    scr = GetDC(g_mainwnd);

    if (g_has_sel) {
        hbmp = CreateCompatibleBitmap(scr, g_sel_w, g_sel_h);
        if (g_hbmp_clip) DeleteObject(g_hbmp_clip);
        g_hbmp_clip = hbmp;
        SelectObject(g_hdc_clip, g_hbmp_clip);
        g_clip_w = g_sel_w;
        g_clip_h = g_sel_h;
        g_has_clip = TRUE;

        if (g_sel_floating && g_hdc_sel) {
            BitBlt(g_hdc_clip, 0, 0, g_sel_w, g_sel_h, g_hdc_sel, 0, 0, SRCCOPY);
        } else {
            BitBlt(g_hdc_clip, 0, 0, g_sel_w, g_sel_h, g_hdc_canvas, g_sel_rc.left, g_sel_rc.top, SRCCOPY);
        }
        set_status("Selection copied to clipboard.");
    } else {
        /* Copy whole canvas */
        hbmp = CreateCompatibleBitmap(scr, g_canvas_w, g_canvas_h);
        if (g_hbmp_clip) DeleteObject(g_hbmp_clip);
        g_hbmp_clip = hbmp;
        SelectObject(g_hdc_clip, g_hbmp_clip);
        g_clip_w = g_canvas_w;
        g_clip_h = g_canvas_h;
        g_has_clip = TRUE;
        BitBlt(g_hdc_clip, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
        set_status("Entire canvas copied to clipboard.");
    }
    ReleaseDC(g_mainwnd, scr);
}

static void do_cut(void)
{
    HBRUSH br;
    if (!g_has_sel) {
        set_status("Nothing selected to cut.");
        return;
    }
    do_copy();
    save_undo_state();
    if (!g_sel_floating) {
        br = CreateSolidBrush(g_col_secondary);
        FillRect(g_hdc_canvas, &g_sel_rc, br);
        DeleteObject(br);
    } else {
        if (g_hbmp_sel) {
            SelectObject(g_hdc_sel, g_hbmp_old_sel);
            DeleteObject(g_hbmp_sel);
            g_hbmp_sel = NULL;
        }
        g_sel_floating = FALSE;
    }
    g_has_sel = FALSE;
    g_sel_dragging = FALSE;
    refresh_canvas();
    set_status("Selection cut to clipboard.");
}

static void do_paste(void)
{
    HDC scr;

    if (!g_has_clip || !g_mainwnd) {
        set_status("Clipboard is empty.");
        return;
    }
    commit_selection();
    save_undo_state();

    scr = GetDC(g_mainwnd);
    if (g_hbmp_sel) {
        SelectObject(g_hdc_sel, g_hbmp_old_sel);
        DeleteObject(g_hbmp_sel);
    }
    g_hbmp_sel = CreateCompatibleBitmap(scr, g_clip_w, g_clip_h);
    ReleaseDC(g_mainwnd, scr);

    SelectObject(g_hdc_sel, g_hbmp_sel);
    BitBlt(g_hdc_sel, 0, 0, g_clip_w, g_clip_h, g_hdc_clip, 0, 0, SRCCOPY);

    g_has_sel = TRUE;
    g_sel_floating = TRUE;
    g_sel_w = g_clip_w;
    g_sel_h = g_clip_h;
    g_sel_rc.left = 10;
    g_sel_rc.top = 10;
    g_sel_rc.right = 10 + g_sel_w;
    g_sel_rc.bottom = 10 + g_sel_h;
    g_active_tool = TOOL_SELECT;

    InvalidateRect(g_mainwnd, NULL, FALSE);
    refresh_canvas();
    set_status("Pasted from clipboard. Drag inside to move, click outside to stamp.");
}

static void do_delete(void)
{
    HBRUSH br;
    if (!g_has_sel) return;
    save_undo_state();
    if (!g_sel_floating) {
        br = CreateSolidBrush(g_col_secondary);
        FillRect(g_hdc_canvas, &g_sel_rc, br);
        DeleteObject(br);
    } else {
        if (g_hbmp_sel) {
            SelectObject(g_hdc_sel, g_hbmp_old_sel);
            DeleteObject(g_hbmp_sel);
            g_hbmp_sel = NULL;
        }
        g_sel_floating = FALSE;
    }
    g_has_sel = FALSE;
    g_sel_dragging = FALSE;
    refresh_canvas();
    set_status("Selection cleared.");
}

static void do_select_all(void)
{
    commit_selection();
    g_has_sel = TRUE;
    g_sel_floating = FALSE;
    g_sel_rc.left = 0;
    g_sel_rc.top = 0;
    g_sel_rc.right = g_canvas_w;
    g_sel_rc.bottom = g_canvas_h;
    g_sel_w = g_canvas_w;
    g_sel_h = g_canvas_h;
    g_active_tool = TOOL_SELECT;
    InvalidateRect(g_mainwnd, NULL, FALSE);
    refresh_canvas();
    set_status("Entire canvas selected.");
}

static void do_undo(void)
{
    cancel_text();
    if (!g_can_undo) {
        MessageBox(g_mainwnd, "No action available to undo.", "Undo", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_has_sel) {
        if (g_sel_floating && g_hbmp_sel) {
            SelectObject(g_hdc_sel, g_hbmp_old_sel);
            DeleteObject(g_hbmp_sel);
            g_hbmp_sel = NULL;
        }
        g_has_sel = FALSE;
        g_sel_floating = FALSE;
        g_sel_dragging = FALSE;
    }
    BitBlt(g_hdc_canvas, 0, 0, g_canvas_w, g_canvas_h, g_hdc_undo, 0, 0, SRCCOPY);
    g_can_undo = FALSE;
    refresh_canvas();
    set_status("Undid last action.");
}

static void clear_canvas(void)
{
    HBRUSH br;
    RECT r;
    if (g_has_sel) {
        do_delete();
        return;
    }
    save_undo_state();
    br = CreateSolidBrush(g_col_secondary);
    SetRect(&r, 0, 0, g_canvas_w, g_canvas_h);
    FillRect(g_hdc_canvas, &r, br);
    DeleteObject(br);
    refresh_canvas();
    set_status("Canvas cleared with background color.");
}

static void invert_canvas(void)
{
    save_undo_state();
    if (g_has_sel) {
        if (g_sel_floating && g_hdc_sel) {
            PatBlt(g_hdc_sel, 0, 0, g_sel_w, g_sel_h, DSTINVERT);
        } else {
            PatBlt(g_hdc_canvas, g_sel_rc.left, g_sel_rc.top, g_sel_w, g_sel_h, DSTINVERT);
        }
        refresh_canvas();
        set_status("Selection colors inverted.");
        return;
    }
    PatBlt(g_hdc_canvas, 0, 0, g_canvas_w, g_canvas_h, DSTINVERT);
    refresh_canvas();
    set_status("Colors inverted.");
}

static void flip_canvas_h(void)
{
    int x, y;
    COLORREF cl, cr;
    save_undo_state();
    if (g_has_sel && g_sel_floating && g_hdc_sel) {
        for (y = 0; y < g_sel_h; y++) {
            for (x = 0; x < g_sel_w / 2; x++) {
                cl = GetPixel(g_hdc_sel, x, y);
                cr = GetPixel(g_hdc_sel, g_sel_w - 1 - x, y);
                SetPixel(g_hdc_sel, x, y, cr);
                SetPixel(g_hdc_sel, g_sel_w - 1 - x, y, cl);
            }
        }
        refresh_canvas();
        set_status("Selection flipped horizontally.");
        return;
    }
    for (y = 0; y < g_canvas_h; y++) {
        for (x = 0; x < g_canvas_w / 2; x++) {
            cl = GetPixel(g_hdc_canvas, x, y);
            cr = GetPixel(g_hdc_canvas, g_canvas_w - 1 - x, y);
            SetPixel(g_hdc_canvas, x, y, cr);
            SetPixel(g_hdc_canvas, g_canvas_w - 1 - x, y, cl);
        }
    }
    refresh_canvas();
    set_status("Canvas flipped horizontally.");
}

static void flip_canvas_v(void)
{
    int x, y;
    COLORREF ct, cb;
    save_undo_state();
    if (g_has_sel && g_sel_floating && g_hdc_sel) {
        for (y = 0; y < g_sel_h / 2; y++) {
            for (x = 0; x < g_sel_w; x++) {
                ct = GetPixel(g_hdc_sel, x, y);
                cb = GetPixel(g_hdc_sel, x, g_sel_h - 1 - y);
                SetPixel(g_hdc_sel, x, y, cb);
                SetPixel(g_hdc_sel, x, g_sel_h - 1 - y, ct);
            }
        }
        refresh_canvas();
        set_status("Selection flipped vertically.");
        return;
    }
    for (y = 0; y < g_canvas_h / 2; y++) {
        for (x = 0; x < g_canvas_w; x++) {
            ct = GetPixel(g_hdc_canvas, x, y);
            cb = GetPixel(g_hdc_canvas, x, g_canvas_h - 1 - y);
            SetPixel(g_hdc_canvas, x, y, cb);
            SetPixel(g_hdc_canvas, x, g_canvas_h - 1 - y, ct);
        }
    }
    refresh_canvas();
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

    g_hdc_sel = CreateCompatibleDC(scr);
    g_hbmp_old_sel = (HBITMAP)SelectObject(g_hdc_sel, CreateCompatibleBitmap(scr, 1, 1));

    g_hdc_clip = CreateCompatibleDC(scr);
    g_hbmp_old_clip = (HBITMAP)SelectObject(g_hdc_clip, CreateCompatibleBitmap(scr, 1, 1));

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
    if (g_hdc_sel) {
        SelectObject(g_hdc_sel, g_hbmp_old_sel);
        if (g_hbmp_sel) DeleteObject(g_hbmp_sel);
        DeleteDC(g_hdc_sel);
        g_hdc_sel = NULL;
    }
    if (g_hdc_clip) {
        SelectObject(g_hdc_clip, g_hbmp_old_clip);
        if (g_hbmp_clip) DeleteObject(g_hbmp_clip);
        DeleteDC(g_hdc_clip);
        g_hdc_clip = NULL;
    }
}

static BOOL change_canvas_size(int new_w, int new_h, BOOL preserve_content)
{
    HDC scr, hdc_mem;
    HBITMAP bmp_c, bmp_u, bmp_t, old_mem_bmp;
    RECT r;
    HBRUSH br;
    int copy_w, copy_h;

    if (new_w < 16) new_w = 16;
    if (new_h < 16) new_h = 16;
    if (new_w > 1280) new_w = 1280;
    if (new_h > 1024) new_h = 1024;

    if (!g_mainwnd || !g_hdc_canvas) return FALSE;
    scr = GetDC(g_mainwnd);
    if (!scr) return FALSE;

    bmp_c = CreateCompatibleBitmap(scr, new_w, new_h);
    bmp_u = CreateCompatibleBitmap(scr, new_w, new_h);
    bmp_t = CreateCompatibleBitmap(scr, new_w, new_h);

    if (!bmp_c || !bmp_u || !bmp_t) {
        if (bmp_c) DeleteObject(bmp_c);
        if (bmp_u) DeleteObject(bmp_u);
        if (bmp_t) DeleteObject(bmp_t);
        ReleaseDC(g_mainwnd, scr);
        return FALSE;
    }

    hdc_mem = CreateCompatibleDC(scr);
    if (hdc_mem) {
        old_mem_bmp = (HBITMAP)SelectObject(hdc_mem, bmp_c);
        br = CreateSolidBrush(RGB(255, 255, 255));
        SetRect(&r, 0, 0, new_w, new_h);
        FillRect(hdc_mem, &r, br);

        if (preserve_content) {
            copy_w = (g_canvas_w < new_w) ? g_canvas_w : new_w;
            copy_h = (g_canvas_h < new_h) ? g_canvas_h : new_h;
            BitBlt(hdc_mem, 0, 0, copy_w, copy_h, g_hdc_canvas, 0, 0, SRCCOPY);
        }

        SelectObject(hdc_mem, bmp_u);
        FillRect(hdc_mem, &r, br);
        if (preserve_content) {
            BitBlt(hdc_mem, 0, 0, copy_w, copy_h, g_hdc_canvas, 0, 0, SRCCOPY);
        }
        DeleteObject(br);

        SelectObject(hdc_mem, old_mem_bmp);
        DeleteDC(hdc_mem);
    }

    SelectObject(g_hdc_canvas, bmp_c);
    DeleteObject(g_hbmp_canvas);
    g_hbmp_canvas = bmp_c;

    SelectObject(g_hdc_undo, bmp_u);
    DeleteObject(g_hbmp_undo);
    g_hbmp_undo = bmp_u;

    SelectObject(g_hdc_temp, bmp_t);
    DeleteObject(g_hbmp_temp);
    g_hbmp_temp = bmp_t;

    ReleaseDC(g_mainwnd, scr);

    g_canvas_w = new_w;
    g_canvas_h = new_h;
    g_can_undo = FALSE;

    update_scrollbars();
    InvalidateRect(g_mainwnd, NULL, TRUE);
    return TRUE;
}

static BOOL resize_canvas_buffers(int new_w, int new_h)
{
    return change_canvas_size(new_w, new_h, FALSE);
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
static BYTE s_bmp_row[4096];
static BMPRGBQUAD s_bmp_palette[256];

static BOOL save_canvas_to_bmp(const char *path)
{
    HFILE fh;
    BMPFILEHDR fhdr;
    BMPINFOHDR ihdr;
    int x, y, row_bytes, pad_bytes, idx;
    COLORREF col;
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
            s_bmp_row[idx]     = (BYTE)((col >> 16) & 0xFF); /* Blue */
            s_bmp_row[idx + 1] = (BYTE)((col >> 8) & 0xFF);  /* Green */
            s_bmp_row[idx + 2] = (BYTE)(col & 0xFF);         /* Red */
            idx += 3;
        }
        _lwrite(fh, (LPCSTR)s_bmp_row, row_bytes);
        if (pad_bytes > 0)
            _lwrite(fh, (LPCSTR)&zero, pad_bytes);
    }
    _lclose(fh);

    lstrcpy(g_curfile, path);
    lstrcpy(g_titlebuf, "PaintCraft - ");
    lstrcat(g_titlebuf, g_curfile);
    SetWindowText(g_mainwnd, g_titlebuf);
    set_status("File saved successfully.");
    return TRUE;
}

static BOOL load_canvas_from_bmp(const char *path)
{
    HFILE fh;
    BMPFILEHDR fhdr;
    BMPINFOHDR ihdr;
    int x, y, dw, dh;
    int img_w, img_h, bpp, top_down;
    DWORD row_stride, bytes_to_read, extra_skip;
    DWORD num_colors, pal_bytes;
    COLORREF col;
    int screen_y;
    HBRUSH br;
    RECT r;

    OutputDebugString("PAINT: load_canvas_from_bmp\n");
    fh = _lopen(path, 0);
    if (fh == HFILE_ERROR) {
        OutputDebugString("PAINT: _lopen failed\n");
        MessageBox(g_mainwnd, "Unable to open selected file.", "Open Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    if (_lread(fh, (LPSTR)&fhdr, sizeof(fhdr)) != sizeof(fhdr)) {
        _lclose(fh);
        OutputDebugString("PAINT: read fhdr failed\n");
        MessageBox(g_mainwnd, "Failed reading BMP file header.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    if (fhdr.bfType != 0x4D42) {
        _lclose(fh);
        OutputDebugString("PAINT: not BM\n");
        MessageBox(g_mainwnd, "Selected file is not a valid BMP bitmap.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    if (_lread(fh, (LPSTR)&ihdr, sizeof(ihdr)) != sizeof(ihdr)) {
        _lclose(fh);
        OutputDebugString("PAINT: read ihdr failed\n");
        MessageBox(g_mainwnd, "Failed reading BMP info header.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    if (ihdr.biCompression != 0) {
        _lclose(fh);
        OutputDebugString("PAINT: compressed BMP\n");
        MessageBox(g_mainwnd, "Compressed BMP files (RLE) are not supported.", "File Format", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    bpp = (int)ihdr.biBitCount;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        _lclose(fh);
        OutputDebugString("PAINT: unsupported bpp\n");
        MessageBox(g_mainwnd, "Unsupported bit depth. Supported: 1, 4, 8, 24, 32 bpp.", "File Format", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    img_w = (int)ihdr.biWidth;
    if (img_w <= 0) {
        _lclose(fh);
        MessageBox(g_mainwnd, "Invalid bitmap width.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    if (ihdr.biHeight < 0) {
        top_down = 1;
        img_h = (int)(-ihdr.biHeight);
    } else {
        top_down = 0;
        img_h = (int)ihdr.biHeight;
    }
    if (img_h <= 0) {
        _lclose(fh);
        MessageBox(g_mainwnd, "Invalid bitmap height.", "Format Error", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    /* Read palette if indexed (<= 8 bpp) */
    if (bpp <= 8) {
        num_colors = ihdr.biClrUsed;
        if (num_colors == 0)
            num_colors = 1UL << bpp;
        if (num_colors > 256)
            num_colors = 256;

        _llseek(fh, (LONG)(sizeof(BMPFILEHDR) + ihdr.biSize), 0);
        pal_bytes = num_colors * sizeof(BMPRGBQUAD);
        memset(s_bmp_palette, 0, sizeof(s_bmp_palette));
        _lread(fh, (LPSTR)s_bmp_palette, (WORD)pal_bytes);
    }

    /* Commit selection & save undo before modifying canvas */
    commit_selection();
    save_undo_state();

    /* Resize canvas buffers if image dimensions differ from current canvas */
    if (img_w != g_canvas_w || img_h != g_canvas_h) {
        if (!resize_canvas_buffers(img_w, img_h)) {
            _lclose(fh);
            MessageBox(g_mainwnd, "Not enough memory for image dimensions.", "Memory Error", MB_OK | MB_ICONSTOP);
            return FALSE;
        }
    }

    /* Clear canvas to clean white before drawing loaded pixels */
    br = CreateSolidBrush(RGB(255, 255, 255));
    SetRect(&r, 0, 0, g_canvas_w, g_canvas_h);
    FillRect(g_hdc_canvas, &r, br);
    DeleteObject(br);

    /* Row stride in bytes, aligned to 4 bytes */
    row_stride = (((DWORD)img_w * bpp + 31) / 32) * 4;
    bytes_to_read = row_stride;
    if (bytes_to_read > sizeof(s_bmp_row)) {
        bytes_to_read = sizeof(s_bmp_row);
        extra_skip = row_stride - bytes_to_read;
    } else {
        extra_skip = 0;
    }

    dw = img_w;
    dh = img_h;

    OutputDebugString("PAINT: seeking to bits\n");
    _llseek(fh, (LONG)fhdr.bfOffBits, 0);

    OutputDebugString("PAINT: reading rows\n");
    for (y = 0; y < img_h; y++) {
        screen_y = top_down ? y : (img_h - 1 - y);

        _lread(fh, (LPSTR)s_bmp_row, (WORD)bytes_to_read);
        if (extra_skip > 0)
            _llseek(fh, (LONG)extra_skip, 1);

        if (screen_y < dh) {
            if (bpp == 1) {
                for (x = 0; x < dw; x++) {
                    int byte_idx = x >> 3;
                    if (byte_idx < (int)bytes_to_read) {
                        int bit_idx = 7 - (x & 7);
                        int pal_idx = (s_bmp_row[byte_idx] >> bit_idx) & 1;
                        col = RGB(s_bmp_palette[pal_idx].rgbRed,
                                  s_bmp_palette[pal_idx].rgbGreen,
                                  s_bmp_palette[pal_idx].rgbBlue);
                        SetPixel(g_hdc_canvas, x, screen_y, col);
                    }
                }
            } else if (bpp == 4) {
                for (x = 0; x < dw; x++) {
                    int byte_idx = x >> 1;
                    if (byte_idx < (int)bytes_to_read) {
                        int pal_idx = ((x & 1) == 0) ? ((s_bmp_row[byte_idx] >> 4) & 0x0F)
                                                     : (s_bmp_row[byte_idx] & 0x0F);
                        col = RGB(s_bmp_palette[pal_idx].rgbRed,
                                  s_bmp_palette[pal_idx].rgbGreen,
                                  s_bmp_palette[pal_idx].rgbBlue);
                        SetPixel(g_hdc_canvas, x, screen_y, col);
                    }
                }
            } else if (bpp == 8) {
                for (x = 0; x < dw; x++) {
                    if (x < (int)bytes_to_read) {
                        int pal_idx = s_bmp_row[x];
                        col = RGB(s_bmp_palette[pal_idx].rgbRed,
                                  s_bmp_palette[pal_idx].rgbGreen,
                                  s_bmp_palette[pal_idx].rgbBlue);
                        SetPixel(g_hdc_canvas, x, screen_y, col);
                    }
                }
            } else if (bpp == 24) {
                int idx = 0;
                for (x = 0; x < dw; x++) {
                    if (idx + 2 < (int)bytes_to_read) {
                        col = RGB(s_bmp_row[idx + 2], s_bmp_row[idx + 1], s_bmp_row[idx]);
                        SetPixel(g_hdc_canvas, x, screen_y, col);
                    }
                    idx += 3;
                }
            } else if (bpp == 32) {
                int idx = 0;
                for (x = 0; x < dw; x++) {
                    if (idx + 2 < (int)bytes_to_read) {
                        col = RGB(s_bmp_row[idx + 2], s_bmp_row[idx + 1], s_bmp_row[idx]);
                        SetPixel(g_hdc_canvas, x, screen_y, col);
                    }
                    idx += 4;
                }
            }
        }
    }
    _lclose(fh);
    OutputDebugString("PAINT: rows read OK, updating viewport\n");

    update_scrollbars();
    InvalidateRect(g_mainwnd, NULL, TRUE);

    lstrcpy(g_curfile, path);
    lstrcpy(g_titlebuf, "PaintCraft - ");
    lstrcat(g_titlebuf, g_curfile);
    SetWindowText(g_mainwnd, g_titlebuf);
    {
        char load_msg[80];
        wsprintf(load_msg, "Loaded BMP image (%d x %d px).", g_canvas_w, g_canvas_h);
        set_status(load_msg);
    }
    OutputDebugString("PAINT: load_canvas_from_bmp complete\n");
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
    commit_selection();
    if (g_curfile[0])
        save_canvas_to_bmp(g_curfile);
    else
        do_file_save_as();
}

BOOL CALLBACK _export CanvasSizeDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemInt(dlg, IDC_CANVAS_W, g_canvas_w, FALSE);
        SetDlgItemInt(dlg, IDC_CANVAS_H, g_canvas_h, FALSE);
        SetFocus(GetDlgItem(dlg, IDC_CANVAS_W));
        SendDlgItemMessage(dlg, IDC_CANVAS_W, EM_SETSEL, 0, MAKELONG(0, -1));
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PRESET_320:
            SetDlgItemInt(dlg, IDC_CANVAS_W, 320, FALSE);
            SetDlgItemInt(dlg, IDC_CANVAS_H, 200, FALSE);
            return TRUE;
        case IDC_PRESET_640:
            SetDlgItemInt(dlg, IDC_CANVAS_W, 640, FALSE);
            SetDlgItemInt(dlg, IDC_CANVAS_H, 480, FALSE);
            return TRUE;
        case IDC_PRESET_800:
            SetDlgItemInt(dlg, IDC_CANVAS_W, 800, FALSE);
            SetDlgItemInt(dlg, IDC_CANVAS_H, 600, FALSE);
            return TRUE;
        case IDC_PRESET_DEF:
            SetDlgItemInt(dlg, IDC_CANVAS_W, CANVAS_DEF_W, FALSE);
            SetDlgItemInt(dlg, IDC_CANVAS_H, CANVAS_DEF_H, FALSE);
            return TRUE;
        case IDOK:
            {
                BOOL ok_w = FALSE, ok_h = FALSE;
                int nw = GetDlgItemInt(dlg, IDC_CANVAS_W, &ok_w, FALSE);
                int nh = GetDlgItemInt(dlg, IDC_CANVAS_H, &ok_h, FALSE);
                if (!ok_w || !ok_h || nw < 16 || nh < 16 || nw > 1280 || nh > 1024) {
                    MessageBox(dlg, "Please enter valid canvas dimensions between 16 and 1024 pixels.", "Canvas Size", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                if (nw != g_canvas_w || nh != g_canvas_h) {
                    if (change_canvas_size(nw, nh, TRUE)) {
                        char sz_msg[80];
                        wsprintf(sz_msg, "Canvas resized to %d x %d px.", nw, nh);
                        set_status(sz_msg);
                    }
                }
                EndDialog(dlg, IDOK);
                return TRUE;
            }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void do_canvas_size_dlg(void)
{
    FARPROC proc;
    commit_selection();
    commit_text();
    proc = MakeProcInstance((FARPROC)CanvasSizeDlgProc, g_instance);
    DialogBox(g_instance, MAKEINTRESOURCE(IDD_CANVAS_SIZE), g_mainwnd, (DLGPROC)proc);
    FreeProcInstance(proc);
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

    /* 5b. Canvas Size Button & Dimension Label */
    SetRect(&r, 330, 52, 435, 76);
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
    SetTextColor(dc, RGB(40, 44, 52));
    DrawText(dc, "Canvas Size...", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    {
        char sz_dim[40];
        wsprintf(sz_dim, "Canvas: %d x %d", g_canvas_w, g_canvas_h);
        SetRect(&r, 442, 55, 590, 75);
        SetTextColor(dc, RGB(70, 75, 85));
        DrawText(dc, sz_dim, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 6. Border around Viewport */
    hp = CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
    old_pen = (HPEN)SelectObject(dc, hp);
    old_br = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, CANVAS_X - 1, CANVAS_Y - 1, CANVAS_X + g_view_w + 1, CANVAS_Y + g_view_h + 1);
    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(hp);

    /* Corner filler between scrollbars if both are active */
    if (g_hwnd_sb_vert && g_hwnd_sb_horz &&
        IsWindowVisible(g_hwnd_sb_vert) && IsWindowVisible(g_hwnd_sb_horz)) {
        RECT corner_rc;
        SetRect(&corner_rc, CANVAS_X + g_view_w, CANVAS_Y + g_view_h,
                CANVAS_X + g_view_w + 16, CANVAS_Y + g_view_h + 16);
        br = (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(dc, &corner_rc, br);
    }

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
    int x, y, i, swatch_idx, cx, cy;
    char coord_buf[80];
    COLORREF target_col;
    HBRUSH fill_br, old_fill_br;

    switch (msg) {
    case WM_CREATE:
        g_mainwnd = hwnd;
        init_canvas_buffers(hwnd);
        g_hwnd_sb_vert = CreateWindow(
            "SCROLLBAR", "",
            WS_CHILD | SBS_VERT,
            0, 0, 16, 100,
            hwnd, (HMENU)2001, g_instance, NULL
        );
        g_hwnd_sb_horz = CreateWindow(
            "SCROLLBAR", "",
            WS_CHILD | SBS_HORZ,
            0, 0, 100, 16,
            hwnd, (HMENU)2002, g_instance, NULL
        );
        g_hwnd_text_edit = CreateWindow(
            "EDIT", "",
            WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 100, 20,
            hwnd, (HMENU)2003, g_instance, NULL
        );
        if (g_hwnd_text_edit) {
            g_old_text_edit_proc = (FARPROC)SetWindowLong(g_hwnd_text_edit, -4 /* GWL_WNDPROC */, (LONG)TextEditProc);
        }
        update_scrollbars();
        lstrcpy(g_statustext, "PaintCraft - Ready. LMB: Primary Color, RMB: Background Color.");
        return 0;

    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        draw_modern_ui(dc);
        render_canvas_to_dc(dc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SIZE:
        update_scrollbars();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_VSCROLL:
        {
            int code = (int)wparam;
            int pos = g_scroll_y;
            int max_scroll = (g_canvas_h > g_view_h) ? (g_canvas_h - g_view_h) : 0;
            switch (code) {
            case SB_LINEUP:     pos -= 16; break;
            case SB_LINEDOWN:   pos += 16; break;
            case SB_PAGEUP:     pos -= g_view_h / 2; break;
            case SB_PAGEDOWN:   pos += g_view_h / 2; break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
                pos = (int)LOWORD(lparam);
                break;
            default:
                return 0;
            }
            if (pos < 0) pos = 0;
            if (pos > max_scroll) pos = max_scroll;
            if (pos != g_scroll_y) {
                g_scroll_y = pos;
                SetScrollPos(g_hwnd_sb_vert, SB_CTL, g_scroll_y, TRUE);
                update_text_edit_pos();
                refresh_canvas();
            }
        }
        return 0;

    case WM_HSCROLL:
        {
            int code = (int)wparam;
            int pos = g_scroll_x;
            int max_scroll = (g_canvas_w > g_view_w) ? (g_canvas_w - g_view_w) : 0;
            switch (code) {
            case SB_LINEUP:     pos -= 16; break;
            case SB_LINEDOWN:   pos += 16; break;
            case SB_PAGEUP:     pos -= g_view_w / 2; break;
            case SB_PAGEDOWN:   pos += g_view_w / 2; break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
                pos = (int)LOWORD(lparam);
                break;
            default:
                return 0;
            }
            if (pos < 0) pos = 0;
            if (pos > max_scroll) pos = max_scroll;
            if (pos != g_scroll_x) {
                g_scroll_x = pos;
                SetScrollPos(g_hwnd_sb_horz, SB_CTL, g_scroll_x, TRUE);
                update_text_edit_pos();
                refresh_canvas();
            }
        }
        return 0;

    case WM_CTLCOLOR:
        if (LOWORD(lparam) == (WORD)g_hwnd_text_edit) {
            COLORREF tc = g_col_primary;
            if (tc == RGB(255, 255, 255)) tc = RGB(0, 0, 0);
            SetTextColor((HDC)wparam, tc);
            SetBkColor((HDC)wparam, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_KEYDOWN:
        {
            BOOL ctrl = (GetKeyState(VK_CONTROL) < 0);
            if (wparam == VK_DELETE) {
                do_delete();
                return 0;
            }
            if (ctrl) {
                switch (wparam) {
                case 'Z': case 'z': do_undo(); return 0;
                case 'X': case 'x': do_cut(); return 0;
                case 'C': case 'c': do_copy(); return 0;
                case 'V': case 'v': do_paste(); return 0;
                case 'A': case 'a': do_select_all(); return 0;
                case 'E': case 'e': do_canvas_size_dlg(); return 0;
                case 'N': case 'n':
                    commit_selection();
                    if (g_canvas_w != CANVAS_DEF_W || g_canvas_h != CANVAS_DEF_H) {
                        resize_canvas_buffers(CANVAS_DEF_W, CANVAS_DEF_H);
                    }
                    clear_canvas();
                    g_curfile[0] = '\0';
                    SetWindowText(hwnd, "PaintCraft - [Untitled]");
                    update_scrollbars();
                    InvalidateRect(hwnd, NULL, TRUE);
                    set_status("New canvas created.");
                    return 0;
                case 'O': case 'o': commit_selection(); do_file_open(); return 0;
                case 'S': case 's': commit_selection(); do_file_save(); return 0;
                }
            } else {
                switch (wparam) {
                case VK_UP:    SendMessage(hwnd, WM_VSCROLL, SB_LINEUP, 0); return 0;
                case VK_DOWN:  SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, 0); return 0;
                case VK_LEFT:  SendMessage(hwnd, WM_HSCROLL, SB_LINEUP, 0); return 0;
                case VK_RIGHT: SendMessage(hwnd, WM_HSCROLL, SB_LINEDOWN, 0); return 0;
                case VK_PRIOR: SendMessage(hwnd, WM_VSCROLL, SB_PAGEUP, 0); return 0;
                case VK_NEXT:  SendMessage(hwnd, WM_VSCROLL, SB_PAGEDOWN, 0); return 0;
                case VK_HOME:  SendMessage(hwnd, WM_HSCROLL, SB_THUMBPOSITION, 0); return 0;
                }
            }
        }
        break;

    case WM_COMMAND:
        if (wparam == 2003) {
            if (HIWORD(lparam) == 0x0200 /* EN_KILLFOCUS */) {
                commit_text();
            }
            return 0;
        }
        commit_text();
        switch (wparam) {
        case IDM_NEW:
            commit_selection();
            if (g_canvas_w != CANVAS_DEF_W || g_canvas_h != CANVAS_DEF_H) {
                resize_canvas_buffers(CANVAS_DEF_W, CANVAS_DEF_H);
            }
            clear_canvas();
            g_curfile[0] = '\0';
            SetWindowText(hwnd, "PaintCraft - [Untitled]");
            update_scrollbars();
            InvalidateRect(hwnd, NULL, TRUE);
            set_status("New canvas created.");
            break;
        case IDM_OPEN:      commit_selection(); do_file_open(); break;
        case IDM_SAVE:      commit_selection(); do_file_save(); break;
        case IDM_SAVEAS:    commit_selection(); do_file_save_as(); break;
        case IDM_UNDO:      do_undo(); break;
        case IDM_CUT:       do_cut(); break;
        case IDM_COPY:      do_copy(); break;
        case IDM_PASTE:     do_paste(); break;
        case IDM_DELETE:    do_delete(); break;
        case IDM_SELECTALL: do_select_all(); break;
        case IDM_CANVASSIZE: do_canvas_size_dlg(); break;
        case IDM_CLEAR:     clear_canvas(); break;
        case IDM_INVERT:    invert_canvas(); break;
        case IDM_FLIPH:     flip_canvas_h(); break;
        case IDM_FLIPV:     flip_canvas_v(); break;
        case IDM_EXIT:      SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        case IDM_ABOUT:
            MessageBox(
                hwnd,
                "PaintCraft Studio for Loader16 (i486) & DOS\r\n"
                "A modern pixel art & graphics editor.\r\n\r\n"
                "Features:\r\n"
                "- Double-buffered GDI canvas with scrolling\r\n"
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
                commit_text();
                if (i != TOOL_SELECT) {
                    commit_selection();
                }
                g_active_tool = i;
                g_poly_count = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                if (i == TOOL_TEXT)
                    set_status("Text tool: Click on canvas to type text. Press Enter to place.");
                else
                    set_status("Active tool changed.");
                return 0;
            }
        }

        /* 2. Brush Size */
        if (y >= 26 && y <= 44 && x >= 52 && x <= 182) {
            commit_text();
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
            commit_text();
            i = ((x - 240) / 44) + 1;
            if (i >= 1 && i <= 3) g_shape_style = i;
            InvalidateRect(hwnd, NULL, FALSE);
            set_status("Shape style changed.");
            return 0;
        }

        /* 4. Quick Actions */
        if (y >= 26 && y <= 44 && x >= 384 && x <= 604) {
            commit_text();
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
                if (g_text_active && g_hwnd_text_edit) {
                    InvalidateRect(g_hwnd_text_edit, NULL, TRUE);
                    SetFocus(g_hwnd_text_edit);
                }
                set_status("Primary color selected (LMB).");
                return 0;
            }
        }

        /* 5b. Canvas Size Button */
        if (y >= 52 && y <= 76 && x >= 330 && x <= 435) {
            do_canvas_size_dlg();
            return 0;
        }

        /* 6. Canvas Area */
        commit_text();
        if (screen_to_canvas(x, y, &cx, &cy)) {
            x = cx;
            y = cy;

            if (g_active_tool == TOOL_SELECT) {
                POINT pt;
                pt.x = x; pt.y = y;
                if (g_has_sel && PtInRect(&g_sel_rc, pt)) {
                    /* Drag existing selection */
                    SetCapture(hwnd);
                    if (!g_sel_floating) {
                        HDC scr;
                        save_undo_state();
                        scr = GetDC(hwnd);
                        if (g_hbmp_sel) {
                            SelectObject(g_hdc_sel, g_hbmp_old_sel);
                            DeleteObject(g_hbmp_sel);
                        }
                        g_hbmp_sel = CreateCompatibleBitmap(scr, g_sel_w, g_sel_h);
                        ReleaseDC(hwnd, scr);
                        SelectObject(g_hdc_sel, g_hbmp_sel);
                        BitBlt(g_hdc_sel, 0, 0, g_sel_w, g_sel_h, g_hdc_canvas, g_sel_rc.left, g_sel_rc.top, SRCCOPY);

                        /* If Ctrl is NOT pressed, clear old spot with secondary color */
                        if (!(GetKeyState(VK_CONTROL) < 0)) {
                            HBRUSH br = CreateSolidBrush(g_col_secondary);
                            FillRect(g_hdc_canvas, &g_sel_rc, br);
                            DeleteObject(br);
                        }
                        g_sel_floating = TRUE;
                    }
                    g_sel_dragging = TRUE;
                    g_sel_anchor_x = x - g_sel_rc.left;
                    g_sel_anchor_y = y - g_sel_rc.top;
                    set_status("Moving selection. Release to place.");
                    return 0;
                } else {
                    /* Clicked outside: commit previous selection, start new rectangle */
                    commit_selection();
                    SetCapture(hwnd);
                    g_is_dragging = TRUE;
                    g_start_x = g_cur_x = x;
                    g_start_y = g_cur_y = y;
                    set_status("Select: Drag cursor to define region.");
                    return 0;
                }
            } else {
                commit_selection();
            }

            if (g_active_tool != TOOL_TEXT) {
                commit_text();
                SetCapture(hwnd);
            }

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
                    blit_canvas_viewport(dc, g_hdc_canvas);
                } else if (g_active_tool == TOOL_BRUSH) {
                    paint_brush_disc(g_hdc_canvas, x, y, g_brush_size / 2, g_col_primary);
                    blit_canvas_viewport(dc, g_hdc_canvas);
                } else if (g_active_tool == TOOL_AIRBRUSH) {
                    draw_spray(g_hdc_canvas, x, y, g_brush_size * 3 + 4, g_col_primary);
                    blit_canvas_viewport(dc, g_hdc_canvas);
                } else if (g_active_tool == TOOL_ERASER) {
                    draw_normal_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_secondary);
                    blit_canvas_viewport(dc, g_hdc_canvas);
                } else if (g_active_tool == TOOL_COLOR_ERASER) {
                    draw_color_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_primary, g_col_secondary);
                    blit_canvas_viewport(dc, g_hdc_canvas);
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
                    blit_canvas_viewport(dc, g_hdc_canvas);
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
                commit_text();
                g_text_cx = x;
                g_text_cy = y;
                {
                    int sx = CANVAS_X + x - g_scroll_x;
                    int sy = CANVAS_Y + y - g_scroll_y;
                    int max_w = (CANVAS_X + g_view_w) - sx;
                    int edit_w = 200;
                    if (edit_w > max_w) edit_w = max_w;
                    if (edit_w < 60) edit_w = 60;
                    if (g_hwnd_text_edit) {
                        MoveWindow(g_hwnd_text_edit, sx, sy, edit_w, 20, TRUE);
                        SetWindowText(g_hwnd_text_edit, "");
                        ShowWindow(g_hwnd_text_edit, SW_SHOW);
                        SetFocus(g_hwnd_text_edit);
                        g_text_active = TRUE;
                        set_status("Type text and press Enter to place on canvas (Esc to cancel).");
                    }
                }
                return 0;

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
                        blit_canvas_viewport(dc, g_hdc_canvas);
                        ReleaseDC(hwnd, dc);
                        set_status("Polygon closed and filled.");
                    } else {
                        g_poly_pts[g_poly_count].x = x;
                        g_poly_pts[g_poly_count].y = y;
                        g_poly_count++;
                        apply_draw_shape(g_hdc_canvas, TOOL_LINE, 0, g_last_x, g_last_y, x, y, g_col_primary, 0, g_brush_size);
                        g_last_x = x; g_last_y = y;
                        dc = GetDC(hwnd);
                        blit_canvas_viewport(dc, g_hdc_canvas);
                        ReleaseDC(hwnd, dc);
                    }
                }
                break;

            case TOOL_LINE:
            case TOOL_RECT:
            case TOOL_ROUNDRECT:
            case TOOL_ELLIPSE:
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

        commit_text();
        if (screen_to_canvas(x, y, &cx, &cy)) {
            x = cx;
            y = cy;
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
                    blit_canvas_viewport(dc, g_hdc_canvas);
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

        if (screen_to_canvas(x, y, &cx, &cy)) {
            wsprintf(coord_buf, "Cursor: X=%d Y=%d px  |  Canvas: %d x %d px", cx, cy, g_canvas_w, g_canvas_h);
            set_status(coord_buf);
        }

        if (g_sel_dragging) {
            int cx_pt = x - CANVAS_X + g_scroll_x;
            int cy_pt = y - CANVAS_Y + g_scroll_y;
            g_sel_rc.left = cx_pt - g_sel_anchor_x;
            g_sel_rc.top  = cy_pt - g_sel_anchor_y;
            g_sel_rc.right = g_sel_rc.left + g_sel_w;
            g_sel_rc.bottom = g_sel_rc.top + g_sel_h;
            refresh_canvas();
            return 0;
        }

        if (g_is_drawing && screen_to_canvas(x, y, &cx, &cy)) {
            x = cx;
            y = cy;
            dc = GetDC(hwnd);

            switch (g_active_tool) {
            case TOOL_PENCIL:
                apply_draw_shape(g_hdc_canvas, TOOL_LINE, 0, g_last_x, g_last_y, x, y, g_col_primary, 0, 1);
                blit_canvas_viewport(dc, g_hdc_canvas);
                break;
            case TOOL_BRUSH:
                draw_continuous_brush(g_hdc_canvas, g_last_x, g_last_y, x, y, g_brush_size, g_col_primary);
                blit_canvas_viewport(dc, g_hdc_canvas);
                break;
            case TOOL_AIRBRUSH:
                draw_spray(g_hdc_canvas, x, y, g_brush_size * 3 + 4, g_col_primary);
                blit_canvas_viewport(dc, g_hdc_canvas);
                break;
            case TOOL_ERASER:
                draw_normal_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_secondary);
                blit_canvas_viewport(dc, g_hdc_canvas);
                break;
            case TOOL_COLOR_ERASER:
                draw_color_eraser(g_hdc_canvas, x, y, g_brush_size * 2, g_col_primary, g_col_secondary);
                blit_canvas_viewport(dc, g_hdc_canvas);
                break;
            }
            g_last_x = x;
            g_last_y = y;
            ReleaseDC(hwnd, dc);
        }

        if (g_is_dragging) {
            cx = x - CANVAS_X + g_scroll_x;
            cy = y - CANVAS_Y + g_scroll_y;
            if (cx < 0) cx = 0;
            if (cx >= g_canvas_w) cx = g_canvas_w - 1;
            if (cy < 0) cy = 0;
            if (cy >= g_canvas_h) cy = g_canvas_h - 1;
            g_cur_x = cx;
            g_cur_y = cy;

            BitBlt(g_hdc_temp, 0, 0, g_canvas_w, g_canvas_h, g_hdc_canvas, 0, 0, SRCCOPY);
            apply_draw_shape(g_hdc_temp, g_active_tool, g_shape_style,
                             g_start_x, g_start_y, g_cur_x, g_cur_y,
                             g_col_primary, g_col_secondary, g_brush_size);

            dc = GetDC(hwnd);
            blit_canvas_viewport(dc, g_hdc_temp);
            ReleaseDC(hwnd, dc);
        }
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_is_drawing)
            g_is_drawing = FALSE;

        if (g_sel_dragging) {
            g_sel_dragging = FALSE;
            refresh_canvas();
            set_status("Selection moved. Drag to move again, or click outside to stamp.");
            return 0;
        }

        if (g_is_dragging) {
            g_is_dragging = FALSE;
            if (g_active_tool == TOOL_SELECT) {
                int l = (g_start_x < g_cur_x) ? g_start_x : g_cur_x;
                int r = (g_start_x < g_cur_x) ? g_cur_x : g_start_x;
                int t = (g_start_y < g_cur_y) ? g_start_y : g_cur_y;
                int b = (g_start_y < g_cur_y) ? g_cur_y : g_start_y;
                if (r - l > 2 && b - t > 2) {
                    g_has_sel = TRUE;
                    g_sel_floating = FALSE;
                    g_sel_rc.left = l;
                    g_sel_rc.top = t;
                    g_sel_rc.right = r;
                    g_sel_rc.bottom = b;
                    g_sel_w = r - l;
                    g_sel_h = b - t;
                    set_status("Area selected. Drag inside to move, Ctrl+C to copy, Del to clear.");
                } else {
                    g_has_sel = FALSE;
                    set_status("Ready.");
                }
                refresh_canvas();
            } else {
                apply_draw_shape(g_hdc_canvas, g_active_tool, g_shape_style,
                                 g_start_x, g_start_y, g_cur_x, g_cur_y,
                                 g_col_primary, g_col_secondary, g_brush_size);
                refresh_canvas();
                set_status("Shape drawn.");
            }
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (g_active_tool == TOOL_POLYGON && g_poly_count >= 2) {
            Polygon(g_hdc_canvas, g_poly_pts, g_poly_count);
            g_poly_count = 0;
            dc = GetDC(hwnd);
            blit_canvas_viewport(dc, g_hdc_canvas);
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
    HMENU hmenu, hfile, hedit, himage, hhelp;
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
    AppendMenu(hedit, MF_STRING, IDM_UNDO,       "&Undo\tCtrl+Z");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, IDM_CUT,        "Cu&t\tCtrl+X");
    AppendMenu(hedit, MF_STRING, IDM_COPY,       "&Copy\tCtrl+C");
    AppendMenu(hedit, MF_STRING, IDM_PASTE,      "&Paste\tCtrl+V");
    AppendMenu(hedit, MF_STRING, IDM_DELETE,     "&Delete\tDel");
    AppendMenu(hedit, MF_STRING, IDM_SELECTALL,  "Select &All\tCtrl+A");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, IDM_CANVASSIZE, "&Canvas Size...\tCtrl+E");
    AppendMenu(hedit, MF_STRING, IDM_CLEAR,      "&Clear Canvas");
    AppendMenu(hedit, MF_STRING, IDM_INVERT,     "&Invert Colors");
    AppendMenu(hedit, MF_STRING, IDM_FLIPH,      "Flip &Horizontal");
    AppendMenu(hedit, MF_STRING, IDM_FLIPV,      "Flip &Vertical");
    AppendMenu(hmenu, MF_POPUP, hedit, "&Edit");

    himage = CreatePopupMenu();
    AppendMenu(himage, MF_STRING, IDM_CANVASSIZE, "&Attributes / Canvas Size...\tCtrl+E");
    AppendMenu(himage, MF_SEPARATOR, 0, NULL);
    AppendMenu(himage, MF_STRING, IDM_INVERT,     "&Invert Colors");
    AppendMenu(himage, MF_STRING, IDM_FLIPH,      "Flip &Horizontal");
    AppendMenu(himage, MF_STRING, IDM_FLIPV,      "Flip &Vertical");
    AppendMenu(himage, MF_STRING, IDM_CLEAR,      "&Clear Canvas");
    AppendMenu(hmenu, MF_POPUP, himage, "&Image");

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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        624, 468,
        NULL, NULL, hinst, NULL
    );

    if (!g_mainwnd) return 1;

    create_main_menu(g_mainwnd);
    ShowWindow(g_mainwnd, show);
    UpdateWindow(g_mainwnd);

    if (cmdline && cmdline[0]) {
        char path_buf[128];
        int pi = 0;
        /* Pomiń spacje, tabulatory i znaki kontrolne (np. CR/LF z linii poleceń DOS/Win16) */
        while (*cmdline && (unsigned char)*cmdline <= ' ') cmdline++;
        if (*cmdline == '"') {
            cmdline++;
            while (*cmdline && *cmdline != '"' && (unsigned char)*cmdline >= ' ' && pi < (int)sizeof(path_buf) - 1) {
                path_buf[pi++] = *cmdline++;
            }
            if (*cmdline == '"') cmdline++;
        } else {
            while (*cmdline && (unsigned char)*cmdline > ' ' && pi < (int)sizeof(path_buf) - 1) {
                path_buf[pi++] = *cmdline++;
            }
        }
        path_buf[pi] = '\0';
        while (pi > 0 && (unsigned char)path_buf[pi - 1] <= ' ') {
            path_buf[--pi] = '\0';
        }
        if (path_buf[0]) {
            OutputDebugString("PAINT: opening cmdline file\n");
            load_canvas_from_bmp(path_buf);
        }
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
