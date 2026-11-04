/* gdiperf.c - GDI: poprawnosc szybkich sciezek rysowania i regresja wydajnosci.
 *
 * Dawniej do_blt/span_rop2 szly piksel po pikselu przez px_get/px_put, a kazde
 * rysowanie na ekranie zdejmowalo kursor myszy, gdy lezal gdziekolwiek w oknie:
 * SkiFree mialo ~1 klatke/s (docs/wydajnosc_gdi.md).
 *
 * Czesc 1: ROP-y, granice okien 64KB (takze piksel ekranu 24bpp rozciety miedzy
 *          selektory), nakladanie, konwersje mono, clip, kursor, tekst.
 * Czesc 2: czasy operacji; progi sa WZGLEDNE (czas operacji / czas prostej petli
 *          kopiujacej w tej aplikacji), zeby nie zalezaly od szybkosci hosta.
 * Aplikacja konczy sie sama.
 */
#include <windows.h>
#include "testlog.h"

#define BW 200                         /* 200x100x32bpp = 80000 B: ponad 64KB */
#define BH 100
#define SRCERASE_   0x00440328UL
#define NOTSRCERASE_ 0x001100A6UL
#define MERGEPAINT_ 0x00BB0226UL

static HWND g_hw;
static HDC  g_scr;                     /* DC okna pelnoekranowego: klient = ekran */
static HDC  g_ms, g_md, g_mi;          /* pamiec: zrodlo, cel, stan poczatkowy celu */

static COLORREF pat_s(int x, int y) { return RGB((x * 7 + y) & 255, (y * 5 + x) & 255, (x ^ y) & 255); }
static COLORREF pat_d(int x, int y) { return RGB((x + y * 3) & 255, (x * 3 ^ y) & 255, (y * 11 + x * 2) & 255); }

static void set_detail(char *d, int x, int y, COLORREF got, COLORREF exp)
{
    wsprintf(d, "(%d,%d) jest %06lX, ma byc %06lX", x, y, got, exp);
}

/* Punkty kontrolne bitmapy BWxBH: siatka + okolice granicy 64KB
 * (pitch 800: 65536 = wiersz 81, x=184) */
static BOOL pt_next(int *i, int *x, int *y)
{
    int k = *i;
    (*i)++;
    if (k < 110) { *x = (k % 11) * 19; *y = (k / 11) * 9; return TRUE; }
    k -= 110;
    if (k < 12) { *x = 178 + k; *y = 81; return TRUE; }
    if (k < 24) { *x = (k - 12) * 17; *y = 82; return TRUE; }
    return FALSE;
}

static DWORD rop_calc(DWORD rop, DWORD s, DWORD d)
{
    DWORD r = 0;
    switch (rop) {
    case SRCCOPY:      r = s; break;
    case SRCAND:       r = s & d; break;
    case SRCPAINT:     r = s | d; break;
    case SRCINVERT:    r = s ^ d; break;
    case NOTSRCCOPY:   r = ~s; break;
    case MERGEPAINT_:  r = ~s | d; break;
    case SRCERASE_:    r = s & ~d; break;
    case NOTSRCERASE_: r = ~(s | d); break;
    }
    return r & 0xFFFFFFUL;
}

static void test_rops(void)
{
    static const struct { DWORD rop; const char *name; } t[] = {
        { SRCCOPY, "SRCCOPY" }, { SRCAND, "SRCAND" }, { SRCPAINT, "SRCPAINT" }, { SRCINVERT, "SRCINVERT" },
        { NOTSRCCOPY, "NOTSRCCOPY" }, { MERGEPAINT_, "MERGEPAINT" }, { SRCERASE_, "SRCERASE" },
        { NOTSRCERASE_, "NOTSRCERASE" } };
    int n;
    for (n = 0; n < sizeof(t) / sizeof(t[0]); n++) {
        int i = 0, x, y, bad = 0;
        char d[80], name[40];
        d[0] = 0;
        BitBlt(g_md, 0, 0, BW, BH, g_mi, 0, 0, SRCCOPY);
        BitBlt(g_md, 0, 0, BW, BH, g_ms, 0, 0, t[n].rop);
        while (pt_next(&i, &x, &y)) {
            COLORREF e = rop_calc(t[n].rop, pat_s(x, y), pat_d(x, y)), g = GetPixel(g_md, x, y);
            if (e != g) { if (!bad) set_detail(d, x, y, g, e); bad++; }
        }
        wsprintf(name, "mem->mem %s", (LPSTR)t[n].name);
        t_log(name, bad == 0, d);
    }
}

