/* w16.h - wspolne definicje malych DLL systemowych (SHELL, KEYBOARD, TOOLHELP,
 * DDEML, VER, MMSYSTEM, COMMDLG).
 *
 * Model -ms -zu: DS = DGROUP biblioteki, SS = stos apki (wskazniki do zmiennych
 * lokalnych musza byc far). Importy KERNEL/USER/GDI po ordynalach daje windows.lib
 * Watcoma (system windows_dll); prywatne importy USER - w dlls/dll_imp.lnk.
 */
#ifndef W16_H
#define W16_H

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef long           LONG;
typedef WORD           HANDLE;
typedef WORD           HWND;
typedef WORD           HICON;
typedef WORD           HINSTANCE;
typedef DWORD          HKEY;
typedef WORD           HMENU;
typedef WORD           HBRUSH;
typedef WORD           HCURSOR;
typedef WORD           HDC;
typedef WORD           HFONT;
typedef WORD           HGDIOBJ;
typedef DWORD          COLORREF;
typedef LONG           LRESULT;
typedef WORD           WPARAM;
typedef LONG           LPARAM;
typedef char __far    *LPSTR;
typedef const char __far *LPCSTR;
typedef void __far    *LPVOID;
typedef DWORD __far   *LPDWORD;
typedef LONG __far    *LPLONG;
typedef void (__far __pascal *FARPROC)();

#define API     __far __pascal
#define NULL    0
#define TRUE    1
#define FALSE   0

typedef struct {
    int left, top, right, bottom;
} RECT;
typedef RECT __far *LPRECT;

typedef struct {
    int x, y;
} POINT;

typedef struct {
    HWND    hwnd;
    WORD    message;
    WORD    wParam;
    LONG    lParam;
    DWORD   time;
    POINT   pt;
} MSG;
typedef MSG __far *LPMSG;

typedef LRESULT (API *WNDPROC)(HWND, WORD, WORD, LONG);

typedef struct {
    WORD        style;
    WNDPROC     lpfnWndProc;
    int         cbClsExtra;
    int         cbWndExtra;
    HINSTANCE   hInstance;
    HICON       hIcon;
    HCURSOR     hCursor;
    HBRUSH      hbrBackground;
    LPCSTR      lpszMenuName;
    LPCSTR      lpszClassName;
} WNDCLASS;

typedef struct tagOFN {
    DWORD        lStructSize;
    HWND         hwndOwner;
    HINSTANCE    hInstance;
    LPCSTR       lpstrFilter;
    LPSTR        lpstrCustomFilter;
    DWORD        nMaxCustFilter;
    DWORD        nFilterIndex;
    LPSTR        lpstrFile;
    DWORD        nMaxFile;
    LPSTR        lpstrFileTitle;
    DWORD        nMaxFileTitle;
    LPCSTR       lpstrInitialDir;
    LPCSTR       lpstrTitle;
    DWORD        Flags;
    WORD         nFileOffset;
    WORD         nFileExtension;
    LPCSTR       lpstrDefExt;
    LPARAM       lCustData;
    FARPROC      lpfnHook;
    LPCSTR       lpTemplateName;
} OPENFILENAME;
typedef OPENFILENAME __far *LPOPENFILENAME;

