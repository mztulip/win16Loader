/*
 * CHARMAP.C - Tablica znakow (Character Map) dla Win16 (Windows 3.1 / Win16Loader)
 *
 * Implementacja natywnej, 16-bitowej aplikacji Character Map:
 *  - Siatka 16x14 komorek (znaki 32..255)
 *  - Wybor kroju czcionki (ComboBox z EnumFonts)
 *  - Pole "Kopiuj" i przyciski Wybierz / Kopiuj / Wyczysc / Zamknij
 *  - Powiekszony podglad wybranego znaku
 *  - Kopiowanie tekstu do schowka systemowego (CF_TEXT)
 *  - Informacja o kodzie ASCII/ANSI i skrocie Alt+0NNN
 */

#include <windows.h>
#include "charmap.h"

#define GRID_X       10
#define GRID_Y       38
#define CELL_W       18
#define CELL_H       18
#define COLS         16
#define ROWS         14

#define PREV_X       308
#define PREV_Y       124
#define PREV_W       164
#define PREV_H       164

#define STAT_X       10
#define STAT_Y       296
#define STAT_W       462
#define STAT_H       18

static HINSTANCE      g_hinst       = 0;
static HFONT          g_hGridFont   = 0;
static HFONT          g_hLargeFont  = 0;
static unsigned char  g_sel_char    = 65; /* Domyślnie 'A' */
static BOOL           g_mouse_down  = FALSE;

static int            g_nfonts      = 0;
static char           g_fontnames[32][LF_FACESIZE];
static char           g_cur_font[LF_FACESIZE];

/* Callback enumeracji fontow GDI */
int FAR PASCAL __export EnumFontProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM data)
{
    int i;
    (void)tm; (void)type; (void)data;
    if (!lf || !lf->lfFaceName[0]) return 1;

    for (i = 0; i < g_nfonts; i++) {
        if (!lstrcmpi(g_fontnames[i], lf->lfFaceName)) return 1;
    }
    if (g_nfonts < 32) {
        lstrcpy(g_fontnames[g_nfonts++], lf->lfFaceName);
    }
    return 1;
}

/* Kopiowanie tekstu do schowka Windows */
static void CopyTextToClipboard(HWND hwnd, LPCSTR szText)
{
    WORD len;
    HANDLE hMem;
    LPSTR pMem;

    if (!szText || !szText[0]) return;
    len = (WORD)(lstrlen(szText) + 1);

    hMem = GlobalAlloc(0x2002 /* GHND */, len);
    if (!hMem) return;

    pMem = (LPSTR)GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return;
    }
    lstrcpy(pMem, szText);
    GlobalUnlock(hMem);

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

/* Aktualizacja tekstu paska stanu */
static void update_status(HWND hwnd)
{
    char buf[100];
    char ch_display = (char)((g_sel_char >= 32 && g_sel_char != 127) ? g_sel_char : ' ');
    wsprintf(buf, " Klawisz: Alt+0%03d   Znak: '%c'   Kod: %d (0x%02X)",
             (int)g_sel_char, ch_display, (int)g_sel_char, (int)g_sel_char);
    SetDlgItemText(hwnd, IDC_STATUS, buf);
}

/* Zmiana aktywnego fontu dla siatki i powiększenia */
static void update_current_font(HWND hwnd)
{
    HWND hCombo = GetDlgItem(hwnd, IDC_FONTCOMBO);
    int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);

    if (sel >= 0) {
        SendMessage(hCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)(LPSTR)g_cur_font);
    } else {
        lstrcpy(g_cur_font, "System");
    }

    if (g_hGridFont) DeleteObject(g_hGridFont);
    if (g_hLargeFont) DeleteObject(g_hLargeFont);

    g_hGridFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, g_cur_font);

    g_hLargeFont = CreateFont(-38, 0, 0, 0, FW_BOLD, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, g_cur_font);

    InvalidateRect(hwnd, NULL, TRUE);
}

