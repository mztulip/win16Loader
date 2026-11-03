/* grgn.c - GDI: regiony (listy prostokatow) i przycinanie DC */
#include "gdi.h"

#define NULLREGION    1
#define SIMPLEREGION  2
#define COMPLEXREGION 3
#define RGN_AND  1
#define RGN_OR   2
#define RGN_XOR  3
#define RGN_DIFF 4
#define RGN_COPY 5

BOOL rect_and(RECT __far *a, RECT __far *b, RECT __far *o)
{
    o->left   = a->left   > b->left   ? a->left   : b->left;
    o->top    = a->top    > b->top    ? a->top    : b->top;
    o->right  = a->right  < b->right  ? a->right  : b->right;
    o->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    return o->left < o->right && o->top < o->bottom;
}

void rgn_box(RGNDATA16 __far *a)
{
    WORD i;
    if (!a->n) { a->box.left = a->box.top = a->box.right = a->box.bottom = 0; return; }
    a->box = a->r[0];
    for (i = 1; i < a->n; i++) {
        if (a->r[i].left   < a->box.left)   a->box.left   = a->r[i].left;
        if (a->r[i].top    < a->box.top)    a->box.top    = a->r[i].top;
        if (a->r[i].right  > a->box.right)  a->box.right  = a->r[i].right;
        if (a->r[i].bottom > a->box.bottom) a->box.bottom = a->r[i].bottom;
    }
}

/* Dodaj prostokat; przy przepelnieniu - zastap obwiednia (przyblizenie) */
static void rgn_add(RGNDATA16 __far *a, int l, int t, int r, int b)
{
    if (l >= r || t >= b) return;
    if (a->n >= MAX_RECTS) {
        rgn_box(a);
        a->r[0] = a->box;
        a->n = 1;
        if (l < a->r[0].left) a->r[0].left = l;
        if (t < a->r[0].top) a->r[0].top = t;
        if (r > a->r[0].right) a->r[0].right = r;
        if (b > a->r[0].bottom) a->r[0].bottom = b;
        return;
    }
    a->r[a->n].left = l; a->r[a->n].top = t; a->r[a->n].right = r; a->r[a->n].bottom = b;
    a->n++;
}

void rgn_set_rect(RGNDATA16 __far *a, int l, int t, int r, int b)
{
    a->n = 0;
    rgn_add(a, l, t, r, b);
    rgn_box(a);
}

void rgn_and_rect(RGNDATA16 __far *a, RECT __far *rc)
{
    WORD i, k = 0;
    for (i = 0; i < a->n; i++) {
        RECT o;
        if (rect_and(&a->r[i], rc, (RECT __far *)&o)) a->r[k++] = o;
    }
    a->n = k;
    rgn_box(a);
}

/* Odejmij prostokat (kazdy prostokat regionu -> do 4 kawalkow) */
void rgn_sub_rect(RGNDATA16 __far *a, RECT __far *s)
{
    static RGNDATA16 tmp;
    RGNDATA16 __far *t = (RGNDATA16 __far *)&tmp;
    WORD i;
    t->n = 0;
    for (i = 0; i < a->n; i++) {
        RECT r = a->r[i], o;
        if (!rect_and((RECT __far *)&r, s, (RECT __far *)&o)) {
            rgn_add(t, r.left, r.top, r.right, r.bottom);
            continue;
        }
        rgn_add(t, r.left, r.top, r.right, o.top);          /* gora */
        rgn_add(t, r.left, o.bottom, r.right, r.bottom);    /* dol */
        rgn_add(t, r.left, o.top, o.left, o.bottom);        /* lewo */
        rgn_add(t, o.right, o.top, r.right, o.bottom);      /* prawo */
    }
    for (i = 0; i < t->n; i++) a->r[i] = t->r[i];
    a->n = t->n;
    rgn_box(a);
}

void rgn_and(RGNDATA16 __far *a, RGNDATA16 __far *b, RGNDATA16 __far *out)
{
    static RGNDATA16 tmp;
    RGNDATA16 __far *t = (RGNDATA16 __far *)&tmp;
    WORD i, j;
    t->n = 0;
    for (i = 0; i < a->n; i++)
        for (j = 0; j < b->n; j++) {
            RECT o;
            if (rect_and(&a->r[i], &b->r[j], (RECT __far *)&o))
                rgn_add(t, o.left, o.top, o.right, o.bottom);
        }
    for (i = 0; i < t->n; i++) out->r[i] = t->r[i];
    out->n = t->n;
    rgn_box(out);
}

