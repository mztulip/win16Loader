/* gdraw.c - GDI: rysowanie (PatBlt, BitBlt, StretchBlt, linie, ksztalty,
 * wielokaty, wypelnianie, regiony) z przycinaniem do regionu DC. */
#include "gdi.h"

#define TRANSP 0x80000000UL          /* piksel pedzla przezroczysty (tlo TRANSPARENT) */

/* Kontekst rysowania: DC, powierzchnia, efektywny clip (wsp. powierzchni) */
typedef struct {
    DCOBJ __far *dc;
    SURF s;
    RGNDATA16 clip;
    BOOL scr;
} DRAW;

static DRAW g_d;                     /* jedna operacja naraz (GDI jednowatkowe) */
static DRAW g_src;                   /* zrodlo dla BitBlt */

#define BLT_LINE 256
static DWORD g_ls[BLT_LINE], g_ld[BLT_LINE];  /* bufory wiersza */

/* Bez kursora: wolajacy zna prostokat rysowania i wola draw_excl */
static DRAW *draw_open(HDC hdc)
{
    DRAW *d = &g_d;
    d->dc = dc_get(hdc);
    if (!d->dc || !surf_of_dc(d->dc, (SURF __far *)&d->s)) return 0;
    dc_effective_clip(d->dc, (RGNDATA16 __far *)&d->clip);
    {   /* przytnij do powierzchni */
        RECT r;
        r.left = 0; r.top = 0; r.right = d->s.w; r.bottom = d->s.h;
        rgn_and_rect((RGNDATA16 __far *)&d->clip, (RECT __far *)&r);
    }
    d->scr = !d->dc->is_mem;
    return d;
}

/* Kursor przeszkadza tylko, gdy lezy w rysowanym prostokacie (nie w calym clipie DC);
 * kazde draw_excl paruje sie z cursor_restore w draw_end */
static void draw_excl(DRAW *d, int l, int t, int r, int b)
{
    if (d->scr) cursor_exclude(l, t, r, b);
}

static DRAW *draw_begin(HDC hdc)
{
    DRAW *d = draw_open(hdc);
    if (d) draw_excl(d, d->clip.box.left, d->clip.box.top, d->clip.box.right, d->clip.box.bottom);
    return d;
}

static void draw_end(DRAW *d)
{
    if (d && d->scr) cursor_restore();
}

static BOOL in_clip(DRAW *d, int x, int y)
{
    WORD i;
    RGNDATA16 *c = &d->clip;
    if (x < c->box.left || x >= c->box.right || y < c->box.top || y >= c->box.bottom) return FALSE;
    if (c->n == 1) return TRUE;
    for (i = 0; i < c->n; i++)
        if (x >= c->r[i].left && x < c->r[i].right && y >= c->r[i].top && y < c->r[i].bottom)
            return TRUE;
    return FALSE;
}

/* ============================================================
 * Pedzle
 * ============================================================ */
DWORD brush_px(BRUSHOBJ __far *b, DCOBJ __far *dc, int x, int y, BOOL mono)
{
    int px = (x - dc->brx) & 7, py = (y - dc->bry) & 7;
    (void)mono;
    switch (b->style) {
    case BS_SOLID:
        return rgb2px(b->color);
    case BS_HATCHED:
        if ((b->pat[py] >> (7 - px)) & 1) return rgb2px(b->color);
        return dc->bkmode == 2 ? rgb2px(dc->bk) : TRANSP;
    case BS_PATTERN:
    case BS_DIBPATTERN:
        if (b->patmem) return ((DWORD __far *)MK_FP(b->patmem, 0))[py * 8 + px];
        return ((b->pat[py] >> (7 - px)) & 1) ? rgb2px(dc->bk) : rgb2px(dc->text);
    }
    return TRANSP;                    /* BS_NULL */
}

