/*
 * HEXVIEW.C - Hex Viewer for Win16 (Windows 3.1 / Win16Loader / Calmira II)
 *
 * Native 16-bit binary viewer:
 *  - Fixedsys monospace hex dump (Offset, Hex bytes, Decoded ASCII)
 *  - Dynamic file reading on paint (handles arbitrarily large files without large memory allocations)
 *  - Vertical & horizontal scrollbars with high-range scaling
 *  - Interactive byte selection & highlighting (Hex <-> ASCII synchronization)
 *  - Status bar displaying cursor offset, decimal/hex byte value, and ASCII character
 *  - File -> Open (via COMMDLG), Close, Exit
 *  - Edit -> Go to Offset (hex address jump), Copy Line to clipboard
 *  - Command-line argument support for file associations (.SYS, .BIN, .DAT, etc.)
 */

#include <windows.h>
#include <commdlg.h>
#include <string.h>
#include "hexview.h"

#define HEADER_HEIGHT   20
#define STATUS_HEIGHT   18
#define MARGIN_LEFT     8
#define LINE_CHARS      78

static HINSTANCE    g_hinst         = 0;
static HWND         g_hwndMain      = 0;
static HFONT        g_hFont         = 0;
static int          g_cxChar        = 8;
static int          g_cyLine        = 12;

static char         g_szFilePath[128];
static char         g_szFileName[64];
static HFILE        g_hFile         = HFILE_ERROR;
static DWORD        g_dwFileSize    = 0;
static DWORD        g_top_line      = 0;
static DWORD        g_sel_offset    = 0;
static int          g_hscroll_x     = 0;
static int          g_max_hscroll   = 0;
static int          g_visible_lines = 1;

static const char   g_szAppName[]   = "HexViewClass";
static const char   g_hexDigits[]   = "0123456789ABCDEF";

/* Header line string: 78 chars exactly */
static const char g_szHeader[] =
    "Offset (h)  00 01 02 03 04 05 06 07 - 08 09 0A 0B 0C 0D 0E 0F  Decoded text  ";

static void UpdateScrollbars(HWND hwnd)
{
    RECT rc;
    int avail_h;
    DWORD total_lines;
    DWORD max_top;
    int total_w;
    int avail_w;

    GetClientRect(hwnd, &rc);
    avail_h = (rc.bottom - rc.top) - HEADER_HEIGHT - STATUS_HEIGHT;
    g_visible_lines = (avail_h > 0 && g_cyLine > 0) ? (avail_h / g_cyLine) : 1;
    if (g_visible_lines < 1) g_visible_lines = 1;

    total_lines = (g_dwFileSize + 15) / 16;
    max_top = (total_lines > (DWORD)g_visible_lines) ? (total_lines - (DWORD)g_visible_lines) : 0;

    if (g_top_line > max_top) g_top_line = max_top;

    if (max_top == 0) {
        SetScrollRange(hwnd, SB_VERT, 0, 0, TRUE);
    } else if (max_top <= 30000) {
        SetScrollRange(hwnd, SB_VERT, 0, (int)max_top, FALSE);
        SetScrollPos(hwnd, SB_VERT, (int)g_top_line, TRUE);
    } else {
        int pos = (int)((g_top_line * 30000UL) / max_top);
        SetScrollRange(hwnd, SB_VERT, 0, 30000, FALSE);
        SetScrollPos(hwnd, SB_VERT, pos, TRUE);
    }

    total_w = MARGIN_LEFT * 2 + LINE_CHARS * g_cxChar;
    avail_w = rc.right - rc.left;
    g_max_hscroll = (total_w > avail_w) ? (total_w - avail_w) : 0;
    if (g_hscroll_x > g_max_hscroll) g_hscroll_x = g_max_hscroll;
    SetScrollRange(hwnd, SB_HORZ, 0, g_max_hscroll, FALSE);
    SetScrollPos(hwnd, SB_HORZ, g_hscroll_x, TRUE);
}

static void UpdateTitle(HWND hwnd)
{
    char szTitle[140];
    if (g_szFileName[0]) {
        wsprintf(szTitle, "Hex Viewer - %s (%lu bytes)", (LPSTR)g_szFileName, g_dwFileSize);
    } else {
        lstrcpy(szTitle, "Hex Viewer");
    }
    SetWindowText(hwnd, szTitle);
}