/* Przesuniecie w ramach tej samej bitmapy we wszystkich kierunkach */
static void test_overlap(void)
{
    static const int dd[4][2] = { { 10, 5 }, { -10, -5 }, { 10, -5 }, { -10, 5 } };
    int n;
    for (n = 0; n < 4; n++) {
        int dx = dd[n][0], dy = dd[n][1], x, y, bad = 0;
        char d[80], name[40];
        d[0] = 0;
        BitBlt(g_md, 0, 0, BW, BH, g_ms, 0, 0, SRCCOPY);
        BitBlt(g_md, 40 + dx, 30 + dy, 100, 40, g_md, 40, 30, SRCCOPY);
        for (y = 0; y < 40; y += 3)
            for (x = 0; x < 100; x += 3) {
                COLORREF e = pat_s(40 + x, 30 + y), g = GetPixel(g_md, 40 + dx + x, 30 + dy + y);
                if (e != g) { if (!bad) set_detail(d, x, y, g, e); bad++; }
            }
        wsprintf(name, "nakladanie mem dx=%d dy=%d", dx, dy);
        t_log(name, bad == 0, d);
    }
}

/* Zrodlo czesciowo poza bitmapa: piksele spoza zrodla zostaja nietkniete */
static void test_src_clip(void)
{
    BitBlt(g_md, 0, 0, BW, BH, g_mi, 0, 0, SRCCOPY);
    BitBlt(g_md, 20, 20, 30, 10, g_ms, -10, 0, SRCCOPY);
    CHECK(GetPixel(g_md, 25, 25) == pat_d(25, 25), "zrodlo x<0: lewa czesc bez zmian");
    CHECK(GetPixel(g_md, 30, 25) == pat_s(0, 5), "zrodlo x<0: pierwszy piksel zrodla");
    CHECK(GetPixel(g_md, 49, 25) == pat_s(19, 5), "zrodlo x<0: ostatni piksel");
    BitBlt(g_md, 0, 0, BW, BH, g_mi, 0, 0, SRCCOPY);
    BitBlt(g_md, 20, 40, 30, 10, g_ms, BW - 10, 0, SRCCOPY);
    CHECK(GetPixel(g_md, 29, 45) == pat_s(BW - 1, 5), "zrodlo poza prawa: ostatni piksel zrodla");
    CHECK(GetPixel(g_md, 30, 45) == pat_d(30, 45), "zrodlo poza prawa: reszta bez zmian");
}

