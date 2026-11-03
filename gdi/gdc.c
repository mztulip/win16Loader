/* gdc.c - GDI: konteksty urzadzen (DC), atrybuty, mapowanie wspolrzednych */
#include "gdi.h"

extern WORD g_default_bitmap;

DCOBJ __far *dc_get(HDC h) { return (DCOBJ __far *)obj_ptr(h, OT_DC); }

static void dc_defaults(DCOBJ __far *dc)
{
    dc->pen = GetStockObject(7);         /* BLACK_PEN */
    dc->brush = GetStockObject(0);       /* WHITE_BRUSH */
    dc->font = GetStockObject(13);       /* SYSTEM_FONT */
    dc->pal = GetStockObject(15);
    dc->text = RGB(0, 0, 0);
    dc->bk = RGB(255, 255, 255);
    dc->bkmode = 2;                      /* OPAQUE */
    dc->rop2 = 13;                       /* R2_COPYPEN */
    dc->polyfill = 1;                    /* ALTERNATE */
    dc->stretch = 1;                     /* BLACKONWHITE */
    dc->mapmode = 1;                     /* MM_TEXT */
    dc->wex = dc->wey = dc->vex = dc->vey = 1;
}

static HDC dc_new(BOOL mem, int orgx, int orgy)
{
    BYTE t = mem ? OT_MEMDC : OT_DC;
    HDC h = obj_new(t);
    DCOBJ __far *dc = dc_get(h);
    if (!dc) return 0;
    dc_defaults(dc);
    dc->is_mem = mem;
    dc->orgx = orgx; dc->orgy = orgy;
    dc->vis = rgn_new();
    if (mem) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(g_default_bitmap, OT_BITMAP);
        dc->bitmap = g_default_bitmap;
        rgn_set_rect(rgn_ptr(dc->vis), 0, 0, b ? b->w : 1, b ? b->h : 1);
    } else {
        rgn_set_rect(rgn_ptr(dc->vis), 0, 0, SCR_W, SCR_H);
    }
    return h;
}

/* ordinal 53 */
HDC API CreateDC(LPCSTR drv, LPCSTR dev, LPCSTR out, LPVOID init)
{
    (void)dev; (void)out; (void)init;
    gdi_init();
    if (drv && HIWORD(drv)) {
        int i;
        const char *disp = "DISPLAY";
        for (i = 0; disp[i]; i++) {
            char c = drv[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c != disp[i]) return 0;          /* drukarki itp. - brak */
        }
    }
    return dc_new(FALSE, 0, 0);
}

/* ordinal 153 */
HDC API CreateIC(LPCSTR drv, LPCSTR dev, LPCSTR out, LPVOID init)
{
    return CreateDC(drv, dev, out, init);
}

/* ordinal 52 */
HDC API CreateCompatibleDC(HDC hdc)
{
    (void)hdc;
    gdi_init();
    return dc_new(TRUE, 0, 0);
}

static void dc_free_contents(DCOBJ __far *dc)
{
    if (dc->vis) obj_free(dc->vis);
    if (dc->clip) obj_free(dc->clip);
    if (dc->is_mem && dc->bitmap) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(dc->bitmap, OT_BITMAP);
        if (b) b->selected = 0;
    }
}

/* ordinal 68 */
BOOL API DeleteDC(HDC hdc)
{
    DCOBJ __far *dc = dc_get(hdc);
    WORD s;
    if (!dc) return FALSE;
    s = dc->saved;
    while (s) {
        DCOBJ __far *sd = dc_get(s);
        WORD n = sd ? sd->saved : 0;
        if (sd) { if (sd->vis) obj_free(sd->vis); if (sd->clip) obj_free(sd->clip); }
        obj_free(s);
        s = n;
    }
    dc_free_contents(dc);
    obj_free(hdc);
    return TRUE;
}

