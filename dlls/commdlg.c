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

BOOL  API ChooseColor(LPVOID cc)      { (void)cc; return FALSE; }
BOOL  API ChooseFont(LPVOID cf)       { (void)cf; return FALSE; }
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
