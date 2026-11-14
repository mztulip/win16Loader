/*
 * CONTROL.EXE - Panel sterowania Windows 16-bit
 * Obsluguje:
 *   control.exe main.cpl Desktop
 *   control.exe main.cpl Date/Time
 *   control.exe (okno glowne)
 */
#include <windows.h>
#include <dos.h>

#ifndef FP_SEG
#define FP_SEG(p) ((WORD)((DWORD)(void __far *)(p) >> 16))
#endif
#ifndef FP_OFF
#define FP_OFF(p) ((WORD)(DWORD)(void __far *)(p))
#endif

#define IDC_OK              1
#define IDC_CANCEL          2

/* Desktop dialog controls */
#define IDC_DESK_WPLBL      101
#define IDC_DESK_WPEDIT     102
#define IDC_DESK_WPLIST     103
#define IDC_DESK_TILE       104
#define IDC_DESK_CENTER     105
#define IDC_DESK_PATLBL     106
#define IDC_DESK_PATEDIT    107
#define IDC_DESK_PATLIST    108
#define IDC_DESK_SPCLBL     109
#define IDC_DESK_SPCEDIT    110

/* Date/Time dialog controls */
#define IDC_DT_DAY          201
#define IDC_DT_MONTH        202
#define IDC_DT_YEAR         203
#define IDC_DT_HOUR         204
#define IDC_DT_MIN          205
#define IDC_DT_SEC          206

/* Main window controls */
#define IDC_MAIN_DESK       301
#define IDC_MAIN_DATETIME   302
#define IDC_MAIN_FONTS      303
#define IDC_MAIN_COLORS     304
#define IDC_MAIN_EXIT       305

static HINSTANCE g_hinst = 0;
static char g_tmp[64];

/* ============================================================
 * Pomocnicze
 * ============================================================ */
static BOOL str_contains(LPCSTR s, LPCSTR sub)
{
    int i, j;
    if (!s || !sub) return FALSE;
    for (i = 0; s[i]; i++) {
        for (j = 0; sub[j]; j++) {
            char c1 = s[i + j];
            char c2 = sub[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) break;
        }
        if (!sub[j]) return TRUE;
    }
    return FALSE;
}

static void int_to_str(int v, char __far *buf)
{
    char tmp[10];
    int i = 0, j = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    if (v < 0) { buf[j++] = '-'; v = -v; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static int str_to_int(LPCSTR s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* ============================================================
 * Obsluga DOS Date / Time (INT 21h)
 * ============================================================ */
static void dos_get_date(int *year, int *month, int *day)
{
    WORD y, m, d;
    _asm {
        mov ah, 2Ah
        int 21h
        mov y, cx
        mov byte ptr m, dh
        mov byte ptr d, dl
    }
    *year = (int)y;
    *month = (int)(m & 0xFF);
    *day = (int)(d & 0xFF);
}

static void dos_get_time(int *hour, int *min, int *sec)
{
    WORD h, m, s;
    _asm {
        mov ah, 2Ch
        int 21h
        mov byte ptr h, ch
        mov byte ptr m, cl
        mov byte ptr s, dh
    }
    *hour = (int)(h & 0xFF);
    *min = (int)(m & 0xFF);
    *sec = (int)(s & 0xFF);
}

static void dos_set_date(int year, int month, int day)
{
    WORD y = (WORD)year;
    BYTE m = (BYTE)month;
    BYTE d = (BYTE)day;
    _asm {
        mov cx, y
        mov dh, m
        mov dl, d
        mov ah, 2Bh
        int 21h
    }
}

static void dos_set_time(int hour, int min, int sec)
{
    BYTE h = (BYTE)hour;
    BYTE m = (BYTE)min;
    BYTE s = (BYTE)sec;
    _asm {
        mov ch, h
        mov cl, m
        mov dh, s
        xor dl, dl
        mov ah, 2Dh
        int 21h
    }
}

/* ============================================================
 * Dialog: Ustawienia Pulpitu (Desktop Properties)
 * ============================================================ */
#pragma pack(1)
typedef struct {
    char reserved[21];
    BYTE attr;
    WORD time;
    WORD date;
    DWORD size;
    char name[13];
} DOS_DTA;
#pragma pack()

static void populate_wallpapers(HWND hwndList)
{
    static DOS_DTA dta;
    char pattern[] = "C:\\WINDOWS\\*.BMP";
    WORD seg_dta = FP_SEG(&dta);
    WORD off_dta = FP_OFF(&dta);
    WORD seg_pat = FP_SEG(pattern);
    WORD off_pat = FP_OFF(pattern);
    int err;

    SendMessage(hwndList, LB_RESETCONTENT, 0, 0);
    SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)"(None)");

    /* Ustaw DTA (AH=1Ah) */
    _asm {
        push ds
        mov ax, seg_dta
        mov ds, ax
        mov dx, off_dta
        mov ah, 1Ah
        int 21h
        pop ds
    }

    /* FindFirst (AH=4Eh) */
    _asm {
        push ds
        mov ax, seg_pat
        mov ds, ax
        mov dx, off_pat
        mov cx, 0
        mov ah, 4Eh
        int 21h
        mov err, 0
        jnc ok1
        mov err, ax
    ok1:
        pop ds
    }

    while (!err) {
        SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)dta.name);
        /* FindNext (AH=4Fh) */
        _asm {
            mov ah, 4Fh
            int 21h
            mov err, 0
            jnc ok2
            mov err, ax
        ok2:
        }
    }
}

