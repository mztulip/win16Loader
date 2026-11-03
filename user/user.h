/* user.h - USER.EXE (Win16 USER): wspolne definicje
 *
 * Model -mm -zu: DS = DGROUP USER, SS = stos apki. Tablica okien w bloku
 * GlobalAlloc (far). Rysowanie wylacznie przez GDI (import), kursor myszy
 * rysuje GDI (prywatne eksporty Gdi*).
 */
#ifndef USER_H
#define USER_H

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef long           LONG;
typedef WORD           HANDLE;
typedef WORD           HWND;
typedef WORD           HDC;
typedef WORD           HMENU;
typedef WORD           HICON;
typedef WORD           HCURSOR;
typedef WORD           HBRUSH;
typedef WORD           HBITMAP;
typedef WORD           HRGN;
typedef WORD           HINSTANCE;
typedef WORD           ATOM;
typedef DWORD          COLORREF;
typedef long           LRESULT;
typedef char __far    *LPSTR;
typedef const char __far *LPCSTR;
typedef void __far    *LPVOID;

#define API     __far __pascal
#define NULL    0
#define TRUE    1
#define FALSE   0

/* Uwaga (bledy Watcoma): (DWORD)(seg)<<16 | (WORD)off rzutowane na far ptr przy seg=0
 * daje DS:off (jak konwersja near->far), a wersja z '+' generuje 'lea' + 'adc' z
 * przypadkowym CF. Obie polowy jawnie jako (DWORD)(WORD) i '|' - dziala. */
#define MK_FP(seg, off) ((void __far *)(((DWORD)(WORD)(seg) << 16) | (DWORD)(WORD)(off)))
#define FP_SEG(p)       ((WORD)((DWORD)(void __far *)(p) >> 16))
#define FP_OFF(p)       ((WORD)(DWORD)(void __far *)(p))
#define LOWORD(l)       ((WORD)(DWORD)(l))
#define HIWORD(l)       ((WORD)((DWORD)(l) >> 16))
#define MAKELONG(a, b) ((DWORD)(WORD)(a) | ((DWORD)(WORD)(b) << 16))
#define MAKELP(s, o)    ((void __far *)MAKELONG(o, s))
#define RGB(r, g, b)    ((COLORREF)(((BYTE)(r) | ((WORD)(BYTE)(g) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x, y; } POINT;
typedef RECT __far *LPRECT;
typedef POINT __far *LPPOINT;

typedef LRESULT (__far __pascal *WNDPROC)(HWND, WORD, WORD, LONG);

typedef struct {
    HWND hwnd;
    WORD message;
    WORD wParam;
    LONG lParam;
    DWORD time;
    POINT pt;
} MSG;
typedef MSG __far *LPMSG;

#define SCR_W 640
#define SCR_H 480
#define SEL_KCB 0x98

/* ---- Okna ---- */
#define MAX_WND        300         /* WND * MAX_WND <= 64KB (jeden segment) */
#define MAX_EXTRA      64          /* cbWndExtra */
#define MAX_TITLE      64

typedef struct {
    WORD    used;
    WORD    cls;                   /* indeks klasy */
    HWND    parent, owner;
    HWND    child, next;           /* pierwszy potomek (gora Z), nastepny rodzenstwa (nizej) */
    DWORD   style, exstyle;
    RECT    wr, cr;                /* okno i obszar klienta - wsp. ekranu */
    WNDPROC proc;
    WORD    hinst;
    WORD    id;                    /* ID kontrolki albo HMENU okna glownego */
    HRGN    upd;                   /* region do odswiezenia (wsp. ekranu), 0 = brak */
    WORD    flags;                 /* WF_* */
    RECT    normal;                /* polozenie przed min/max */
    int     sbpos[2], sbmin[2], sbmax[2];   /* paski przewijania: 0 = poziomy, 1 = pionowy */
    WORD    textlen;
    char    text[MAX_TITLE];
    BYTE    extra[MAX_EXTRA];
} WND;

#define WF_ERASE      0x0001       /* WM_ERASEBKGND przy BeginPaint */
#define WF_NCPAINT    0x0002       /* do narysowania obszar nieklienta */
#define WF_DESTROYING 0x0004
#define WF_ACTIVE     0x0008       /* aktywna ramka (pasek tytulu) */
#define WF_INPAINT    0x0010
#define WF_HASTEXT    0x0020
#define WF_DLG        0x0040       /* okno dialogowe (DefDlgProc) */
#define WF_MAXIMIZED  0x0080
#define WF_MINIMIZED  0x0100
#define WF_CREATED    0x0200       /* WM_CREATE zakonczony */
#define WF_HIDDENPOPUP 0x0800      /* ukryte przez ShowOwnedPopups(FALSE) */
#define WF_NEEDSIZE   0x0400       /* WM_SIZE/WM_MOVE jeszcze nie wyslane (pierwszy ShowWindow) */