static void rgn_copy(RGNDATA16 __far *d, RGNDATA16 __far *s)
{
    WORD i;
    d->n = s->n;
    for (i = 0; i < s->n; i++) d->r[i] = s->r[i];
    d->box = s->box;
}

static void rgn_diff(RGNDATA16 __far *a, RGNDATA16 __far *b, RGNDATA16 __far *out)
{
    WORD j;
    if (out != a) rgn_copy(out, a);
    for (j = 0; j < b->n; j++) rgn_sub_rect(out, &b->r[j]);
}

static void rgn_or(RGNDATA16 __far *a, RGNDATA16 __far *b, RGNDATA16 __far *out)
{
    static RGNDATA16 tb;
    RGNDATA16 __far *t = (RGNDATA16 __far *)&tb;
    WORD i;
    rgn_diff(b, a, t);
    if (out != a) rgn_copy(out, a);
    for (i = 0; i < t->n; i++) rgn_add(out, t->r[i].left, t->r[i].top, t->r[i].right, t->r[i].bottom);
    rgn_box(out);
}

void rgn_offset(RGNDATA16 __far *a, int dx, int dy)
{
    WORD i;
    for (i = 0; i < a->n; i++) {
        a->r[i].left += dx; a->r[i].right += dx;
        a->r[i].top += dy; a->r[i].bottom += dy;
    }
    rgn_box(a);
}

static int rgn_kind(RGNDATA16 __far *a)
{
    return a->n == 0 ? NULLREGION : a->n == 1 ? SIMPLEREGION : COMPLEXREGION;
}

RGNDATA16 __far *rgn_ptr(WORD h) { return (RGNDATA16 __far *)obj_ptr(h, OT_RGN); }
WORD rgn_new(void) { return obj_new(OT_RGN); }

/* ============================================================
 * API regionow
 * ============================================================ */
/* ordinal 64 */
HRGN API CreateRectRgn(int l, int t, int r, int b)
{
    WORD h = rgn_new();
    if (h) rgn_set_rect(rgn_ptr(h), l, t, r, b);
    return h;
}
/* ordinal 65 */
HRGN API CreateRectRgnIndirect(LPRECT rc) { return CreateRectRgn(rc->left, rc->top, rc->right, rc->bottom); }

/* ordinal 54: elipsa jako pasy (przyblizenie do MAX_RECTS) */
HRGN API CreateEllipticRgn(int l, int t, int r, int b)
{
    WORD h = rgn_new();
    RGNDATA16 __far *g = rgn_ptr(h);
    long a2, b2, cx2, cy2;
    int bands, i, hgt = b - t;
    if (!g) return 0;
    g->n = 0;
    if (r <= l || b <= t) { rgn_box(g); return h; }
    bands = hgt < MAX_RECTS ? hgt : MAX_RECTS;
    cx2 = (long)l + r; cy2 = (long)t + b;
    a2 = (long)(r - l) * (r - l); b2 = (long)(b - t) * (b - t);
    for (i = 0; i < bands; i++) {
        int y0 = t + (int)((long)hgt * i / bands), y1 = t + (int)((long)hgt * (i + 1) / bands);
        long dy = 2L * ((y0 + y1) / 2) + 1 - cy2, dx2, w;
        if (b2 == 0) break;
        dx2 = a2 - a2 * dy / b2 * dy;            /* (2dx)^2 = a^2 (1 - (2dy)^2/b^2) */
        if (dx2 <= 0) continue;
        w = 0;
        while ((w + 1) * (w + 1) <= dx2) w++;
        rgn_add(g, (int)((cx2 - w) / 2), y0, (int)((cx2 + w + 1) / 2), y1);
    }
    rgn_box(g);
    return h;
}
/* ordinal 55 */
HRGN API CreateEllipticRgnIndirect(LPRECT rc) { return CreateEllipticRgn(rc->left, rc->top, rc->right, rc->bottom); }
/* ordinal 444 */
HRGN API CreateRoundRectRgn(int l, int t, int r, int b, int ew, int eh)
{
    (void)ew; (void)eh;
    return CreateRectRgn(l, t, r, b);
}
/* ordinal 63: obwiednia wielokata */
HRGN API CreatePolygonRgn(LPPOINT p, int n, int mode)
{
    int i, l = 32767, t = 32767, r = -32768, b = -32768;
    (void)mode;
    for (i = 0; i < n; i++) {
        if (p[i].x < l) l = p[i].x;
        if (p[i].y < t) t = p[i].y;
        if (p[i].x > r) r = p[i].x;
        if (p[i].y > b) b = p[i].y;
    }
    return CreateRectRgn(l, t, r, b);
}
/* ordinal 172 */
void API SetRectRgn(HRGN h, int l, int t, int r, int b)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    if (g) rgn_set_rect(g, l, t, r, b);
}

