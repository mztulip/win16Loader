/* gdi.h - GDI.EXE (Win16 GDI): wspolne definicje
 *
 * Model: -mm (wiele segmentow kodu), DS = DGROUP GDI, SS = stos apki (-zu).
 * Powierzchnie: ekran (LFB 640x480x24) i bitmapy (32bpp 0x00RRGGBB w XMS,
 * pamiec z KERNEL GlobalAlloc - ciag selektorow po 64KB, __AHINCR = 8).
 */
#ifndef GDI_H
#define GDI_H

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef long           LONG;
typedef WORD           HANDLE;
typedef WORD           HDC;
typedef WORD           HGDIOBJ;
typedef WORD           HBITMAP;
typedef WORD           HBRUSH;
typedef WORD           HPEN;
typedef WORD           HFONT;
typedef WORD           HRGN;
typedef WORD           HPALETTE;
typedef DWORD          COLORREF;
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
#define RGB(r, g, b)    ((COLORREF)(((BYTE)(r) | ((WORD)(BYTE)(g) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c)    ((BYTE)(c))
#define GetGValue(c)    ((BYTE)((c) >> 8))
#define GetBValue(c)    ((BYTE)((c) >> 16))

typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x, y; } POINT;
typedef RECT __far *LPRECT;
typedef POINT __far *LPPOINT;

/* ---- Selektory systemowe (pm_call.asm) ---- */
#define SEL_VESA_BASE   0xA0      /* okna 64KB LFB, kolejne co 8 */
#define SEL_KCB         0x98
#define SCR_W           640
#define SCR_H           480
#define SCR_PITCH       (SCR_W * 3)

/* ---- KERNEL (importy) ---- */
WORD  API GlobalAlloc(WORD flags, DWORD size);
WORD  API GlobalFree(WORD h);
DWORD API GlobalSize(WORD h);
LPVOID API GlobalLock(WORD h);
WORD  API FindResource(WORD hinst, LPCSTR name, LPCSTR type);
WORD  API LoadResource(WORD hinst, WORD hres);
LPVOID API LockResource(WORD h);
DWORD API SizeofResource(WORD hinst, WORD hres);
WORD  API LoadLibrary(LPCSTR name);
void  API OutputDebugString(LPCSTR s);

/* ---- Typy obiektow ---- */
#define OT_FREE    0
#define OT_PEN     1
#define OT_BRUSH   2
#define OT_FONT    3
#define OT_PAL     4
#define OT_BITMAP  5
#define OT_RGN     6
#define OT_DC      7
#define OT_MEMDC   8
#define OT_META    9

/* GetObject/GetObjectType numeracja Win16 */
#define MAX_RECTS  64              /* prostokaty w regionie / clipie */

typedef struct {
    WORD  n;                       /* liczba prostokatow */
    RECT  box;                     /* obwiednia */
    RECT  r[MAX_RECTS];
} RGNDATA16;

/* Pioro */
typedef struct {
    WORD     style;                /* PS_* */
    int      width;
    COLORREF color;
} PENOBJ;

/* Pedzel */
#define BS_SOLID    0
#define BS_NULL     1
#define BS_HATCHED  2
#define BS_PATTERN  3
#define BS_DIBPATTERN 5
typedef struct {
    WORD     style;
    COLORREF color;
    WORD     hatch;
    BYTE     pat[8];               /* BS_HATCHED/mono PATTERN: bity wierszy 8x8 */
    WORD     patmem;               /* kolorowy wzor 8x8: GlobalAlloc 256 B (0x00RRGGBB), 0 = brak */
} BRUSHOBJ;

