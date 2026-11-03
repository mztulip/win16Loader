/* ctltest.c - test kontrolek standardowych USER (BUTTON, STATIC, EDIT, LISTBOX,
 * COMBOBOX, SCROLLBAR) w oknie dialogowym z zasobu.
 *
 * Testy automatyczne w WM_INITDIALOG (komunikaty kontrolek, *DlgItem*),
 * potem okno zostaje do recznego sprawdzenia; zmiany stanu (BN_CLICKED,
 * LBN_SELCHANGE, CBN_SELCHANGE, WM_HSCROLL) ida do logu i paska stanu.
 * Esc / Anuluj / OK zamyka.
 */
#include <windows.h>
#include "testlog.h"
#include "ctltest.h"

static void status(HWND dlg, const char *fmt, int a)
{
    char b[80];
    wsprintf(b, fmt, a);
    SetDlgItemText(dlg, IDC_STATUS, b);
    lstrcat(b, "\r\n");
    OutputDebugString("CTLTEST: ");
    OutputDebugString(b);
}

static void run_tests(HWND dlg)
{
    char buf[100];
    HWND h;
    int i;
    BOOL ok;
    static const char *fruits[] = { "Sliwka", "Jablko", "Gruszka", "Banan", "Wisnia" };

    /* --- BUTTON --- */
    CHECK_INT(IsDlgButtonChecked(dlg, IDC_CHECK), 0, "checkbox poczatkowo");
    CheckDlgButton(dlg, IDC_CHECK, 1);
    CHECK_INT(IsDlgButtonChecked(dlg, IDC_CHECK), 1, "CheckDlgButton");
    CheckRadioButton(dlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO2);
    CHECK_INT(IsDlgButtonChecked(dlg, IDC_RADIO2), 1, "CheckRadioButton B");
    CHECK_INT(IsDlgButtonChecked(dlg, IDC_RADIO1), 0, "CheckRadioButton A wylaczony");
    SendDlgItemMessage(dlg, IDC_3STATE, BM_SETCHECK, 2, 0);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_3STATE, BM_GETCHECK, 0, 0), 2, "3-state = 2");
    CHECK_INT(LOWORD(SendMessage(dlg, DM_GETDEFID, 0, 0)), IDOK, "domyslny przycisk = IDOK");

    /* --- EDIT --- */
    SetDlgItemText(dlg, IDC_EDIT, "Ala ma kota");
    GetDlgItemText(dlg, IDC_EDIT, buf, sizeof(buf));
    CHECK_STR(buf, "Ala ma kota", "Set/GetDlgItemText");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_EDIT, WM_GETTEXTLENGTH, 0, 0), 11, "WM_GETTEXTLENGTH");
    SendDlgItemMessage(dlg, IDC_EDIT, EM_SETSEL, 0, MAKELONG(4, 6));
    CHECK_INT(SendDlgItemMessage(dlg, IDC_EDIT, EM_GETSEL, 0, 0), MAKELONG(4, 6), "EM_SETSEL/EM_GETSEL");
    SendDlgItemMessage(dlg, IDC_EDIT, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"nie ma");
    GetDlgItemText(dlg, IDC_EDIT, buf, sizeof(buf));
    CHECK_STR(buf, "Ala nie ma kota", "EM_REPLACESEL");
    SendDlgItemMessage(dlg, IDC_EDIT, EM_SETMODIFY, FALSE, 0);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_EDIT, EM_GETMODIFY, 0, 0), 0, "EM_SETMODIFY");
    SendDlgItemMessage(dlg, IDC_EDIT, EM_LIMITTEXT, 5, 0);
    SetDlgItemText(dlg, IDC_MULTI, "linia 1\r\nlinia 2\r\nlinia 3");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_MULTI, EM_GETLINECOUNT, 0, 0), 3, "EM_GETLINECOUNT");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_MULTI, EM_LINEINDEX, 1, 0), 9, "EM_LINEINDEX(1)");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_MULTI, EM_LINELENGTH, 9, 0), 7, "EM_LINELENGTH");
    *(WORD *)buf = sizeof(buf);
    i = (int)SendDlgItemMessage(dlg, IDC_MULTI, EM_GETLINE, 2, (LPARAM)(LPSTR)buf);
    buf[i] = 0;
    CHECK_STR(buf, "linia 3", "EM_GETLINE(2)");
    SetDlgItemInt(dlg, IDC_NUMBER, -1234, TRUE);
    CHECK_INT((int)GetDlgItemInt(dlg, IDC_NUMBER, &ok, TRUE), -1234, "Set/GetDlgItemInt");
    CHECK(ok, "GetDlgItemInt ok");

    /* --- LISTBOX (LBS_SORT) --- */
    for (i = 0; i < 5; i++) SendDlgItemMessage(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)fruits[i]);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_GETCOUNT, 0, 0), 5, "LB_GETCOUNT");
    SendDlgItemMessage(dlg, IDC_LIST, LB_GETTEXT, 0, (LPARAM)(LPSTR)buf);
    CHECK_STR(buf, "Banan", "LB_SORT: pierwszy");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_FINDSTRING, (WPARAM)-1, (LPARAM)(LPSTR)"Gr"), 1, "LB_FINDSTRING prefiks");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)"wisnia"), 4, "LB_FINDSTRINGEXACT");
    SendDlgItemMessage(dlg, IDC_LIST, LB_SETCURSEL, 2, 0);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_GETCURSEL, 0, 0), 2, "LB_SETCURSEL");
    SendDlgItemMessage(dlg, IDC_LIST, LB_SETITEMDATA, 2, 0x12345678L);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_GETITEMDATA, 2, 0), 0x12345678L, "LB_SET/GETITEMDATA");
    SendDlgItemMessage(dlg, IDC_LIST, LB_DELETESTRING, 0, 0);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_GETCOUNT, 0, 0), 4, "LB_DELETESTRING");
    CHECK_INT(SendDlgItemMessage(dlg, IDC_LIST, LB_GETTEXTLEN, 0, 0), 7, "LB_GETTEXTLEN");

    /* --- COMBOBOX --- */
    for (i = 0; i < 5; i++) SendDlgItemMessage(dlg, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)(LPSTR)fruits[i]);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_COMBO, CB_GETCOUNT, 0, 0), 5, "CB_GETCOUNT");
    SendDlgItemMessage(dlg, IDC_COMBO, CB_SETCURSEL, 3, 0);
    CHECK_INT(SendDlgItemMessage(dlg, IDC_COMBO, CB_GETCURSEL, 0, 0), 3, "CB_SETCURSEL");
    SendDlgItemMessage(dlg, IDC_COMBO, CB_GETLBTEXT, 3, (LPARAM)(LPSTR)buf);
    CHECK_STR(buf, "Banan", "CB_GETLBTEXT");
    GetDlgItemText(dlg, IDC_COMBO, buf, sizeof(buf));
    CHECK_STR(buf, "Banan", "tekst combo = wybrany element");
    SendDlgItemMessage(dlg, IDC_COMBOEDIT, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"pierwszy");
    SetDlgItemText(dlg, IDC_COMBOEDIT, "wpisany");
    GetDlgItemText(dlg, IDC_COMBOEDIT, buf, sizeof(buf));
    CHECK_STR(buf, "wpisany", "CBS_DROPDOWN: pole edycji");

    /* --- SCROLLBAR (SB_CTL) --- */
    h = GetDlgItem(dlg, IDC_SCROLL);
    SetScrollRange(h, SB_CTL, 0, 50, FALSE);
    SetScrollPos(h, SB_CTL, 20, TRUE);
    CHECK_INT(GetScrollPos(h, SB_CTL), 20, "SetScrollPos SB_CTL");
    SetScrollPos(h, SB_CTL, 99, TRUE);
    CHECK_INT(GetScrollPos(h, SB_CTL), 50, "SetScrollPos ograniczony do max");

    /* --- STATIC / ogolne --- */
    CHECK(SendDlgItemMessage(dlg, IDC_APPICON, STM_GETICON, 0, 0) != 0, "SS_ICON zaladowana");
    CHECK_INT(GetDlgCtrlID(GetDlgItem(dlg, IDC_LIST)), IDC_LIST, "GetDlgCtrlID");
    CHECK(GetNextDlgTabItem(dlg, GetDlgItem(dlg, IDC_CHECK), FALSE) != 0, "GetNextDlgTabItem");
    EnableWindow(GetDlgItem(dlg, IDC_3STATE), FALSE);
    CHECK(!IsWindowEnabled(GetDlgItem(dlg, IDC_3STATE)), "EnableWindow(FALSE)");
    EnableWindow(GetDlgItem(dlg, IDC_3STATE), TRUE);

    t_done();
}

