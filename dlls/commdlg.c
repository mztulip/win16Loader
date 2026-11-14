/* commdlg.c - COMMDLG.DLL: standardowe okna dialogowe (GetOpenFileName, GetSaveFileName) */
#include "w16.h"

#pragma pack(push, 1)
typedef struct {
    char  reserved[21];
    BYTE  attr;
    WORD  time;
    WORD  date;
    DWORD size;
    char  name[13];
} DOS_DTA;
#pragma pack(pop)

typedef struct {
    char desc[48];
    char pattern[24];
} FILE_FILTER;

static HINSTANCE      g_hinst = 0;
static BOOL           g_class_reg = FALSE;
static HWND           g_dlg = 0;
static BOOL           g_dlg_res = FALSE;
static BOOL           g_is_save = FALSE;
static LPOPENFILENAME g_ofn = NULL;

static char           g_cur_dir[80];
static char           g_orig_dir[80];
static char           g_dos_tmp[80];
static char           g_cur_pattern[32];
static FILE_FILTER    g_filters[8];
static int            g_filter_count = 0;
static int            g_cur_filter = 0;

static DOS_DTA        g_dta;

/* INT 21h z PM */
static BYTE dos_getdrive(void);
#pragma aux dos_getdrive = "mov ah, 19h" "int 21h" value [al] modify [ax];

static void dos_setdrive(BYTE d);
#pragma aux dos_setdrive = "mov ah, 0Eh" "int 21h" parm [dl] modify [ax dx];

static void dos_getdir(char *b);
#pragma aux dos_getdir = "mov ah, 47h" "xor dl, dl" "int 21h" parm [si] modify [ax dx];

static int dos_chdir(char *p);
#pragma aux dos_chdir = "mov ah, 3Bh" "int 21h" "jc failed" "xor ax, ax" "failed:" parm [dx] value [ax] modify [ax];

static void dos_setdta(char *d);
#pragma aux dos_setdta = "mov ah, 1Ah" "int 21h" parm [dx] modify [ax];

static int dos_findfirst(char *p, WORD attr);
#pragma aux dos_findfirst = "mov ah, 4Eh" "int 21h" "jc failed" "xor ax, ax" "failed:" parm [dx] [cx] value [ax] modify [ax];

static int dos_findnext(void);
#pragma aux dos_findnext = "mov ah, 4Fh" "int 21h" "jc failed" "xor ax, ax" "failed:" value [ax] modify [ax];

static void update_curdir_text(HWND hwnd)
{
    BYTE drv = dos_getdrive();
    g_cur_dir[0] = (char)('A' + drv);
    g_cur_dir[1] = ':';
    g_cur_dir[2] = '\\';
    g_cur_dir[3] = 0;
    dos_getdir(g_cur_dir + 3);
    if (hwnd) SetDlgItemText(hwnd, stc1, (LPCSTR)g_cur_dir);
}

static void refresh_dirs(HWND hwnd)
{
    int res;
    SendDlgItemMessage(hwnd, lst2, LB_RESETCONTENT, 0, 0);

    /* Jesli nie jestesmy w katalogu glownym, dodaj [..] */
    if (g_cur_dir[3] != 0) {
        SendDlgItemMessage(hwnd, lst2, LB_ADDSTRING, 0, (LONG)(LPCSTR)"[..]");
    }

    dos_setdta((char *)&g_dta);
    res = dos_findfirst("*.*", 0x10);
    while (res == 0) {
        if ((g_dta.attr & 0x10) && g_dta.name[0] != '.') {
            char dstr[20];
            int dl;
            dstr[0] = '[';
            w_strncpy((LPSTR)(dstr + 1), (LPCSTR)g_dta.name, 14);
            dl = w_strlen((LPCSTR)dstr);
            dstr[dl++] = ']';
            dstr[dl] = 0;
            SendDlgItemMessage(hwnd, lst2, LB_ADDSTRING, 0, (LONG)(LPCSTR)dstr);
        }
        res = dos_findnext();
    }
}

static void refresh_files(HWND hwnd)
{
    int res;
    SendDlgItemMessage(hwnd, lst1, LB_RESETCONTENT, 0, 0);

    dos_setdta((char *)&g_dta);
    res = dos_findfirst(g_cur_pattern[0] ? g_cur_pattern : "*.*", 0x07);
    while (res == 0) {
        if (!(g_dta.attr & 0x10) && g_dta.name[0] != '.') {
            SendDlgItemMessage(hwnd, lst1, LB_ADDSTRING, 0, (LONG)(LPCSTR)g_dta.name);
        }
        res = dos_findnext();
    }
}

static void refresh_all(HWND hwnd)
{
    update_curdir_text(hwnd);
    refresh_dirs(hwnd);
    refresh_files(hwnd);
}