/* Wybor bitmapy do DC pamieci (SelectObject) */
WORD dc_select_bitmap(HDC hdc, HBITMAP hbm)
{
    DCOBJ __far *dc = dc_get(hdc);
    BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(hbm, OT_BITMAP), __far *ob;
    WORD old;
    if (!dc || !b || !dc->is_mem) return 0;
    if (b->selected && b->selected != hdc) return 0;
    old = dc->bitmap;
    ob = (BMPOBJ __far *)obj_ptr(old, OT_BITMAP);
    if (ob) ob->selected = 0;
    dc->bitmap = hbm;
    if (hbm != g_default_bitmap) b->selected = hdc;
    rgn_set_rect(rgn_ptr(dc->vis), 0, 0, b->w, b->h);
    if (dc->clip) { obj_free(dc->clip); dc->clip = 0; }
    return old;
}

/* ============================================================
 * SaveDC / RestoreDC (ordinal 30 / 39)
 * ============================================================ */
static WORD rgn_clone(WORD h)
{
    RGNDATA16 __far *s = rgn_ptr(h), __far *d;
    WORD n, i;
    if (!s) return 0;
    n = rgn_new();
    d = rgn_ptr(n);
    if (!d) return 0;
    d->n = s->n; d->box = s->box;
    for (i = 0; i < s->n; i++) d->r[i] = s->r[i];
    return n;
}

int API SaveDC(HDC hdc)
{
    DCOBJ __far *dc = dc_get(hdc), __far *sd;
    WORD h = obj_new(OT_DC);
    if (!dc || !h) return 0;
    sd = dc_get(h);
    *sd = *dc;
    sd->vis = rgn_clone(dc->vis);
    sd->clip = rgn_clone(dc->clip);
    dc->saved = h;
    dc->savelevel++;
    return dc->savelevel;
}

BOOL API RestoreDC(HDC hdc, int level)
{
    DCOBJ __far *dc = dc_get(hdc);
    if (!dc || !dc->saved) return FALSE;
    if (level < 0) level = dc->savelevel + level + 1;
    if (level < 1 || level > dc->savelevel) return FALSE;
    while (dc->savelevel >= level) {
        WORD s = dc->saved;
        DCOBJ __far *sd = dc_get(s);
        WORD keepbmp = dc->bitmap, isMem = dc->is_mem;
        if (!sd) return FALSE;
        if (dc->vis) obj_free(dc->vis);
        if (dc->clip) obj_free(dc->clip);
        *dc = *sd;                          /* saved/savelevel z kopii */
        dc->is_mem = isMem;
        if (isMem && keepbmp != dc->bitmap) {
            BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(keepbmp, OT_BITMAP);
            if (b) b->selected = 0;
            b = (BMPOBJ __far *)obj_ptr(dc->bitmap, OT_BITMAP);
            if (b && dc->bitmap != g_default_bitmap) b->selected = hdc;
        }
        obj_free(s);
    }
    return TRUE;
}

/* ============================================================
 * Mapowanie wspolrzednych
 * ============================================================ */
void dc_lp2dp(DCOBJ __far *dc, int __far *x, int __far *y)
{
    if (dc->mapmode == 1) {                 /* MM_TEXT: tylko przesuniecia */
        *x = *x - dc->wox + dc->vox;
        *y = *y - dc->woy + dc->voy;
        return;
    }
    *x = (int)((long)(*x - dc->wox) * dc->vex / (dc->wex ? dc->wex : 1)) + dc->vox;
    *y = (int)((long)(*y - dc->woy) * dc->vey / (dc->wey ? dc->wey : 1)) + dc->voy;
}

static void dc_dp2lp(DCOBJ __far *dc, int __far *x, int __far *y)
{
    if (dc->mapmode == 1) {
        *x = *x - dc->vox + dc->wox;
        *y = *y - dc->voy + dc->woy;
        return;
    }
    *x = (int)((long)(*x - dc->vox) * dc->wex / (dc->vex ? dc->vex : 1)) + dc->wox;
    *y = (int)((long)(*y - dc->voy) * dc->wey / (dc->vey ? dc->vey : 1)) + dc->woy;
}