static void test_mono(void)
{
    HDC m = CreateCompatibleDC(g_scr);
    HBITMAP b = CreateBitmap(16, 16, 1, 1, NULL), old = SelectObject(m, b);
    PatBlt(m, 0, 0, 16, 16, BLACKNESS);
    PatBlt(m, 4, 0, 4, 16, WHITENESS);
    SetTextColor(g_md, RGB(10, 20, 30));
    SetBkColor(g_md, RGB(200, 210, 220));
    BitBlt(g_md, 0, 0, 16, 16, m, 0, 0, SRCCOPY);
    CHECK(GetPixel(g_md, 1, 1) == RGB(10, 20, 30), "mono->kolor: 0 = kolor tekstu");
    CHECK(GetPixel(g_md, 5, 1) == RGB(200, 210, 220), "mono->kolor: 1 = kolor tla");
    /* kolor -> mono: kolor tla zrodla = bialy, reszta czarny */
    PatBlt(g_md, 0, 0, 16, 16, WHITENESS);
    SetPixel(g_md, 3, 3, RGB(1, 2, 3));
    SetBkColor(g_md, RGB(1, 2, 3));
    BitBlt(m, 0, 0, 16, 16, g_md, 0, 0, SRCCOPY);
    CHECK(GetPixel(m, 3, 3) == RGB(255, 255, 255), "kolor->mono: kolor tla = bialy");
    CHECK(GetPixel(m, 4, 4) == RGB(0, 0, 0), "kolor->mono: inny = czarny");
    /* mono -> mono: bez konwersji */
    {
        HDC m2 = CreateCompatibleDC(g_scr);
        HBITMAP b2 = CreateBitmap(16, 16, 1, 1, NULL), old2 = SelectObject(m2, b2);
        PatBlt(m2, 0, 0, 16, 16, WHITENESS);
        BitBlt(m2, 0, 0, 16, 16, m, 0, 0, SRCAND);
        CHECK(GetPixel(m2, 3, 3) == RGB(255, 255, 255) && GetPixel(m2, 4, 4) == RGB(0, 0, 0), "mono->mono SRCAND");
        SelectObject(m2, old2); DeleteObject(b2); DeleteDC(m2);
    }
    SetBkColor(g_md, RGB(255, 255, 255));
    SetTextColor(g_md, RGB(0, 0, 0));
    SelectObject(m, old); DeleteObject(b); DeleteDC(m);
}

/* Ekran 24bpp, pitch 1920: y=34 x=85 i y=68 x=170 maja piksel rozciety miedzy selektory */
static void test_screen(void)
{
    static const int p[][2] = { { 84, 34 }, { 85, 34 }, { 86, 34 }, { 169, 68 }, { 170, 68 }, { 171, 68 },
                                { 60, 20 }, { 259, 79 }, { 100, 50 } };
    int i, bad = 0, y;
    char d[80];
    d[0] = 0;
    BitBlt(g_scr, 60, 20, 200, 60, g_ms, 0, 0, SRCCOPY);
    for (i = 0; i < sizeof(p) / sizeof(p[0]); i++) {
        COLORREF e = pat_s(p[i][0] - 60, p[i][1] - 20), g = GetPixel(g_scr, p[i][0], p[i][1]);
        if (e != g) { if (!bad) set_detail(d, p[i][0], p[i][1], g, e); bad++; }
    }
    t_log("mem->ekran przez granice selektorow", bad == 0, d);
    /* ekran -> pamiec */
    PatBlt(g_md, 0, 0, BW, BH, BLACKNESS);
    BitBlt(g_md, 0, 0, 200, 60, g_scr, 60, 20, SRCCOPY);
    bad = 0; d[0] = 0;
    for (y = 0; y < 60; y++) {
        COLORREF e = pat_s(25, y), g = GetPixel(g_md, 25, y);
        if (e != g) { if (!bad) set_detail(d, 25, y, g, e); bad++; }
    }
    t_log("ekran->mem przez granice selektorow", bad == 0, d);
    /* ekran -> ekran, nakladanie w dol i w prawo (jak ScrollWindow) */
    BitBlt(g_scr, 70, 25, 150, 40, g_scr, 60, 20, SRCCOPY);
    CHECK(GetPixel(g_scr, 85, 34) == pat_s(15, 9), "ekran->ekran nakladanie: rozciety piksel");
    CHECK(GetPixel(g_scr, 219, 64) == pat_s(149, 39), "ekran->ekran nakladanie: rog");
    /* w gore i w lewo */
    BitBlt(g_scr, 60, 20, 200, 60, g_ms, 0, 0, SRCCOPY);
    BitBlt(g_scr, 50, 15, 150, 40, g_scr, 60, 20, SRCCOPY);
    CHECK(GetPixel(g_scr, 85, 34) == pat_s(35, 19), "ekran->ekran w lewo/gore");
    /* wypelnienie jednolite i XOR przez granice */
    {
        HBRUSH br = CreateSolidBrush(RGB(12, 34, 56)), o = SelectObject(g_scr, br);
        PatBlt(g_scr, 80, 30, 100, 45, PATCOPY);
        CHECK(GetPixel(g_scr, 85, 34) == RGB(12, 34, 56), "PatBlt ekran: rozciety piksel");
        CHECK(GetPixel(g_scr, 170, 68) == RGB(12, 34, 56), "PatBlt ekran: rozciety piksel 2");
        CHECK(GetPixel(g_scr, 180, 34) != RGB(12, 34, 56), "PatBlt ekran: prawa krawedz wylaczna");
        PatBlt(g_scr, 80, 30, 100, 45, DSTINVERT);
        CHECK(GetPixel(g_scr, 85, 34) == RGB(255 - 12, 255 - 34, 255 - 56), "PatBlt ekran: DSTINVERT");
        SelectObject(g_scr, o); DeleteObject(br);
    }
    /* ROP2 na ekranie (span_rop2) */
    {
        HPEN np = GetStockObject(NULL_PEN), op = SelectObject(g_scr, np);
        HBRUSH wb = GetStockObject(WHITE_BRUSH), ob = SelectObject(g_scr, wb);
        int r2;
        PatBlt(g_scr, 80, 30, 100, 45, BLACKNESS);
        r2 = SetROP2(g_scr, R2_NOT);
        Rectangle(g_scr, 80, 30, 150, 60);
        SetROP2(g_scr, r2);
        CHECK(GetPixel(g_scr, 85, 34) == RGB(255, 255, 255), "R2_NOT na ekranie");
        CHECK(GetPixel(g_scr, 160, 34) == RGB(0, 0, 0), "R2_NOT: poza prostokatem bez zmian");
        SelectObject(g_scr, op); SelectObject(g_scr, ob);
    }
}

