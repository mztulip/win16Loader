/* gtext.c - GDI: fonty bitmapowe (.FON / FNT 2.0-3.0) i tekst
 *
 * Pliki .FON (np. z Wine: SSERIFE, VGASYS, VGAFIX, COURE, SMALLE) loader
 * wczytuje jako moduly z zasobami (katalog systemowy); tu znajdujemy je przez
 * LoadLibrary + FindResource(RT_FONT) i czytamy naglowki FNT.
 */
#include "gdi.h"

#define MAX_FNT 24

typedef struct {
    WORD  sel;                 /* LoadResource: dane FNT */
    WORD  ver;                 /* 0x200 / 0x300 */
    char  face[32];
    int   height, ascent, intlead, extlead, avgw, maxw, pixw, weight, points;
    BYTE  italic, charset, pitch, first, last, defch, breakch;
    WORD  ctab;                /* offset tablicy znakow */
} FNT;

static FNT g_fnt[MAX_FNT];
static int g_nfnt;

static WORD rw(BYTE __far *p, WORD o) { return (WORD)(p[o] | ((WORD)p[o + 1] << 8)); }
static DWORD rd(BYTE __far *p, WORD o) { return rw(p, o) | ((DWORD)rw(p, o + 2) << 16); }

static int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int fstricmp(const char __far *a, const char __far *b)
{
    while (*a && upc(*a) == upc(*b)) { a++; b++; }
    return upc(*a) - upc(*b);
}

static void load_fon(LPCSTR mod)
{
    WORD hi = LoadLibrary(mod), ids[16], nid = 0, i;
    BYTE __far *ne, __far *rt;
    if (hi < 32) return;
    /* Identyfikatory RT_FONT z tablicy zasobow NE (hModule = selektor kopii naglowka NE) */
    ne = (BYTE __far *)MK_FP(hi, 0);
    if (ne[0] != 'N' || ne[1] != 'E') return;
    rt = ne + rw(ne, 0x24) + 2;
    while (rw(rt, 0)) {
        WORD type = rw(rt, 0), cnt = rw(rt, 2);
        for (i = 0; i < cnt; i++)
            if (type == 0x8008 && nid < 16 && (rw(rt, 8 + i * 12 + 6) & 0x8000))
                ids[nid++] = rw(rt, 8 + i * 12 + 6) & 0x7FFF;
        rt += 8 + cnt * 12;
    }
    for (i = 0; i < nid && g_nfnt < MAX_FNT; i++) {
        WORD hr = FindResource(hi, (LPCSTR)MK_FP(0, ids[i]), (LPCSTR)MK_FP(0, 8));
        WORD h;
        BYTE __far *p;
        FNT *f;
        DWORD faceoff;
        int k;
        if (!hr) continue;
        h = LoadResource(hi, hr);
        p = (BYTE __far *)LockResource(h);
        if (!p) continue;
        f = &g_fnt[g_nfnt];
        f->sel = h;
        f->ver = rw(p, 0);
        f->points = rw(p, 68);
        f->ascent = rw(p, 74);
        f->intlead = rw(p, 76);
        f->extlead = rw(p, 78);
        f->italic = p[80];
        f->weight = rw(p, 83);
        f->charset = p[85];
        f->pixw = rw(p, 86);
        f->height = rw(p, 88);
        f->pitch = p[90];
        f->avgw = rw(p, 91);
        f->maxw = rw(p, 93);
        f->first = p[95];
        f->last = p[96];
        f->defch = p[97];
        f->breakch = p[98];
        faceoff = rd(p, 105);
        for (k = 0; k < 31 && p[(WORD)faceoff + k]; k++) f->face[k] = p[(WORD)faceoff + k];
        f->face[k] = 0;
        f->ctab = f->ver >= 0x300 ? 148 : 118;
        gdbg("GDI: font '"); gdbg((LPCSTR)(char __far *)f->face); gdbg("' h=");
        gdbg_hex(f->height, 2); gdbg(" w="); gdbg_hex(f->weight, 3); gdbg("\n");
        g_nfnt++;
    }
}

void font_init(void)
{
    static const char * const files[] = { "SSERIFE", "VGASYS", "VGAFIX", "COURE", "SMALLE",
                                          "SSERIFF", "SERIFE", "VGAOEM", 0 };
    int i;
    if (g_nfnt) return;
    for (i = 0; files[i]; i++) load_fon((LPCSTR)files[i]);
    if (!g_nfnt) gdbg("GDI: UWAGA - brak fontow .FON (tekst nie bedzie rysowany)\n");
}