int dc_lenx(DCOBJ __far *dc, int v)
{
    long r;
    if (dc->mapmode == 1) return v;
    r = (long)v * dc->vex / (dc->wex ? dc->wex : 1);
    return (int)(r < 0 ? -r : r);
}

int dc_leny(DCOBJ __far *dc, int v)
{
    long r;
    if (dc->mapmode == 1) return v;
    r = (long)v * dc->vey / (dc->wey ? dc->wey : 1);
    return (int)(r < 0 ? -r : r);
}

/* ordinal 99 / 67 */
BOOL API LPtoDP(HDC hdc, LPPOINT p, int n)
{
    DCOBJ __far *dc = dc_get(hdc);
    if (!dc) return FALSE;
    while (n-- > 0) { dc_lp2dp(dc, &p->x, &p->y); p++; }
    return TRUE;
}

BOOL API DPtoLP(HDC hdc, LPPOINT p, int n)
{
    DCOBJ __far *dc = dc_get(hdc);
    if (!dc) return FALSE;
    while (n-- > 0) { dc_dp2lp(dc, &p->x, &p->y); p++; }
    return TRUE;
}

/* ordinal 3 */
int API SetMapMode(HDC hdc, int mode)
{
    DCOBJ __far *dc = dc_get(hdc);
    int old;
    if (!dc) return 0;
    old = dc->mapmode;
    dc->mapmode = (WORD)mode;
    switch (mode) {                      /* 96 dpi: 1 cal = 25.4 mm */
    case 1: dc->wex = dc->wey = dc->vex = dc->vey = 1; break;
    case 2: dc->wex = 254; dc->wey = 254; dc->vex = 96; dc->vey = -96; break;     /* LOMETRIC */
    case 3: dc->wex = 2540; dc->wey = 2540; dc->vex = 96; dc->vey = -96; break;   /* HIMETRIC */
    case 4: dc->wex = 100; dc->wey = 100; dc->vex = 96; dc->vey = -96; break;     /* LOENGLISH */
    case 5: dc->wex = 1000; dc->wey = 1000; dc->vex = 96; dc->vey = -96; break;   /* HIENGLISH */
    case 6: dc->wex = 1440; dc->wey = 1440; dc->vex = 96; dc->vey = -96; break;   /* TWIPS */
    case 7: case 8: dc->wex = dc->wey = dc->vex = dc->vey = 1; break;
    }
    return old;
}

int  API GetMapMode(HDC hdc) { DCOBJ __far *dc = dc_get(hdc); return dc ? dc->mapmode : 0; }

#define DCW(field) DCOBJ __far *dc = dc_get(hdc); DWORD old; if (!dc) return 0
static DWORD pt(int x, int y) { return MAKELONG(x, y); }

DWORD API SetWindowOrg(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->wox, dc->woy); dc->wox = x; dc->woy = y; return old; }
DWORD API SetViewportOrg(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->vox, dc->voy); dc->vox = x; dc->voy = y; return old; }
DWORD API SetWindowExt(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->wex, dc->wey); if (dc->mapmode >= 7) { dc->wex = x; dc->wey = y; } return old; }
DWORD API SetViewportExt(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->vex, dc->vey); if (dc->mapmode >= 7) { dc->vex = x; dc->vey = y; } return old; }
DWORD API GetWindowOrg(HDC hdc)   { DCW(); (void)old; return pt(dc->wox, dc->woy); }
DWORD API GetViewportOrg(HDC hdc) { DCW(); (void)old; return pt(dc->vox, dc->voy); }
DWORD API GetWindowExt(HDC hdc)   { DCW(); (void)old; return pt(dc->wex, dc->wey); }
DWORD API GetViewportExt(HDC hdc) { DCW(); (void)old; return pt(dc->vex, dc->vey); }
DWORD API OffsetWindowOrg(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->wox, dc->woy); dc->wox += x; dc->woy += y; return old; }
DWORD API OffsetViewportOrg(HDC hdc, int x, int y)
{ DCW(); old = pt(dc->vox, dc->voy); dc->vox += x; dc->voy += y; return old; }
DWORD API ScaleWindowExt(HDC hdc, int xn, int xd, int yn, int yd)
{ DCW(); old = pt(dc->wex, dc->wey); if (dc->mapmode >= 7 && xd && yd) { dc->wex = (int)((long)dc->wex * xn / xd); dc->wey = (int)((long)dc->wey * yn / yd); } return old; }
DWORD API ScaleViewportExt(HDC hdc, int xn, int xd, int yn, int yd)
{ DCW(); old = pt(dc->vex, dc->vey); if (dc->mapmode >= 7 && xd && yd) { dc->vex = (int)((long)dc->vex * xn / xd); dc->vey = (int)((long)dc->vey * yn / yd); } return old; }