#pragma pack(push, 1)
typedef struct {
    int  lfHeight;
    int  lfWidth;
    int  lfEscapement;
    int  lfOrientation;
    int  lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONT;
typedef LOGFONT __far *LPLOGFONT;

typedef struct {
    DWORD        lStructSize;
    HWND         hwndOwner;
    HDC          hDC;
    LPLOGFONT    lpLogFont;
    int          iPointSize;
    DWORD        Flags;
    COLORREF     rgbColors;
    LPARAM       lCustData;
    FARPROC      lpfnHook;
    LPCSTR       lpTemplateName;
    HINSTANCE    hInstance;
    LPSTR        lpszStyle;
    WORD         nFontType;
    int          nSizeMin;
    int          nSizeMax;
} CHOOSEFONT;
typedef CHOOSEFONT __far *LPCHOOSEFONT;

typedef struct {
    DWORD        lStructSize;
    HWND         hwndOwner;
    HINSTANCE    hInstance;
    COLORREF     rgbResult;
    COLORREF __far *lpCustColors;
    DWORD        Flags;
    LPARAM       lCustData;
    FARPROC      lpfnHook;
    LPCSTR       lpTemplateName;
} CHOOSECOLOR;
typedef CHOOSECOLOR __far *LPCHOOSECOLOR;
#pragma pack(pop)

#define CF_SCREENFONTS          0x00000001L
#define CF_PRINTERFONTS         0x00000002L
#define CF_BOTH                 0x00000003L
#define CF_SHOWHELP             0x00000004L
#define CF_ENABLEHOOK           0x00000008L
#define CF_ENABLETEMPLATE       0x00000010L
#define CF_ENABLETEMPLATEHANDLE 0x00000020L
#define CF_INITTOLOGFONTSTRUCT  0x00000040L
#define CF_USESTYLE             0x00000080L
#define CF_EFFECTS              0x00000100L

#define SCREEN_FONTTYPE         0x2000
#define BOLD_FONTTYPE           0x0100
#define ITALIC_FONTTYPE         0x0200
#define REGULAR_FONTTYPE        0x0400

#define CC_RGBINIT              0x00000001L

#define OFN_READONLY                    0x00000001L
#define OFN_OVERWRITEPROMPT             0x00000002L
#define OFN_HIDEREADONLY                0x00000004L
#define OFN_NOCHANGEDIR                 0x00000008L
#define OFN_SHOWHELP                    0x00000010L
#define OFN_ENABLEHOOK                  0x00000020L
#define OFN_ENABLETEMPLATE              0x00000040L
#define OFN_ENABLETEMPLATEHANDLE        0x00000080L
#define OFN_NOVALIDATE                  0x00000100L
#define OFN_ALLOWMULTISELECT            0x00000200L
#define OFN_EXTENSIONDIFFERENT          0x00000400L
#define OFN_PATHMUSTEXIST               0x00000800L
#define OFN_FILEMUSTEXIST               0x00001000L
#define OFN_CREATEPROMPT                0x00002000L

#define WM_CREATE                       0x0001
#define WM_DESTROY                      0x0002
#define WM_MOVE                         0x0003
#define WM_SIZE                         0x0005
#define WM_SETFOCUS                     0x0007
#define WM_KILLFOCUS                    0x0008
#define WM_SETTEXT                      0x000C
#define WM_GETTEXT                      0x000D
#define WM_GETTEXTLENGTH                0x000E
#define WM_CLOSE                        0x0010
#define WM_COMMAND                      0x0111
#define WM_SETFONT                      0x0030
#define WM_USER                         0x0400

#define EM_SETSEL                       (WM_USER + 1)
#define LB_ADDSTRING                    (WM_USER + 1)
#define LB_RESETCONTENT                 (WM_USER + 5)
#define LB_SETCURSEL                    (WM_USER + 7)
#define LB_GETCURSEL                    (WM_USER + 9)
#define LB_GETTEXT                      (WM_USER + 10)
#define LB_GETCOUNT                     (WM_USER + 12)

#define CB_ADDSTRING                    (WM_USER + 3)
#define CB_GETLBTEXT                    (WM_USER + 8)
#define CB_RESETCONTENT                 (WM_USER + 11)
#define CB_GETCURSEL                    (WM_USER + 12)
#define CB_SETCURSEL                    (WM_USER + 14)

#define LBN_SELCHANGE                   1
#define LBN_DBLCLK                      2
#define CBN_SELCHANGE                   1

#define WS_OVERLAPPED                   0x00000000L
#define WS_POPUP                        0x80000000L
#define WS_CHILD                        0x40000000L
#define WS_VISIBLE                      0x10000000L
#define WS_CAPTION                      0x00C00000L
#define WS_BORDER                       0x00800000L
#define WS_SYSMENU                      0x00080000L
#define WS_VSCROLL                      0x00200000L
#define WS_TABSTOP                      0x00010000L

#define ES_AUTOHSCROLL                  0x0080
#define LBS_NOTIFY                      0x0001
#define LBS_SORT                        0x0002
#define CBS_DROPDOWNLIST                0x0003
#define BS_PUSHBUTTON                   0x0000
#define BS_DEFPUSHBUTTON                0x0001
#define BS_GROUPBOX                     0x0007
#define SS_LEFT                         0x0000
#define SS_CENTER                       0x0001

#define IDOK                            1
#define IDCANCEL                        2
#define IDYES                           6
#define IDNO                            7

#define MB_OK                           0x0000
#define MB_YESNO                        0x0004
#define MB_ICONEXCLAMATION              0x0030

#define COLOR_BTNFACE                   15
#define IDC_ARROW                       ((LPCSTR)32512)
#define SYSTEM_FONT                     13

#define stc1                            0x0440
#define stc2                            0x0441
#define stc3                            0x0442
#define stc4                            0x0443
#define stc5                            0x0444
#define lst1                            0x0460
#define lst2                            0x0461
#define cmb1                            0x0470
#define cmb2                            0x0471
#define edt1                            0x0480

/* Uwaga (bledy Watcoma): (DWORD)(seg)<<16 | (WORD)off rzutowane na far ptr przy seg=0
 * daje DS:off (jak konwersja near->far), a wersja z '+' generuje 'lea' + 'adc' z
 * przypadkowym CF. Obie polowy jawnie jako (DWORD)(WORD) i '|' - dziala. */
#define MK_FP(seg, off) ((void __far *)(((DWORD)(WORD)(seg) << 16) | (DWORD)(WORD)(off)))
#define LOWORD(l)       ((WORD)(DWORD)(l))
#define HIWORD(l)       ((WORD)((DWORD)(l) >> 16))
#define MAKELONG(a, b)  ((DWORD)(WORD)(a) | ((DWORD)(WORD)(b) << 16))

/* ---- KERNEL ---- */
WORD   API GlobalAlloc(WORD, DWORD);
WORD   API GlobalFree(WORD);
LPVOID API GlobalLock(WORD);
BOOL   API GlobalUnlock(WORD);
void   API OutputDebugString(LPCSTR);
int    API GetProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int);
int    API _lopen(LPCSTR, int);
int    API _lcreat(LPCSTR, int);
int    API _lclose(int);
WORD   API _lread(int, LPVOID, WORD);
WORD   API _lwrite(int, LPCSTR, WORD);
LONG   API _llseek(int, LONG, int);
int    API lstrlen(LPCSTR);
LPSTR  API lstrcpy(LPSTR, LPCSTR);
int    API lstrcmpi(LPCSTR, LPCSTR);
WORD   API WinExec(LPCSTR, WORD);
WORD   API GetWindowsDirectory(LPSTR, WORD);
WORD   API GetDriveType(int);
int    API GetModuleFileName(WORD, LPSTR, int);
DWORD  API GetTickCount(void);