static void on_ok(HWND hwnd)
{
    char name[128];
    char fullpath[160];
    int i, len;
    BOOL has_wildcard = FALSE;

    name[0] = 0;
    GetDlgItemText(hwnd, edt1, (LPSTR)name, sizeof(name));
    dbg("COMMDLG: on_ok called, name='"); dbg((LPCSTR)name); dbg("'\n");

    /* Obetnij biale znaki */
    for (i = 0; name[i] == ' '; i++);
    if (i > 0) w_strcpy((LPSTR)name, (LPCSTR)(name + i));
    len = w_strlen((LPCSTR)name);
    while (len > 0 && name[len - 1] == ' ') name[--len] = 0;

    if (len == 0) {
        w_strcpy((LPSTR)name, (LPCSTR)g_cur_pattern);
        SetDlgItemText(hwnd, edt1, (LPCSTR)name);
        refresh_files(hwnd);
        return;
    }

    for (i = 0; name[i]; i++) {
        if (name[i] == '*' || name[i] == '?') {
            has_wildcard = TRUE;
            break;
        }
    }

    if (has_wildcard) {
        w_strncpy((LPSTR)g_cur_pattern, (LPCSTR)name, sizeof(g_cur_pattern));
        refresh_files(hwnd);
        return;
    }

    /* Sprawdz czy podano sciezke do katalogu */
    w_strncpy((LPSTR)g_dos_tmp, (LPCSTR)name, sizeof(g_dos_tmp));
    if (dos_chdir(g_dos_tmp) == 0) {
        refresh_all(hwnd);
        SetDlgItemText(hwnd, edt1, (LPCSTR)g_cur_pattern);
        return;
    }

    /* Domyslne rozszerzenie */
    {
        BOOL has_dot = FALSE;
        for (i = 0; name[i]; i++) if (name[i] == '.') has_dot = TRUE;
        if (!has_dot) {
            if (g_ofn->lpstrDefExt && HIWORD(g_ofn->lpstrDefExt) && g_ofn->lpstrDefExt[0]) {
                if (len < (int)sizeof(name) - 5) {
                    name[len++] = '.';
                    w_strncpy((LPSTR)(name + len), g_ofn->lpstrDefExt, 4);
                    name[len + 4] = 0;
                }
            } else if (g_cur_pattern[0] == '*' && g_cur_pattern[1] == '.' && g_cur_pattern[2] && g_cur_pattern[2] != '*') {
                if (len < (int)sizeof(name) - 5) {
                    w_strncpy((LPSTR)(name + len), (LPCSTR)(g_cur_pattern + 1), 5);
                }
            }
        }
    }

    /* Zbuduj pelna sciezke */
    if (name[1] == ':') {
        w_strncpy((LPSTR)fullpath, (LPCSTR)name, sizeof(fullpath));
    } else if (name[0] == '\\') {
        fullpath[0] = (char)('A' + dos_getdrive());
        fullpath[1] = ':';
        w_strncpy((LPSTR)(fullpath + 2), (LPCSTR)name, sizeof(fullpath) - 2);
    } else {
        int flen;
        w_strncpy((LPSTR)fullpath, (LPCSTR)g_cur_dir, sizeof(fullpath));
        flen = w_strlen((LPCSTR)fullpath);
        if (flen > 0 && fullpath[flen - 1] != '\\') {
            fullpath[flen++] = '\\';
            fullpath[flen] = 0;
        }
        w_strncpy((LPSTR)(fullpath + flen), (LPCSTR)name, sizeof(fullpath) - flen);
    }

    /* Walidacja istnienia / nadpisania */
    if (!g_is_save) {
        if (g_ofn->Flags & OFN_FILEMUSTEXIST) {
            int fh = _lopen((LPCSTR)fullpath, 0);
            if (fh < 0) {
                MessageBox(hwnd, (LPCSTR)"Podany plik nie istnieje.", (LPCSTR)"Blad", MB_OK | MB_ICONEXCLAMATION);
                return;
            }
            _lclose(fh);
        }
    } else {
        if (g_ofn->Flags & OFN_OVERWRITEPROMPT) {
            int fh = _lopen((LPCSTR)fullpath, 0);
            if (fh >= 0) {
                _lclose(fh);
                if (MessageBox(hwnd, (LPCSTR)"Plik o podanej nazwie juz istnieje.\r\nCzy chcesz go zastapic?", (LPCSTR)"Zapisz jako", MB_YESNO | MB_ICONEXCLAMATION) != IDYES) {
                    return;
                }
            }
        }
    }

    /* Skopiuj wynik do struktury OPENFILENAME */
    if (g_ofn->lpstrFile && HIWORD(g_ofn->lpstrFile) && g_ofn->nMaxFile > 0) {
        w_strncpy(g_ofn->lpstrFile, (LPCSTR)fullpath, (int)g_ofn->nMaxFile);
    }

    {
        int f_idx = 0, e_idx = 0;
        for (i = 0; fullpath[i]; i++) {
            if (fullpath[i] == '\\' || fullpath[i] == ':') f_idx = i + 1;
            if (fullpath[i] == '.') e_idx = i + 1;
        }
        if (e_idx < f_idx) e_idx = 0;
        g_ofn->nFileOffset = (WORD)f_idx;
        g_ofn->nFileExtension = (WORD)e_idx;
        if (g_ofn->lpstrFileTitle && HIWORD(g_ofn->lpstrFileTitle) && g_ofn->nMaxFileTitle > 0) {
            w_strncpy(g_ofn->lpstrFileTitle, (LPCSTR)(fullpath + f_idx), (int)g_ofn->nMaxFileTitle);
        }
    }

    g_ofn->nFilterIndex = (DWORD)(g_cur_filter + 1);
    g_dlg_res = TRUE;
    DestroyWindow(hwnd);
}