extern WORD g_wndmem;
#define WP(h)       wnd_ptr(h)
WND __far *wnd_ptr(HWND h);        /* NULL dla niepoprawnego */
HWND  wnd_handle(WORD idx);
#define HDESKTOP    wnd_handle(0)

/* ---- Klasy ---- */
#define MAX_CLASS 96
typedef struct {
    WORD    used;
    char    name[40];
    WORD    style;
    WNDPROC proc;
    int     clsextra, wndextra;
    WORD    hinst;
    WORD    icon, cursor, brush;
    LPCSTR  menu;
    BOOL    system;                /* wbudowana (BUTTON, EDIT...) */
    BYTE    extra[32];
} CLASS;
extern CLASS g_cls[MAX_CLASS];
int   cls_find(LPCSTR name, WORD hinst);

/* ---- Styl okien ---- */
#define WS_OVERLAPPED   0x00000000UL
#define WS_POPUP        0x80000000UL
#define WS_CHILD        0x40000000UL
#define WS_MINIMIZE     0x20000000UL
#define WS_VISIBLE      0x10000000UL
#define WS_DISABLED     0x08000000UL
#define WS_CLIPSIBLINGS 0x04000000UL
#define WS_CLIPCHILDREN 0x02000000UL
#define WS_MAXIMIZE     0x01000000UL
#define WS_CAPTION      0x00C00000UL
#define WS_BORDER       0x00800000UL
#define WS_DLGFRAME     0x00400000UL
#define WS_VSCROLL      0x00200000UL
#define WS_HSCROLL      0x00100000UL
#define WS_SYSMENU      0x00080000UL
#define WS_THICKFRAME   0x00040000UL
#define WS_MINIMIZEBOX  0x00020000UL
#define WS_MAXIMIZEBOX  0x00010000UL
#define WS_EX_DLGMODALFRAME 0x0001UL
#define WS_EX_TOPMOST       0x0008UL

/* ---- Komunikaty ---- */
#define WM_NULL           0x0000
#define WM_CREATE         0x0001
#define WM_DESTROY        0x0002
#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_ACTIVATE       0x0006
#define WM_SETFOCUS       0x0007
#define WM_KILLFOCUS      0x0008
#define WM_ENABLE         0x000A
#define WM_SETREDRAW      0x000B
#define WM_SETTEXT        0x000C
#define WM_GETTEXT        0x000D
#define WM_GETTEXTLENGTH  0x000E
#define WM_PAINT          0x000F
#define WM_CLOSE          0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT           0x0012
#define WM_QUERYOPEN      0x0013
#define WM_ERASEBKGND     0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_ENDSESSION     0x0016
#define WM_SHOWWINDOW     0x0018
#define WM_CTLCOLOR       0x0019
#define WM_WININICHANGE   0x001A
#define WM_ACTIVATEAPP    0x001C
#define WM_FONTCHANGE     0x001D
#define WM_TIMECHANGE     0x001E
#define WM_CANCELMODE     0x001F
#define WM_SETCURSOR      0x0020
#define WM_MOUSEACTIVATE  0x0021
#define WM_CHILDACTIVATE  0x0022
#define WM_QUEUESYNC      0x0023
#define WM_GETMINMAXINFO  0x0024
#define WM_PAINTICON      0x0026
#define WM_ICONERASEBKGND 0x0027
#define WM_NEXTDLGCTL     0x0028
#define WM_DRAWITEM       0x002B
#define WM_MEASUREITEM    0x002C
#define WM_DELETEITEM     0x002D
#define WM_VKEYTOITEM     0x002E
#define WM_CHARTOITEM     0x002F
#define WM_SETFONT        0x0030
#define WM_GETFONT        0x0031
#define WM_QUERYDRAGICON  0x0037
#define WM_COMPAREITEM    0x0039
#define WM_COMMNOTIFY     0x0044
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED  0x0047
#define WM_NCCREATE       0x0081
#define WM_NCDESTROY      0x0082
#define WM_NCCALCSIZE     0x0083
#define WM_NCHITTEST      0x0084
#define WM_NCPAINT        0x0085
#define WM_NCACTIVATE     0x0086
#define WM_GETDLGCODE     0x0087
#define WM_NCMOUSEMOVE    0x00A0
#define WM_NCLBUTTONDOWN  0x00A1
#define WM_NCLBUTTONUP    0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCRBUTTONDOWN  0x00A4
#define WM_NCRBUTTONUP    0x00A5
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_DEADCHAR       0x0103
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_SYSCHAR        0x0106
#define WM_INITDIALOG     0x0110
#define WM_COMMAND        0x0111
#define WM_SYSCOMMAND     0x0112
#define WM_TIMER          0x0113
#define WM_HSCROLL        0x0114
#define WM_VSCROLL        0x0115
#define WM_INITMENU       0x0116
#define WM_INITMENUPOPUP  0x0117
#define WM_MENUSELECT     0x011F
#define WM_MENUCHAR       0x0120
#define WM_ENTERIDLE      0x0121
#define WM_CTLCOLORFIRST  0x0019
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define WM_PARENTNOTIFY   0x0210
#define WM_ENTERMENULOOP  0x0211
#define WM_EXITMENULOOP   0x0212
#define WM_MDICREATE      0x0220
#define WM_MDIDESTROY     0x0221
#define WM_MDIACTIVATE    0x0222
#define WM_MDIGETACTIVE   0x0229
#define WM_DROPFILES      0x0233
#define WM_CUT            0x0300
#define WM_COPY           0x0301
#define WM_PASTE          0x0302
#define WM_CLEAR          0x0303
#define WM_UNDO           0x0304
#define WM_USER           0x0400