/* ---- USER ---- */
HWND    API CreateWindow(LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL    API DestroyWindow(HWND);
BOOL    API ShowWindow(HWND, int);
BOOL    API EnableWindow(HWND, BOOL);
HWND    API SetActiveWindow(HWND);
HWND    API SetFocus(HWND);
BOOL    API IsWindow(HWND);
BOOL    API RegisterClass(const WNDCLASS __far *);
LRESULT API DefWindowProc(HWND, WORD, WORD, LONG);
LRESULT API SendMessage(HWND, WORD, WORD, LONG);
BOOL    API PostMessage(HWND, WORD, WORD, LONG);
BOOL    API GetMessage(LPMSG, HWND, WORD, WORD);
BOOL    API TranslateMessage(const MSG __far *);
LRESULT API DispatchMessage(const MSG __far *);
BOOL    API IsDialogMessage(HWND, LPMSG);
void    API GetWindowRect(HWND, LPRECT);
void    API GetClientRect(HWND, LPRECT);
BOOL    API SetWindowPos(HWND, HWND, int, int, int, int, WORD);
void    API SetWindowText(HWND, LPCSTR);
int     API GetWindowText(HWND, LPSTR, int);
int     API GetWindowTextLength(HWND);
HWND    API GetDlgItem(HWND, int);
void    API SetDlgItemText(HWND, int, LPCSTR);
int     API GetDlgItemText(HWND, int, LPSTR, int);
LRESULT API SendDlgItemMessage(HWND, int, WORD, WORD, LONG);
int     API MessageBox(HWND, LPCSTR, LPCSTR, WORD);
HCURSOR API LoadCursor(HINSTANCE, LPCSTR);
void    API InvalidateRect(HWND, LPRECT, BOOL);

/* ---- GDI ---- */
HANDLE  API GetStockObject(int);
HFONT   API CreateFontIndirect(const LOGFONT __far *);
BOOL    API DeleteObject(HGDIOBJ);
HGDIOBJ API SelectObject(HDC, HGDIOBJ);

/* ---- USER (prywatne) ---- */
HICON  API UserIconFromDIB(BYTE __far *bi, DWORD size);

/* ---- Pomocnicze (w16lib.c) ---- */
void   dbg(LPCSTR s);
void   dbg_hex(DWORD v, int digits);
int    w_strlen(LPCSTR s);
void   w_strcpy(LPSTR d, LPCSTR s);
void   w_strncpy(LPSTR d, LPCSTR s, int n);
int    w_stricmp(LPCSTR a, LPCSTR b);
int    w_strnicmp(LPCSTR a, LPCSTR b, int n);
void   w_memcpy(LPVOID d, const void __far *s, WORD n);
void   w_memset(LPVOID d, BYTE v, WORD n);

#endif /* W16_H */
