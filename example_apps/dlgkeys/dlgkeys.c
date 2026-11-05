/* dlgkeys.c - klawisze i zaznaczenie w dialogu:
 * WM_GETDLGCODE kontrolek (VCL Delphi wysyla Enter do przycisku domyslnego tylko, gdy kontrolka
 * nie ma DLGC_WANTALLKEYS: okno "Run Program" Calmiry nie reagowalo na Enter w combo),
 * Enter i Esc w IsDialogMessage, zaznaczanie calego tekstu przez menedzer dialogu (DLGC_HASSETSEL)
 * i przez combo przy pierwszym fokusie.
 */
#include <windows.h>
#include "testlog.h"

#define WANTED (DLGC_WANTARROWS | DLGC_WANTCHARS)

static int g_ok, g_cancel;

BOOL FAR PASCAL _export DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)h; (void)lp;
    if (m == WM_INITDIALOG) return TRUE;
    if (m == WM_COMMAND) {
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

    DestroyWindow(d);
    t_done();
    return 0;
}