/* Wypelnij prostokat (wsp. powierzchni) pedzlem z ROP3 (wzor = P) */
static void fill_rop3(DRAW *d, RECT __far *r, HBRUSH hb, BYTE rop)
{
    static DWORD g_tile[8][8];
    BRUSHOBJ __far *b = (BRUSHOBJ __far *)obj_ptr(hb, OT_BRUSH);
    BOOL tile_ok = FALSE, has_transp = FALSE;
    int brx = d->dc->brx, bry = d->dc->bry;
    WORD i;
    for (i = 0; i < d->clip.n; i++) {
        RECT o;
        int x, y;
        if (!rect_and(r, (RECT __far *)&d->clip.r[i], (RECT __far *)&o)) continue;
        if (rop == 0xF0 && b && b->style == BS_SOLID) {
            DWORD c = rgb2px(b->color);
            for (y = o.top; y < o.bottom; y++) span_rop2((SURF __far *)&d->s, y, o.left, o.right, c, 13);
            continue;
        }
        if (rop == 0x00 || rop == 0xFF) {
            for (y = o.top; y < o.bottom; y++)
                span_rop2((SURF __far *)&d->s, y, o.left, o.right, rop ? 0xFFFFFFUL : 0, 13);
            continue;
        }
        if (!tile_ok) {
            int ty, tx;
            for (ty = 0; ty < 8; ty++)
                for (tx = 0; tx < 8; tx++) {
                    DWORD p = b ? brush_px(b, d->dc, tx + brx, ty + bry, d->s.mono) : 0;
                    if (p & TRANSP) has_transp = TRUE;
                    else if (d->s.mono) p = mono_of(p);
                    g_tile[ty][tx] = p;
                }
            tile_ok = TRUE;
        }
        for (y = o.top; y < o.bottom; y++) {
            DWORD *row = g_tile[(y - bry) & 7];
            for (x = o.left; x < o.right; ) {
                int n = o.right - x > BLT_LINE ? BLT_LINE : o.right - x, k, ph = x - brx;
                if (rop == 0xF0 && !has_transp) {
                    for (k = 0; k < n; k++) g_ld[k] = row[(ph + k) & 7];
                } else {
                    row_get((SURF __far *)&d->s, x, y, n, g_ld);
                    for (k = 0; k < n; k++) {
                        DWORD p = row[(ph + k) & 7];
                        if (p & TRANSP) continue;
                        g_ld[k] = rop == 0xF0 ? p : rop3(rop, p, 0, g_ld[k]) & 0xFFFFFFUL;
                    }
                }
                row_put((SURF __far *)&d->s, x, y, n, g_ld);
                x += n;
            }
        }
    }
}

/* Wypelnij prostokat pedzlem z ROP2 DC (wnetrze Rectangle/Ellipse) */
static void fill_span_brush(DRAW *d, int y, int x0, int x1, BRUSHOBJ __far *b)
{
    WORD i;
    if (!b || b->style == BS_NULL || x0 >= x1) return;
    for (i = 0; i < d->clip.n; i++) {
        RECT *c = &d->clip.r[i];
        int a, z, x;
        if (y < c->top || y >= c->bottom) continue;
        a = x0 > c->left ? x0 : c->left;
        z = x1 < c->right ? x1 : c->right;
        if (a >= z) continue;
        if (b->style == BS_SOLID) {
            span_rop2((SURF __far *)&d->s, y, a, z, rgb2px(b->color), d->dc->rop2);
            continue;
        }
        for (x = a; x < z; x++) {
            DWORD p = brush_px(b, d->dc, x, y, d->s.mono);
            if (!(p & TRANSP)) span_rop2((SURF __far *)&d->s, y, x, x + 1, p, d->dc->rop2);
        }
    }
}

void fill_rect_brush(DCOBJ __far *dc, SURF __far *s, RECT __far *r, HBRUSH hb, WORD rop)
{
    (void)dc; (void)s;
    fill_rop3(&g_d, r, hb, (BYTE)rop);
}

static void norm_rect(DCOBJ __far *dc, int l, int t, int r, int b, RECT __far *o)
{
    int k;
    dc_lp2dp(dc, (int __far *)&l, (int __far *)&t);
    dc_lp2dp(dc, (int __far *)&r, (int __far *)&b);
    if (l > r) { k = l; l = r; r = k; }
    if (t > b) { k = t; t = b; b = k; }
    o->left = l + dc->orgx; o->right = r + dc->orgx;
    o->top = t + dc->orgy;  o->bottom = b + dc->orgy;
}

/* ordinal 29 */
BOOL API PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop)
{
    DRAW *d = draw_open(hdc);
    RECT r;
    if (!d) return FALSE;
    norm_rect(d->dc, x, y, x + w, y + h, (RECT __far *)&r);
    draw_excl(d, r.left, r.top, r.right, r.bottom);
    fill_rop3(d, (RECT __far *)&r, d->dc->brush, (BYTE)(rop >> 16));
    draw_end(d);
    return TRUE;
}