static void test_clip(void)
{
    HRGN r = CreateRectRgn(100, 100, 200, 200);
    PatBlt(g_scr, 80, 80, 150, 150, BLACKNESS);
    SelectClipRgn(g_scr, r);
    ExcludeClipRect(g_scr, 140, 140, 160, 160);        /* dziura: kilka prostokatow w clipie */
    BitBlt(g_scr, 80, 80, 150, 100, g_ms, 0, 0, SRCCOPY);
    CHECK(GetPixel(g_scr, 99, 120) == 0, "clip: lewo od regionu bez zmian");
    CHECK(GetPixel(g_scr, 100, 120) == pat_s(20, 40), "clip: pierwszy piksel regionu");
    CHECK(GetPixel(g_scr, 150, 150) == 0, "clip: dziura bez zmian");
    CHECK(GetPixel(g_scr, 165, 150) == pat_s(85, 70), "clip: za dziura");
    PatBlt(g_scr, 80, 80, 150, 150, WHITENESS);
    CHECK(GetPixel(g_scr, 150, 150) == 0, "clip PatBlt: dziura bez zmian");
    CHECK(GetPixel(g_scr, 120, 120) == RGB(255, 255, 255), "clip PatBlt: wnetrze");
    SelectClipRgn(g_scr, NULL);
    DeleteObject(r);
}

