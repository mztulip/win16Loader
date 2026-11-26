/* gmisc.c - GDI: palety (ekran true color), metapliki i drukowanie (brak),
 * Escape i drobne funkcje */
#include "gdi.h"

typedef struct { BYTE r, g, b, flags; } PALENTRY;

static void vga_outb(WORD port, BYTE val);
#pragma aux vga_outb = "out dx, al" parm [dx] [al];

DWORD g_dac_rgb[256];
WORD  g_dac_gen = 0;
static BOOL g_dac_ready = FALSE;

/* VBE 4F09h (BL=0 ustaw): ES:DI = tablica B,G,R,0 (6 bit). KERNEL kopiuje ja do bufora RM. */
static WORD vbe_set_pal(WORD start, WORD n, BYTE __far *tab);
#pragma aux vbe_set_pal = \
    "mov cx, ax"            \
    "mov ax, 0x4F09"        \
    "xor bl, bl"            \
    "int 0x10"              \
    parm [dx] [ax] [es di] value [ax] modify [ax bx cx dx si di es];

static void dac_outsb(BYTE __far *src, WORD cnt);
#pragma aux dac_outsb = \
    "push ds"               \
    "mov ds, dx"            \
    "mov dx, 0x3C9"         \
    "cld"                   \
    "rep outsb"             \
    "pop ds"                \
    parm [dx si] [cx] modify [cx dx si];

/* Wpisy [start, start+n) z g_dac_rgb do sprzetowego DAC */
static void dac_upload(WORD start, WORD n)
{
    WORD i;
    g_dac_gen++;
    if (g_hw_bpp != 8 || !n) return;
    if (g_hw_dacvbe) {
        static BYTE tab[256 * 4];
        for (i = 0; i < n; i++) {
            DWORD px = g_dac_rgb[start + i];
            tab[i * 4 + 0] = (BYTE)((px >> 2)  & 0x3F);
            tab[i * 4 + 1] = (BYTE)((px >> 10) & 0x3F);
            tab[i * 4 + 2] = (BYTE)((px >> 18) & 0x3F);
            tab[i * 4 + 3] = 0;
        }
        if (vbe_set_pal(start, n, (BYTE __far *)tab) == 0x004F) return;
        g_hw_dacvbe = 0;                           /* nie dziala: sprobuj portow VGA */
    }
    {
        /* Caly zakres jednym "rep outsb": pod KVM to jedno wyjscie do QEMU zamiast 3 na wpis
         * (z oknem GTK kazde "out 3C9h" czekalo ~1 ms na watek ekranu -> sciemnianie filmow
         * trwalo minuty i gra wygladala na zawieszona). Na 486 to zwykly szybki zapis palety. */
        static BYTE rgb[256 * 3];
        for (i = 0; i < n; i++) {
            DWORD px = g_dac_rgb[start + i];
            rgb[i * 3 + 0] = (BYTE)((px >> 18) & 0x3F);
            rgb[i * 3 + 1] = (BYTE)((px >> 10) & 0x3F);
            rgb[i * 3 + 2] = (BYTE)((px >> 2)  & 0x3F);
        }
        vga_outb(0x3C8, (BYTE)start);
        dac_outsb((BYTE __far *)rgb, n * 3);
    }
}

void vga_dac_init(void)
{
    static const BYTE sys20[20][3] = {
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
        {192,220,192},{166,202,240},{255,251,240},{160,160,164},{128,128,128},{255,0,0},
        {0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}
    };
    WORD i, r, g, b, idx = 10;
    for (i = 0; i < 10; i++)
        g_dac_rgb[i] = (DWORD)sys20[i][2] | ((DWORD)sys20[i][1] << 8) | ((DWORD)sys20[i][0] << 16);
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++, idx++)
                g_dac_rgb[idx] = (DWORD)(b * 51) | ((DWORD)(g * 51) << 8) | ((DWORD)(r * 51) << 16);
    for (; idx < 246; idx++) g_dac_rgb[idx] = 0;
    for (i = 0; i < 10; i++)
        g_dac_rgb[246 + i] = (DWORD)sys20[10 + i][2] | ((DWORD)sys20[10 + i][1] << 8) | ((DWORD)sys20[10 + i][0] << 16);
    dac_upload(0, 256);
    g_dac_ready = TRUE;
}

/* PALETTEENTRY (R,G,B,flags) -> DAC */
WORD vga_dac_set_rgb(WORD start, WORD n, const BYTE __far *pal_entries)
{
    WORD i, changed = 0, lo = 0xFFFF, hi = 0;
    if (start >= 256 || !n) return 0;
    if (start + n > 256) n = 256 - start;
    for (i = 0; i < n; i++) {
        const BYTE __far *e = pal_entries + i * 4;
        DWORD px = (DWORD)e[2] | ((DWORD)e[1] << 8) | ((DWORD)e[0] << 16);
        if (!g_dac_ready || g_dac_rgb[start + i] != px) {
            g_dac_rgb[start + i] = px;
            changed++;
            if (lo == 0xFFFF) lo = i;
            hi = i;
        }
    }
    g_dac_ready = TRUE;
    if (changed) dac_upload(start + lo, hi - lo + 1);
    return changed;
}