/* ordinal 47 */
int API CombineRgn(HRGN d, HRGN s1, HRGN s2, int mode)
{
    static RGNDATA16 t1, t2;
    RGNDATA16 __far *rd = rgn_ptr(d), __far *a = rgn_ptr(s1), __far *b = rgn_ptr(s2);
    RGNDATA16 __far *x = (RGNDATA16 __far *)&t1, __far *y = (RGNDATA16 __far *)&t2;
    if (!rd || !a) return 0;
    rgn_copy(x, a);
    if (b) rgn_copy(y, b); else y->n = 0;
    switch (mode) {
    case RGN_AND:  rgn_and(x, y, rd); break;
    case RGN_OR:   rgn_or(x, y, rd); break;
    case RGN_DIFF: rgn_diff(x, y, rd); break;
    case RGN_COPY: rgn_copy(rd, x); break;
    case RGN_XOR: {
        static RGNDATA16 t3;
        RGNDATA16 __far *z = (RGNDATA16 __far *)&t3;
        rgn_diff(x, y, z);
        rgn_diff(y, x, rd);
        rgn_or(rd, z, rd);
        break;
    }
    default: return 0;
    }
    rgn_box(rd);
    return rgn_kind(rd);
}

/* ordinal 101 */
int API OffsetRgn(HRGN h, int dx, int dy)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    if (!g) return 0;
    rgn_offset(g, dx, dy);
    return rgn_kind(g);
}

/* ordinal 134 */
int API GetRgnBox(HRGN h, LPRECT rc)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    if (!g) return 0;
    *rc = g->box;
    return rgn_kind(g);
}

/* ordinal 161 */
BOOL API PtInRegion(HRGN h, int x, int y)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    WORD i;
    if (!g) return FALSE;
    for (i = 0; i < g->n; i++)
        if (x >= g->r[i].left && x < g->r[i].right && y >= g->r[i].top && y < g->r[i].bottom)
            return TRUE;
    return FALSE;
}

/* ordinal 181 */
BOOL API RectInRegion(HRGN h, LPRECT rc)
{
    RGNDATA16 __far *g = rgn_ptr(h);
    WORD i;
    RECT o;
    if (!g) return FALSE;
    for (i = 0; i < g->n; i++) if (rect_and(&g->r[i], rc, (RECT __far *)&o)) return TRUE;
    return FALSE;
}

/* ordinal 72 */
BOOL API EqualRgn(HRGN a, HRGN b)
{
    RGNDATA16 __far *x = rgn_ptr(a), __far *y = rgn_ptr(b);
    static RGNDATA16 t;
    RGNDATA16 __far *d = (RGNDATA16 __far *)&t;
    if (!x || !y) return FALSE;
    rgn_diff(x, y, d);
    if (d->n) return FALSE;
    rgn_diff(y, x, d);
    return d->n == 0;
}

/* ============================================================
 * Przycinanie DC (regiony w ukladzie powierzchni)
 * ============================================================ */
/* Efektywny clip = widoczny (USER) AND clip apki, przyciety do powierzchni */
void dc_effective_clip(DCOBJ __far *dc, RGNDATA16 __far *out)
{
    RGNDATA16 __far *v = rgn_ptr(dc->vis), __far *c = rgn_ptr(dc->clip);
    if (v) rgn_copy(out, v); else out->n = 0;
    if (c) rgn_and(out, c, out);
    rgn_box(out);
}

static RGNDATA16 __far *dc_clip_rgn(DCOBJ __far *dc)
{
    if (!dc->clip) {
        RGNDATA16 __far *v;
        dc->clip = rgn_new();
        if (!dc->clip) return 0;
        v = rgn_ptr(dc->vis);
        if (v) rgn_copy(rgn_ptr(dc->clip), v);
        else rgn_set_rect(rgn_ptr(dc->clip), -32000, -32000, 32000, 32000);
    }
    return rgn_ptr(dc->clip);
}

