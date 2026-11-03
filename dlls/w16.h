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
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef long           LONG;
typedef WORD           HANDLE;
typedef WORD           HWND;
typedef WORD           HICON;
typedef WORD           HINSTANCE;
typedef DWORD          HKEY;
typedef char __far    *LPSTR;
typedef const char __far *LPCSTR;
typedef void __far    *LPVOID;
typedef DWORD __far   *LPDWORD;
typedef LONG __far    *LPLONG;

#define API     __far __pascal
#define NULL    0
#define TRUE    1
#define FALSE   0

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
int    API _lopen(LPCSTR, int);
int    API _lcreat(LPCSTR, int);
int    API _lclose(int);
WORD   API _lread(int, LPVOID, WORD);
WORD   API _lwrite(int, LPCSTR, WORD);
LONG   API _llseek(int, LONG, int);
int    API lstrlen(LPCSTR);
LPSTR  API lstrcpy(LPSTR, LPCSTR);
int    API lstrcmpi(LPCSTR, LPCSTR);
WORD   API GetWindowsDirectory(LPSTR, WORD);
WORD   API GetDriveType(int);
int    API GetModuleFileName(WORD, LPSTR, int);
DWORD  API GetTickCount(void);

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