/* Aliasy: nazwa z LOGFONT -> nazwa fontu bitmapowego */
static const char __far *alias(const char __far *n, BYTE pitch)
{
    static const char * const map[][2] = {
        {"ARIAL", "MS Sans Serif"}, {"HELV", "MS Sans Serif"}, {"HELVETICA", "MS Sans Serif"},
        {"MS SHELL DLG", "MS Sans Serif"}, {"TAHOMA", "MS Sans Serif"},
        {"TIMES NEW ROMAN", "MS Serif"}, {"TMS RMN", "MS Serif"}, {"TIMES", "MS Serif"},
        {"COURIER NEW", "Courier"}, {"TERMINAL", "Fixedsys"}, {"SMALL FONTS", "Small Fonts"},
        {0, 0}
    };
    int i;
    if (!n || !n[0]) return (pitch & 3) == 1 ? "Courier" : "MS Sans Serif";
    for (i = 0; map[i][0]; i++) if (!fstricmp(n, map[i][0])) return map[i][1];
    return n;
}

int font_match(LOGFONT __far *lf)
{
    const char __far *want = alias(lf->lfFaceName, lf->lfPitchAndFamily);
    int i, best = -1, bestscore = 32767, h = lf->lfHeight, pass;
    if (!g_nfnt) return -1;
    for (pass = 0; pass < 3 && best < 0; pass++) {
        for (i = 0; i < g_nfnt; i++) {
            FNT *f = &g_fnt[i];
            int fh, d, score;
            if (pass == 0 && fstricmp((const char __far *)f->face, want)) continue;
            if (pass == 1 && fstricmp((const char __far *)f->face,
                                      (lf->lfPitchAndFamily & 3) == 1 ? "Courier" : "MS Sans Serif"))
                continue;
            if (h < 0) fh = f->height - f->intlead;        /* wysokosc znaku (em) */
            else fh = f->height;                           /* wysokosc komorki */
            d = h == 0 ? (f->height == 13 || f->height == 16 ? 0 : 4) : (fh - (h < 0 ? -h : h));
            score = (d < 0 ? -d * 3 : d * 2);              /* wieksze lepsze niz mniejsze */
            if ((lf->lfWeight >= 600) != (f->weight >= 600)) score += 1;
            if (score < bestscore) { bestscore = score; best = i; }
        }
    }
    return best < 0 ? 0 : best;
}

/* ============================================================
 * Glify
 * ============================================================ */
static BYTE __far *g_fbits;
static FNT *g_f;

static int glyph(BYTE ch, WORD __far *off)
{
    WORD idx;
    BYTE __far *e;
    if (ch < g_f->first || ch > g_f->last) ch = (BYTE)(g_f->first + g_f->defch);
    if (ch < g_f->first || ch > g_f->last) ch = g_f->first;
    idx = ch - g_f->first;
    if (g_f->ver >= 0x300) {
        e = g_fbits + g_f->ctab + idx * 6;
        *off = (WORD)rd(e, 2);
    } else {
        e = g_fbits + g_f->ctab + idx * 4;
        *off = rw(e, 2);
    }
    return rw(e, 0);
}

typedef struct {
    FNT  *f;
    BOOL bold, italic, under, strike;
} FONTSEL;

static BOOL font_of_dc(DCOBJ __far *dc, FONTSEL __far *fs)
{
    FONTOBJ __far *fo = (FONTOBJ __far *)obj_ptr(dc->font, OT_FONT);
    int idx;
    if (!g_nfnt) return FALSE;
    idx = (fo && fo->face >= 0) ? fo->face : 0;
    fs->f = &g_fnt[idx];
    fs->bold = fo && fo->lf.lfWeight >= 600 && fs->f->weight < 600;
    fs->italic = fo && fo->lf.lfItalic && !fs->f->italic;
    fs->under = fo && fo->lf.lfUnderline;
    fs->strike = fo && fo->lf.lfStrikeOut;
    g_f = fs->f;
    g_fbits = (BYTE __far *)MK_FP(fs->f->sel, 0);
    return TRUE;
}

static int char_w(FONTSEL __far *fs, BYTE ch)
{
    WORD off;
    return glyph(ch, (WORD __far *)&off) + (fs->bold ? 1 : 0);
}

static int text_width(FONTSEL __far *fs, DCOBJ __far *dc, LPCSTR s, int n, int __far *dx)
{
    int w = 0, i;
    for (i = 0; i < n; i++) w += dx ? dx[i] : char_w(fs, (BYTE)s[i]) + dc->charextra;
    return w;
}