/* Kursor: nie moze zostawiac sladu ani byc zamazany/odczytany przez rysowanie */
static void test_cursor(void)
{
    HDC m = g_md;
    HBRUSH br = CreateSolidBrush(RGB(0, 128, 0)), o;
    int x, y, bad = 0;
    PatBlt(g_scr, 380, 280, 80, 80, WHITENESS);
    SetCursorPos(400, 300);
    /* blit tuz obok kursora, blit na kursor, tekst na kursor */
    PatBlt(m, 0, 0, 40, 40, BLACKNESS);
    BitBlt(g_scr, 440, 300, 10, 10, m, 0, 0, SRCCOPY);
    BitBlt(g_scr, 395, 295, 10, 10, m, 0, 0, SRCCOPY);
    TextOut(g_scr, 400, 330, "Ab", 2);
    /* odczyt z ekranu: bez kursora, ale z tym co pod nim */
    BitBlt(m, 0, 0, 40, 40, g_scr, 395, 295, SRCCOPY);
    CHECK(GetPixel(m, 2, 2) == 0, "ekran->mem pod kursorem: narysowany piksel");
    CHECK(GetPixel(m, 20, 20) == RGB(255, 255, 255), "ekran->mem pod kursorem: tlo, nie kursor");
    /* kursor nad zrodlem, cel daleko: kopia nie moze zawierac kursora */
    BitBlt(g_scr, 500, 200, 40, 40, g_scr, 395, 295, SRCCOPY);
    CHECK(GetPixel(g_scr, 500 + 20, 200 + 20) == RGB(255, 255, 255), "ekran->ekran: kursor nad zrodlem nie jest kopiowany");
    CHECK(GetPixel(g_scr, 500 + 7, 200 + 7) != RGB(1, 2, 3), "ekran->ekran: kopia wykonana");
    o = SelectObject(g_scr, br);
    PatBlt(g_scr, 405, 305, 5, 5, PATCOPY);
    SelectObject(g_scr, o);
    SetCursorPos(600, 20);
    /* po odjechaniu kursora: dokladnie to co rysowano */
    for (y = 280; y < 330; y++)
        for (x = 380; x < 440; x++) {
            COLORREF e = RGB(255, 255, 255);
            if (x >= 395 && x < 405 && y >= 295 && y < 305) e = 0;
            if (x >= 405 && x < 410 && y >= 305 && y < 310) e = RGB(0, 128, 0);
            if (GetPixel(g_scr, x, y) != e) bad++;
        }
    CHECK_INT(bad, 0, "kursor nie zostawia sladu");
    /* cel blitu ekran->ekran nad kursorem, zrodlo daleko: kursor musi zejsc (wykluczenie
     * celu jest zagniezdzone w wykluczeniu zrodla), inaczej po odjechaniu odtworzy stare tlo */
    PatBlt(g_scr, 80, 420, 60, 40, WHITENESS);
    PatBlt(g_scr, 520, 420, 40, 40, BLACKNESS);
    SetCursorPos(100, 430);
    BitBlt(g_scr, 90, 420, 40, 40, g_scr, 520, 420, SRCCOPY);
    SetCursorPos(600, 20);
    CHECK(GetPixel(g_scr, 101, 431) == 0 && GetPixel(g_scr, 110, 440) == 0, "ekran->ekran: cel nad kursorem");
    DeleteObject(br);
}

/* Liczba pikseli koloru c w prostokacie */
static long count_px(HDC dc, int x0, int y0, int w, int h, COLORREF c)
{
    int x, y;
    long n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (GetPixel(dc, x, y) == c) n++;
    return n;
}