/* ============================================================
 * BitBlt / StretchBlt
 * ============================================================ */
static BOOL rop_uses_src(BYTE rop) { return ((rop >> 2) ^ rop) & 0x33; }
static BOOL rop_uses_pat(BYTE rop) { return ((rop >> 4) ^ rop) & 0x0F; }

/* Piksel zrodla przekonwertowany do glebi celu (mono <-> kolor jak Windows) */
static DWORD src_px(SURF __far *ss, DCOBJ __far *sdc, int sx, int sy, DCOBJ __far *ddc, BOOL dmono)
{
    DWORD v = px_get(ss, sx, sy);
    if (ss->mono && !dmono)              /* mono -> kolor: 0 = text, 1 = tlo (DC celu) */
        return v ? rgb2px(ddc->bk) : rgb2px(ddc->text);
    if (!ss->mono && dmono)              /* kolor -> mono: kolor tla zrodla = bialy */
        return v == rgb2px(sdc->bk) ? 0xFFFFFFUL : 0;
    return v;
}

/* BitBlt bez skalowania i wzorca: wiersz przez bufor (nakladanie w poziomie bezpieczne) */
static void blt_row(DRAW *d, DRAW *s, int xx, int yy, int sxx, int syy, int n, BYTE rop)
{
    BOOL dmono = d->s.mono, smono = s->s.mono;
    DWORD text = 0, bk = 0;
    if (smono != dmono) {
        text = rgb2px(d->dc->text);
        bk = smono ? rgb2px(d->dc->bk) : rgb2px(s->dc->bk);
    }
    while (n > 0) {
        int k = n > BLT_LINE ? BLT_LINE : n, i;
        row_get((SURF __far *)&s->s, sxx, syy, k, g_ls);
        if (smono && !dmono) {                 /* mono -> kolor: 0 = text, 1 = tlo (DC celu) */
            for (i = 0; i < k; i++) g_ls[i] = g_ls[i] ? bk : text;
        } else if (!smono && dmono) {          /* kolor -> mono: kolor tla zrodla = bialy */
            for (i = 0; i < k; i++) g_ls[i] = g_ls[i] == bk ? 0xFFFFFFUL : 0;
        }
        if (rop != 0xCC) {
            if (rop != 0x33) row_get((SURF __far *)&d->s, xx, yy, k, g_ld);
            switch (rop) {
            case 0x88: for (i = 0; i < k; i++) g_ls[i] &= g_ld[i]; break;
            case 0xEE: for (i = 0; i < k; i++) g_ls[i] |= g_ld[i]; break;
            case 0x66: for (i = 0; i < k; i++) g_ls[i] ^= g_ld[i]; break;
            case 0x33: for (i = 0; i < k; i++) g_ls[i] = ~g_ls[i] & 0xFFFFFFUL; break;
            default:   for (i = 0; i < k; i++) g_ls[i] = rop3(rop, 0, g_ls[i], g_ld[i]) & 0xFFFFFFUL; break;
            }
        }
        row_put((SURF __far *)&d->s, xx, yy, k, g_ls);
        xx += k; sxx += k; n -= k;
    }
}