/* ============================================================
 * Rysowanie tekstu
 * ============================================================ */
typedef struct { DCOBJ __far *dc; SURF s; RGNDATA16 clip; } TDRAW;
static TDRAW g_t;

static BOOL t_in(int x, int y)
{
    WORD i;
    RGNDATA16 *c = &g_t.clip;
    if (x < c->box.left || x >= c->box.right || y < c->box.top || y >= c->box.bottom) return FALSE;
    if (c->n == 1) return TRUE;
    for (i = 0; i < c->n; i++)
        if (x >= c->r[i].left && x < c->r[i].right && y >= c->r[i].top && y < c->r[i].bottom) return TRUE;
    return FALSE;
}

static void t_fill(RECT __far *r, DWORD c)
{
    WORD i;
    for (i = 0; i < g_t.clip.n; i++) {
        RECT o;
        int y;
        if (!rect_and(r, (RECT __far *)&g_t.clip.r[i], (RECT __far *)&o)) continue;
        for (y = o.top; y < o.bottom; y++) span_rop2((SURF __far *)&g_t.s, y, o.left, o.right, c, 13);
    }
}

static void draw_glyph(FONTSEL __far *fs, int x, int y, BYTE ch, DWORD fg)
{
    WORD off, i;
    int w = glyph(ch, (WORD __far *)&off), h = fs->f->height, cy, pass;
    RGNDATA16 *c = &g_t.clip;
    for (pass = 0; pass < (fs->bold ? 2 : 1); pass++)
        for (cy = 0; cy < h; cy++) {
            int py = y + cy, px = x + pass + (fs->italic ? (h - cy) / 3 : 0);
            if (py < c->box.top || py >= c->box.bottom) continue;
            for (i = 0; i < c->n; i++) {
                RECT *r = &c->r[i];
                int xa, xb;
                if (py < r->top || py >= r->bottom) continue;
                xa = px > r->left ? px : r->left;
                xb = px + w < r->right ? px + w : r->right;
                if (xa < xb) bits_row((SURF __far *)&g_t.s, py, px, xa, xb, g_fbits + off + cy, h, fg);
            }
        }
}

#define ETO_OPAQUE  2
#define ETO_CLIPPED 4