static void test_text(void)
{
    static const char txt[] = "Zazolc gesla jazn";
    HFONT f;
    long a, b;
    int n = sizeof(txt) - 1, x, y, bad = 0;
    char d[60];
    PatBlt(g_scr, 20, 400, 300, 40, WHITENESS);
    PatBlt(g_md, 0, 0, BW, BH, WHITENESS);
    SetBkMode(g_scr, OPAQUE);
    SetBkMode(g_md, OPAQUE);
    SetTextColor(g_scr, RGB(200, 0, 0)); SetBkColor(g_scr, RGB(255, 255, 0));
    SetTextColor(g_md, RGB(200, 0, 0)); SetBkColor(g_md, RGB(255, 255, 0));
    TextOut(g_scr, 20, 400, txt, n);
    TextOut(g_md, 0, 0, txt, n);
    a = count_px(g_scr, 20, 400, 200, 20, RGB(200, 0, 0));
    b = count_px(g_md, 0, 0, 200, 20, RGB(200, 0, 0));
    wsprintf(d, "ekran=%ld pamiec=%ld", a, b);
    t_log("TextOut: piksele tekstu ekran == pamiec", a == b && a > 100, d);
    for (y = 0; y < 20; y++)
        for (x = 0; x < 200; x++)
            if (GetPixel(g_scr, 20 + x, 400 + y) != GetPixel(g_md, x, y)) bad++;
    CHECK_INT(bad, 0, "TextOut: ekran i pamiec identyczne");
    /* TRANSPARENT: tlo nietkniete; clip: czesc tekstu */
    PatBlt(g_scr, 20, 400, 300, 40, WHITENESS);
    SetBkMode(g_scr, TRANSPARENT);
    TextOut(g_scr, 20, 400, txt, n);
    CHECK_INT(count_px(g_scr, 20, 400, 200, 20, RGB(200, 0, 0)), a, "TextOut TRANSPARENT: te same piksele");
    CHECK_INT(count_px(g_scr, 20, 400, 200, 20, RGB(255, 255, 0)), 0, "TextOut TRANSPARENT: bez tla");
    PatBlt(g_scr, 20, 400, 300, 40, WHITENESS);
    IntersectClipRect(g_scr, 20, 400, 60, 440);
    TextOut(g_scr, 20, 400, txt, n);
    SelectClipRgn(g_scr, NULL);
    CHECK_INT(count_px(g_scr, 60, 400, 200, 20, RGB(200, 0, 0)), 0, "TextOut clip: nic poza regionem");
    CHECK(count_px(g_scr, 20, 400, 40, 20, RGB(200, 0, 0)) > 10, "TextOut clip: wnetrze narysowane");
    /* pogrubienie i podkreslenie: przechodza inna droga niz zwykly glif */
    f = CreateFont(0, 0, 0, 0, FW_BOLD, 0, 1, 0, 0, 0, 0, 0, 0, "System");
    {
        HFONT of = SelectObject(g_md, f);
        long c;
        PatBlt(g_md, 0, 0, BW, BH, WHITENESS);
        TextOut(g_md, 0, 0, txt, n);
        c = count_px(g_md, 0, 0, 199, 22, RGB(200, 0, 0));
        CHECK(c > b, "TextOut pogrubiony+podkreslony ma wiecej pikseli");
        SelectObject(g_md, of);
    }
    DeleteObject(f);
    SetBkMode(g_scr, OPAQUE); SetTextColor(g_scr, 0); SetBkColor(g_scr, RGB(255, 255, 255));
    SetTextColor(g_md, 0); SetBkColor(g_md, RGB(255, 255, 255));
}

static void test_pattern(void)
{
    HBRUSH h = CreateHatchBrush(HS_DIAGCROSS, RGB(0, 0, 255)), o = SelectObject(g_scr, h), om = SelectObject(g_md, h);
    int x, y, bad = 0;
    SetBkColor(g_scr, RGB(255, 255, 255)); SetBkColor(g_md, RGB(255, 255, 255));
    PatBlt(g_scr, 0, 0, 64, 64, PATCOPY);
    PatBlt(g_md, 0, 0, 64, 64, PATCOPY);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if (GetPixel(g_scr, x, y) != GetPixel(g_md, x, y)) bad++;
    CHECK_INT(bad, 0, "pedzel kreskowany: ekran == pamiec");
    CHECK(count_px(g_md, 0, 0, 64, 64, RGB(0, 0, 255)) > 200, "pedzel kreskowany: rysuje wzor");
    PatBlt(g_md, 0, 0, 16, 16, 0x005A0049UL);          /* PATINVERT dwa razy = bez zmian */
    PatBlt(g_md, 0, 0, 16, 16, 0x005A0049UL);
    CHECK(GetPixel(g_md, 8, 8) == GetPixel(g_scr, 8, 8), "PATINVERT x2 = bez zmian");
    SelectObject(g_scr, o); SelectObject(g_md, om);
    DeleteObject(h);
}

/* ============================================================
 * Czesc 2: pomiary
 * ============================================================ */
#define CAL_N 4096
static DWORD __far *g_a, __far *g_b;
static long g_cal;                     /* us na CAL_N pikseli prostej petli */

typedef void (*BENCHFN)(int i);

/* Powtarzaj fn przez co najmniej 1,1 s; wynik: us na wywolanie */
static long bench(BENCHFN fn)
{
    DWORD t0, el;
    long n = 0;
    t0 = GetTickCount();
    while (GetTickCount() == t0) ;     /* poczatek ticka */
    t0 = GetTickCount();
    do {
        fn((int)n);
        n++;
        el = GetTickCount() - t0;
    } while (el < 1100);
    return (long)(el * 1000UL / (DWORD)n);
}