static BOOL do_blt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy,
                   int sw, int sh, DWORD dwrop)
{
    DRAW *d = draw_open(hd);
    DRAW *s = &g_src;
    BYTE rop = (BYTE)(dwrop >> 16);
    BOOL usesrc = rop_uses_src(rop), usepat = rop_uses_pat(rop);
    BRUSHOBJ __far *b;
    RECT dr;
    WORD i;
    BOOL scr_src = FALSE;
    if (!d) return FALSE;
    b = (BRUSHOBJ __far *)obj_ptr(d->dc->brush, OT_BRUSH);
    if (usesrc) {
        s->dc = dc_get(hs);
        if (!s->dc || !surf_of_dc(s->dc, (SURF __far *)&s->s)) return FALSE;
        dc_lp2dp(s->dc, (int __far *)&sx, (int __far *)&sy);
        sx += s->dc->orgx; sy += s->dc->orgy;
        sw = dc_lenx(s->dc, sw); sh = dc_leny(s->dc, sh);
        scr_src = !s->dc->is_mem;
        if (scr_src) cursor_exclude(sx, sy, sx + sw, sy + sh);
    }
    norm_rect(d->dc, x, y, x + w, y + h, (RECT __far *)&dr);
    draw_excl(d, dr.left, dr.top, dr.right, dr.bottom);
    w = dr.right - dr.left; h = dr.bottom - dr.top;
    if (w <= 0 || h <= 0) goto out;
    if (!usesrc) { sw = w; sh = h; }
    for (i = 0; i < d->clip.n; i++) {
        RECT o;
        int xx, yy, ystart, yend, ystep, xstart, xend, xstep;
        if (!rect_and((RECT __far *)&dr, (RECT __far *)&d->clip.r[i], (RECT __far *)&o)) continue;
        /* kierunek kopiowania dla tej samej powierzchni (nakladanie) */
        ystart = o.top; yend = o.bottom; ystep = 1;
        xstart = o.left; xend = o.right; xstep = 1;
        if (usesrc && s->s.sel == d->s.sel && sw == w && sh == h) {
            if (sy < dr.top) { ystart = o.bottom - 1; yend = o.top - 1; ystep = -1; }
            if (sx < dr.left) { xstart = o.right - 1; xend = o.left - 1; xstep = -1; }
        }
        for (yy = ystart; yy != yend; yy += ystep) {
            int syy = usesrc ? sy + (int)((long)(yy - dr.top) * sh / h) : 0;
            if (usesrc && (syy < 0 || syy >= s->s.h)) continue;
            if (usesrc && !usepat && sw == w && sh == h) {
                int a = o.left, sxx = sx + (a - dr.left), n = o.right - o.left;
                if (sxx < 0) { a -= sxx; n += sxx; sxx = 0; }
                if (sxx + n > s->s.w) n = s->s.w - sxx;
                if (n > 0) blt_row(d, s, a, yy, sxx, syy, n, rop);
                continue;
            }
            for (xx = xstart; xx != xend; xx += xstep) {
                DWORD sv = 0, pv = 0, dv = 0, r;
                if (usesrc) {
                    int sxx = sx + (int)((long)(xx - dr.left) * sw / w);
                    if (sxx < 0 || sxx >= s->s.w) continue;
                    sv = src_px((SURF __far *)&s->s, s->dc, sxx, syy, d->dc, d->s.mono);
                }
                if (usepat) {
                    pv = b ? brush_px(b, d->dc, xx, yy, d->s.mono) : 0;
                    if (pv & TRANSP) pv = rgb2px(d->dc->bk);
                }
                if (rop != 0xCC && rop != 0x33 && rop != 0xF0) dv = px_get((SURF __far *)&d->s, xx, yy);
                r = rop3(rop, pv, sv, dv) & 0xFFFFFFUL;
                px_put((SURF __far *)&d->s, xx, yy, r);
            }
        }
    }
out:
    if (scr_src) cursor_restore();
    draw_end(d);
    return TRUE;
}

/* ordinal 34 */
BOOL API BitBlt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy, DWORD rop)
{
    return do_blt(hd, x, y, w, h, hs, sx, sy, w, h, rop);
}

/* ordinal 35 */
BOOL API StretchBlt(HDC hd, int x, int y, int w, int h, HDC hs, int sx, int sy,
                    int sw, int sh, DWORD rop)
{
    if (w < 0) { x += w; w = -w; sx += sw; sw = -sw; }
    if (h < 0) { y += h; h = -h; sy += sh; sh = -sh; }
    if (sw < 0) { sx += sw + 1; sw = -sw; }      /* lustrzane odbicia - uproszczone */
    if (sh < 0) { sy += sh + 1; sh = -sh; }
    return do_blt(hd, x, y, w, h, hs, sx, sy, sw, sh, rop);
}

/* ============================================================
 * Linie i piora
 * ============================================================ */
static PENOBJ __far *g_pen;
static int g_dashpos;

static BOOL pen_on(void)
{
    static const BYTE dash[] = { 18, 6 }, dot[] = { 3, 3 }, dd[] = { 9, 6, 3, 6 },
                      ddd[] = { 9, 3, 3, 3, 3, 3 };
    const BYTE *p;
    int n, i, pos;
    switch (g_pen->style) {
    case 1: p = dash; n = 2; break;
    case 2: p = dot; n = 2; break;
    case 3: p = dd; n = 4; break;
    case 4: p = ddd; n = 6; break;
    default: return TRUE;
    }
    for (i = 0, pos = 0; i < n; i++) pos += p[i];
    pos = g_dashpos % pos;
    for (i = 0; i < n; i++) {
        if (pos < p[i]) return !(i & 1);
        pos -= p[i];
    }
    return TRUE;
}