/* Wersje Ex (Win 3.1): stara wartosc do POINT/SIZE */
static BOOL ex(DWORD v, LPPOINT p) { if (p) { p->x = (int)LOWORD(v); p->y = (int)HIWORD(v); } return TRUE; }
BOOL API SetWindowOrgEx(HDC h, int x, int y, LPPOINT p)   { return ex(SetWindowOrg(h, x, y), p); }
BOOL API SetViewportOrgEx(HDC h, int x, int y, LPPOINT p) { return ex(SetViewportOrg(h, x, y), p); }
BOOL API SetWindowExtEx(HDC h, int x, int y, LPPOINT p)   { return ex(SetWindowExt(h, x, y), p); }
BOOL API SetViewportExtEx(HDC h, int x, int y, LPPOINT p) { return ex(SetViewportExt(h, x, y), p); }
BOOL API GetWindowOrgEx(HDC h, LPPOINT p)   { return ex(GetWindowOrg(h), p); }
BOOL API GetViewportOrgEx(HDC h, LPPOINT p) { return ex(GetViewportOrg(h), p); }
BOOL API GetWindowExtEx(HDC h, LPPOINT p)   { return ex(GetWindowExt(h), p); }
BOOL API GetViewportExtEx(HDC h, LPPOINT p) { return ex(GetViewportExt(h), p); }
BOOL API OffsetWindowOrgEx(HDC h, int x, int y, LPPOINT p)   { return ex(OffsetWindowOrg(h, x, y), p); }
BOOL API OffsetViewportOrgEx(HDC h, int x, int y, LPPOINT p) { return ex(OffsetViewportOrg(h, x, y), p); }
BOOL API ScaleWindowExtEx(HDC h, int a, int b, int c, int d, LPPOINT p)   { return ex(ScaleWindowExt(h, a, b, c, d), p); }
BOOL API ScaleViewportExtEx(HDC h, int a, int b, int c, int d, LPPOINT p) { return ex(ScaleViewportExt(h, a, b, c, d), p); }

/* ============================================================
 * Atrybuty
 * ============================================================ */