static LRESULT __far __pascal FileDlgProc(HWND hwnd, WORD msg, WORD wp, LONG lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        int d, cur_drv, sel_drv = 0, drv_cnt = 0;
        HANDLE hfont = GetStockObject(SYSTEM_FONT);

        /* Lewa kolumna: pliki i typ */
        CreateWindow("STATIC", "Nazwa &pliku:", WS_CHILD | WS_VISIBLE, 12, 10, 160, 16, hwnd, (HMENU)stc3, g_hinst, NULL);
        CreateWindow("EDIT", g_cur_pattern, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 12, 28, 160, 22, hwnd, (HMENU)edt1, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_SORT | WS_TABSTOP, 12, 54, 160, 124, hwnd, (HMENU)lst1, g_hinst, NULL);
        CreateWindow("STATIC", "Typ p&likow:", WS_CHILD | WS_VISIBLE, 12, 184, 160, 16, hwnd, (HMENU)stc2, g_hinst, NULL);
        CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_TABSTOP, 12, 202, 160, 80, hwnd, (HMENU)cmb1, g_hinst, NULL);

        /* Prawa kolumna: sciezka, katalogi i dyski */
        CreateWindow("STATIC", "&Katalogi:", WS_CHILD | WS_VISIBLE, 184, 10, 150, 16, hwnd, (HMENU)stc4, g_hinst, NULL);
        CreateWindow("STATIC", g_cur_dir, WS_CHILD | WS_VISIBLE, 184, 28, 150, 22, hwnd, (HMENU)stc1, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_SORT | WS_TABSTOP, 184, 54, 150, 124, hwnd, (HMENU)lst2, g_hinst, NULL);
        CreateWindow("STATIC", "&Dyski:", WS_CHILD | WS_VISIBLE, 184, 184, 150, 16, hwnd, (HMENU)stc5, g_hinst, NULL);
        CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_TABSTOP, 184, 202, 150, 80, hwnd, (HMENU)cmb2, g_hinst, NULL);

        /* Przyciski */
        CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 348, 28, 76, 24, hwnd, (HMENU)IDOK, g_hinst, NULL);
        CreateWindow("BUTTON", "Anuluj", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 348, 58, 76, 24, hwnd, (HMENU)IDCANCEL, g_hinst, NULL);

        /* Ustawienie fontu na kontrolkach */
        {
            int ids[12];
            ids[0] = stc1; ids[1] = stc2; ids[2] = stc3; ids[3] = stc4;
            ids[4] = stc5; ids[5] = lst1; ids[6] = lst2; ids[7] = cmb1;
            ids[8] = cmb2; ids[9] = edt1; ids[10] = IDOK; ids[11] = IDCANCEL;
            for (d = 0; d < 12; d++) {
                HWND hctl = GetDlgItem(hwnd, ids[d]);
                if (hctl) SendMessage(hctl, WM_SETFONT, (WORD)hfont, 0);
            }
        }

        /* Wypelnij filtry (cmb1) */
        for (d = 0; d < g_filter_count; d++) {
            SendDlgItemMessage(hwnd, cmb1, CB_ADDSTRING, 0, (LONG)(LPCSTR)g_filters[d].desc);
        }
        SendDlgItemMessage(hwnd, cmb1, CB_SETCURSEL, g_cur_filter, 0);

        /* Wypelnij dyski (cmb2) */
        cur_drv = dos_getdrive();
        for (d = 0; d < 26; d++) {
            if (GetDriveType(d) > 1) {
                char drvstr[8];
                drvstr[0] = '[';
                drvstr[1] = '-';
                drvstr[2] = (char)('a' + d);
                drvstr[3] = '-';
                drvstr[4] = ']';
                drvstr[5] = 0;
                SendDlgItemMessage(hwnd, cmb2, CB_ADDSTRING, 0, (LONG)(LPCSTR)drvstr);
                if (d == cur_drv) sel_drv = drv_cnt;
                drv_cnt++;
            }
        }
        SendDlgItemMessage(hwnd, cmb2, CB_SETCURSEL, sel_drv, 0);

        refresh_all(hwnd);

        SetFocus(GetDlgItem(hwnd, edt1));
        SendDlgItemMessage(hwnd, edt1, EM_SETSEL, 0, MAKELONG(0, 0xFFFF));
        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = wp;
        WORD code = HIWORD(lp);
        dbg("COMMDLG: WM_COMMAND id="); dbg_hex(id, 4); dbg(" code="); dbg_hex(code, 4); dbg("\n");

        switch (id) {
        case lst1:
            if (code == LBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, lst1, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    char fn[32];
                    SendDlgItemMessage(hwnd, lst1, LB_GETTEXT, sel, (LONG)(LPSTR)fn);
                    SetDlgItemText(hwnd, edt1, (LPCSTR)fn);
                }
            } else if (code == LBN_DBLCLK) {
                on_ok(hwnd);
            }
            break;

        case lst2:
            if (code == LBN_DBLCLK) {
                int sel = (int)SendDlgItemMessage(hwnd, lst2, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    char dn[32];
                    int len;
                    SendDlgItemMessage(hwnd, lst2, LB_GETTEXT, sel, (LONG)(LPSTR)dn);
                    len = w_strlen((LPCSTR)dn);
                    if (dn[0] == '[' && dn[len - 1] == ']') {
                        dn[len - 1] = 0;
                        w_strncpy((LPSTR)g_dos_tmp, (LPCSTR)(dn + 1), sizeof(g_dos_tmp));
                        dos_chdir(g_dos_tmp);
                        refresh_all(hwnd);
                    }
                }
            }
            break;

        case cmb1:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, cmb1, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_filter_count) {
                    g_cur_filter = sel;
                    w_strncpy((LPSTR)g_cur_pattern, (LPCSTR)g_filters[sel].pattern, sizeof(g_cur_pattern));
                    SetDlgItemText(hwnd, edt1, (LPCSTR)g_cur_pattern);
                    refresh_files(hwnd);
                }
            }
            break;

        case cmb2:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, cmb2, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    char ds[16];
                    char dch;
                    SendDlgItemMessage(hwnd, cmb2, CB_GETLBTEXT, sel, (LONG)(LPSTR)ds);
                    dch = ds[0] == '[' ? ds[2] : ds[0];
                    if (dch >= 'A' && dch <= 'Z') dch = (char)(dch + 32);
                    if (dch >= 'a' && dch <= 'z') {
                        dos_setdrive((BYTE)(dch - 'a'));
                        refresh_all(hwnd);
                    }
                }
            }
            break;

        case IDOK:
            on_ok(hwnd);
            break;

        case IDCANCEL:
            g_dlg_res = FALSE;
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_CLOSE:
        g_dlg_res = FALSE;
        DestroyWindow(hwnd);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static BOOL ExecuteFileDialog(LPOPENFILENAME ofn, BOOL is_save)
{
    MSG msg;
    RECT rc;
    int x, y;
    BYTE orig_drv;
    LPCSTR title;

    if (!ofn || !HIWORD(ofn)) return FALSE;
    if (!ofn->lpstrFile || !HIWORD(ofn->lpstrFile) || ofn->nMaxFile == 0) return FALSE;

    g_ofn = ofn;
    g_is_save = is_save;
    g_dlg_res = FALSE;

    /* Zapamietaj oryginalny katalog */
    orig_drv = dos_getdrive();
    g_orig_dir[0] = (char)('A' + orig_drv);
    g_orig_dir[1] = ':';
    g_orig_dir[2] = '\\';
    g_orig_dir[3] = 0;
    dos_getdir(g_orig_dir + 3);

    /* Opcjonalny katalog poczatkowy */
    if (ofn->lpstrInitialDir && HIWORD(ofn->lpstrInitialDir) && ofn->lpstrInitialDir[0]) {
        if (ofn->lpstrInitialDir[1] == ':') {
            char dch = ofn->lpstrInitialDir[0];
            if (dch >= 'A' && dch <= 'Z') dch = (char)(dch + 32);
            if (dch >= 'a' && dch <= 'z') dos_setdrive((BYTE)(dch - 'a'));
        }
        w_strncpy((LPSTR)g_dos_tmp, ofn->lpstrInitialDir, sizeof(g_dos_tmp));
        dos_chdir(g_dos_tmp);
    }

    /* Przygotuj biezacy katalog */
    update_curdir_text(0);

    /* Parsowanie lpstrFilter */
    g_filter_count = 0;
    if (ofn->lpstrFilter && HIWORD(ofn->lpstrFilter) && ofn->lpstrFilter[0]) {
        LPCSTR p = ofn->lpstrFilter;
        while (*p && g_filter_count < 8) {
            w_strncpy((LPSTR)g_filters[g_filter_count].desc, p, sizeof(g_filters[0].desc));
            while (*p) p++;
            p++;
            if (!*p) break;
            w_strncpy((LPSTR)g_filters[g_filter_count].pattern, p, sizeof(g_filters[0].pattern));
            while (*p) p++;
            p++;
            g_filter_count++;
        }
    }
    if (g_filter_count == 0) {
        w_strcpy((LPSTR)g_filters[0].desc, (LPCSTR)"Wszystkie pliki (*.*)");
        w_strcpy((LPSTR)g_filters[0].pattern, (LPCSTR)"*.*");
        w_strcpy((LPSTR)g_filters[1].desc, (LPCSTR)"Pliki tekstowe (*.txt)");
        w_strcpy((LPSTR)g_filters[1].pattern, (LPCSTR)"*.txt");
        g_filter_count = 2;
    }

    g_cur_filter = (ofn->nFilterIndex > 0 && ofn->nFilterIndex <= (DWORD)g_filter_count) ? (int)(ofn->nFilterIndex - 1) : 0;
    w_strncpy(g_cur_pattern, (LPCSTR)g_filters[g_cur_filter].pattern, sizeof(g_cur_pattern));

    /* Jesli w lpstrFile przekazano poczatkowa nazwe z maska */
    if (ofn->lpstrFile[0]) {
        int i;
        BOOL wild = FALSE;
        for (i = 0; ofn->lpstrFile[i]; i++) {
            if (ofn->lpstrFile[i] == '*' || ofn->lpstrFile[i] == '?') wild = TRUE;
        }
        if (wild) w_strncpy(g_cur_pattern, ofn->lpstrFile, sizeof(g_cur_pattern));
    }

    /* Zarejestruj klase okna */
    if (!g_class_reg) {
        WNDCLASS wc;
        w_memset(&wc, 0, sizeof(wc));
        wc.style = 3; /* CS_HREDRAW | CS_VREDRAW */
        wc.lpfnWndProc = FileDlgProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "W16FileOpenDlg";
        if (RegisterClass(&wc)) g_class_reg = TRUE;
    }

    /* Pozycja okna */
    x = 100; y = 100;
    if (ofn->hwndOwner && IsWindow(ofn->hwndOwner)) {
        GetWindowRect(ofn->hwndOwner, (LPRECT)&rc);
        x = rc.left + (rc.right - rc.left - 436) / 2;
        y = rc.top + (rc.bottom - rc.top - 280) / 2;
    }
    if (x < 10) x = 10;
    if (y < 10) y = 10;

    title = ofn->lpstrTitle && HIWORD(ofn->lpstrTitle) && ofn->lpstrTitle[0]
            ? ofn->lpstrTitle
            : (is_save ? (LPCSTR)"Zapisz jako" : (LPCSTR)"Otworz");

    g_dlg = CreateWindow(
        "W16FileOpenDlg",
        title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_BORDER,
        x, y, 436, 280,
        ofn->hwndOwner, 0, g_hinst, NULL
    );

    if (!g_dlg) return FALSE;

    if (ofn->hwndOwner && IsWindow(ofn->hwndOwner)) EnableWindow(ofn->hwndOwner, FALSE);

    while (IsWindow(g_dlg) && GetMessage(&msg, 0, 0, 0)) {
        if (!IsDialogMessage(g_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (ofn->hwndOwner && IsWindow(ofn->hwndOwner)) {
        EnableWindow(ofn->hwndOwner, TRUE);
        SetActiveWindow(ofn->hwndOwner);
    }

    if (ofn->Flags & OFN_NOCHANGEDIR) {
        dos_setdrive(orig_drv);
        dos_chdir(g_orig_dir);
    }

    dbg("COMMDLG: ExecuteFileDialog end, res="); dbg_hex(g_dlg_res, 4); dbg(" file='"); dbg(g_ofn->lpstrFile); dbg("'\n");
    return g_dlg_res;
}

BOOL API GetOpenFileName(LPOPENFILENAME ofn)
{
    return ExecuteFileDialog(ofn, FALSE);
}

BOOL API GetSaveFileName(LPOPENFILENAME ofn)
{
    return ExecuteFileDialog(ofn, TRUE);
}

/* ============================================================
 * ChooseFont
 * ============================================================ */
#define IDC_CF_FACELABEL   101
#define IDC_CF_FACEEDIT    102
#define IDC_CF_FACELIST    103
#define IDC_CF_STYLELABEL  104
#define IDC_CF_STYLEEDIT   105
#define IDC_CF_STYLELIST   106
#define IDC_CF_SIZELABEL   107
#define IDC_CF_SIZEEDIT    108
#define IDC_CF_SIZELIST    109
#define IDC_CF_SAMPLEGRP   110
#define IDC_CF_SAMPLETEXT  111

static const char * const g_font_names[] = {
    "MS Sans Serif",
    "System",
    "Fixedsys",
    "Courier",
    "Small Fonts"
};
#define NUM_FONT_NAMES 5

static const char * const g_style_names[] = {
    "Regular",
    "Italic",
    "Bold",
    "Bold Italic"
};
#define NUM_STYLE_NAMES 4

static const int g_size_pts[] = {
    8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24
};
#define NUM_SIZE_PTS 11

static BOOL         g_cf_res = FALSE;
static LPCHOOSEFONT g_cf = NULL;
static LOGFONT      g_cf_lf;
static int          g_cf_pt = 10;
static HFONT        g_cf_hfont = 0;
static HWND         g_cf_dlg = 0;

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

static void update_sample_font(HWND hwnd)
{
    HWND hSample = GetDlgItem(hwnd, IDC_CF_SAMPLETEXT);
    HFONT hNew;
    int pt = g_cf_pt;
    if (pt < 4) pt = 4;
    if (pt > 72) pt = 72;

    /* Wysokosc w pikselach: h = -((pt * 96 + 36) / 72) */
    g_cf_lf.lfHeight = -((pt * 96 + 36) / 72);
    g_cf_lf.lfWidth = 0;

    hNew = CreateFontIndirect((const LOGFONT __far *)&g_cf_lf);
    if (hNew) {
        if (hSample) {
            SendMessage(hSample, WM_SETFONT, (WORD)hNew, 1);
            InvalidateRect(hSample, NULL, TRUE);
        }
        if (g_cf_hfont) DeleteObject(g_cf_hfont);
        g_cf_hfont = hNew;
    }
}

static LRESULT __far __pascal ChooseFontDlgProc(HWND hwnd, WORD msg, WORD wp, LONG lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        int i, sel_face = 0, sel_style = 0, sel_size = 2;
        HANDLE hsys = GetStockObject(SYSTEM_FONT);
        char szbuf[16];

        /* Kolumna 1: Czcionka (Font) */
        CreateWindow("STATIC", "Czcionka:", WS_CHILD | WS_VISIBLE, 12, 8, 130, 16, hwnd, (HMENU)IDC_CF_FACELABEL, g_hinst, NULL);
        CreateWindow("EDIT", g_cf_lf.lfFaceName, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 12, 26, 130, 22, hwnd, (HMENU)IDC_CF_FACEEDIT, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 12, 50, 130, 110, hwnd, (HMENU)IDC_CF_FACELIST, g_hinst, NULL);

        /* Kolumna 2: Styl (Style) */
        CreateWindow("STATIC", "Styl:", WS_CHILD | WS_VISIBLE, 150, 8, 110, 16, hwnd, (HMENU)IDC_CF_STYLELABEL, g_hinst, NULL);
        CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 150, 26, 110, 22, hwnd, (HMENU)IDC_CF_STYLEEDIT, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 150, 50, 110, 110, hwnd, (HMENU)IDC_CF_STYLELIST, g_hinst, NULL);

        /* Kolumna 3: Rozmiar (Size) */
        CreateWindow("STATIC", "Rozmiar:", WS_CHILD | WS_VISIBLE, 268, 8, 60, 16, hwnd, (HMENU)IDC_CF_SIZELABEL, g_hinst, NULL);
        CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 268, 26, 60, 22, hwnd, (HMENU)IDC_CF_SIZEEDIT, g_hinst, NULL);
        CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 268, 50, 60, 110, hwnd, (HMENU)IDC_CF_SIZELIST, g_hinst, NULL);

        /* Przyciski */
        CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 338, 26, 82, 24, hwnd, (HMENU)IDOK, g_hinst, NULL);
        CreateWindow("BUTTON", "Anuluj", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 338, 56, 82, 24, hwnd, (HMENU)IDCANCEL, g_hinst, NULL);

        /* Grupa i tekst podgladu */
        CreateWindow("BUTTON", "Przyklad", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 12, 166, 408, 76, hwnd, (HMENU)IDC_CF_SAMPLEGRP, g_hinst, NULL);
        CreateWindow("STATIC", "AaBbYyZz 123", WS_CHILD | WS_VISIBLE | SS_CENTER, 22, 186, 388, 48, hwnd, (HMENU)IDC_CF_SAMPLETEXT, g_hinst, NULL);

        /* Ustawienie fontu systemowego na etykietach i kontrolkach */
        {
            int ctl_ids[] = {
                IDC_CF_FACELABEL, IDC_CF_FACEEDIT, IDC_CF_FACELIST,
                IDC_CF_STYLELABEL, IDC_CF_STYLEEDIT, IDC_CF_STYLELIST,
                IDC_CF_SIZELABEL, IDC_CF_SIZEEDIT, IDC_CF_SIZELIST,
                IDOK, IDCANCEL, IDC_CF_SAMPLEGRP
            };
            for (i = 0; i < (int)(sizeof(ctl_ids)/sizeof(ctl_ids[0])); i++) {
                HWND hc = GetDlgItem(hwnd, ctl_ids[i]);
                if (hc) SendMessage(hc, WM_SETFONT, (WORD)hsys, 0);
            }
        }

        /* Wypelnij liste nazw fontow */
        for (i = 0; i < NUM_FONT_NAMES; i++) {
            SendDlgItemMessage(hwnd, IDC_CF_FACELIST, LB_ADDSTRING, 0, (LONG)(LPCSTR)g_font_names[i]);
            if (w_stricmp(g_cf_lf.lfFaceName, g_font_names[i]) == 0) sel_face = i;
        }
        SendDlgItemMessage(hwnd, IDC_CF_FACELIST, LB_SETCURSEL, sel_face, 0);
        SetDlgItemText(hwnd, IDC_CF_FACEEDIT, g_font_names[sel_face]);
        w_strncpy(g_cf_lf.lfFaceName, g_font_names[sel_face], sizeof(g_cf_lf.lfFaceName));

        /* Wypelnij liste styli */
        for (i = 0; i < NUM_STYLE_NAMES; i++) {
            SendDlgItemMessage(hwnd, IDC_CF_STYLELIST, LB_ADDSTRING, 0, (LONG)(LPCSTR)g_style_names[i]);
        }
        sel_style = (g_cf_lf.lfWeight >= 600 ? 2 : 0) | (g_cf_lf.lfItalic ? 1 : 0);
        SendDlgItemMessage(hwnd, IDC_CF_STYLELIST, LB_SETCURSEL, sel_style, 0);
        SetDlgItemText(hwnd, IDC_CF_STYLEEDIT, g_style_names[sel_style]);

        /* Wypelnij liste rozmiarow */
        for (i = 0; i < NUM_SIZE_PTS; i++) {
            int_to_str(g_size_pts[i], szbuf);
            SendDlgItemMessage(hwnd, IDC_CF_SIZELIST, LB_ADDSTRING, 0, (LONG)(LPCSTR)szbuf);
            if (g_size_pts[i] == g_cf_pt) sel_size = i;
        }
        SendDlgItemMessage(hwnd, IDC_CF_SIZELIST, LB_SETCURSEL, sel_size, 0);
        int_to_str(g_cf_pt, szbuf);
        SetDlgItemText(hwnd, IDC_CF_SIZEEDIT, szbuf);

        update_sample_font(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_CF_FACELIST));
        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = wp;
        WORD code = HIWORD(lp);

        switch (id) {
        case IDC_CF_FACELIST:
            if (code == LBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_CF_FACELIST, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_FONT_NAMES) {
                    w_strncpy(g_cf_lf.lfFaceName, g_font_names[sel], sizeof(g_cf_lf.lfFaceName));
                    SetDlgItemText(hwnd, IDC_CF_FACEEDIT, g_font_names[sel]);
                    update_sample_font(hwnd);
                }
            } else if (code == LBN_DBLCLK) {
                SendMessage(hwnd, WM_COMMAND, IDOK, 0);
            }
            break;

        case IDC_CF_STYLELIST:
            if (code == LBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_CF_STYLELIST, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_STYLE_NAMES) {
                    SetDlgItemText(hwnd, IDC_CF_STYLEEDIT, g_style_names[sel]);
                    g_cf_lf.lfWeight = (sel == 2 || sel == 3) ? 700 : 400;
                    g_cf_lf.lfItalic = (sel == 1 || sel == 3) ? 1 : 0;
                    update_sample_font(hwnd);
                }
            } else if (code == LBN_DBLCLK) {
                SendMessage(hwnd, WM_COMMAND, IDOK, 0);
            }
            break;

        case IDC_CF_SIZELIST:
            if (code == LBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_CF_SIZELIST, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_SIZE_PTS) {
                    char szbuf[16];
                    g_cf_pt = g_size_pts[sel];
                    int_to_str(g_cf_pt, szbuf);
                    SetDlgItemText(hwnd, IDC_CF_SIZEEDIT, szbuf);
                    update_sample_font(hwnd);
                }
            } else if (code == LBN_DBLCLK) {
                SendMessage(hwnd, WM_COMMAND, IDOK, 0);
            }
            break;

        case IDOK:
        {
            char buf[32];
            int sel_pt;

            GetDlgItemText(hwnd, IDC_CF_FACEEDIT, (LPSTR)buf, sizeof(buf));
            if (buf[0]) w_strncpy(g_cf_lf.lfFaceName, buf, sizeof(g_cf_lf.lfFaceName));

            GetDlgItemText(hwnd, IDC_CF_SIZEEDIT, (LPSTR)buf, sizeof(buf));
            sel_pt = str_to_int(buf);
            if (sel_pt >= 4 && sel_pt <= 72) g_cf_pt = sel_pt;

            g_cf_lf.lfHeight = -((g_cf_pt * 96 + 36) / 72);
            g_cf_lf.lfWidth = 0;

            if (g_cf->lpLogFont && HIWORD(g_cf->lpLogFont)) {
                w_memcpy(g_cf->lpLogFont, &g_cf_lf, sizeof(LOGFONT));
            }
            g_cf->iPointSize = g_cf_pt * 10;
            g_cf->nFontType = SCREEN_FONTTYPE | (g_cf_lf.lfWeight >= 600 ? BOLD_FONTTYPE : 0) | (g_cf_lf.lfItalic ? ITALIC_FONTTYPE : 0);
            if (g_cf_lf.lfWeight < 600 && !g_cf_lf.lfItalic) g_cf->nFontType |= REGULAR_FONTTYPE;

            g_cf_res = TRUE;
            DestroyWindow(hwnd);
            break;
        }

        case IDCANCEL:
            g_cf_res = FALSE;
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_CLOSE:
        g_cf_res = FALSE;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_cf_hfont) {
            DeleteObject(g_cf_hfont);
            g_cf_hfont = 0;
        }
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