static void plot(DRAW *d, int x, int y, DWORD c, WORD rop2)
{
    int w = g_pen ? g_pen->width : 1, a, b;
    if (w <= 1) {
        if (in_clip(d, x, y)) span_rop2((SURF __far *)&d->s, y, x, x + 1, c, rop2);
        return;
    }
    for (b = y - w / 2; b < y - w / 2 + w; b++)
        for (a = x - w / 2; a < x - w / 2 + w; a++)
            if (in_clip(d, a, b)) span_rop2((SURF __far *)&d->s, b, a, a + 1, c, rop2);
}

/* Linia (wsp. powierzchni), bez ostatniego punktu */
static void line_dev(DRAW *d, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, e2;
    DWORD c = rgb2px(g_pen->color), bk = rgb2px(d->dc->bk);
    while (x0 != x1 || y0 != y1) {
        if (pen_on()) plot(d, x0, y0, c, d->dc->rop2);
        else if (d->dc->bkmode == 2) plot(d, x0, y0, bk, d->dc->rop2);
        g_dashpos++;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static BOOL pen_select(DRAW *d)
{
    g_pen = (PENOBJ __far *)obj_ptr(d->dc->pen, OT_PEN);
    g_dashpos = 0;
    return g_pen && g_pen->style != 5;
}

static void lp2surf(DCOBJ __far *dc, int __far *x, int __far *y)
{
    dc_lp2dp(dc, x, y);
    *x += dc->orgx; *y += dc->orgy;
}

/* ordinal 19 */
BOOL API LineTo(HDC hdc, int x, int y)
{
    DRAW *d = draw_begin(hdc);
    int x0, y0, x1 = x, y1 = y;
    if (!d) return FALSE;
    x0 = d->dc->curx; y0 = d->dc->cury;
    d->dc->curx = x; d->dc->cury = y;
    if (pen_select(d)) {
        lp2surf(d->dc, (int __far *)&x0, (int __far *)&y0);
        lp2surf(d->dc, (int __far *)&x1, (int __far *)&y1);
        line_dev(d, x0, y0, x1, y1);
    }
    draw_end(d);
    return TRUE;
}

/* ordinal 37 */
BOOL API Polyline(HDC hdc, LPPOINT p, int n)
{
    DRAW *d = draw_begin(hdc);
    int i;
    if (!d) return FALSE;
    if (n >= 2 && pen_select(d)) {
        for (i = 0; i + 1 < n; i++) {
            int x0 = p[i].x, y0 = p[i].y, x1 = p[i + 1].x, y1 = p[i + 1].y;
            lp2surf(d->dc, (int __far *)&x0, (int __far *)&y0);
            lp2surf(d->dc, (int __far *)&x1, (int __far *)&y1);
            line_dev(d, x0, y0, x1, y1);
        }
    }
    draw_end(d);
    return TRUE;
}

/* ordinal 31 */
COLORREF API SetPixel(HDC hdc, int x, int y, COLORREF c)
{
    DRAW *d = draw_open(hdc);
    if (!d) return (COLORREF)-1;
    lp2surf(d->dc, (int __far *)&x, (int __far *)&y);
    draw_excl(d, x, y, x + 1, y + 1);
    if (in_clip(d, x, y)) px_put((SURF __far *)&d->s, x, y, rgb2px(c));
    draw_end(d);
    return c;
}

/* ordinal 83 */
COLORREF API GetPixel(HDC hdc, int x, int y)
{
    DRAW *d = draw_open(hdc);
    COLORREF r = (COLORREF)-1;
    if (!d) return r;
    lp2surf(d->dc, (int __far *)&x, (int __far *)&y);
    draw_excl(d, x, y, x + 1, y + 1);
    if (x >= 0 && y >= 0 && x < d->s.w && y < d->s.h) r = px2rgb(px_get((SURF __far *)&d->s, x, y));
    draw_end(d);
    return r;
}

/* ============================================================
 * Ksztalty
 * ============================================================ */
static void outline_rect(DRAW *d, int l, int t, int r, int b)
{
    line_dev(d, l, t, r - 1, t);
    line_dev(d, r - 1, t, r - 1, b - 1);
    line_dev(d, r - 1, b - 1, l, b - 1);
    line_dev(d, l, b - 1, l, t);
}

/* ordinal 27 */
BOOL API Rectangle(HDC hdc, int l, int t, int r, int b)
{
    DRAW *d = draw_begin(hdc);
    RECT rc;
    BRUSHOBJ __far *br;
    int y, pw;
    if (!d) return FALSE;
    norm_rect(d->dc, l, t, r, b, (RECT __far *)&rc);
    br = (BRUSHOBJ __far *)obj_ptr(d->dc->brush, OT_BRUSH);
    pw = pen_select(d) ? g_pen->width : 0;
    if (pw > 1 && g_pen->style == 6) {           /* PS_INSIDEFRAME */
        rc.left += pw / 2; rc.top += pw / 2; rc.right -= (pw - 1) / 2; rc.bottom -= (pw - 1) / 2;
    }
    for (y = rc.top + (pw ? 1 : 0); y < rc.bottom - (pw ? 1 : 0); y++)
        fill_span_brush(d, y, rc.left + (pw ? 1 : 0), rc.right - (pw ? 1 : 0), br);
    if (pw) outline_rect(d, rc.left, rc.top, rc.right, rc.bottom);
    draw_end(d);
    return TRUE;
}

/* Elipsa wpisana w [l,r)x[t,b): pasy wypelnienia + punkty obrysu.
 * Dla kazdego wiersza wyznacz zakres x (rownanie elipsy). */
static void ellipse_dev(DRAW *d, int l, int t, int r, int b, BOOL fill, BOOL outline)
{
    long a2 = (long)(r - l) * (r - l), b2 = (long)(b - t) * (b - t);
    long cx2 = (long)l + r - 1, cy2 = (long)t + b - 1;
    BRUSHOBJ __far *br = (BRUSHOBJ __far *)obj_ptr(d->dc->brush, OT_BRUSH);
    DWORD pc = g_pen ? rgb2px(g_pen->color) : 0;
    int y, prev0 = 0, prev1 = 0, first = 1;
    if (r - l < 1 || b - t < 1) return;
    for (y = t; y < b; y++) {
        long dy = 2L * y - cy2, q, w = 0;
        int x0, x1;
        if (b2 == 0) break;
        q = a2 - (a2 / b2) * dy * dy - (a2 % b2) * dy * dy / b2;   /* (2dx)^2 */
        if (q < 0) q = 0;
        while ((w + 1) * (w + 1) <= q) w++;
        x0 = (int)((cx2 - w + 1) / 2);
        x1 = (int)((cx2 + w) / 2) + 1;
        if (fill) fill_span_brush(d, y, x0 + (outline ? 1 : 0), x1 - (outline ? 1 : 0), br);
        if (outline) {
            int a, z;
            /* obrys: krawedzie wiersza + uzupelnienie przerw wzgledem poprzedniego */
            if (first || y == b - 1) {
                for (a = x0; a < x1; a++) plot(d, a, y, pc, d->dc->rop2);
            } else {
                z = prev0 < x0 ? prev0 : x0;
                for (a = (x0 < prev0 ? x0 : prev0); a <= (x0 > prev0 ? x0 : prev0) && a < x1; a++)
                    plot(d, a, y, pc, d->dc->rop2);
                for (a = (x1 > prev1 ? x1 : prev1) - 1; a >= (x1 < prev1 ? x1 : prev1) - 1 && a >= x0; a--)
                    plot(d, a, y, pc, d->dc->rop2);
                (void)z;
            }
        }
        prev0 = x0; prev1 = x1; first = 0;
    }
}

/* ordinal 24 */
BOOL API Ellipse(HDC hdc, int l, int t, int r, int b)
{
    DRAW *d = draw_begin(hdc);
    RECT rc;
    BOOL pen;
    if (!d) return FALSE;
    norm_rect(d->dc, l, t, r, b, (RECT __far *)&rc);
    pen = pen_select(d);
    ellipse_dev(d, rc.left, rc.top, rc.right, rc.bottom, TRUE, pen);
    draw_end(d);
    return TRUE;
}

/* ordinal 28: zaokraglony prostokat - rogi jako cwiartki elipsy (uproszczenie: prostokat) */
BOOL API RoundRect(HDC hdc, int l, int t, int r, int b, int ew, int eh)
{
    (void)ew; (void)eh;
    return Rectangle(hdc, l, t, r, b);
}

/* ordinal 26/23/30: Pie / Arc / Chord - przyblizone pelna elipsa / obrysem */
BOOL API Pie(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    (void)x1; (void)y1; (void)x2; (void)y2;
    return Ellipse(hdc, l, t, r, b);
}
BOOL API Chord(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    (void)x1; (void)y1; (void)x2; (void)y2;
    return Ellipse(hdc, l, t, r, b);
}
BOOL API Arc(HDC hdc, int l, int t, int r, int b, int x1, int y1, int x2, int y2)
{
    DRAW *d = draw_begin(hdc);
    RECT rc;
    (void)x1; (void)y1; (void)x2; (void)y2;
    if (!d) return FALSE;
    norm_rect(d->dc, l, t, r, b, (RECT __far *)&rc);
    if (pen_select(d)) ellipse_dev(d, rc.left, rc.top, rc.right, rc.bottom, FALSE, TRUE);
    draw_end(d);
    return TRUE;
}

/* ============================================================
 * Wielokaty (scanline, ALTERNATE/WINDING)
 * ============================================================ */
#define MAXPT 256
static POINT g_pts[MAXPT];

static void poly_fill(DRAW *d, POINT *p, int n)
{
    BRUSHOBJ __far *br = (BRUSHOBJ __far *)obj_ptr(d->dc->brush, OT_BRUSH);
    int ymin = 32767, ymax = -32768, i, y;
    static int xs[MAXPT];
    if (!br || br->style == BS_NULL || n < 3) return;
    for (i = 0; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    if (ymin < d->clip.box.top) ymin = d->clip.box.top;
    if (ymax > d->clip.box.bottom) ymax = d->clip.box.bottom;
    for (y = ymin; y < ymax; y++) {
        int k = 0, a, bb;
        for (i = 0; i < n; i++) {
            POINT *u = &p[i], *v = &p[(i + 1) % n];
            if ((u->y <= y && v->y > y) || (v->y <= y && u->y > y))
                xs[k++] = u->x + (int)((long)(y - u->y) * (v->x - u->x) / (v->y - u->y));
        }
        for (a = 1; a < k; a++) {                 /* sortowanie przez wstawianie */
            int t = xs[a];
            for (bb = a - 1; bb >= 0 && xs[bb] > t; bb--) xs[bb + 1] = xs[bb];
            xs[bb + 1] = t;
        }
        for (a = 0; a + 1 < k; a += 2) fill_span_brush(d, y, xs[a], xs[a + 1], br);
    }
}

static int poly_dev(DCOBJ __far *dc, LPPOINT p, int n)
{
    int i;
    if (n > MAXPT) n = MAXPT;
    for (i = 0; i < n; i++) {
        g_pts[i] = p[i];
        lp2surf(dc, (int __far *)&g_pts[i].x, (int __far *)&g_pts[i].y);
    }
    return n;
}

/* ordinal 36 */
BOOL API Polygon(HDC hdc, LPPOINT p, int n)
{
    DRAW *d = draw_begin(hdc);
    int i;
    if (!d) return FALSE;
    n = poly_dev(d->dc, p, n);
    poly_fill(d, g_pts, n);
    if (pen_select(d))
        for (i = 0; i < n; i++)
            line_dev(d, g_pts[i].x, g_pts[i].y, g_pts[(i + 1) % n].x, g_pts[(i + 1) % n].y);
    draw_end(d);
    return TRUE;
}

/* ordinal 450 */
BOOL API PolyPolygon(HDC hdc, LPPOINT p, int __far *counts, int npoly)
{
    int i;
    for (i = 0; i < npoly; i++) { Polygon(hdc, p, counts[i]); p += counts[i]; }
    return TRUE;
}

/* ============================================================
 * FloodFill (ordinal 25) / ExtFloodFill (ordinal 372)
 * ============================================================ */
#define FSTACK 1024
static struct { int x, y; } g_fs[FSTACK];

BOOL API ExtFloodFill(HDC hdc, int x, int y, COLORREF c, WORD type)
{
    DRAW *d = draw_begin(hdc);
    BRUSHOBJ __far *br;
    DWORD ref, fill;
    int sp = 0;
    if (!d) return FALSE;
    br = (BRUSHOBJ __far *)obj_ptr(d->dc->brush, OT_BRUSH);
    lp2surf(d->dc, (int __far *)&x, (int __far *)&y);
    if (!br || !in_clip(d, x, y)) { draw_end(d); return FALSE; }
    ref = rgb2px(c);
    fill = brush_px(br, d->dc, x, y, d->s.mono) & 0xFFFFFFUL;
    /* FLOODFILLBORDER (0): do koloru granicy; FLOODFILLSURFACE (1): obszar koloru c */
#define INSIDE(px) (type ? (px) == ref : (px) != ref)
    if (!INSIDE(px_get((SURF __far *)&d->s, x, y)) || px_get((SURF __far *)&d->s, x, y) == fill) {
        draw_end(d); return FALSE;
    }
    g_fs[sp].x = x; g_fs[sp].y = y; sp++;
    while (sp) {
        int l, r, yy, cx;
        sp--;
        cx = g_fs[sp].x; yy = g_fs[sp].y;
        if (!in_clip(d, cx, yy) || !INSIDE(px_get((SURF __far *)&d->s, cx, yy))) continue;
        l = cx; r = cx;
        while (l > d->clip.box.left && in_clip(d, l - 1, yy) && INSIDE(px_get((SURF __far *)&d->s, l - 1, yy))) l--;
        while (r + 1 < d->clip.box.right && in_clip(d, r + 1, yy) && INSIDE(px_get((SURF __far *)&d->s, r + 1, yy))) r++;
        span_rop2((SURF __far *)&d->s, yy, l, r + 1, fill, 13);
        if (fill == ref && type) continue;        /* zabezpieczenie przed petla */
        for (cx = l; cx <= r && sp < FSTACK - 2; cx++) {
            if (yy > 0) { g_fs[sp].x = cx; g_fs[sp].y = yy - 1; sp++; }
            if (yy + 1 < d->s.h) { g_fs[sp].x = cx; g_fs[sp].y = yy + 1; sp++; }
        }
    }
#undef INSIDE
    draw_end(d);
    return TRUE;
}

BOOL API FloodFill(HDC hdc, int x, int y, COLORREF c) { return ExtFloodFill(hdc, x, y, c, 0); }

/* ============================================================
 * Regiony: FillRgn (40), FrameRgn (41), InvertRgn (42), PaintRgn (43)
 * ============================================================ */
static BOOL rgn_op(HDC hdc, HRGN hr, HBRUSH hb, BYTE rop)
{
    DRAW *d;
    RGNDATA16 __far *g = rgn_ptr(hr);
    WORD i;
    if (!g) return FALSE;
    d = draw_begin(hdc);
    if (!d) return FALSE;
    for (i = 0; i < g->n; i++) {
        RECT r = g->r[i];
        r.left += d->dc->orgx; r.right += d->dc->orgx;
        r.top += d->dc->orgy;  r.bottom += d->dc->orgy;
        fill_rop3(d, (RECT __far *)&r, hb, rop);
    }
    draw_end(d);
    return TRUE;
}

BOOL API FillRgn(HDC hdc, HRGN hr, HBRUSH hb)  { return rgn_op(hdc, hr, hb, 0xF0); }
BOOL API PaintRgn(HDC hdc, HRGN hr)
{
    DCOBJ __far *dc = dc_get(hdc);
    return dc ? rgn_op(hdc, hr, dc->brush, 0xF0) : FALSE;
}
BOOL API InvertRgn(HDC hdc, HRGN hr)           { return rgn_op(hdc, hr, 0, 0x55); }
BOOL API FrameRgn(HDC hdc, HRGN hr, HBRUSH hb, int w, int h)
{
    RGNDATA16 __far *g = rgn_ptr(hr);
    DCOBJ __far *dc = dc_get(hdc);
    HBRUSH old;
    if (!g || !dc) return FALSE;
    old = dc->brush;
    dc->brush = hb;
    PatBlt(hdc, g->box.left, g->box.top, g->box.right - g->box.left, h, 0xF00021UL);
    PatBlt(hdc, g->box.left, g->box.bottom - h, g->box.right - g->box.left, h, 0xF00021UL);
    PatBlt(hdc, g->box.left, g->box.top, w, g->box.bottom - g->box.top, 0xF00021UL);
    PatBlt(hdc, g->box.right - w, g->box.top, w, g->box.bottom - g->box.top, 0xF00021UL);
    dc->brush = old;
    return TRUE;
}