/* CREATESTRUCT (Win16): lpCreateParams 0, hInstance 4, hMenu 6, hwndParent 8,
 * cy 10, cx 12, y 14, x 16, style 18, lpszName 22, lpszClass 26, dwExStyle 30 */
#define CS_NAME_OFF 22

/* Hit test */
#define HTERROR       (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE     0
#define HTCLIENT      1
#define HTCAPTION     2
#define HTSYSMENU     3
#define HTSIZE        4
#define HTMENU        5
#define HTHSCROLL     6
#define HTVSCROLL     7
#define HTREDUCE      8
#define HTZOOM        9
#define HTLEFT        10
#define HTRIGHT       11
#define HTTOP         12
#define HTTOPLEFT     13
#define HTTOPRIGHT    14
#define HTBOTTOM      15
#define HTBOTTOMLEFT  16
#define HTBOTTOMRIGHT 17
#define HTBORDER      18
#define HTCLOSE       20           /* przycisk "X" paska tytulu (wartosc jak w Win95) */

/* SC_* */
#define SC_SIZE       0xF000
#define SC_MOVE       0xF010
#define SC_MINIMIZE   0xF020
#define SC_MAXIMIZE   0xF030
#define SC_NEXTWINDOW 0xF040
#define SC_CLOSE      0xF060
#define SC_KEYMENU    0xF100
#define SC_RESTORE    0xF120
#define SC_TASKLIST   0xF130

/* ---- Kolory systemowe (COLOR_*) ---- */
#define COLOR_SCROLLBAR      0
#define COLOR_BACKGROUND     1
#define COLOR_ACTIVECAPTION  2
#define COLOR_INACTIVECAPTION 3
#define COLOR_MENU           4
#define COLOR_WINDOW         5
#define COLOR_WINDOWFRAME    6
#define COLOR_MENUTEXT       7
#define COLOR_WINDOWTEXT     8
#define COLOR_CAPTIONTEXT    9
#define COLOR_ACTIVEBORDER   10
#define COLOR_INACTIVEBORDER 11
#define COLOR_APPWORKSPACE   12
#define COLOR_HIGHLIGHT      13
#define COLOR_HIGHLIGHTTEXT  14
#define COLOR_BTNFACE        15
#define COLOR_BTNSHADOW      16
#define COLOR_GRAYTEXT       17
#define COLOR_BTNTEXT        18
#define COLOR_INACTIVECAPTIONTEXT 19
#define COLOR_BTNHIGHLIGHT   20
#define NCOLORS 21