BOOL API ChooseFont(LPCHOOSEFONT cf)
{
    MSG msg;
    RECT rc;
    int x, y;
    static BOOL cf_class_reg = FALSE;

    if (!cf || !HIWORD(cf)) return FALSE;
    dbg("COMMDLG: ChooseFont called, flags="); dbg_hex((WORD)cf->Flags, 4); dbg("\n");

    g_cf = cf;
    g_cf_res = FALSE;
    g_cf_hfont = 0;
    w_memset(&g_cf_lf, 0, sizeof(LOGFONT));
    g_cf_pt = 10;

    if ((cf->Flags & CF_INITTOLOGFONTSTRUCT) && cf->lpLogFont && HIWORD(cf->lpLogFont)) {
        w_memcpy(&g_cf_lf, cf->lpLogFont, sizeof(LOGFONT));
        if (cf->iPointSize > 0) {
            g_cf_pt = cf->iPointSize / 10;
        } else if (g_cf_lf.lfHeight != 0) {
            int h = g_cf_lf.lfHeight < 0 ? -g_cf_lf.lfHeight : g_cf_lf.lfHeight;
            g_cf_pt = (h * 72 + 48) / 96;
        }
    } else {
        w_strcpy(g_cf_lf.lfFaceName, "MS Sans Serif");
        g_cf_lf.lfWeight = 400;
        g_cf_lf.lfItalic = 0;
        g_cf_pt = 10;
    }
    if (g_cf_pt < 6) g_cf_pt = 10;

    if (!cf_class_reg) {
        WNDCLASS wc;
        w_memset(&wc, 0, sizeof(wc));
        wc.style = 3; /* CS_HREDRAW | CS_VREDRAW */
        wc.lpfnWndProc = ChooseFontDlgProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "W16ChooseFontDlg";
        if (RegisterClass(&wc)) cf_class_reg = TRUE;
    }

    x = 100; y = 80;
    if (cf->hwndOwner && IsWindow(cf->hwndOwner)) {
        GetWindowRect(cf->hwndOwner, (LPRECT)&rc);
        x = rc.left + (rc.right - rc.left - 436) / 2;
        y = rc.top + (rc.bottom - rc.top - 280) / 2;
    }
    if (x < 10) x = 10;
    if (y < 10) y = 10;

    g_cf_dlg = CreateWindow(
        "W16ChooseFontDlg",
        "Czcionka",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_BORDER,
        x, y, 436, 280,
        cf->hwndOwner, 0, g_hinst, NULL
    );

    if (!g_cf_dlg) return FALSE;

    if (cf->hwndOwner && IsWindow(cf->hwndOwner)) EnableWindow(cf->hwndOwner, FALSE);

    while (IsWindow(g_cf_dlg) && GetMessage(&msg, 0, 0, 0)) {
        if (!IsDialogMessage(g_cf_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (cf->hwndOwner && IsWindow(cf->hwndOwner)) {
        EnableWindow(cf->hwndOwner, TRUE);
        SetActiveWindow(cf->hwndOwner);
    }

    dbg("COMMDLG: ChooseFont end, res="); dbg_hex(g_cf_res, 4); dbg(" face='"); dbg(g_cf_lf.lfFaceName); dbg("'\n");
    return g_cf_res;
}

/* ============================================================
 * ChooseColor
 * ============================================================ */
static const COLORREF g_basic_colors[16] = {
    0x000000L, 0x000080L, 0x008000L, 0x008080L,
    0x800000L, 0x800080L, 0x808000L, 0x808080L,
    0xC0C0C0L, 0x0000FFL, 0x00FF00L, 0x00FFFFL,
    0xFF0000L, 0xFF00FFL, 0xFFFF00L, 0xFFFFFFL
};

static BOOL          g_cc_res = FALSE;
static LPCHOOSECOLOR g_cc = NULL;
static COLORREF      g_cc_cur = 0;
static HWND          g_cc_dlg = 0;

static LRESULT __far __pascal ChooseColorDlgProc(HWND hwnd, WORD msg, WORD wp, LONG lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        int i;
        HANDLE hsys = GetStockObject(SYSTEM_FONT);
        CreateWindow("STATIC", "Podstawowe kolory:", WS_CHILD | WS_VISIBLE, 12, 10, 200, 16, hwnd, (HMENU)100, g_hinst, NULL);

        for (i = 0; i < 16; i++) {
            int col = i % 8;
            int row = i / 8;
            char num[4];
            num[0] = (char)('1' + i);
            if (i >= 9) { num[0] = '1'; num[1] = (char)('0' + (i - 9)); num[2] = 0; } else num[1] = 0;
            CreateWindow("BUTTON", num, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                         12 + col * 28, 32 + row * 28, 24, 24, hwnd, (HMENU)(200 + i), g_hinst, NULL);
        }

        CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 12, 100, 76, 24, hwnd, (HMENU)IDOK, g_hinst, NULL);
        CreateWindow("BUTTON", "Anuluj", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 100, 100, 76, 24, hwnd, (HMENU)IDCANCEL, g_hinst, NULL);

        {
            HWND hc = GetDlgItem(hwnd, 100);
            if (hc) SendMessage(hc, WM_SETFONT, (WORD)hsys, 0);
            hc = GetDlgItem(hwnd, IDOK);
            if (hc) SendMessage(hc, WM_SETFONT, (WORD)hsys, 0);
            hc = GetDlgItem(hwnd, IDCANCEL);
            if (hc) SendMessage(hc, WM_SETFONT, (WORD)hsys, 0);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = wp;
        if (id >= 200 && id < 216) {
            g_cc_cur = g_basic_colors[id - 200];
            SendMessage(hwnd, WM_COMMAND, IDOK, 0);
        } else if (id == IDOK) {
            if (g_cc) g_cc->rgbResult = g_cc_cur;
            g_cc_res = TRUE;
            DestroyWindow(hwnd);
        } else if (id == IDCANCEL) {
            g_cc_res = FALSE;
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_CLOSE:
        g_cc_res = FALSE;
        DestroyWindow(hwnd);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

BOOL API ChooseColor(LPCHOOSECOLOR cc)
{
    MSG msg;
    RECT rc;
    int x, y;
    static BOOL cc_class_reg = FALSE;

    if (!cc || !HIWORD(cc)) return FALSE;
    dbg("COMMDLG: ChooseColor called\n");

    g_cc = cc;
    g_cc_res = FALSE;
    g_cc_cur = (cc->Flags & CC_RGBINIT) ? cc->rgbResult : 0;

    if (!cc_class_reg) {
        WNDCLASS wc;
        w_memset(&wc, 0, sizeof(wc));
        wc.style = 3;
        wc.lpfnWndProc = ChooseColorDlgProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "W16ChooseColorDlg";
        if (RegisterClass(&wc)) cc_class_reg = TRUE;
    }

    x = 120; y = 120;
    if (cc->hwndOwner && IsWindow(cc->hwndOwner)) {
        GetWindowRect(cc->hwndOwner, (LPRECT)&rc);
        x = rc.left + (rc.right - rc.left - 250) / 2;
        y = rc.top + (rc.bottom - rc.top - 160) / 2;
    }
    if (x < 10) x = 10;
    if (y < 10) y = 10;

    g_cc_dlg = CreateWindow(
        "W16ChooseColorDlg",
        "Kolor",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_BORDER,
        x, y, 250, 160,
        cc->hwndOwner, 0, g_hinst, NULL
    );

    if (!g_cc_dlg) return FALSE;

    if (cc->hwndOwner && IsWindow(cc->hwndOwner)) EnableWindow(cc->hwndOwner, FALSE);

    while (IsWindow(g_cc_dlg) && GetMessage(&msg, 0, 0, 0)) {
        if (!IsDialogMessage(g_cc_dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (cc->hwndOwner && IsWindow(cc->hwndOwner)) {
        EnableWindow(cc->hwndOwner, TRUE);
        SetActiveWindow(cc->hwndOwner);
    }

    return g_cc_res;
}

BOOL  API PrintDlg(LPVOID pd)         { (void)pd; return FALSE; }
HWND  API FindText(LPVOID fr)         { (void)fr; return 0; }
HWND  API ReplaceText(LPVOID fr)      { (void)fr; return 0; }
DWORD API CommDlgExtendedError(void)  { return 0; }

int API GetFileTitle(LPCSTR f, LPSTR t, WORD n)
{
    int i, s = 0;
    for (i = 0; f[i]; i++) if (f[i] == '\\' || f[i] == ':' || f[i] == '/') s = i + 1;
    if (t && n) w_strncpy(t, f + s, n);
    return 0;
}

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd)
{
    (void)ds; (void)heap; (void)cmd;
    g_hinst = hi;
    return 1;
}