/* RGBQUAD/RGBTRIPLE (B,G,R[,0]) -> DAC */
void vga_dac_set_bgr(WORD start, WORD n, const BYTE __far *rgb_entries, WORD csize)
{
    WORD i, changed = 0, lo = 0xFFFF, hi = 0;
    if (g_hw_bpp != 8 || start >= 256 || !n || csize < 3) return;
    if (start + n > 256) n = 256 - start;
    for (i = 0; i < n; i++) {
        const BYTE __far *c = rgb_entries + i * csize;
        DWORD px = (DWORD)c[0] | ((DWORD)c[1] << 8) | ((DWORD)c[2] << 16);
        if (!g_dac_ready || g_dac_rgb[start + i] != px) {
            g_dac_rgb[start + i] = px;
            changed++;
            if (lo == 0xFFFF) lo = i;
            hi = i;
        }
    }
    g_dac_ready = TRUE;
    if (changed) dac_upload(start + lo, hi - lo + 1);
}

static HPALETTE g_realized_pal = 0;   /* paleta ostatnio zrealizowana na ekranie 8bpp */

/* ordinal 360 */
HPALETTE API CreatePalette(WORD __far *lp)
{
    WORD h = obj_new(OT_PAL), n = lp[1], i;
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    if (!p) return 0;
    p->n = n;
    p->mem = GlobalAlloc(0, (DWORD)n * 4 + 4);
    if (p->mem) {
        PALENTRY __far *d = (PALENTRY __far *)MK_FP(p->mem, 0);
        PALENTRY __far *s = (PALENTRY __far *)(lp + 2);
        for (i = 0; i < n; i++) d[i] = s[i];
    }
    return h;
}

/* ordinal 363 */
WORD API GetPaletteEntries(HPALETTE h, WORD start, WORD n, PALENTRY __far *out)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    WORD i;
    if (!p) return 0;
    if (!p->mem) {                     /* paleta domyslna: 20 kolorow systemowych */
        static const BYTE sys[20][3] = {
            {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},
            {192,220,192},{166,202,240},{255,251,240},{160,160,164},{128,128,128},{255,0,0},
            {0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
        if (!out) return 20;
        for (i = 0; i < n && start + i < 20; i++) {
            out[i].r = sys[start + i][0]; out[i].g = sys[start + i][1];
            out[i].b = sys[start + i][2]; out[i].flags = 0;
        }
        return i;
    }
    if (!out) return p->n;
    for (i = 0; i < n && start + i < p->n; i++)
        out[i] = ((PALENTRY __far *)MK_FP(p->mem, 0))[start + i];
    return i;
}

/* ordinal 364 */
WORD API SetPaletteEntries(HPALETTE h, WORD start, WORD n, PALENTRY __far *in)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    WORD i;
    if (!p || !p->mem) return 0;
    for (i = 0; i < n && start + i < p->n; i++)
        ((PALENTRY __far *)MK_FP(p->mem, 0))[start + i] = in[i];
    return i;
}

/* ordinal 375 */
WORD API GetSystemPaletteEntries(HDC hdc, WORD start, WORD n, PALENTRY __far *out)
{
    (void)hdc;
    return GetPaletteEntries(GetStockObject(15), start, n, out);
}

/* ordinal 361/362 (wolane przez USER SelectPalette/RealizePalette) */
HPALETTE API GDISelectPalette(HDC hdc, HPALETTE h, BOOL bg)
{
    DCOBJ __far *dc = dc_get(hdc);
    WORD o;
    (void)bg;
    if (!dc) return 0;
    o = dc->pal;
    dc->pal = h;
    return o;
}
WORD API GDIRealizePalette(HDC hdc)
{
    DCOBJ __far *dc = dc_get(hdc);
    PALOBJ __far *po;
    WORD n, changed;
    if (g_disp_bpp != 8 || !dc || dc->is_mem || !dc->pal) return 0;
    po = (PALOBJ __far *)obj_ptr(dc->pal, OT_PAL);
    if (!po || !po->mem) return 0;
    n = po->n > 256 ? 256 : po->n;
    g_realized_pal = dc->pal;
    changed = vga_dac_set_rgb(0, n, (const BYTE __far *)MK_FP(po->mem, 0));
    /* W natywnym trybie 8bpp VGA RAMDAC (0x3C8/0x3C9) juz zaktualizowal ekran sprzetowo;
     * w trybie emulacji na 24bpp przeliczamy bufor indeksow */
    if (changed && g_hw_bpp != 8) pal_idx_flush(dc, g_dac_rgb);
    return changed;
}
WORD API RealizeDefaultPalette(HDC hdc) { (void)hdc; return 0; }
/* ordinal 367: zmiana wpisow palety + natychmiast w DAC, gdy paleta jest na ekranie
 * (cykle kolorow / sciemnianie bez przerysowania: na 486 to jedyna szybka droga) */