static void b_cal(int i)
{
    int k;
    (void)i;
    for (k = 0; k < CAL_N; k++) g_b[k] = g_a[k];
}
static void b_api(int i)
{
    int k;
    (void)i;
    for (k = 0; k < 256; k++) GetBkColor(g_md);
}
static void b_mm(int i)  { (void)i; BitBlt(g_md, 10, 10, 64, 64, g_ms, 0, 0, SRCCOPY); }
static void b_mma(int i) { (void)i; BitBlt(g_md, 10, 10, 64, 64, g_ms, 0, 0, SRCAND); }
static void b_ms(int i)  { BitBlt(g_scr, 20 + (i & 7), 20, 64, 64, g_ms, 0, 0, SRCCOPY); }
static void b_sc(int i)  { (void)i; BitBlt(g_scr, 150, 20, 64, 64, g_scr, 20, 20, SRCCOPY); }
static void b_pat(int i) { (void)i; PatBlt(g_scr, 20, 100, 64, 64, WHITENESS); }
static void b_hat(int i) { (void)i; PatBlt(g_scr, 100, 100, 64, 64, PATCOPY); }
static void b_txt(int i) { (void)i; TextOut(g_scr, 20, 200, "Zazolc gesla jazn 012", 21); }
static void b_txm(int i) { (void)i; TextOut(g_md, 0, 0, "Zazolc gesla jazn 012", 21); }
static HWND g_wtest;
static void b_mov(int i) { MoveWindow(g_wtest, 200 + (i & 7) * 8, 150, 300, 200, TRUE); UpdateWindow(g_wtest); }
static void b_rep(int i) { (void)i; InvalidateRect(g_wtest, NULL, TRUE); UpdateWindow(g_wtest); }

/* maxpct: dopuszczalny czas operacji jako % czasu prostej petli na tej samej liczbie
 * pikseli (0 = tylko raport); px: pikseli na wywolanie */
static void measure(const char *name, BENCHFN fn, long px, long maxpct)
{
    char d[80], n[40];
    long us = bench(fn);
    long pct = us * 100L / (g_cal * px / CAL_N + 1);
    wsprintf(n, "czas %s", (LPSTR)name);
    wsprintf(d, "%ld us, %ld%% petli%s", us, pct, (LPSTR)(maxpct ? "" : ", bez progu"));
    t_log(n, maxpct == 0 || pct <= maxpct, d);
}

/* Puste PeekMessage w petli czekajacej oddaja CPU (PEEK_SPIN w user/umsg.c), ale gra
 * dalej dostaje kontrole co tick. Zuzyty czas hosta i log QEMU: docs/wydajnosc_gdi.md */
static void test_peek_idle(void)
{
    MSG m;
    DWORD t0, el;
    long peeks = 0, ticks = 0;
    char d[60];
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
    t0 = GetTickCount();
    while (GetTickCount() == t0) ;
    t0 = GetTickCount();
    {
        DWORD last = t0;
        do {
            if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
            peeks++;
            el = GetTickCount();
            if (el != last) { ticks++; last = el; }
            el -= t0;
        } while (el < 1100);
    }
    wsprintf(d, "%ld na tick, tickow %ld", peeks / (ticks ? ticks : 1), ticks);
    t_log("puste PeekMessage oddaja CPU", peeks / (ticks ? ticks : 1) <= 100, d);
    t_log("petla dostaje kazdy tick", ticks >= 18, d);
}