/* ---- Import GDI ---- */
HDC   API CreateCompatibleDC(HDC);
BOOL  API DeleteDC(HDC);
WORD  API SelectObject(HDC, WORD);
BOOL  API DeleteObject(WORD);
WORD  API GetStockObject(int);
WORD  API CreateSolidBrush(COLORREF);
WORD  API CreatePen(int, int, COLORREF);
BOOL  API PatBlt(HDC, int, int, int, int, DWORD);
BOOL  API BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL  API StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
COLORREF API SetTextColor(HDC, COLORREF);
COLORREF API SetBkColor(HDC, COLORREF);
int   API SetBkMode(HDC, int);
BOOL  API TextOut(HDC, int, int, LPCSTR, int);
BOOL  API ExtTextOut(HDC, int, int, WORD, LPRECT, LPCSTR, WORD, int __far *);
DWORD API GetTextExtent(HDC, LPCSTR, int);
BOOL  API GetTextMetrics(HDC, LPVOID);
DWORD API MoveTo(HDC, int, int);
BOOL  API LineTo(HDC, int, int);
BOOL  API Rectangle(HDC, int, int, int, int);
int   API SetROP2(HDC, int);
WORD  API CreateRectRgn(int, int, int, int);
int   API CombineRgn(WORD, WORD, WORD, int);
int   API GetRgnBox(WORD, LPRECT);
int   API OffsetRgn(WORD, int, int);
void  API SetRectRgn(WORD, int, int, int, int);
int   API IntersectClipRect(HDC, int, int, int, int);
int   API ExcludeClipRect(HDC, int, int, int, int);
WORD  API CreateBitmap(int, int, WORD, WORD, LPVOID);
WORD  API CreateDIBitmap(HDC, LPVOID, DWORD, LPVOID, LPVOID, WORD);
WORD  API CreateCompatibleBitmap(HDC, int, int);
int   API GetObject(WORD, int, LPVOID);
int   API SetDIBits(HDC, WORD, WORD, WORD, LPVOID, LPVOID, WORD);
WORD  API GDISelectPalette(HDC, WORD, BOOL);
WORD  API GDIRealizePalette(HDC);
WORD  API CreateFont(int, int, int, int, int, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, BYTE, LPCSTR);
/* prywatne GDI */
void  API GdiMoveCursor(int, int);
int   API GdiShowCursor(BOOL);
void  API GdiSetCursor(BYTE __far *, BYTE __far *, int, int, int, int);
void  API GdiCursorDefault(void);
HDC   API GdiCreateWindowDC(int, int, LPRECT, int, WORD);
void  API GdiSetDCVis(HDC, int, int, LPRECT, int);
void  API GdiIntersectVis(HDC, LPRECT, int);
WORD  API GdiDCWindow(HDC);
int   API GdiGetRgnRects(WORD, LPRECT, int);

/* ---- Import KERNEL ---- */
WORD  API GlobalAlloc(WORD, DWORD);
WORD  API GlobalFree(WORD);
LPVOID API GlobalLock(WORD);
DWORD API GlobalSize(WORD);
WORD  API FindResource(WORD, LPCSTR, LPCSTR);
WORD  API LoadResource(WORD, WORD);
LPVOID API LockResource(WORD);
DWORD API SizeofResource(WORD, WORD);
void  API OutputDebugString(LPCSTR);
int   API GetProfileInt(LPCSTR, LPCSTR, int);
void  API FatalAppExit(WORD, LPCSTR);

/* ---- Wewnetrzne (miedzy plikami) ---- */
void   udbg(LPCSTR s);
void   udbg_hex(DWORD v, int d);
void   user_init(void);
LRESULT send_msg(HWND h, WORD m, WORD w, LONG l);
LRESULT call_proc(WNDPROC p, WORD ds, HWND h, WORD m, WORD w, LONG l);
WORD   proc_ds(WNDPROC p, WORD hinst);  /* DS dla procedury okna (USER albo hinst) */
BOOL   post_msg(HWND h, WORD m, WORD w, LONG l);
void   wnd_invalidate(HWND h, RECT __far *r, BOOL erase, BOOL children);
void   wnd_invalidate_screen(RECT __far *r, HWND except);
void   wnd_vis_rects(HWND h, BOOL client, LPRECT out, int __far *n, int max);
void   wnd_calc_client(HWND h);
HWND   wnd_from_point(int x, int y, int __far *hit);
BOOL   wnd_visible(HWND h);
HWND   wnd_toplevel(HWND h);
void   nc_paint(HWND h);
int    nc_hittest(HWND h, int x, int y);
void   nc_buttondown(HWND h, int hit, int x, int y);
COLORREF syscolor(int i);
HBRUSH sysbrush(int i);
int    sysmetric(int i);
void   draw_3d(HDC dc, int l, int t, int r, int b, COLORREF hi, COLORREF sh);
void   fill_color(HDC dc, int l, int t, int r, int b, COLORREF c);
void   input_poll(void);
DWORD  tick(void);
void   set_focus_internal(HWND h);
void   activate_window(HWND h, BOOL mouse);
int    menu_bar_height(HWND h);
void   menu_draw_bar(HWND h, HDC dc);
void   menu_track_bar(HWND h, int x, int y, WORD key);
void   cursor_update(HWND h, int hit);
void   caret_on_paint(HWND h, BOOL before);
int    ustrlen(LPCSTR s);
void   ustrcpy(LPSTR d, LPCSTR s);
void   ustrncpy(LPSTR d, LPCSTR s, int n);
int    ustricmp(LPCSTR a, LPCSTR b);
void   umemset(LPVOID d, BYTE v, WORD n);
void   umemcpy(LPVOID d, const void __far *s, WORD n);
BOOL   rect_and(LPRECT o, const RECT __far *a, const RECT __far *b);
void   ctl_register_classes(void);
void   dce_update(void);
WORD   chrome_font(void);                  /* font ramek okien i menu (Fixedsys) */