static void CloseCurrentFile(HWND hwnd)
{
    if (g_hFile != HFILE_ERROR) {
        _lclose(g_hFile);
        g_hFile = HFILE_ERROR;
    }
    g_szFilePath[0] = 0;
    g_szFileName[0] = 0;
    g_dwFileSize = 0;
    g_top_line = 0;
    g_sel_offset = 0xFFFFFFFFUL;
    UpdateTitle(hwnd);
    UpdateScrollbars(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

static BOOL OpenFileForViewing(HWND hwnd, LPCSTR szPath)
{
    HFILE hf;
    long sz;
    LPCSTR p, last;

    if (!szPath || !szPath[0]) return FALSE;

    if (g_hFile != HFILE_ERROR) {
        _lclose(g_hFile);
        g_hFile = HFILE_ERROR;
    }

    hf = _lopen(szPath, 0 /* OF_READ */);
    if (hf == HFILE_ERROR) {
        char msg[160];
        wsprintf(msg, "Cannot open file:\n%s", (LPSTR)szPath);
        MessageBox(hwnd, msg, "Hex Viewer", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    sz = _llseek(hf, 0L, 2 /* SEEK_END */);
    _llseek(hf, 0L, 0 /* SEEK_SET */);

    g_hFile = hf;
    g_dwFileSize = (sz > 0) ? (DWORD)sz : 0;
    g_top_line = 0;
    g_sel_offset = (g_dwFileSize > 0) ? 0 : 0xFFFFFFFFUL;
    g_hscroll_x = 0;

    lstrcpy(g_szFilePath, szPath);
    last = szPath;
    for (p = szPath; *p; p++) {
        if (*p == '\\' || *p == '/' || *p == ':') last = p + 1;
    }
    lstrcpy(g_szFileName, last);

    UpdateTitle(hwnd);
    UpdateScrollbars(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    return TRUE;
}

static void EnsureVisible(HWND hwnd, DWORD offset)
{
    DWORD line = offset / 16;
    BOOL changed = FALSE;

    if (line < g_top_line) {
        g_top_line = line;
        changed = TRUE;
    } else if (line >= g_top_line + (DWORD)g_visible_lines) {
        g_top_line = line - (DWORD)g_visible_lines + 1;
        changed = TRUE;
    }

    if (changed) {
        UpdateScrollbars(hwnd);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void FormatHexDumpLine(DWORD offset, const BYTE FAR *data, int count, char *out)
{
    int i;
    DWORD temp = offset;

    /* Fill line with spaces */
    for (i = 0; i < LINE_CHARS; i++) out[i] = ' ';
    out[LINE_CHARS] = 0;

    /* 8 hex digits for offset */
    for (i = 7; i >= 0; i--) {
        out[i] = g_hexDigits[temp & 0x0F];
        temp >>= 4;
    }
    out[8] = ':';

    /* Hex bytes: 0..7 and 8..15 */
    for (i = 0; i < count; i++) {
        int pos;
        BYTE b = data[i];
        if (i < 8) {
            pos = 12 + i * 3;
        } else {
            pos = 12 + 8 * 3 + 2 + (i - 8) * 3;
        }
        out[pos]     = g_hexDigits[(b >> 4) & 0x0F];
        out[pos + 1] = g_hexDigits[b & 0x0F];

        /* ASCII character at col 65 + i */
        out[65 + i] = (b >= 32 && b <= 126) ? (char)b : '.';
    }
    out[36] = '-';
}

static void CopyCurrentLineToClipboard(HWND hwnd)
{
    DWORD line_offset;
    BYTE buf[16];
    WORD nread;
    char szLine[LINE_CHARS + 4];
    HANDLE hMem;
    LPSTR pMem;

    if (g_hFile == HFILE_ERROR || g_dwFileSize == 0) return;

    line_offset = (g_sel_offset != 0xFFFFFFFFUL) ? ((g_sel_offset / 16) * 16) : (g_top_line * 16);
    if (line_offset >= g_dwFileSize) return;

    _llseek(g_hFile, line_offset, 0);
    nread = _lread(g_hFile, buf, 16);
    if (nread == (WORD)HFILE_ERROR || nread == 0) return;

    FormatHexDumpLine(line_offset, buf, nread, szLine);
    lstrcat(szLine, "\r\n");

    hMem = GlobalAlloc(GHND, (DWORD)(lstrlen(szLine) + 1));
    if (!hMem) return;
    pMem = (LPSTR)GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return; }
    lstrcpy(pMem, szLine);
    GlobalUnlock(hMem);

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

/* Parse hex or dec number from string */
static DWORD ParseOffsetStr(LPCSTR s)
{
    DWORD val = 0;
    BOOL is_hex = FALSE;

    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        is_hex = TRUE;
        s += 2;
    }
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            val = val * (is_hex ? 16 : 16) + (DWORD)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            val = val * 16 + (DWORD)(c - 'a' + 10);
            is_hex = TRUE;
        } else if (c >= 'A' && c <= 'F') {
            val = val * 16 + (DWORD)(c - 'A' + 10);
            is_hex = TRUE;
        } else if (c == 'h' || c == 'H') {
            break;
        } else {
            break;
        }
    }
    return val;
}

static DWORD g_goto_result = 0xFFFFFFFFUL;

BOOL FAR PASCAL __export GotoDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hdlg, IDC_GOTO_EDIT));
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK) {
            char buf[32];
            GetDlgItemText(hdlg, IDC_GOTO_EDIT, buf, sizeof(buf));
            g_goto_result = ParseOffsetStr(buf);
            EndDialog(hdlg, 1);
            return TRUE;
        } else if (wParam == IDCANCEL) {
            g_goto_result = 0xFFFFFFFFUL;
            EndDialog(hdlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void DoGoToOffset(HWND hwnd)
{
    FARPROC proc;
    if (g_hFile == HFILE_ERROR || g_dwFileSize == 0) return;

    proc = MakeProcInstance((FARPROC)GotoDlgProc, g_hinst);
    if (DialogBox(g_hinst, MAKEINTRESOURCE(IDD_GOTO), hwnd, (DLGPROC)proc)) {
        if (g_goto_result != 0xFFFFFFFFUL) {
            if (g_goto_result >= g_dwFileSize) {
                g_goto_result = (g_dwFileSize > 0) ? (g_dwFileSize - 1) : 0;
            }
            g_sel_offset = g_goto_result;
            EnsureVisible(hwnd, g_sel_offset);
        }
    }
    FreeProcInstance(proc);
}

static void DoFileOpenDlg(HWND hwnd)
{
    OPENFILENAME ofn;
    char szFile[128];
    szFile[0] = 0;

    _fmemset(&ofn, 0, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0Binary Files (*.sys;*.bin;*.dat;*.rom;*.com)\0*.sys;*.bin;*.dat;*.rom;*.com\0System Files (*.sys)\0*.sys\0\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn)) {
        OpenFileForViewing(hwnd, szFile);
    }
}

LRESULT FAR PASCAL __export HexViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        TEXTMETRIC tm;
        g_hFont = GetStockObject(SYSTEM_FIXED_FONT);
        if (!g_hFont) g_hFont = GetStockObject(ANSI_FIXED_FONT);
        SelectObject(hdc, g_hFont);
        GetTextMetrics(hdc, &tm);
        g_cxChar = tm.tmAveCharWidth;
        g_cyLine = tm.tmHeight;
        if (g_cxChar < 1) g_cxChar = 8;
        if (g_cyLine < 1) g_cyLine = 12;
        ReleaseDC(hwnd, hdc);
        return 0;
    }

    case WM_SIZE:
        UpdateScrollbars(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_VSCROLL: {
        int code = (int)wParam;
        DWORD total_lines = (g_dwFileSize + 15) / 16;
        DWORD max_top = (total_lines > (DWORD)g_visible_lines) ? (total_lines - (DWORD)g_visible_lines) : 0;
        DWORD new_top = g_top_line;

        switch (code) {
        case SB_LINEUP:
            if (new_top > 0) new_top--;
            break;
        case SB_LINEDOWN:
            if (new_top < max_top) new_top++;
            break;
        case SB_PAGEUP:
            if (new_top >= (DWORD)g_visible_lines) new_top -= (DWORD)g_visible_lines;
            else new_top = 0;
            break;
        case SB_PAGEDOWN:
            if (new_top + (DWORD)g_visible_lines <= max_top) new_top += (DWORD)g_visible_lines;
            else new_top = max_top;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            int pos = (int)LOWORD(lParam);
            if (max_top <= 30000) {
                new_top = (DWORD)pos;
            } else {
                new_top = (DWORD)(((DWORD)pos * max_top) / 30000UL);
            }
            break;
        }
        case SB_TOP:
            new_top = 0;
            break;
        case SB_BOTTOM:
            new_top = max_top;
            break;
        default:
            return 0;
        }

        if (new_top > max_top) new_top = max_top;
        if (new_top != g_top_line) {
            g_top_line = new_top;
            UpdateScrollbars(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_HSCROLL: {
        int code = (int)wParam;
        int new_h = g_hscroll_x;
        switch (code) {
        case SB_LINEUP:     new_h -= g_cxChar * 2; break;
        case SB_LINEDOWN:   new_h += g_cxChar * 2; break;
        case SB_PAGEUP:     new_h -= g_cxChar * 16; break;
        case SB_PAGEDOWN:   new_h += g_cxChar * 16; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: new_h = (int)LOWORD(lParam); break;
        default:            return 0;
        }
        if (new_h < 0) new_h = 0;
        if (new_h > g_max_hscroll) new_h = g_max_hscroll;
        if (new_h != g_hscroll_x) {
            g_hscroll_x = new_h;
            UpdateScrollbars(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mouse_x = (int)LOWORD(lParam);
        int mouse_y = (int)HIWORD(lParam);
        int data_y  = mouse_y - HEADER_HEIGHT - 2;

        if (data_y >= 0 && g_cyLine > 0 && g_hFile != HFILE_ERROR && g_dwFileSize > 0) {
            int row = data_y / g_cyLine;
            DWORD line_offset = (g_top_line + (DWORD)row) * 16;
            if (line_offset < g_dwFileSize) {
                int col = (mouse_x + g_hscroll_x - MARGIN_LEFT) / g_cxChar;
                int byte_idx = -1;

                if (col >= 12 && col < 36) {
                    byte_idx = (col - 12) / 3;
                    if (byte_idx > 7) byte_idx = 7;
                } else if (col >= 38 && col < 62) {
                    byte_idx = 8 + (col - 38) / 3;
                    if (byte_idx > 15) byte_idx = 15;
                } else if (col >= 65 && col < 81) {
                    byte_idx = col - 65;
                    if (byte_idx > 15) byte_idx = 15;
                }

                if (byte_idx >= 0 && line_offset + (DWORD)byte_idx < g_dwFileSize) {
                    g_sel_offset = line_offset + (DWORD)byte_idx;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (g_hFile == HFILE_ERROR || g_dwFileSize == 0) return 0;
        switch (wParam) {
        case VK_LEFT:
            if (g_sel_offset > 0) {
                g_sel_offset--;
                EnsureVisible(hwnd, g_sel_offset);
            }
            return 0;
        case VK_RIGHT:
            if (g_sel_offset + 1 < g_dwFileSize) {
                g_sel_offset++;
                EnsureVisible(hwnd, g_sel_offset);
            }
            return 0;
        case VK_UP:
            if (g_sel_offset >= 16) {
                g_sel_offset -= 16;
                EnsureVisible(hwnd, g_sel_offset);
            }
            return 0;
        case VK_DOWN:
            if (g_sel_offset + 16 < g_dwFileSize) {
                g_sel_offset += 16;
                EnsureVisible(hwnd, g_sel_offset);
            }
            return 0;
        case VK_PRIOR: /* Page Up */
            if (g_sel_offset >= (DWORD)g_visible_lines * 16) {
                g_sel_offset -= (DWORD)g_visible_lines * 16;
            } else {
                g_sel_offset = 0;
            }
            EnsureVisible(hwnd, g_sel_offset);
            return 0;
        case VK_NEXT: /* Page Down */
            if (g_sel_offset + (DWORD)g_visible_lines * 16 < g_dwFileSize) {
                g_sel_offset += (DWORD)g_visible_lines * 16;
            } else {
                g_sel_offset = (g_dwFileSize > 0) ? (g_dwFileSize - 1) : 0;
            }
            EnsureVisible(hwnd, g_sel_offset);
            return 0;
        case VK_HOME:
            g_sel_offset = 0;
            EnsureVisible(hwnd, g_sel_offset);
            return 0;
        case VK_END:
            g_sel_offset = (g_dwFileSize > 0) ? (g_dwFileSize - 1) : 0;
            EnsureVisible(hwnd, g_sel_offset);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
        switch (wParam) {
        case IDM_FILE_OPEN:
            DoFileOpenDlg(hwnd);
            return 0;
        case IDM_FILE_CLOSE:
            CloseCurrentFile(hwnd);
            return 0;
        case IDM_FILE_EXIT:
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_EDIT_GOTO:
            DoGoToOffset(hwnd);
            return 0;
        case IDM_EDIT_COPY:
            CopyCurrentLineToClipboard(hwnd);
            return 0;
        case IDM_HELP_ABOUT:
            MessageBox(hwnd,
                       "Hex Viewer v1.0\nWin16 native binary viewer for Win16Loader & Calmira II.\nSupports .SYS, .BIN, .DAT, .ROM, .EXE, etc.",
                       "About Hex Viewer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC hdc;
        HFONT hOldFont;
        HPEN hPenDark, hPenWhite, hOldPen;
        HBRUSH hBrushFace, hBrushWin;
        int i;
        BYTE sel_val = 0;
        BOOL have_sel_val = FALSE;

        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        hOldFont = SelectObject(hdc, g_hFont);

        hPenDark  = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
        hPenWhite = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
        hBrushFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        hBrushWin  = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

        /* 1. Header banner */
        {
            RECT rcHdr;
            rcHdr.left = 0;
            rcHdr.top = 0;
            rcHdr.right = rc.right;
            rcHdr.bottom = HEADER_HEIGHT;
            FillRect(hdc, &rcHdr, hBrushFace);

            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
            TextOut(hdc, MARGIN_LEFT - g_hscroll_x, 3, g_szHeader, lstrlen(g_szHeader));

            /* 3D separator line at bottom of header */
            hOldPen = SelectObject(hdc, hPenDark);
            MoveTo(hdc, 0, HEADER_HEIGHT - 2);
            LineTo(hdc, rc.right, HEADER_HEIGHT - 2);
            SelectObject(hdc, hPenWhite);
            MoveTo(hdc, 0, HEADER_HEIGHT - 1);
            LineTo(hdc, rc.right, HEADER_HEIGHT - 1);
            SelectObject(hdc, hOldPen);
        }

        /* 2. Hex data lines */
        {
            RECT rcData;
            rcData.left = 0;
            rcData.top = HEADER_HEIGHT;
            rcData.right = rc.right;
            rcData.bottom = rc.bottom - STATUS_HEIGHT;
            FillRect(hdc, &rcData, hBrushWin);

            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));

            if (g_hFile != HFILE_ERROR && g_dwFileSize > 0) {
                for (i = 0; i < g_visible_lines; i++) {
                    DWORD line_offset = (g_top_line + (DWORD)i) * 16;
                    BYTE buf[16];
                    WORD nread;
                    char szLine[LINE_CHARS + 2];
                    int line_y = HEADER_HEIGHT + 2 + i * g_cyLine;

                    if (line_offset >= g_dwFileSize) break;

                    _llseek(g_hFile, line_offset, 0);
                    nread = _lread(g_hFile, buf, 16);
                    if (nread == (WORD)HFILE_ERROR || nread == 0) break;

                    FormatHexDumpLine(line_offset, buf, nread, szLine);
                    TextOut(hdc, MARGIN_LEFT - g_hscroll_x, line_y, szLine, LINE_CHARS);

                    /* Highlight selected byte if present on this line */
                    if (g_sel_offset >= line_offset && g_sel_offset < line_offset + nread) {
                        int b_idx = (int)(g_sel_offset - line_offset);
                        int hex_col = (b_idx < 8) ? (12 + b_idx * 3) : (12 + 8 * 3 + 2 + (b_idx - 8) * 3);
                        int asc_col = 65 + b_idx;
                        RECT rcHex, rcAsc;

                        sel_val = buf[b_idx];
                        have_sel_val = TRUE;

                        rcHex.left   = MARGIN_LEFT - g_hscroll_x + hex_col * g_cxChar;
                        rcHex.top    = line_y;
                        rcHex.right  = rcHex.left + 2 * g_cxChar;
                        rcHex.bottom = line_y + g_cyLine;
                        InvertRect(hdc, &rcHex);

                        rcAsc.left   = MARGIN_LEFT - g_hscroll_x + asc_col * g_cxChar;
                        rcAsc.top    = line_y;
                        rcAsc.right  = rcAsc.left + g_cxChar;
                        rcAsc.bottom = line_y + g_cyLine;
                        InvertRect(hdc, &rcAsc);
                    }
                }
            } else {
                /* Placeholder text when no file is loaded */
                LPCSTR msg = "No file loaded. Choose File -> Open... to view binary files.";
                TextOut(hdc, MARGIN_LEFT, HEADER_HEIGHT + 16, msg, lstrlen(msg));
            }
        }

        /* 3. Status Bar at bottom */
        {
            RECT rcStat;
            char szStatus[160];
            rcStat.left = 0;
            rcStat.top = rc.bottom - STATUS_HEIGHT;
            rcStat.right = rc.right;
            rcStat.bottom = rc.bottom;
            FillRect(hdc, &rcStat, hBrushFace);

            /* 3D top border for status bar */
            hOldPen = SelectObject(hdc, hPenDark);
            MoveTo(hdc, 0, rcStat.top);
            LineTo(hdc, rc.right, rcStat.top);
            SelectObject(hdc, hPenWhite);
            MoveTo(hdc, 0, rcStat.top + 1);
            LineTo(hdc, rc.right, rcStat.top + 1);
            SelectObject(hdc, hOldPen);

            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

            if (g_hFile != HFILE_ERROR && g_dwFileSize > 0) {
                if (have_sel_val) {
                    char c = (sel_val >= 32 && sel_val <= 126) ? (char)sel_val : '.';
                    wsprintf(szStatus, "Offset: 0x%08lX (%lu) | Byte: 0x%02X (%u, '%c') | Size: %lu bytes",
                             g_sel_offset, g_sel_offset, (WORD)sel_val, (WORD)sel_val, c, g_dwFileSize);
                } else {
                    wsprintf(szStatus, "File: %s | Size: %lu bytes", (LPSTR)g_szFileName, g_dwFileSize);
                }
            } else {
                lstrcpy(szStatus, "Ready");
            }
            TextOut(hdc, MARGIN_LEFT, rcStat.top + 3, szStatus, lstrlen(szStatus));
        }

        SelectObject(hdc, hOldFont);
        DeleteObject(hPenDark);
        DeleteObject(hPenWhite);
        DeleteObject(hBrushFace);
        DeleteObject(hBrushWin);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (g_hFile != HFILE_ERROR) {
            _lclose(g_hFile);
            g_hFile = HFILE_ERROR;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ParseCmdLineArgs(LPCSTR lpCmdLine, LPSTR szOut, int maxLen)
{
    LPCSTR p = lpCmdLine;
    int i = 0;
    szOut[0] = 0;
    if (!p) return;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && i < maxLen - 1) {
            szOut[i++] = *p++;
        }
    } else {
        while (*p && *p != ' ' && *p != '\t' && i < maxLen - 1) {
            szOut[i++] = *p++;
        }
    }
    szOut[i] = 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    WNDCLASS wc;
    HACCEL hAccel;
    char szInitFile[128];

    g_hinst = hInstance;

    if (!hPrevInstance) {
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = HexViewWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIcon(hInstance, "HEXVIEW_ICON");
        wc.hCursor       = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName  = "HEXVIEW_MENU";
        wc.lpszClassName = g_szAppName;

        if (!RegisterClass(&wc)) return FALSE;
    }

    g_hwndMain = CreateWindow(
        g_szAppName,
        "Hex Viewer",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT,
        630, 420,
        0, 0, hInstance, NULL
    );

    if (!g_hwndMain) return FALSE;

    hAccel = LoadAccelerators(hInstance, "HEXVIEW_ACCEL");

    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    /* Open initial file if provided on command line */
    ParseCmdLineArgs(lpCmdLine, szInitFile, sizeof(szInitFile));
    if (szInitFile[0]) {
        OpenFileForViewing(g_hwndMain, szInitFile);
    }

    while (GetMessage(&msg, 0, 0, 0)) {
        if (!hAccel || !TranslateAccelerator(g_hwndMain, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}