/* ordinal 351 */
BOOL API ExtTextOut(HDC hdc, int x, int y, WORD opt, LPRECT rc, LPCSTR s, WORD n, int __far *dx)
{
    DCOBJ __far *dc = dc_get(hdc);
    FONTSEL fs;
    RECT box, orc;
    int w, h, i, cx, ty;
    DWORD fg, bg;
    if (!dc || !surf_of_dc(dc, (SURF __far *)&g_t.s)) return FALSE;
    g_t.dc = dc;
    dc_effective_clip(dc, (RGNDATA16 __far *)&g_t.clip);
    {
        RECT r;
        r.left = 0; r.top = 0; r.right = g_t.s.w; r.bottom = g_t.s.h;
        rgn_and_rect((RGNDATA16 __far *)&g_t.clip, (RECT __far *)&r);
    }
    if (dc->align & 1) { x = dc->curx; y = dc->cury; }          /* TA_UPDATECP */
    dc_lp2dp(dc, (int __far *)&x, (int __far *)&y);
    x += dc->orgx; y += dc->orgy;
    if (rc) {
        int l = rc->left, t = rc->top, r = rc->right, b = rc->bottom;
        dc_lp2dp(dc, (int __far *)&l, (int __far *)&t);
        dc_lp2dp(dc, (int __far *)&r, (int __far *)&b);
        orc.left = l + dc->orgx; orc.top = t + dc->orgy;
        orc.right = r + dc->orgx; orc.bottom = b + dc->orgy;
        if (opt & ETO_CLIPPED) rgn_and_rect((RGNDATA16 __far *)&g_t.clip, (RECT __far *)&orc);
    }
    fg = rgb2px(dc->text);
    bg = rgb2px(dc->bk);
    if (!font_of_dc(dc, &fs) || !s || !n) {
        if (rc && (opt & ETO_OPAQUE)) {
            if (!g_t.dc->is_mem) cursor_exclude(orc.left, orc.top, orc.right, orc.bottom);
            t_fill((RECT __far *)&orc, bg);
            if (!g_t.dc->is_mem) cursor_restore();
        }
        return TRUE;
    }
    w = text_width(&fs, dc, s, n, dx);
    h = fs.f->height;
    /* wyrownanie poziome: TA_RIGHT=2, TA_CENTER=6; pionowe: TA_BOTTOM=8, TA_BASELINE=24 */
    if ((dc->align & 6) == 6) x -= w / 2;
    else if (dc->align & 2) x -= w;
    if ((dc->align & 24) == 24) ty = y - fs.f->ascent;
    else if (dc->align & 8) ty = y - h;
    else ty = y;
    box.left = x; box.top = ty; box.right = x + w; box.bottom = ty + h;
    if (!g_t.dc->is_mem) {                     /* z zapasem: kursywa, pogrubienie, dx[], podkreslenie */
        RECT e;
        e.left = x - fs.f->maxw; e.top = ty - 1;
        e.right = x + w + h / 3 + 2 + fs.f->maxw; e.bottom = ty + h + 2;
        if (rc && (opt & ETO_OPAQUE)) {
            if (orc.left < e.left) e.left = orc.left;
            if (orc.top < e.top) e.top = orc.top;
            if (orc.right > e.right) e.right = orc.right;
            if (orc.bottom > e.bottom) e.bottom = orc.bottom;
        }
        cursor_exclude(e.left, e.top, e.right, e.bottom);
    }
    if (rc && (opt & ETO_OPAQUE)) t_fill((RECT __far *)&orc, bg);
    if (dc->bkmode == 2 && !(rc && (opt & ETO_OPAQUE))) t_fill((RECT __far *)&box, bg);
    for (i = 0, cx = x; i < (int)n; i++) {
        draw_glyph(&fs, cx, ty, (BYTE)s[i], fg);
        cx += dx ? dx[i] : char_w(&fs, (BYTE)s[i]) + dc->charextra;
    }
    if (fs.under || fs.strike) {
        int ly = fs.under ? ty + fs.f->ascent + 1 : ty + fs.f->ascent * 2 / 3, xx;
        for (xx = x; xx < x + w; xx++) if (t_in(xx, ly)) px_put((SURF __far *)&g_t.s, xx, ly, fg);
    }
    if (dc->align & 1) {
        int dx2 = w, dy2 = 0;
        if (dc->mapmode != 1 && dc->vex) dx2 = (int)((long)w * dc->wex / dc->vex);
        dc->curx += dx2; dc->cury += dy2;
    }
    if (!g_t.dc->is_mem) cursor_restore();
    return TRUE;
}

/* ordinal 33 */
BOOL API TextOut(HDC hdc, int x, int y, LPCSTR s, int n)
{
    return ExtTextOut(hdc, x, y, 0, 0, s, (WORD)n, 0);
}

/* ordinal 91 */
DWORD API GetTextExtent(HDC hdc, LPCSTR s, int n)
{
    DCOBJ __far *dc = dc_get(hdc);
    FONTSEL fs;
    int w, h;
    if (!dc || !font_of_dc(dc, &fs)) return 0;
    w = text_width(&fs, dc, s, n, 0);
    h = fs.f->height;
    if (dc->mapmode != 1) {
        if (dc->vex) w = (int)((long)w * dc->wex / dc->vex);
        if (dc->vey) h = (int)((long)h * dc->wey / dc->vey);
        if (w < 0) w = -w;
        if (h < 0) h = -h;
    }
    return MAKELONG(w, h);
}

/* ordinal 471 */
BOOL API GetTextExtentPoint(HDC hdc, LPCSTR s, int n, LPPOINT sz)
{
    DWORD e = GetTextExtent(hdc, s, n);
    sz->x = (int)LOWORD(e); sz->y = (int)HIWORD(e);
    return TRUE;
}

typedef struct {
    int  tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    int  tmAveCharWidth, tmMaxCharWidth, tmWeight;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmFirstChar, tmLastChar;
    BYTE tmDefaultChar, tmBreakChar, tmPitchAndFamily, tmCharSet;
    int  tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
} TEXTMETRIC;