static const char * const g_pat_list[] = {
    "(None)",
    "50% Gray",
    "Boxes",
    "Critters",
    "Diamonds",
    "Weave",
    "Waffle"
};
#define NUM_PAT 7

LRESULT CALLBACK __export DesktopDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        int i, sel_wp = 0, sel_pat = 0, tile = 0;
        char cur_wp[64], cur_pat[64], cur_spc[16];
        HFONT hsys = (HFONT)GetStockObject(SYSTEM_FONT);

        /* Odczyt z WIN.INI */
        GetProfileString("Desktop", "Wallpaper", "(None)", (LPSTR)cur_wp, sizeof(cur_wp));
        GetProfileString("Desktop", "Pattern", "(None)", (LPSTR)cur_pat, sizeof(cur_pat));
        tile = GetProfileInt("Desktop", "TileWallpaper", 0);
        int_to_str(GetProfileInt("Desktop", "IconSpacing", 75), (LPSTR)cur_spc);

        /* Ramka: Tapeta */
        CreateWindow("BUTTON", "Tapeta (Wallpaper)", WS_CHILD | WS_VISIBLE | 0x07 /*BS_GROUPBOX*/,
                     12, 10, 180, 150, hwnd, 0, g_hinst, NULL);
        CreateWindow("STATIC", "Plik:", WS_CHILD | WS_VISIBLE, 24, 30, 150, 16, hwnd, (HMENU)IDC_DESK_WPLBL, g_hinst, NULL);
        CreateWindow("EDIT", cur_wp, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                     24, 48, 156, 22, hwnd, (HMENU)IDC_DESK_WPEDIT, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
                     24, 72, 156, 54, hwnd, (HMENU)IDC_DESK_WPLIST, g_hinst, NULL);
        CreateWindow("BUTTON", "Kafelki", WS_CHILD | WS_VISIBLE | 0x09 /*BS_AUTORADIOBUTTON*/ | WS_TABSTOP,
                     24, 130, 70, 20, hwnd, (HMENU)IDC_DESK_TILE, g_hinst, NULL);
        CreateWindow("BUTTON", "Srodek", WS_CHILD | WS_VISIBLE | 0x09 /*BS_AUTORADIOBUTTON*/ | WS_TABSTOP,
                     104, 130, 70, 20, hwnd, (HMENU)IDC_DESK_CENTER, g_hinst, NULL);

        /* Ramka: Wzor */
        CreateWindow("BUTTON", "Wzor (Pattern)", WS_CHILD | WS_VISIBLE | 0x07 /*BS_GROUPBOX*/,
                     204, 10, 180, 150, hwnd, 0, g_hinst, NULL);
        CreateWindow("STATIC", "Nazwa wzoru:", WS_CHILD | WS_VISIBLE, 216, 30, 150, 16, hwnd, (HMENU)IDC_DESK_PATLBL, g_hinst, NULL);
        CreateWindow("EDIT", cur_pat, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                     216, 48, 156, 22, hwnd, (HMENU)IDC_DESK_PATEDIT, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
                     216, 72, 156, 78, hwnd, (HMENU)IDC_DESK_PATLIST, g_hinst, NULL);

        /* Ramka: Ikony */
        CreateWindow("BUTTON", "Ikony (Icons)", WS_CHILD | WS_VISIBLE | 0x07 /*BS_GROUPBOX*/,
                     12, 166, 372, 54, hwnd, 0, g_hinst, NULL);
        CreateWindow("STATIC", "Odstep ikon (Spacing):", WS_CHILD | WS_VISIBLE, 24, 188, 150, 18, hwnd, (HMENU)IDC_DESK_SPCLBL, g_hinst, NULL);
        CreateWindow("EDIT", cur_spc, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
                     180, 186, 60, 22, hwnd, (HMENU)IDC_DESK_SPCEDIT, g_hinst, NULL);

        /* Przyciski OK / Anuluj */
        CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                     110, 230, 80, 24, hwnd, (HMENU)IDC_OK, g_hinst, NULL);
        CreateWindow("BUTTON", "Anuluj", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     204, 230, 80, 24, hwnd, (HMENU)IDC_CANCEL, g_hinst, NULL);

        /* Wypelnij listbox tapet */
        populate_wallpapers(GetDlgItem(hwnd, IDC_DESK_WPLIST));
        sel_wp = (int)SendDlgItemMessage(hwnd, IDC_DESK_WPLIST, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPCSTR)cur_wp);
        if (sel_wp < 0) sel_wp = 0;
        SendDlgItemMessage(hwnd, IDC_DESK_WPLIST, LB_SETCURSEL, sel_wp, 0);

        /* Wypelnij listbox wzorow */
        for (i = 0; i < NUM_PAT; i++) {
            SendDlgItemMessage(hwnd, IDC_DESK_PATLIST, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)g_pat_list[i]);
            if (lstrcmpi(cur_pat, g_pat_list[i]) == 0) sel_pat = i;
        }
        SendDlgItemMessage(hwnd, IDC_DESK_PATLIST, LB_SETCURSEL, sel_pat, 0);

        /* Radio tile / center */
        CheckRadioButton(hwnd, IDC_DESK_TILE, IDC_DESK_CENTER, tile ? IDC_DESK_TILE : IDC_DESK_CENTER);

        /* Font na kontrolkach */
        {
            int ids[] = {
                IDC_DESK_WPLBL, IDC_DESK_WPEDIT, IDC_DESK_WPLIST, IDC_DESK_TILE, IDC_DESK_CENTER,
                IDC_DESK_PATLBL, IDC_DESK_PATEDIT, IDC_DESK_PATLIST, IDC_DESK_SPCLBL, IDC_DESK_SPCEDIT,
                IDC_OK, IDC_CANCEL
            };
            for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
                HWND hc = GetDlgItem(hwnd, ids[i]);
                if (hc) SendMessage(hc, WM_SETFONT, (WPARAM)hsys, 0);
            }
        }
        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = wp;
        WORD code = HIWORD(lp);

        if (id == IDC_DESK_WPLIST && code == LBN_SELCHANGE) {
            int sel = (int)SendDlgItemMessage(hwnd, IDC_DESK_WPLIST, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                char text[64];
                SendDlgItemMessage(hwnd, IDC_DESK_WPLIST, LB_GETTEXT, sel, (LPARAM)(LPSTR)text);
                SetDlgItemText(hwnd, IDC_DESK_WPEDIT, text);
            }
        } else if (id == IDC_DESK_PATLIST && code == LBN_SELCHANGE) {
            int sel = (int)SendDlgItemMessage(hwnd, IDC_DESK_PATLIST, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                char text[64];
                SendDlgItemMessage(hwnd, IDC_DESK_PATLIST, LB_GETTEXT, sel, (LPARAM)(LPSTR)text);
                SetDlgItemText(hwnd, IDC_DESK_PATEDIT, text);
            }
        } else if (id == IDC_OK) {
            char wp_buf[64], pat_buf[64], spc_buf[16];
            BOOL tile = (SendDlgItemMessage(hwnd, IDC_DESK_TILE, BM_GETCHECK, 0, 0) == 1);

            GetDlgItemText(hwnd, IDC_DESK_WPEDIT, (LPSTR)wp_buf, sizeof(wp_buf));
            GetDlgItemText(hwnd, IDC_DESK_PATEDIT, (LPSTR)pat_buf, sizeof(pat_buf));
            GetDlgItemText(hwnd, IDC_DESK_SPCEDIT, (LPSTR)spc_buf, sizeof(spc_buf));

            WriteProfileString("Desktop", "Wallpaper", wp_buf);
            WriteProfileString("Desktop", "TileWallpaper", tile ? "1" : "0");
            WriteProfileString("Desktop", "Pattern", pat_buf);
            if (spc_buf[0]) WriteProfileString("Desktop", "IconSpacing", spc_buf);

            /* Zastosuj natychmiast tapete */
            SystemParametersInfo(20 /*SPI_SETDESKWALLPAPER*/, 0, (LPVOID)(LPCSTR)wp_buf, 3 /*SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE*/);

            /* Powiadomienie innych aplikacji */
            PostMessage(0xFFFF /*HWND_BROADCAST*/, 0x001A /*WM_WININICHANGE*/, 0, (LPARAM)(LPCSTR)"Desktop");
            DestroyWindow(hwnd);
        } else if (id == IDC_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static void ShowDesktopDialog(HWND hParent)
{
    MSG msg;
    WNDCLASS wc;
    HWND hDlg;
    int x = 120, y = 90;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DesktopDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "W16DesktopPropDlg";
    RegisterClass(&wc);

    hDlg = CreateWindow(
        "W16DesktopPropDlg",
        "Wlasciwosci pulpitu (Desktop Properties)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_BORDER,
        x, y, 404, 296,
        hParent, 0, g_hinst, NULL
    );

    if (!hDlg) return;
    if (hParent) EnableWindow(hParent, FALSE);

    while (IsWindow(hDlg) && GetMessage(&msg, 0, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }
}

/* ============================================================
 * Dialog: Data i Godzina (Date / Time)
 * ============================================================ */
LRESULT CALLBACK __export DateTimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        int y, m, d, hr, mn, sc;
        char buf[16];
        HFONT hsys = (HFONT)GetStockObject(SYSTEM_FONT);
        int ids[] = { IDC_DT_DAY, IDC_DT_MONTH, IDC_DT_YEAR, IDC_DT_HOUR, IDC_DT_MIN, IDC_DT_SEC, IDC_OK, IDC_CANCEL };
        int i;

        dos_get_date(&y, &m, &d);
        dos_get_time(&hr, &mn, &sc);

        CreateWindow("BUTTON", "Data (Date)", WS_CHILD | WS_VISIBLE | 0x07 /*BS_GROUPBOX*/,
                     12, 10, 260, 64, hwnd, 0, g_hinst, NULL);
        CreateWindow("STATIC", "Dzien:", WS_CHILD | WS_VISIBLE, 24, 30, 40, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(d, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 66, 28, 30, 22, hwnd, (HMENU)IDC_DT_DAY, g_hinst, NULL);

        CreateWindow("STATIC", "Mies:", WS_CHILD | WS_VISIBLE, 104, 30, 36, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(m, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 142, 28, 30, 22, hwnd, (HMENU)IDC_DT_MONTH, g_hinst, NULL);

        CreateWindow("STATIC", "Rok:", WS_CHILD | WS_VISIBLE, 180, 30, 32, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(y, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 214, 28, 48, 22, hwnd, (HMENU)IDC_DT_YEAR, g_hinst, NULL);

        CreateWindow("BUTTON", "Godzina (Time)", WS_CHILD | WS_VISIBLE | 0x07 /*BS_GROUPBOX*/,
                     12, 84, 260, 64, hwnd, 0, g_hinst, NULL);
        CreateWindow("STATIC", "Godz:", WS_CHILD | WS_VISIBLE, 24, 104, 40, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(hr, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 66, 102, 30, 22, hwnd, (HMENU)IDC_DT_HOUR, g_hinst, NULL);

        CreateWindow("STATIC", "Min:", WS_CHILD | WS_VISIBLE, 104, 104, 36, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(mn, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 142, 102, 30, 22, hwnd, (HMENU)IDC_DT_MIN, g_hinst, NULL);

        CreateWindow("STATIC", "Sek:", WS_CHILD | WS_VISIBLE, 180, 104, 32, 16, hwnd, 0, g_hinst, NULL);
        int_to_str(sc, buf);
        CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 214, 102, 30, 22, hwnd, (HMENU)IDC_DT_SEC, g_hinst, NULL);

        CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                     50, 160, 80, 24, hwnd, (HMENU)IDC_OK, g_hinst, NULL);
        CreateWindow("BUTTON", "Anuluj", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     150, 160, 80, 24, hwnd, (HMENU)IDC_CANCEL, g_hinst, NULL);

        for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
            HWND hc = GetDlgItem(hwnd, ids[i]);
            if (hc) SendMessage(hc, WM_SETFONT, (WPARAM)hsys, 0);
        }
        return 0;
    }

    case WM_COMMAND:
        if (wp == IDC_OK) {
            char b[16];
            int d, m, y, hr, mn, sc;
            GetDlgItemText(hwnd, IDC_DT_DAY, b, sizeof(b));   d = str_to_int(b);
            GetDlgItemText(hwnd, IDC_DT_MONTH, b, sizeof(b)); m = str_to_int(b);
            GetDlgItemText(hwnd, IDC_DT_YEAR, b, sizeof(b));  y = str_to_int(b);
            GetDlgItemText(hwnd, IDC_DT_HOUR, b, sizeof(b));  hr = str_to_int(b);
            GetDlgItemText(hwnd, IDC_DT_MIN, b, sizeof(b));   mn = str_to_int(b);
            GetDlgItemText(hwnd, IDC_DT_SEC, b, sizeof(b));   sc = str_to_int(b);

            if (y > 0 && m > 0 && d > 0) dos_set_date(y, m, d);
            if (hr >= 0 && mn >= 0 && sc >= 0) dos_set_time(hr, mn, sc);

            DestroyWindow(hwnd);
        } else if (wp == IDC_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static void ShowDateTimeDialog(HWND hParent)
{
    MSG msg;
    WNDCLASS wc;
    HWND hDlg;
    int x = 160, y = 120;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DateTimeDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "W16DateTimeDlg";
    RegisterClass(&wc);

    hDlg = CreateWindow(
        "W16DateTimeDlg",
        "Data i godzina (Date & Time)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_BORDER,
        x, y, 292, 226,
        hParent, 0, g_hinst, NULL
    );

    if (!hDlg) return;
    if (hParent) EnableWindow(hParent, FALSE);

    while (IsWindow(hDlg) && GetMessage(&msg, 0, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }
}

/* ============================================================
 * Okno glowne: Panel sterowania (Control Panel)
 * ============================================================ */
LRESULT CALLBACK __export MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        HFONT hsys = (HFONT)GetStockObject(SYSTEM_FONT);
        int ids[] = { IDC_MAIN_DESK, IDC_MAIN_DATETIME, IDC_MAIN_FONTS, IDC_MAIN_COLORS, IDC_MAIN_EXIT };
        int i;

        CreateWindow("BUTTON", "Pulpit (Desktop)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     20, 20, 150, 36, hwnd, (HMENU)IDC_MAIN_DESK, g_hinst, NULL);
        CreateWindow("BUTTON", "Data i godzina", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     180, 20, 150, 36, hwnd, (HMENU)IDC_MAIN_DATETIME, g_hinst, NULL);

        CreateWindow("BUTTON", "Czcionki (Fonts)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     20, 68, 150, 36, hwnd, (HMENU)IDC_MAIN_FONTS, g_hinst, NULL);
        CreateWindow("BUTTON", "Kolory (Colors)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                     180, 68, 150, 36, hwnd, (HMENU)IDC_MAIN_COLORS, g_hinst, NULL);

        CreateWindow("BUTTON", "Zamknij", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                     105, 120, 140, 28, hwnd, (HMENU)IDC_MAIN_EXIT, g_hinst, NULL);

        for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++) {
            HWND hc = GetDlgItem(hwnd, ids[i]);
            if (hc) SendMessage(hc, WM_SETFONT, (WPARAM)hsys, 0);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (wp) {
        case IDC_MAIN_DESK:
            ShowDesktopDialog(hwnd);
            break;
        case IDC_MAIN_DATETIME:
            ShowDateTimeDialog(hwnd);
            break;
        case IDC_MAIN_FONTS:
            MessageBox(hwnd, "Ustawienia czcionek sa dostepne we wlasciwosciach Calmiry oraz w oknach wyboru czcionki.", "Czcionki", MB_OK);
            break;
        case IDC_MAIN_COLORS:
            MessageBox(hwnd, "Ustawienia kolorow mozna konfigurowac w pliku WIN.INI [colors].", "Kolory", MB_OK);
            break;
        case IDC_MAIN_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

/* ============================================================
 * WinMain
 * ============================================================ */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    WNDCLASS wc;
    HWND hwnd;
    (void)hPrevInstance;
    (void)nCmdShow;
    g_hinst = hInstance;

    /* Sprawdz parametry linii polecen */
    if (str_contains(lpCmdLine, "desktop")) {
        ShowDesktopDialog(0);
        return 0;
    }
    if (str_contains(lpCmdLine, "date") || str_contains(lpCmdLine, "time")) {
        ShowDateTimeDialog(0);
        return 0;
    }

    /* Standardowy widok okna Panelu Sterowania */
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "ControlPanelMainClass";

    if (!RegisterClass(&wc)) return 1;

    hwnd = CreateWindow(
        "ControlPanelMainClass",
        "Panel sterowania (Control Panel)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_VISIBLE,
        140, 100, 360, 190,
        0, 0, hInstance, NULL
    );

    if (!hwnd) return 2;

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
