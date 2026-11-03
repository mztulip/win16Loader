/* commdlg.c - COMMDLG.DLL: wspolne okna dialogowe - na razie zawsze "Anuluj" */
#include "w16.h"

BOOL  API GetOpenFileName(LPVOID ofn) { (void)ofn; dbg("COMMDLG: GetOpenFileName (anulowano)\n"); return FALSE; }
BOOL  API GetSaveFileName(LPVOID ofn) { (void)ofn; dbg("COMMDLG: GetSaveFileName (anulowano)\n"); return FALSE; }
BOOL  API ChooseColor(LPVOID cc)      { (void)cc; return FALSE; }
BOOL  API ChooseFont(LPVOID cf)       { (void)cf; return FALSE; }
BOOL  API PrintDlg(LPVOID pd)         { (void)pd; return FALSE; }
HWND  API FindText(LPVOID fr)         { (void)fr; return 0; }
HWND  API ReplaceText(LPVOID fr)      { (void)fr; return 0; }
DWORD API CommDlgExtendedError(void)  { return 0; }
int   API GetFileTitle(LPCSTR f, LPSTR t, WORD n)
{
    int i, s = 0;
    for (i = 0; f[i]; i++) if (f[i] == '\\' || f[i] == ':' || f[i] == '/') s = i + 1;
    if (t && n) w_strncpy(t, f + s, n);
    return 0;
}

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
