/* fonttest.c - test fontow bitmapowych GDI (.FON z katalogu systemowego):
 * EnumFonts, CreateFont/CreateFontIndirect z dopasowaniem nazwy i rozmiaru,
 * GetTextFace, GetTextMetrics, GetTextExtent, pogrubienie syntetyczne,
 * kolory/tlo tekstu (SetBkMode OPAQUE/TRANSPARENT). Alt+F4 zamyka.
 */
#include <windows.h>
#include "testlog.h"

static int g_nfaces;
static char g_faces[16][LF_FACESIZE];

int FAR PASCAL __export EnumProc(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPARAM data)
{
    (void)tm; (void)type; (void)data;
    if (g_nfaces < 16) lstrcpy(g_faces[g_nfaces], lf->lfFaceName);
    g_nfaces++;
    return 1;
}

static HFONT make_font(int h, int weight, const char *face)
{
    return CreateFont(h, 0, 0, 0, weight, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, face);
}

static void font_tests(HWND hwnd, HINSTANCE hi)
{
    HDC dc = GetDC(hwnd);
    FARPROC p = MakeProcInstance((FARPROC)EnumProc, hi);
    TEXTMETRIC tm, tmb;
    HFONT f, fb, old;
    char face[LF_FACESIZE];
    DWORD ext, extb;
    int i;
    BOOL found;

    EnumFonts(dc, NULL, (FONTENUMPROC)p, 0);
    CHECK(g_nfaces >= 5, "EnumFonts: >= 5 krojow (.FON)");
    for (i = 0, found = FALSE; i < g_nfaces && i < 16; i++) if (!lstrcmp(g_faces[i], "MS Sans Serif")) found = TRUE;
    CHECK(found, "EnumFonts: jest MS Sans Serif");

    f = make_font(-11, FW_NORMAL, "MS Sans Serif");          /* 8 pt */
    old = SelectObject(dc, f);
    GetTextFace(dc, sizeof(face), face);
    CHECK_STR(face, "MS Sans Serif", "GetTextFace po CreateFont");
    GetTextMetrics(dc, &tm);
    CHECK_INT(tm.tmHeight, 13, "MS Sans Serif 8pt: tmHeight");
    ext = GetTextExtent(dc, "Hello", 5);
    CHECK(LOWORD(ext) > 15 && LOWORD(ext) < 60, "GetTextExtent: rozsadna szerokosc");
    CHECK_INT(HIWORD(ext), tm.tmHeight, "GetTextExtent: wysokosc = tmHeight");

    {   /* DrawText: GetTextMetrics pisal poza za mala lokalna strukture i niszczyl szerokosc */
        RECT rc;
        SetRect(&rc, 0, 0, 0, 0);
        DrawText(dc, "Hello", -1, &rc, DT_CALCRECT | DT_SINGLELINE);
        CHECK_INT(rc.right, LOWORD(ext), "DrawText DT_CALCRECT: szerokosc");
        CHECK_INT(rc.bottom, tm.tmHeight, "DrawText DT_CALCRECT: wysokosc");
    }

    fb = make_font(-11, FW_BOLD, "MS Sans Serif");
    SelectObject(dc, fb);
    GetTextMetrics(dc, &tmb);
    extb = GetTextExtent(dc, "Hello", 5);
    CHECK(tmb.tmWeight >= FW_BOLD, "pogrubienie: tmWeight");
    /* MS Sans Serif ma prawdziwy font pogrubiony (fonts/sserifb.fon), bez syntezy */
    CHECK_INT(tmb.tmOverhang, 0, "pogrubienie MS Sans Serif: prawdziwy font");
    {
        HDC d2 = GetDC(0);
        HFONT o2 = SelectObject(d2, fb);
        char face[32];
        GetTextFace(d2, sizeof(face), face);
        CHECK_STR(face, "MS Sans Serif", "pogrubienie MS Sans Serif: nazwa");
        CHECK(LOWORD(GetTextExtent(d2, "Start", 5)) <= 22, "pogrubione Start miesci sie jak w Calmirze");
        SelectObject(d2, o2);
        {   /* font bez wersji pogrubionej: synteza jak dotad */
            HFONT cr = make_font(0, FW_NORMAL, "Courier"), cb = make_font(0, FW_BOLD, "Courier");
            int wr, wb;
            o2 = SelectObject(d2, cr);
            wr = LOWORD(GetTextExtent(d2, "Start", 5));
            SelectObject(d2, cb);
            wb = LOWORD(GetTextExtent(d2, "Start", 5));
            CHECK(wb > wr, "pogrubienie syntetyczne: tekst szerszy");
            SelectObject(d2, o2);
            DeleteObject(cr); DeleteObject(cb);
        }
        ReleaseDC(0, d2);
    }
    SelectObject(dc, old);
    DeleteObject(fb);
    DeleteObject(f);

    f = make_font(0, FW_NORMAL, "Courier");
    old = SelectObject(dc, f);
    GetTextMetrics(dc, &tm);
    CHECK((tm.tmPitchAndFamily & 1) == 0, "Courier: staly rozstaw (TMPF_FIXED_PITCH = 0)");
    CHECK_INT(LOWORD(GetTextExtent(dc, "iiii", 4)), LOWORD(GetTextExtent(dc, "WWWW", 4)), "Courier: i i W tej samej szerokosci");
    SelectObject(dc, old);
    DeleteObject(f);

    old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    GetTextFace(dc, sizeof(face), face);
    CHECK_STR(face, "System", "SYSTEM_FONT = System");
    SelectObject(dc, GetStockObject(OEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    CHECK(tm.tmHeight > 0, "OEM_FIXED_FONT");
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    FreeProcInstance(p);
}

LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        font_tests(hwnd, ((LPCREATESTRUCT)lp)->hInstance);
        t_done();
        return 0;
    case WM_PAINT: {
        static const struct { const char *face; int h, w; } fl[] = {
            { "MS Sans Serif", -11, FW_NORMAL }, { "MS Sans Serif", -11, FW_BOLD },
            { "MS Sans Serif", -13, FW_NORMAL }, { "MS Sans Serif", -16, FW_NORMAL },
            { "System", 0, FW_BOLD }, { "Fixedsys", 0, FW_NORMAL },
            { "Courier", 0, FW_NORMAL }, { "Small Fonts", 0, FW_NORMAL } };
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        int i, y = 4;
        for (i = 0; i < 8; i++) {
            char b[80];
            TEXTMETRIC tm;
            HFONT f = make_font(fl[i].h, fl[i].w, fl[i].face), o = SelectObject(dc, f);
            GetTextMetrics(dc, &tm);
            wsprintf(b, "%s %s (h=%d) Zazolc gesla jazn 0123", (LPSTR)fl[i].face,
                     (LPSTR)(fl[i].w >= FW_BOLD ? "bold" : ""), tm.tmHeight);
            SetTextColor(dc, i == 2 ? RGB(0, 0, 192) : RGB(0, 0, 0));
            if (i == 3) { SetBkMode(dc, OPAQUE); SetBkColor(dc, RGB(255, 255, 0)); } else SetBkMode(dc, TRANSPARENT);
            TextOut(dc, 8, y, b, lstrlen(b));
            y += tm.tmHeight + 4;
            SelectObject(dc, o);
            DeleteObject(f);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h;
    (void)cmd;
    t_app = "FONTTEST";
    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "FontTest";
        RegisterClass(&wc);
    }
    h = CreateWindow("FontTest", "Fonty .FON - test", WS_OVERLAPPEDWINDOW, 10, 10, 520, 260, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    UpdateWindow(h);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