/* Kolory wlasnego stylu menu (pasek menu i ramka rozwijanego; tlo rozwijanego = COLOR_MENU) */
#define MENUBAR_BG    RGB(0x88, 0xC0, 0xC8)
#define MENUBAR_LINE  RGB(0x40, 0x80, 0x88)
#define POPUP_FRAME   RGB(0x00, 0x40, 0x48)
#define POPUP_SEP     RGB(0x40, 0x90, 0x98)
void   sb_paint(HDC dc, LPRECT r, BOOL vert, int pos, int mn, int mx);
LRESULT API DefWindowProc(HWND, WORD, WORD, LONG);
LRESULT API SendMessage(HWND, WORD, WORD, LONG);
BOOL   API PostMessage(HWND, WORD, WORD, LONG);
HDC    API GetDC(HWND);
HDC    API GetWindowDC(HWND);
int    API ReleaseDC(HWND, HDC);
BOOL   API IsWindow(HWND);
HWND   API GetParent(HWND);
BOOL   API ShowWindow(HWND, int);
BOOL   API SetWindowPos(HWND, HWND, int, int, int, int, WORD);
void   API InvalidateRect(HWND, LPRECT, BOOL);
void   API UpdateWindow(HWND);
BOOL   API DestroyWindow(HWND);
HWND   API SetFocus(HWND);
HWND   API GetFocus(void);
int    API FillRect(HDC, LPRECT, HBRUSH);
int    API DrawText(HDC, LPCSTR, int, LPRECT, WORD);
HWND   API SetCapture(HWND);
void   API ReleaseCapture(void);
HWND   API GetCapture(void);
DWORD  API GetTickCount(void);
int    API GetSystemMetrics(int);
COLORREF API GetSysColor(int);
LONG   API GetWindowLong(HWND, int);
WORD   API GetWindowWord(HWND, int);
BOOL   API EnableWindow(HWND, BOOL);
BOOL   API IsWindowEnabled(HWND);
BOOL   API IsWindowVisible(HWND);
HWND   API GetDlgItem(HWND, int);
BOOL   API EndDialog(HWND, int);
HWND   API CreateWindowEx(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, WORD, LPVOID);
HWND   API GetActiveWindow(void);
BOOL   API PeekMessage(LPMSG, HWND, WORD, WORD, WORD);
BOOL   API TranslateMessage(LPMSG);
LONG   API DispatchMessage(LPMSG);
BOOL   API GetMessage(LPMSG, HWND, WORD, WORD);
void   API WaitMessage(void);
int    API GetWindowText(HWND, LPSTR, int);
BOOL   API IsChild(HWND, HWND);
HWND   API GetWindow(HWND, WORD);
BOOL   API ClientToScreen(HWND, LPPOINT);
BOOL   API ScreenToClient(HWND, LPPOINT);
void   API GetClientRect(HWND, LPRECT);
void   API SetWindowText(HWND, LPCSTR);
void   API AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD);
void   API SetRect(LPRECT, int, int, int, int);
void   API SetRectEmpty(LPRECT);
void   API InflateRect(LPRECT, int, int);
void   API OffsetRect(LPRECT, int, int);
HMENU  API LoadMenu(WORD, LPCSTR);
int    API lstrcmpi(LPCSTR, LPCSTR);
void   API PostQuitMessage(int);
BOOL   API IsDialogMessage(HWND, LPMSG);
HWND   API SetActiveWindow(HWND);
HWND   API GetNextDlgTabItem(HWND, HWND, BOOL);
void   API MessageBeep(WORD);
WORD   API GetDoubleClickTime(void);
BOOL   API IsIconic(HWND);
WORD   API SetWindowWord(HWND, int, WORD);
int    API GetKeyState(int);
int    API wvsprintf(LPSTR, LPCSTR, WORD __far *);

#endif /* USER_H */