BOOL FAR PASCAL __export MainDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        run_tests(dlg);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: case IDCANCEL:
            EndDialog(dlg, wp);
            return TRUE;
        case IDC_CHECK: case IDC_RADIO1: case IDC_RADIO2: case IDC_3STATE:
            if (HIWORD(lp) == BN_CLICKED) status(dlg, "klik przycisku %d", wp);
            return TRUE;
        case IDC_LIST:
            if (HIWORD(lp) == LBN_SELCHANGE) status(dlg, "lista: wybrano %d", (int)SendMessage((HWND)LOWORD(lp), LB_GETCURSEL, 0, 0));
            return TRUE;
        case IDC_COMBO:
            if (HIWORD(lp) == CBN_SELCHANGE) status(dlg, "combo: wybrano %d", (int)SendMessage((HWND)LOWORD(lp), CB_GETCURSEL, 0, 0));
            return TRUE;
        }
        break;
    case WM_HSCROLL: {
        HWND sb = (HWND)HIWORD(lp);
        int p = GetScrollPos(sb, SB_CTL);
        switch (wp) {
        case SB_LINELEFT: p--; break;
        case SB_LINERIGHT: p++; break;
        case SB_PAGELEFT: p -= 10; break;
        case SB_PAGERIGHT: p += 10; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: p = LOWORD(lp); break;
        default: return TRUE;
        }
        SetScrollPos(sb, SB_CTL, p, TRUE);
        status(dlg, "suwak: %d", GetScrollPos(sb, SB_CTL));
        return TRUE;
    }
    }
    return FALSE;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    FARPROC p;
    (void)prev; (void)cmd; (void)show;
    t_app = "CTLTEST";
    p = MakeProcInstance((FARPROC)MainDlgProc, hi);
    DialogBox(hi, MAKEINTRESOURCE(IDD_MAIN), 0, (DLGPROC)p);
    FreeProcInstance(p);
    return 0;
}
