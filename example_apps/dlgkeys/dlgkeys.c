/* dlgkeys.c - klawisze i zaznaczenie w dialogu:
 * WM_GETDLGCODE kontrolek (VCL Delphi wysyla Enter do przycisku domyslnego tylko, gdy kontrolka
 * nie ma DLGC_WANTALLKEYS: okno "Run Program" Calmiry nie reagowalo na Enter w combo),
 * Enter i Esc w IsDialogMessage, zaznaczanie calego tekstu przez menedzer dialogu (DLGC_HASSETSEL)
 * i przez combo przy pierwszym fokusie.
 */
#include <windows.h>
#include "testlog.h"

#define WANTED (DLGC_WANTARROWS | DLGC_WANTCHARS)

static int g_ok, g_cancel, g_dropdown;

BOOL FAR PASCAL _export DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)h; (void)lp;
    if (m == WM_INITDIALOG) return TRUE;
    if (m == WM_COMMAND) {
        if (wp == 104 && HIWORD(lp) == CBN_DROPDOWN) {      /* jak VCL (AdjustDropDown): zamyka i powieksza okno */
            RECT r;
            HWND dd = (HWND)LOWORD(lp);
            g_dropdown++;
            SendMessage(dd, CB_SHOWDROPDOWN, FALSE, 0);
            GetWindowRect(dd, &r);
            SetWindowPos(dd, 0, 0, 0, r.right - r.left, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (wp == IDOK) g_ok++;
        if (wp == IDCANCEL) g_cancel++;
        return TRUE;
    }
    return FALSE;
}

static void key(HWND d, HWND h, WPARAM vk)
{
    MSG m;
    memset(&m, 0, sizeof(m));
    m.hwnd = h;
    m.message = WM_KEYDOWN;
    m.wParam = vk;
    m.lParam = 1;
    IsDialogMessage(d, &m);
}

static LONG code(HWND h) { return SendMessage(h, WM_GETDLGCODE, 0, 0); }

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int sh)
{
    HWND d, edit, combo, cedit, list, multi, ok, cancel;
    int i;
    t_app = "DLGKEYS";
    (void)hp; (void)cl; (void)sh;
    d = CreateDialog(hi, MAKEINTRESOURCE(1), NULL, (DLGPROC)MakeProcInstance((FARPROC)DlgProc, hi));
    edit = GetDlgItem(d, 100); combo = GetDlgItem(d, 101); list = GetDlgItem(d, 102);
    multi = GetDlgItem(d, 103); ok = GetDlgItem(d, IDOK); cancel = GetDlgItem(d, IDCANCEL);

    /* pierwsza kontrolka dialogu: fokus i caly tekst zaznaczony */
    CHECK(GetFocus() == edit, "fokus startowy");
    CHECK_INT(SendMessage(edit, EM_GETSEL, 0, 0), MAKELONG(0, 4), "fokus startowy zaznacza tekst");

    /* kody */
    CHECK_INT(code(edit) & (WANTED | DLGC_HASSETSEL | DLGC_WANTALLKEYS), WANTED | DLGC_HASSETSEL, "kod EDIT");
    CHECK(code(multi) & DLGC_WANTALLKEYS, "kod EDIT wielowierszowego: wszystkie klawisze");
    CHECK_INT(code(combo) & (WANTED | DLGC_WANTALLKEYS), WANTED, "kod COMBOBOX");
    CHECK_INT(code(list) & (WANTED | DLGC_WANTALLKEYS), WANTED, "kod LISTBOX");
    SendMessage(combo, CB_SHOWDROPDOWN, TRUE, 0);
    CHECK(code(combo) & DLGC_WANTALLKEYS, "kod COMBOBOX z rozwinieta lista");
    SendMessage(combo, CB_SHOWDROPDOWN, FALSE, 0);

    /* Enter i Esc */
    key(d, combo, VK_RETURN);
    CHECK_INT(g_ok, 1, "Enter na combo: przycisk domyslny");
    key(d, list, VK_RETURN);
    CHECK_INT(g_ok, 2, "Enter na liscie: przycisk domyslny");
    key(d, edit, VK_RETURN);
    CHECK_INT(g_ok, 3, "Enter na polu edycji: przycisk domyslny");
    key(d, multi, VK_RETURN);
    CHECK_INT(g_ok, 3, "Enter w polu wielowierszowym zostaje w polu");
    key(d, combo, VK_ESCAPE);
    CHECK_INT(g_cancel, 1, "Esc na combo: anuluj");

    /* combo: tylko pierwszy fokus zaznacza caly tekst */
    SetFocus(ok);
    SetWindowText(combo, "program.exe");
    SetFocus(combo);
    cedit = GetFocus();
    CHECK(IsChild(combo, cedit), "fokus combo trafia do pola edycji");
    CHECK_INT(SendMessage(combo, CB_GETEDITSEL, 0, 0), MAKELONG(0, 11), "pierwszy fokus combo zaznacza tekst");
    SendMessage(combo, CB_SETEDITSEL, 0, MAKELONG(2, 2));
    SetFocus(ok);
    SetFocus(combo);
    CHECK_INT(SendMessage(combo, CB_GETEDITSEL, 0, 0), MAKELONG(2, 2), "kolejny fokus combo nie rusza zaznaczenia");

    /* wybor z listy przy fokusie zaznacza tekst, bez fokusu nie */
    SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"abc");
    SendMessage(combo, CB_SETCURSEL, 0, 0);
    CHECK_INT(SendMessage(combo, CB_GETEDITSEL, 0, 0), MAKELONG(0, 3), "CB_SETCURSEL przy fokusie zaznacza tekst");
    SetFocus(ok);
    SendMessage(combo, CB_SETCURSEL, 0, 0);
    CHECK_INT(SendMessage(combo, CB_GETEDITSEL, 0, 0), MAKELONG(0, 0), "CB_SETCURSEL bez fokusu nie zaznacza");

    /* EDIT: menedzer dialogu zaznacza, samo SetFocus nie */
    SendMessage(edit, EM_SETSEL, 0, MAKELONG(1, 1));
    SetFocus(cancel);
    SetFocus(edit);
    CHECK_INT(SendMessage(edit, EM_GETSEL, 0, 0), MAKELONG(1, 1), "SetFocus nie rusza zaznaczenia");
    SetFocus(cancel);
    SendMessage(d, WM_NEXTDLGCTL, (WPARAM)edit, TRUE);
    CHECK(GetFocus() == edit, "WM_NEXTDLGCTL ustawia fokus");
    CHECK_INT(SendMessage(edit, EM_GETSEL, 0, 0), MAKELONG(0, 4), "WM_NEXTDLGCTL zaznacza tekst");
    SendMessage(edit, EM_SETSEL, 0, MAKELONG(1, 1));
    SetFocus(ok);
    for (i = 0; i < 10 && GetFocus() != edit; i++) key(d, GetFocus(), VK_TAB);
    CHECK(GetFocus() == edit, "Tab dochodzi do pola edycji");
    CHECK_INT(SendMessage(edit, EM_GETSEL, 0, 0), MAKELONG(0, 4), "Tab zaznacza tekst");

    /* CBS_DROPDOWNLIST jak w Delphi: VCL zmienia wysokosc okna combo (wysokosc z lista) przy
     * CBN_DROPDOWN; okno zostaje wysokosci pola, a lista jest osobnym oknem pod nim */
    {
        HWND dd = GetDlgItem(d, 104), lb = 0;
        RECT r0, r1, rl;
        int k;
        for (k = 0; k < 6; k++) {
            char nm[16];
            wsprintf(nm, "pozycja %d", k);
            SendMessage(dd, CB_SETITEMDATA, (WPARAM)SendMessage(dd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)nm), 1000L + k);
        }
        CHECK_INT(SendMessage(dd, CB_GETCOUNT, 0, 0), 6, "DROPDOWNLIST: 6 pozycji");
        CHECK_INT(SendMessage(dd, CB_GETITEMDATA, 3, 0), 1003, "DROPDOWNLIST: CB_GETITEMDATA");
        GetWindowRect(dd, &r0);
        SetWindowPos(dd, 0, 0, 0, r0.right - r0.left, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(dd, &r1);
        CHECK_INT(r1.bottom - r1.top, r0.bottom - r0.top, "SetWindowPos z wysokoscia listy nie powieksza combo");
        SendMessage(dd, CB_SHOWDROPDOWN, TRUE, 0);
        CHECK(SendMessage(dd, CB_GETDROPPEDSTATE, 0, 0) != 0, "CB_SHOWDROPDOWN rozwija");
        lb = (HWND)GetWindowWord(dd, 2);                 /* dodatkowe bajty COMBOBOX: okno listy (wnetrze USER.EXE) */
        CHECK(lb != 0, "okno listy istnieje");
        if (lb) {
            GetWindowRect(lb, &rl);
            CHECK(IsWindowVisible(lb), "lista jest widoczna");
            CHECK_INT(rl.top, r0.bottom, "lista zaczyna sie tuz pod combo");
            CHECK(rl.bottom - rl.top >= 6 * 8, "lista ma wysokosc na pozycje");
        }
        SendMessage(dd, CB_SHOWDROPDOWN, FALSE, 0);

        /* klik myszka: VCL odpowiada na CBN_DROPDOWN zamknieciem i zmiana wysokosci, a po puszczeniu
         * przycisku wysyla CB_SHOWDROPDOWN(0) (WM_CANCELMODE): lista otwarta tym kliknieciem zostaje */
        g_dropdown = 0;
        SendMessage(dd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(10, 5));
        CHECK_INT(g_dropdown, 1, "klik w combo: jedno CBN_DROPDOWN");
        CHECK(SendMessage(dd, CB_GETDROPPEDSTATE, 0, 0) != 0, "klik w combo: lista otwarta mimo zamkniecia w CBN_DROPDOWN");
        GetWindowRect(dd, &r1);
        CHECK_INT(r1.bottom - r1.top, r0.bottom - r0.top, "klik w combo: okno zostaje wysokosci pola");
        CHECK(IsWindowVisible((HWND)GetWindowWord(dd, 2)), "klik w combo: lista widoczna");
        SendMessage(dd, CB_SHOWDROPDOWN, FALSE, 0);
        CHECK(SendMessage(dd, CB_GETDROPPEDSTATE, 0, 0) != 0, "pierwsze CB_SHOWDROPDOWN(0) po kliknieciu pomijane");
        SendMessage(dd, CB_SHOWDROPDOWN, FALSE, 0);
        CHECK(SendMessage(dd, CB_GETDROPPEDSTATE, 0, 0) == 0, "kolejne CB_SHOWDROPDOWN(0) zamyka liste");
    }

    DestroyWindow(d);
    t_done();
    return 0;
}