/* Dodanie znaku do pola edycji */
static void append_char_to_edit(HWND hwnd, unsigned char ch)
{
    char buf[128];
    int len;
    GetDlgItemText(hwnd, IDC_COPYEDIT, buf, sizeof(buf) - 2);
    len = lstrlen(buf);
    if (len < (int)sizeof(buf) - 2) {
        buf[len] = (char)ch;
        buf[len + 1] = '\0';
        SetDlgItemText(hwnd, IDC_COPYEDIT, buf);
        SendDlgItemMessage(hwnd, IDC_COPYEDIT, EM_SETSEL, 0, MAKELONG(len + 1, len + 1));
    }
}

/* Obsługa kliknięcia i przeciągania myszą na siatce */
static void handle_grid_mouse(HWND hwnd, int x, int y, BOOL dbl_clk)
{
    if (x >= GRID_X && x < GRID_X + COLS * CELL_W &&
        y >= GRID_Y && y < GRID_Y + ROWS * CELL_H) {
        int col = (x - GRID_X) / CELL_W;
        int row = (y - GRID_Y) / CELL_H;
        int code = 32 + row * 16 + col;
        if (code >= 32 && code <= 255) {
            g_sel_char = (unsigned char)code;
            update_status(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            if (dbl_clk) {
                append_char_to_edit(hwnd, g_sel_char);
            }
        }
    }
}

/* Procedura okna głównego */
LRESULT CALLBACK __export MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        HFONT hsys = (HFONT)GetStockObject(SYSTEM_FONT);
        HDC hdc;
        FARPROC pEnum;
        HWND hCombo;
        int i, sel;

        /* Etykiety i kontrolki wyboru czcionki */
        CreateWindow("STATIC", "Czcionka:", WS_CHILD | WS_VISIBLE,
                     10, 12, 60, 18, hwnd, (HMENU)IDC_FONTLABEL, g_hinst, NULL);

        hCombo = CreateWindow("COMBOBOX", "",
                              WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                              72, 9, 150, 120, hwnd, (HMENU)IDC_FONTCOMBO, g_hinst, NULL);

        /* Etykiety i pole kopiowania */
        CreateWindow("STATIC", "Kopiuj:", WS_CHILD | WS_VISIBLE,
                     235, 12, 45, 18, hwnd, (HMENU)IDC_COPYLABEL, g_hinst, NULL);

        CreateWindow("EDIT", "",
                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                     282, 9, 110, 22, hwnd, (HMENU)IDC_COPYEDIT, g_hinst, NULL);

        /* Przyciski operacji */
        CreateWindow("BUTTON", "Wybierz",
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     400, 8, 74, 23, hwnd, (HMENU)IDC_SELECTBTN, g_hinst, NULL);

        CreateWindow("BUTTON", "Kopiuj",
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     400, 34, 74, 23, hwnd, (HMENU)IDC_COPYBTN, g_hinst, NULL);

        CreateWindow("BUTTON", "Wyczysc",
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     400, 60, 74, 23, hwnd, (HMENU)IDC_CLEARBTN, g_hinst, NULL);

        CreateWindow("BUTTON", "Zamknij",
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     400, 86, 74, 23, hwnd, (HMENU)IDC_CLOSEBTN, g_hinst, NULL);

        /* Pasek stanu na dole */
        CreateWindow("STATIC", "",
                     WS_CHILD | WS_VISIBLE | WS_BORDER,
                     STAT_X, STAT_Y, STAT_W, STAT_H, hwnd, (HMENU)IDC_STATUS, g_hinst, NULL);

        /* Ustawienie fontu systemowego dla kontrolek */
        SendDlgItemMessage(hwnd, IDC_FONTLABEL, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_FONTCOMBO, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_COPYLABEL, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_COPYEDIT, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_SELECTBTN, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_COPYBTN, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_CLEARBTN, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_CLOSEBTN, WM_SETFONT, (WPARAM)hsys, 0);
        SendDlgItemMessage(hwnd, IDC_STATUS, WM_SETFONT, (WPARAM)hsys, 0);

        /* Enumeracja czcionek */
        hdc = GetDC(hwnd);
        pEnum = MakeProcInstance((FARPROC)EnumFontProc, g_hinst);
        EnumFonts(hdc, NULL, (FONTENUMPROC)pEnum, 0);
        FreeProcInstance(pEnum);
        ReleaseDC(hwnd, hdc);

        /* Domyślne czcionki awaryjne jeśli brak w systemie */
        if (g_nfonts == 0) {
            lstrcpy(g_fontnames[g_nfonts++], "System");
            lstrcpy(g_fontnames[g_nfonts++], "FreeSans");
            lstrcpy(g_fontnames[g_nfonts++], "Courier");
        }

        sel = 0;
        for (i = 0; i < g_nfonts; i++) {
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)(LPSTR)g_fontnames[i]);
            if (!lstrcmpi(g_fontnames[i], "System")) sel = i;
        }
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
        update_current_font(hwnd);
        update_status(hwnd);
        return 0;
    }

    case WM_COMMAND:
        switch (wp) {
        case IDC_FONTCOMBO:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                update_current_font(hwnd);
            }
            break;

        case IDC_SELECTBTN:
            append_char_to_edit(hwnd, g_sel_char);
            break;

        case IDC_COPYBTN:
        {
            char buf[128];
            GetDlgItemText(hwnd, IDC_COPYEDIT, buf, sizeof(buf));
            if (buf[0]) {
                CopyTextToClipboard(hwnd, buf);
                SendDlgItemMessage(hwnd, IDC_COPYEDIT, EM_SETSEL, 0, MAKELONG(0, -1));
                SetFocus(GetDlgItem(hwnd, IDC_COPYEDIT));
            }
            break;
        }

        case IDC_CLEARBTN:
            SetDlgItemText(hwnd, IDC_COPYEDIT, "");
            SetFocus(GetDlgItem(hwnd, IDC_COPYEDIT));
            break;

        case IDC_CLOSEBTN:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_mouse_down = TRUE;
        handle_grid_mouse(hwnd, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (g_mouse_down) {
            handle_grid_mouse(hwnd, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_mouse_down) {
            g_mouse_down = FALSE;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        handle_grid_mouse(hwnd, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), TRUE);
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:
            if (g_sel_char > 32) g_sel_char--;
            break;
        case VK_RIGHT:
            if (g_sel_char < 255) g_sel_char++;
            break;
        case VK_UP:
            if (g_sel_char >= 32 + 16) g_sel_char -= 16;
            break;
        case VK_DOWN:
            if (g_sel_char <= 255 - 16) g_sel_char += 16;
            break;
        case VK_HOME:
            g_sel_char = 32;
            break;
        case VK_END:
            g_sel_char = 255;
            break;
        case VK_SPACE:
        case VK_RETURN:
            append_char_to_edit(hwnd, g_sel_char);
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        update_status(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH hbrWhite = (HBRUSH)GetStockObject(WHITE_BRUSH);
        HPEN hpenGrid, hpenOld;
        HFONT hfontOld;
        int r, c;
        RECT rcGrid;

        /* 1. Ramka wokół siatki znaków */
        rcGrid.left = GRID_X - 1;
        rcGrid.top = GRID_Y - 1;
        rcGrid.right = GRID_X + COLS * CELL_W + 1;
        rcGrid.bottom = GRID_Y + ROWS * CELL_H + 1;
        FrameRect(hdc, &rcGrid, (HBRUSH)GetStockObject(BLACK_BRUSH));

        /* 2. Rysowanie komórek siatki znaków */
        hfontOld = SelectObject(hdc, g_hGridFont ? g_hGridFont : (HFONT)GetStockObject(SYSTEM_FONT));
        hpenGrid = CreatePen(PS_SOLID, 1, RGB(192, 192, 192));
        hpenOld  = SelectObject(hdc, hpenGrid);

        for (r = 0; r < ROWS; r++) {
            for (c = 0; c < COLS; c++) {
                unsigned char code = (unsigned char)(32 + r * 16 + c);
                int cx = GRID_X + c * CELL_W;
                int cy = GRID_Y + r * CELL_H;
                RECT rcCell;
                char str[2];

                rcCell.left = cx;
                rcCell.top = cy;
                rcCell.right = cx + CELL_W;
                rcCell.bottom = cy + CELL_H;

                str[0] = (char)code;
                str[1] = '\0';

                if (code == g_sel_char) {
                    HBRUSH hbrSel = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                    FillRect(hdc, &rcCell, hbrSel);
                    DeleteObject(hbrSel);
                    SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
                } else {
                    FillRect(hdc, &rcCell, hbrWhite);
                    SetTextColor(hdc, RGB(0, 0, 0));
                    SetBkColor(hdc, RGB(255, 255, 255));

                    /* Linie siatki */
                    MoveTo(hdc, cx + CELL_W - 1, cy);
                    LineTo(hdc, cx + CELL_W - 1, cy + CELL_H);
                    MoveTo(hdc, cx, cy + CELL_H - 1);
                    LineTo(hdc, cx + CELL_W, cy + CELL_H - 1);
                }

                DrawText(hdc, str, 1, &rcCell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(hdc, hpenOld);
        DeleteObject(hpenGrid);

        /* 3. Panel powiększenia / podglądu (Preview Box) */
        {
            RECT rcBox, rcInner;
            char buf[64];
            char ch_display = (char)((g_sel_char >= 32 && g_sel_char != 127) ? g_sel_char : ' ');
            HBRUSH hbrFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

            rcBox.left = PREV_X;
            rcBox.top = PREV_Y;
            rcBox.right = PREV_X + PREV_W;
            rcBox.bottom = PREV_Y + PREV_H;

            /* Wyczyść tło całego panelu podglądu */
            FillRect(hdc, &rcBox, hbrFace);
            DeleteObject(hbrFace);

            /* Ramka podglądu */
            FrameRect(hdc, &rcBox, (HBRUSH)GetStockObject(BLACK_BRUSH));

            /* Wewnętrzne pole powiększonego glifu */
            rcInner.left = PREV_X + 16;
            rcInner.top = PREV_Y + 12;
            rcInner.right = PREV_X + PREV_W - 16;
            rcInner.bottom = PREV_Y + 84;

            FillRect(hdc, &rcInner, hbrWhite);
            FrameRect(hdc, &rcInner, (HBRUSH)GetStockObject(BLACK_BRUSH));

            /* Rysowanie powiększonego znaku */
            if (g_hLargeFont) SelectObject(hdc, g_hLargeFont);
            SetTextColor(hdc, RGB(0, 0, 128)); /* Ciemnoniebieski */
            SetBkColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, OPAQUE);

            buf[0] = (char)g_sel_char;
            buf[1] = '\0';
            DrawText(hdc, buf, 1, &rcInner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* Informacje tekstowe pod powiększeniem */
            SelectObject(hdc, GetStockObject(SYSTEM_FONT));
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetBkMode(hdc, OPAQUE);

            wsprintf(buf, "Znak: '%c'          ", ch_display);
            TextOut(hdc, PREV_X + 16, PREV_Y + 92, buf, lstrlen(buf));

            wsprintf(buf, "Kod: %d (0x%02X)    ", (int)g_sel_char, (int)g_sel_char);
            TextOut(hdc, PREV_X + 16, PREV_Y + 112, buf, lstrlen(buf));

            wsprintf(buf, "Klawisz: Alt+0%03d  ", (int)g_sel_char);
            TextOut(hdc, PREV_X + 16, PREV_Y + 132, buf, lstrlen(buf));
        }

        SelectObject(hdc, hfontOld);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (g_hGridFont) {
            DeleteObject(g_hGridFont);
            g_hGridFont = 0;
        }
        if (g_hLargeFont) {
            DeleteObject(g_hLargeFont);
            g_hLargeFont = 0;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

/* Punkt wejścia aplikacji Win16 */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hinst = hInstance;

    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = MainWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CHARMAP));
    wc.hCursor       = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "Win16CharMapClass";

    if (!RegisterClass(&wc)) return 1;

    hwnd = CreateWindow(
        "Win16CharMapClass",
        "Tablica znakow (Character Map)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 492, 356,
        0, 0, hInstance, NULL
    );

    if (!hwnd) return 2;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