COLORREF API SetBkColor(HDC hdc, COLORREF c)   { DCW(); old = dc->bk; dc->bk = c & 0xFFFFFFUL; return old; }
COLORREF API GetBkColor(HDC hdc)               { DCW(); (void)old; return dc->bk; }
COLORREF API SetTextColor(HDC hdc, COLORREF c) { DCW(); old = dc->text; dc->text = c & 0xFFFFFFUL; return old; }
COLORREF API GetTextColor(HDC hdc)             { DCW(); (void)old; return dc->text; }
int API SetBkMode(HDC hdc, int m)        { DCW(); old = dc->bkmode; dc->bkmode = (WORD)m; return (int)old; }
int API GetBkMode(HDC hdc)               { DCW(); (void)old; return dc->bkmode; }
int API SetROP2(HDC hdc, int m)          { DCW(); old = dc->rop2; dc->rop2 = (WORD)m; return (int)old; }
int API GetROP2(HDC hdc)                 { DCW(); (void)old; return dc->rop2; }
int API SetPolyFillMode(HDC hdc, int m)  { DCW(); old = dc->polyfill; dc->polyfill = (WORD)m; return (int)old; }
int API GetPolyFillMode(HDC hdc)         { DCW(); (void)old; return dc->polyfill; }
int API SetStretchBltMode(HDC hdc, int m){ DCW(); old = dc->stretch; dc->stretch = (WORD)m; return (int)old; }
int API GetStretchBltMode(HDC hdc)       { DCW(); (void)old; return dc->stretch; }
WORD API SetTextAlign(HDC hdc, WORD a)   { DCW(); old = dc->align; dc->align = a; return (WORD)old; }
WORD API GetTextAlign(HDC hdc)           { DCW(); (void)old; return dc->align; }
int API SetTextCharacterExtra(HDC hdc, int e) { DCW(); old = dc->charextra; dc->charextra = e; return (int)old; }
int API GetTextCharacterExtra(HDC hdc)   { DCW(); (void)old; return dc->charextra; }
int API SetTextJustification(HDC hdc, int e, int n) { (void)hdc; (void)e; (void)n; return 1; }
int API SetRelAbs(HDC hdc, int m)        { (void)hdc; (void)m; return 1; }
int API GetRelAbs(HDC hdc)               { (void)hdc; return 1; }
DWORD API SetBrushOrg(HDC hdc, int x, int y) { DCW(); old = pt(dc->brx, dc->bry); dc->brx = x & 7; dc->bry = y & 7; return old; }
DWORD API GetBrushOrg(HDC hdc)           { DCW(); (void)old; return pt(dc->brx, dc->bry); }
DWORD API GetCurrentPosition(HDC hdc)    { DCW(); (void)old; return pt(dc->curx, dc->cury); }
BOOL  API GetCurrentPositionEx(HDC h, LPPOINT p) { return ex(GetCurrentPosition(h), p); }
DWORD API MoveTo(HDC hdc, int x, int y)  { DCW(); old = pt(dc->curx, dc->cury); dc->curx = x; dc->cury = y; return old; }
BOOL  API MoveToEx(HDC h, int x, int y, LPPOINT p) { return ex(MoveTo(h, x, y), p); }
DWORD API GetDCOrg(HDC hdc)              { DCW(); (void)old; return pt(dc->orgx, dc->orgy); }
DWORD API GetAspectRatioFilter(HDC hdc)  { (void)hdc; return pt(1, 1); }
BOOL  API GetAspectRatioFilterEx(HDC h, LPPOINT p) { return ex(pt(1, 1), p); }
HDC   API GetDCHook(HDC hdc, LPVOID p)   { (void)hdc; (void)p; return 0; }

/* ordinal 80 */
int API GetDeviceCaps(HDC hdc, int idx)
{
    DCOBJ __far *dc = dc_get(hdc);
    BOOL mono = FALSE;
    if (dc && dc->is_mem) {
        BMPOBJ __far *b = (BMPOBJ __far *)obj_ptr(dc->bitmap, OT_BITMAP);
        mono = b && b->bpp == 1;
    }
    switch (idx) {
    case 0:   return 0x030A;              /* DRIVERVERSION */
    case 2:   return 1;                   /* TECHNOLOGY: DT_RASDISPLAY */
    case 4:   return 208;                 /* HORZSIZE mm */
    case 6:   return 156;                 /* VERTSIZE mm */
    case 8:   return SCR_W;               /* HORZRES */
    case 10:  return SCR_H;               /* VERTRES */
    case 12:  return mono ? 1 : 24;       /* BITSPIXEL */
    case 14:  return 1;                   /* PLANES */
    case 16:  return -1;                  /* NUMBRUSHES */
    case 18:  return -1;                  /* NUMPENS */
    case 22:  return 0;                   /* NUMFONTS */
    case 24:  return mono ? 2 : -1;       /* NUMCOLORS */
    case 26:  return sizeof(DCOBJ);       /* PDEVICESIZE */
    case 28:  return 0xFF;                /* CURVECAPS */
    case 30:  return 0xFE;                /* LINECAPS */
    case 32:  return 0xFF;                /* POLYGONALCAPS */
    case 34:  return 0x0004;              /* TEXTCAPS: TC_CP_STROKE */
    case 36:  return 1;                   /* CLIPCAPS */
    case 38:  return 0x0001 | 0x0008 | 0x0010 | 0x0080 | 0x0200 | 0x0400 | 0x0800 | 0x2000;
    case 40:  return 36;                  /* ASPECTX */
    case 42:  return 36;                  /* ASPECTY */
    case 44:  return 51;                  /* ASPECTXY */
    case 88:  return 96;                  /* LOGPIXELSX */
    case 90:  return 96;                  /* LOGPIXELSY */
    case 104: return 0;                   /* SIZEPALETTE */
    case 106: return 0;                   /* NUMRESERVED */
    case 108: return 24;                  /* COLORRES */
    }
    return 0;
}