static void fill_tm(FONTSEL __far *fs, FONTOBJ __far *fo, TEXTMETRIC __far *tm)
{
    FNT *f = fs->f;
    tm->tmHeight = f->height;
    tm->tmAscent = f->ascent;
    tm->tmDescent = f->height - f->ascent;
    tm->tmInternalLeading = f->intlead;
    tm->tmExternalLeading = f->extlead;
    tm->tmAveCharWidth = f->avgw + (fs->bold ? 1 : 0);
    tm->tmMaxCharWidth = f->maxw + (fs->bold ? 1 : 0);
    tm->tmWeight = fs->bold ? 700 : f->weight;
    tm->tmItalic = (BYTE)(f->italic || fs->italic);
    tm->tmUnderlined = (BYTE)fs->under;
    tm->tmStruckOut = (BYTE)fs->strike;
    tm->tmFirstChar = f->first;
    tm->tmLastChar = f->last;
    tm->tmDefaultChar = (BYTE)(f->first + f->defch);
    tm->tmBreakChar = (BYTE)(f->first + f->breakch);
    tm->tmPitchAndFamily = f->pitch;
    tm->tmCharSet = f->charset;
    tm->tmOverhang = fs->bold ? 1 : 0;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    (void)fo;
}

/* ordinal 93 */
BOOL API GetTextMetrics(HDC hdc, TEXTMETRIC __far *tm)
{
    DCOBJ __far *dc = dc_get(hdc);
    FONTSEL fs;
    if (!dc || !font_of_dc(dc, &fs)) return FALSE;
    fill_tm(&fs, (FONTOBJ __far *)obj_ptr(dc->font, OT_FONT), tm);
    return TRUE;
}

/* ordinal 92 */
int API GetTextFace(HDC hdc, int n, LPSTR buf)
{
    DCOBJ __far *dc = dc_get(hdc);
    FONTSEL fs;
    int i;
    if (!dc || !font_of_dc(dc, &fs) || n <= 0) return 0;
    for (i = 0; i < n - 1 && fs.f->face[i]; i++) buf[i] = fs.f->face[i];
    buf[i] = 0;
    return i;
}

/* ordinal 350 */
BOOL API GetCharWidth(HDC hdc, WORD first, WORD last, int __far *buf)
{
    DCOBJ __far *dc = dc_get(hdc);
    FONTSEL fs;
    WORD c;
    if (!dc || !font_of_dc(dc, &fs)) return FALSE;
    for (c = first; c <= last; c++) *buf++ = char_w(&fs, (BYTE)c);
    return TRUE;
}

/* ordinal 70: EnumFonts(hdc, face, proc, data) - po jednym na nazwe (face=NULL)
 * albo wszystkie rozmiary danej nazwy */
typedef int (__far __pascal *FONTENUMPROC)(LOGFONT __far *, TEXTMETRIC __far *, int, DWORD);

int API EnumFonts(HDC hdc, LPCSTR face, FONTENUMPROC proc, DWORD data)
{
    static LOGFONT lf;
    static TEXTMETRIC tm;
    int i, j, r = 1;
    (void)hdc;
    font_init();
    for (i = 0; i < g_nfnt && r; i++) {
        FNT *f = &g_fnt[i];
        FONTSEL fs;
        BYTE *p;
        if (face && HIWORD(face)) {
            if (fstricmp((const char __far *)f->face, face)) continue;
        } else {
            for (j = 0; j < i; j++) if (!fstricmp((const char __far *)g_fnt[j].face, (const char __far *)f->face)) break;
            if (j < i) continue;
        }
        for (p = (BYTE *)&lf; p < (BYTE *)&lf + sizeof(lf); p++) *p = 0;
        lf.lfHeight = f->height; lf.lfWidth = f->avgw; lf.lfWeight = f->weight;
        lf.lfItalic = f->italic; lf.lfCharSet = f->charset; lf.lfPitchAndFamily = f->pitch;
        for (j = 0; j < 31 && f->face[j]; j++) lf.lfFaceName[j] = f->face[j];
        fs.f = f; fs.bold = fs.italic = fs.under = fs.strike = FALSE;
        fill_tm(&fs, 0, (TEXTMETRIC __far *)&tm);
        r = proc((LOGFONT __far *)&lf, (TEXTMETRIC __far *)&tm, 1 /* RASTER_FONTTYPE */, data);
    }
    return r;
}

/* ordinal 330 */
int API EnumFontFamilies(HDC hdc, LPCSTR face, FONTENUMPROC proc, DWORD data)
{
    return EnumFonts(hdc, face, proc, data);
}

/* ordinal 119 / 136 */
int  API AddFontResource(LPCSTR f)    { (void)f; return 0; }
BOOL API RemoveFontResource(LPCSTR f) { (void)f; return FALSE; }

/* ordinal 172+? GetAspectRatioFilter - w gdc.c; SetMapperFlags (349) */
DWORD API SetMapperFlags(HDC hdc, DWORD f) { (void)hdc; (void)f; return 0; }