LRESULT FAR PASCAL __export WTestProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        int y;
        for (y = 0; y < 5; y++) TextOut(dc, 8, 8 + y * 18, "Tekst w oknie testowym", 22);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void run_perf(HINSTANCE hi)
{
    HGLOBAL ga = GlobalAlloc(GMEM_FIXED, CAL_N * 4L), gb = GlobalAlloc(GMEM_FIXED, CAL_N * 4L);
    HBRUSH hb = CreateHatchBrush(HS_DIAGCROSS, RGB(0, 0, 255)), ob;
    char d[60];
    int i;
    g_a = (DWORD __far *)GlobalLock(ga);
    g_b = (DWORD __far *)GlobalLock(gb);
    g_cal = bench(b_cal);
    wsprintf(d, "%ld us na %d pikseli", g_cal, CAL_N);
    t_log("kalibracja", g_cal > 0, d);
    SetCursorPos(320, 240);            /* kursor w oknie, poza rysowanymi prostokatami */
    ob = SelectObject(g_scr, hb);
    measure("wywolanie API (GetBkColor) x256", b_api, 4096, 0);
    measure("BitBlt mem->mem SRCCOPY 64x64", b_mm, 4096, 120);
    measure("BitBlt mem->mem SRCAND 64x64", b_mma, 4096, 200);
    measure("BitBlt mem->ekran 64x64", b_ms, 4096, 300);
    measure("BitBlt ekran->ekran 64x64", b_sc, 4096, 450);
    measure("PatBlt ekran 64x64", b_pat, 4096, 100);
    measure("PatBlt kreskowany ekran 64x64", b_hat, 4096, 300);
    measure("TextOut ekran 21 znakow", b_txt, 4096, 300);
    measure("TextOut pamiec 21 znakow", b_txm, 4096, 400);
    SelectObject(g_scr, ob);
    DeleteObject(hb);
    {
        WNDCLASS wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WTestProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "GpWin";
        RegisterClass(&wc);
    }
    g_wtest = CreateWindow("GpWin", "Okno testowe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 150, 300, 200, NULL, NULL, hi, NULL);
    for (i = 0; i < 3; i++)
        CreateWindow("BUTTON", "Przycisk", WS_CHILD | WS_VISIBLE, 8 + i * 90, 120, 80, 24, g_wtest, (HMENU)(100 + i), hi, NULL);
    UpdateWindow(g_wtest);
#ifdef GP_LOOP                         /* do tools/qemu_prof.py: -DGP_LOOP=1 TextOut, 2 odrys, 3 ruch */
    for (i = 0; ; i++) {
        if (GP_LOOP == 1) b_txm(i);
        else if (GP_LOOP == 2) b_rep(i);
        else b_mov(i);
    }
#endif
    measure("MoveWindow z przerysowaniem", b_mov, 4096, 4000);
    measure("przerysowanie okna", b_rep, 4096, 2500);
    DestroyWindow(g_wtest);
    GlobalUnlock(ga); GlobalFree(ga);
    GlobalUnlock(gb); GlobalFree(gb);
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static HBITMAP mk_bitmap(HDC m)
{
    HBITMAP b = CreateCompatibleBitmap(g_scr, BW, BH);
    SelectObject(m, b);
    return b;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HBITMAP bs, bd, bi;
    int x, y;
    (void)cmd; (void)show;
    t_app = "GDIPERF";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = "GdiPerf";
        RegisterClass(&wc);
    }
    g_hw = CreateWindow("GdiPerf", "", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, NULL, NULL, hi, NULL);
    UpdateWindow(g_hw);
    g_scr = GetDC(g_hw);
    g_ms = CreateCompatibleDC(g_scr);
    g_md = CreateCompatibleDC(g_scr);
    g_mi = CreateCompatibleDC(g_scr);
    bs = mk_bitmap(g_ms); bd = mk_bitmap(g_md); bi = mk_bitmap(g_mi);
    for (y = 0; y < BH; y++)
        for (x = 0; x < BW; x++) {
            SetPixel(g_ms, x, y, pat_s(x, y));
            SetPixel(g_mi, x, y, pat_d(x, y));
        }
    CHECK(GetPixel(g_ms, 184, 81) == pat_s(184, 81), "SetPixel/GetPixel na granicy 64KB");
    test_rops();
    test_overlap();
    test_src_clip();
    test_mono();
    test_screen();
    test_clip();
    test_cursor();
    test_text();
    test_pattern();
    test_peek_idle();
    run_perf(hi);
    t_done();
    ReleaseDC(g_hw, g_scr);
    DeleteDC(g_ms); DeleteDC(g_md); DeleteDC(g_mi);
    DeleteObject(bs); DeleteObject(bd); DeleteObject(bi);
    DestroyWindow(g_hw);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