/* Font (LOGFONT Win16) */
typedef struct {
    int  lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONT;

typedef struct {
    LOGFONT lf;
    int     face;                  /* indeks zaladowanego fontu FNT (g_fnt) */
} FONTOBJ;

/* Bitmapa */
typedef struct {
    int   w, h;
    BYTE  bpp;                     /* logiczna glebia (1, 4, 8, 24) - dla GetObject */
    BYTE  planes;
    WORD  mem;                     /* GlobalAlloc: w*h*4 bajtow, 0x00RRGGBB */
    WORD  selected;                /* HDC, w ktorym wybrana (0 = zadnym) */
    int   dimx, dimy;              /* SetBitmapDimension */
} BMPOBJ;

/* Paleta */
typedef struct {
    WORD  n;
    WORD  mem;                     /* GlobalAlloc: n * 4 (PALETTEENTRY) */
} PALOBJ;

/* DC */
typedef struct {
    WORD     is_mem;               /* 1 = DC pamieci (powierzchnia = bitmapa) */
    WORD     bitmap;               /* wybrana bitmapa (DC pamieci) */
    WORD     pen, brush, font, pal, rgn;
    int      orgx, orgy;           /* origin okna na powierzchni (urzadzenie) */
    WORD     vis;                  /* region widoczny (uchwyt regionu, wsp. powierzchni) */
    WORD     clip;                 /* clip apki (uchwyt regionu, wsp. powierzchni), 0 = brak */
    COLORREF text, bk;
    WORD     bkmode;               /* 1 = TRANSPARENT, 2 = OPAQUE */
    WORD     rop2;
    WORD     polyfill;
    WORD     stretch;
    WORD     align;                /* TA_* */
    int      charextra;
    int      brx, bry;             /* brush origin */
    int      curx, cury;           /* current position (logiczne) */
    WORD     mapmode;
    int      wox, woy, wex, wey;   /* window org/ext */
    int      vox, voy, vex, vey;   /* viewport org/ext */
    WORD     hwnd;                 /* okno (DC USER) */
    WORD     saved;                /* stos SaveDC: uchwyt kopii (OT_DC wolny) */
    int      savelevel;
} DCOBJ;

typedef struct {
    BYTE type;
    BYTE stock;                    /* obiekt magazynowy - DeleteObject ignoruje */
    WORD slot;                     /* indeks w puli danego typu */
} GOBJ;

#define MAX_OBJS 1024
extern GOBJ g_obj[MAX_OBJS];

/* Uchwyty: 0x4000 + 4*indeks (nie koliduja z HWND USER i malymi liczbami) */
#define H2I(h)   ((WORD)(((h) - 0x4000) >> 2))
#define I2H(i)   ((WORD)(0x4000 + ((i) << 2)))

void __far *obj_ptr(WORD h, BYTE type);           /* NULL gdy zly typ */
WORD  obj_new(BYTE type);                         /* zwraca uchwyt (dane wyzerowane) */
void  gdi_init(void);
void  obj_free(WORD h);
BYTE  obj_type(WORD h);

/* ---- gdc.c ---- */
DCOBJ __far *dc_get(HDC h);
void  dc_lp2dp(DCOBJ __far *dc, int __far *x, int __far *y);
int   dc_lenx(DCOBJ __far *dc, int v);
int   dc_leny(DCOBJ __far *dc, int v);
void  dc_effective_clip(DCOBJ __far *dc, RGNDATA16 __far *out);
RGNDATA16 __far *rgn_ptr(WORD h);
WORD  rgn_new(void);

/* ---- gsurf.c: powierzchnie i operacje pikselowe ---- */
typedef struct {
    WORD  sel;                     /* pierwszy selektor (ciag po 64KB) */
    WORD  bpp;                     /* 24 = ekran (3 B/piksel), 32 = bitmapa */
    WORD  mono;                    /* bitmapa 1bpp (piksele 0 / 0xFFFFFF) */
    int   w, h;
    DWORD pitch;
} SURF;

BOOL  surf_of_dc(DCOBJ __far *dc, SURF __far *s);
DWORD px_get(SURF __far *s, int x, int y);
void  px_put(SURF __far *s, int x, int y, DWORD c);
DWORD rgb2px(COLORREF c);              /* COLORREF -> 0x00RRGGBB */
COLORREF px2rgb(DWORD p);
DWORD mono_of(DWORD p);                /* kolor -> 0 / 0xFFFFFF */
void  span_rop2(SURF __far *s, int y, int x0, int x1, DWORD c, WORD rop2);
DWORD rop3(BYTE rop, DWORD p, DWORD s, DWORD d);

/* Kursor myszy (gsurf.c) - ukryj gdy rysowanie dotyka jego prostokata */
void  cursor_exclude(int x0, int y0, int x1, int y1);
void  cursor_restore(void);

/* ---- gdraw.c ---- */
void  fill_rect_brush(DCOBJ __far *dc, SURF __far *s, RECT __far *r, HBRUSH hb, WORD rop);
DWORD brush_px(BRUSHOBJ __far *b, DCOBJ __far *dc, int x, int y, BOOL mono);

/* ---- grgn.c ---- */
void  rgn_set_rect(RGNDATA16 __far *r, int l, int t, int ri, int b);
void  rgn_and(RGNDATA16 __far *a, RGNDATA16 __far *b, RGNDATA16 __far *out);
void  rgn_sub_rect(RGNDATA16 __far *a, RECT __far *rc);
void  rgn_and_rect(RGNDATA16 __far *a, RECT __far *rc);
void  rgn_offset(RGNDATA16 __far *a, int dx, int dy);
void  rgn_box(RGNDATA16 __far *a);
BOOL  rect_and(RECT __far *a, RECT __far *b, RECT __far *out);

/* ---- gtext.c ---- */
void  font_init(void);
int   font_match(LOGFONT __far *lf);

/* ---- API uzywane wewnetrznie ---- */
HGDIOBJ API GetStockObject(int i);
HGDIOBJ API SelectObject(HDC hdc, HGDIOBJ h);
BOOL    API DeleteObject(HGDIOBJ h);
HDC     API CreateCompatibleDC(HDC hdc);
BOOL    API DeleteDC(HDC hdc);
BOOL    API PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop);

/* ---- debug ---- */
void  gdbg(LPCSTR s);
void  gdbg_hex(DWORD v, int digits);

#endif /* GDI_H */