/* ============================================================
 * Prywatne API dla USER (ordinale 910+)
 * ============================================================ */
/* 910: DC okna - origin (wsp. ekranu) i region widoczny (prostokaty ekranu) */
HDC API GdiCreateWindowDC(int orgx, int orgy, LPRECT vis, int n, WORD hwnd)
{
    HDC h;
    DCOBJ __far *dc;
    RGNDATA16 __far *v;
    int i;
    gdi_init();
    h = dc_new(FALSE, orgx, orgy);
    dc = dc_get(h);
    if (!dc) return 0;
    dc->hwnd = hwnd;
    v = rgn_ptr(dc->vis);
    v->n = 0;
    for (i = 0; i < n && i < MAX_RECTS; i++) v->r[v->n++] = vis[i];
    {
        RECT scr;
        scr.left = 0; scr.top = 0; scr.right = SCR_W; scr.bottom = SCR_H;
        rgn_and_rect(v, (RECT __far *)&scr);
    }
    return h;
}

/* 911: zmien origin/region widoczny istniejacego DC */
void API GdiSetDCVis(HDC hdc, int orgx, int orgy, LPRECT vis, int n)
{
    DCOBJ __far *dc = dc_get(hdc);
    RGNDATA16 __far *v;
    int i, dx, dy;
    if (!dc) return;
    dx = orgx - dc->orgx; dy = orgy - dc->orgy;
    dc->orgx = orgx; dc->orgy = orgy;
    if (dc->clip) rgn_offset(rgn_ptr(dc->clip), dx, dy);
    v = rgn_ptr(dc->vis);
    v->n = 0;
    for (i = 0; i < n && i < MAX_RECTS; i++) v->r[v->n++] = vis[i];
    rgn_box(v);
}

/* 912: przytnij clip DC do regionu (aktualizacja BeginPaint) - prostokaty ekranu */
void API GdiIntersectVis(HDC hdc, LPRECT r, int n)
{
    DCOBJ __far *dc = dc_get(hdc);
    static RGNDATA16 t;
    RGNDATA16 __far *u = (RGNDATA16 __far *)&t;
    int i;
    if (!dc) return;
    u->n = 0;
    for (i = 0; i < n && i < MAX_RECTS; i++) u->r[u->n++] = r[i];
    rgn_box(u);
    rgn_and(rgn_ptr(dc->vis), u, rgn_ptr(dc->vis));
}

/* 913: HWND zapisany w DC (WindowFromDC) */
WORD API GdiDCWindow(HDC hdc)
{
    DCOBJ __far *dc = dc_get(hdc);
    return dc ? dc->hwnd : 0;
}

/* 914: prostokaty regionu (dla USER: region aktualizacji -> clip BeginPaint) */
int API GdiGetRgnRects(HRGN h, LPRECT out, int max)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    int i;
    if (!g) return 0;
    for (i = 0; i < (int)g->n && i < max; i++) out[i] = g->r[i];
    return i;
}