static void lrect2dev(DCOBJ __far *dc, int l, int t, int r, int b, RECT __far *o)
{
    int x0 = l, y0 = t, x1 = r, y1 = b, k;
    dc_lp2dp(dc, (int __far *)&x0, (int __far *)&y0);
    dc_lp2dp(dc, (int __far *)&x1, (int __far *)&y1);
    if (x0 > x1) { k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { k = y0; y0 = y1; y1 = k; }
    o->left = x0 + dc->orgx; o->right = x1 + dc->orgx;
    o->top = y0 + dc->orgy;  o->bottom = y1 + dc->orgy;
}

/* ordinal 21 */
int API ExcludeClipRect(HDC hdc, int l, int t, int r, int b)
{
    DCOBJ __far *dc = dc_get(hdc);
    RGNDATA16 __far *c;
    RECT rc;
    if (!dc || !(c = dc_clip_rgn(dc))) return 0;
    lrect2dev(dc, l, t, r, b, (RECT __far *)&rc);
    rgn_sub_rect(c, (RECT __far *)&rc);
    return rgn_kind(c);
}

/* ordinal 22 */
int API IntersectClipRect(HDC hdc, int l, int t, int r, int b)
{
    DCOBJ __far *dc = dc_get(hdc);
    RGNDATA16 __far *c;
    RECT rc;
    if (!dc || !(c = dc_clip_rgn(dc))) return 0;
    lrect2dev(dc, l, t, r, b, (RECT __far *)&rc);
    rgn_and_rect(c, (RECT __far *)&rc);
    return rgn_kind(c);
}

/* ordinal 32 */
int API OffsetClipRgn(HDC hdc, int dx, int dy)
{
    DCOBJ __far *dc = dc_get(hdc);
    RGNDATA16 __far *c;
    if (!dc || !(c = dc_clip_rgn(dc))) return 0;
    rgn_offset(c, dx, dy);
    return rgn_kind(c);
}

/* SelectClipRgn: kopia regionu (wsp. logiczne MM_TEXT -> powierzchnia) */
int dc_select_cliprgn(HDC hdc, HRGN h)
{
    DCOBJ __far *dc = dc_get(hdc);
    RGNDATA16 __far *s = rgn_ptr(h), __far *c;
    if (!dc) return 0;
    if (!s) {
        if (dc->clip) { obj_free(dc->clip); dc->clip = 0; }
        return SIMPLEREGION;
    }
    if (!(c = dc_clip_rgn(dc))) return 0;
    rgn_copy(c, s);
    rgn_offset(c, dc->orgx, dc->orgy);
    return rgn_kind(c);
}

/* ordinal 44 */
int API SelectClipRgn(HDC hdc, HRGN h) { return dc_select_cliprgn(hdc, h); }

/* ordinal 77: GetClipBox (logiczne) */
int API GetClipBox(HDC hdc, LPRECT rc)
{
    DCOBJ __far *dc = dc_get(hdc);
    static RGNDATA16 t;
    RGNDATA16 __far *e = (RGNDATA16 __far *)&t;
    if (!dc) return 0;
    dc_effective_clip(dc, e);
    rc->left = e->box.left - dc->orgx; rc->right = e->box.right - dc->orgx;
    rc->top = e->box.top - dc->orgy;   rc->bottom = e->box.bottom - dc->orgy;
    if (dc->mapmode != 1) {           /* odwroc mapowanie (przyblizenie dla MM_TEXT) */
        if (dc->vex && dc->wex) {
            rc->left = (int)((long)(rc->left - dc->vox) * dc->wex / dc->vex) + dc->wox;
            rc->right = (int)((long)(rc->right - dc->vox) * dc->wex / dc->vex) + dc->wox;
        }
        if (dc->vey && dc->wey) {
            rc->top = (int)((long)(rc->top - dc->voy) * dc->wey / dc->vey) + dc->woy;
            rc->bottom = (int)((long)(rc->bottom - dc->voy) * dc->wey / dc->vey) + dc->woy;
        }
    }
    return rgn_kind(e);
}

/* ordinal 104 (RectVisibleOld) / 465 (RectVisible) */
BOOL API RectVisible(HDC hdc, LPRECT r)
{
    DCOBJ __far *dc = dc_get(hdc);
    static RGNDATA16 t;
    RGNDATA16 __far *e = (RGNDATA16 __far *)&t;
    RECT d, o;
    WORD i;
    if (!dc) return FALSE;
    dc_effective_clip(dc, e);
    lrect2dev(dc, r->left, r->top, r->right, r->bottom, (RECT __far *)&d);
    for (i = 0; i < e->n; i++) if (rect_and(&e->r[i], (RECT __far *)&d, (RECT __far *)&o)) return TRUE;
    return FALSE;
}

/* ordinal 103 */
BOOL API PtVisible(HDC hdc, int x, int y)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + 1; r.bottom = y + 1;
    return RectVisible(hdc, (LPRECT)&r);
}

/* ordinal 451: GetClipRgn-like (Win16: GetClipRgn nie istnieje) - pomijamy */