BOOL API AnimatePalette(HPALETTE h, WORD s, WORD n, LPVOID e)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    PALENTRY __far *in = (PALENTRY __far *)e;
    WORD i;
    if (!p || !p->mem || !in || s >= p->n) return FALSE;
    if (s + n > p->n) n = p->n - s;
    for (i = 0; i < n; i++)
        ((PALENTRY __far *)MK_FP(p->mem, 0))[s + i] = in[i];
    if (g_disp_bpp == 8 && h == g_realized_pal && s < 256) {
        if (s + n > 256) n = 256 - s;
        vga_dac_set_rgb(s, n, (const BYTE __far *)in);
    }
    return TRUE;
}
BOOL API ResizePalette(HPALETTE h, WORD n) { (void)h; (void)n; return TRUE; }
int  API UpdateColors(HDC hdc)       { (void)hdc; return 0; }
/* ordinal 370 */
WORD API GetNearestPaletteIndex(HPALETTE h, COLORREF c)
{
    PALOBJ __far *p = (PALOBJ __far *)obj_ptr(h, OT_PAL);
    PALENTRY __far *e;
    DWORD best = 0xFFFFFFFFUL;
    WORD i, bi = 0;
    int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
    if (!p || !p->mem) return 0;
    e = (PALENTRY __far *)MK_FP(p->mem, 0);
    for (i = 0; i < p->n; i++) {
        long dr = r - e[i].r, dg = g - e[i].g, db = b - e[i].b;
        DWORD d = (DWORD)(dr * dr + dg * dg + db * db);
        if (d < best) { best = d; bi = i; if (!d) break; }
    }
    return bi;
}
/* ordinal 154: true color - kolor dokladny; 8bpp - najblizszy kolor palety sprzetowej */
COLORREF API GetNearestColor(HDC hdc, COLORREF c)
{
    (void)hdc;
    if (g_disp_bpp == 8 && g_hw_bpp == 8 && !(c & 0xFF000000UL))
        return px2rgb(g_dac_rgb[gsurf_nearest(rgb2px(c))]);
    return c & 0xFFFFFFUL;
}
WORD API SetSystemPaletteUse(HDC hdc, WORD u) { (void)hdc; (void)u; return 1; }
WORD API GetSystemPaletteUse(HDC hdc) { (void)hdc; return 1; }

/* ============================================================
 * Metapliki - nieobslugiwane (aplikacje sprawdzaja blad)
 * ============================================================ */
HDC    API CreateMetaFile(LPCSTR f)     { (void)f; return 0; }
HANDLE API CloseMetaFile(HDC h)         { (void)h; return 0; }
BOOL   API DeleteMetaFile(HANDLE h)     { (void)h; return TRUE; }
BOOL   API PlayMetaFile(HDC hdc, HANDLE h) { (void)hdc; (void)h; return FALSE; }
HANDLE API GetMetaFile(LPCSTR f)        { (void)f; return 0; }
HANDLE API CopyMetaFile(HANDLE h, LPCSTR f) { (void)h; (void)f; return 0; }
HANDLE API GetMetaFileBits(HANDLE h)    { return h; }
HANDLE API SetMetaFileBits(HANDLE h)    { (void)h; return 0; }
HANDLE API SetMetaFileBitsBetter(HANDLE h) { (void)h; return 0; }
BOOL   API EnumMetaFile(HDC hdc, HANDLE h, LPVOID p, DWORD d) { (void)hdc; (void)h; (void)p; (void)d; return FALSE; }

/* ============================================================
 * Drukowanie - brak drukarek
 * ============================================================ */
int API Escape(HDC hdc, int esc, int n, LPCSTR in, LPVOID out)
{
    (void)hdc; (void)n; (void)in; (void)out;
    if (esc == 8) return 0;            /* QUERYESCSUPPORT: nic nie wspieramy */
    return 0;
}
int API StartDoc(HDC hdc, LPVOID di)   { (void)hdc; (void)di; return -1; }
int API EndDoc(HDC hdc)                { (void)hdc; return -1; }
int API StartPage(HDC hdc)             { (void)hdc; return -1; }
int API EndPage(HDC hdc)               { (void)hdc; return -1; }
int API AbortDoc(HDC hdc)              { (void)hdc; return -1; }
int API SetAbortProc(HDC hdc, LPVOID p){ (void)hdc; (void)p; return -1; }

/* ordinal 1-200 rozne */
BOOL API FastWindowFrame(HDC hdc, LPRECT r, int w, int h, DWORD rop) { (void)hdc; (void)r; (void)w; (void)h; (void)rop; return FALSE; }
int  API GetEnvironment(LPCSTR port, LPVOID buf, WORD n) { (void)port; (void)buf; (void)n; return 0; }
int  API SetEnvironment(LPCSTR port, LPVOID buf, WORD n) { (void)port; (void)buf; (void)n; return 0; }

/* LibMain - nieuzywane (loader nie wola LibEntry systemowych DLL) */
int API LibMain(WORD a, WORD b, WORD c, LPCSTR d) { (void)a; (void)b; (void)c; (void)d; return 1; }
