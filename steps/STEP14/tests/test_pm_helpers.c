/*
 * test_pm_helpers.c - testy jednostkowe dla pm_helpers.c::patch_gdt_c()
 *
 * Kompilacja na hoście (Linux/GCC):
 *   gcc -Itests/ -I. -Wall -o tests/test_pm_helpers tests/test_pm_helpers.c pm_helpers.c
 *
 * Strategia:
 *   - dos.h zastepuje MK_FP i __far (patrz tests/dos.h)
 *   - g_gdt_off_c = 0, wiec MK_FP(g_orig_cs, 0+sel) -> _mock_gdt_base + sel
 *   - patch_gdt_c() pisze do fake_gdt[] zamiast prawdziwego GDT
 *   - sprawdzamy bazy, limity i access byte dla wszystkich selektorow
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* -------------------------------------------------------------------
 * Zmienne globalne eksportowane przez loader.c (pm_helpers.c extern)
 * ------------------------------------------------------------------- */
unsigned short g_orig_cs      = 0x1000;   /* wartosc nieistotna przy mock MK_FP */
unsigned short g_gdt_off_c    = 0;        /* offset = 0 -> sel bezposrednio w buforze */
unsigned long  g_cs_phys      = 0x10000;
unsigned long  g_app_phys     = 0x20000;
unsigned long  g_app_data_phys= 0x30000;
unsigned short g_has_data     = 1;
unsigned long  g_thunk_phys   = 0x40000;
unsigned short g_thunk_size   = 0x0200;
unsigned long  g_lfb_phys     = 0xFD000000UL;
unsigned long  g_font_phys    = 0x000C6720UL;
unsigned long  g_kcb_phys     = 0x50000;
unsigned long  g_psp_phys     = 0x60000;
unsigned long  g_bitmaps_phys = 0x70000;
unsigned short g_ndll         = 3;   /* 3 DLL: KERNEL, USER, GDI (GDI bez data) */
unsigned long  g_dll_code_phys[4] = { 0x80000, 0x90000, 0xC0000, 0 };
unsigned short g_dll_code_size[4] = { 0x1000,  0x2000,  0x3000,  0 };
unsigned long  g_dll_data_phys[4] = { 0xA0000, 0xB0000, 0, 0 };
unsigned short g_dll_has_data [4] = { 1, 1, 0, 0 };  /* DLL2 (GDI) bez data */
unsigned long  g_ext_mem_kb       = 14336;  /* 14MB XMS; 0 = brak XMS */

/* -------------------------------------------------------------------
 * Mock GDT buffer
 * ------------------------------------------------------------------- */
#define GDT_SIZE 0x530
static unsigned char fake_gdt[GDT_SIZE];
unsigned char *_mock_gdt_base = fake_gdt;  /* uzywany przez MK_FP w dos.h */

#define FILL_BYTE 0xCC  /* wartosc inicjalna; jesli nadal CC -> nie zapisano */

/* -------------------------------------------------------------------
 * Funkcja testowana
 * ------------------------------------------------------------------- */
void patch_gdt_c(void);

/* -------------------------------------------------------------------
 * Pomocniki do czytania pol deskryptora GDT
 * ------------------------------------------------------------------- */
static unsigned long read_base(unsigned sel)
{
    unsigned char *e = &fake_gdt[sel];
    return (unsigned long)e[2]
         | ((unsigned long)e[3] << 8)
         | ((unsigned long)e[4] << 16)
         | ((unsigned long)e[7] << 24);
}

static unsigned short read_limit(unsigned sel)
{
    unsigned char *e = &fake_gdt[sel];
    return (unsigned short)(e[0] | ((unsigned short)e[1] << 8));
}

static unsigned char read_access(unsigned sel)
{
    return fake_gdt[sel + 5];
}

/* -------------------------------------------------------------------
 * Prosty framework testowy
 * ------------------------------------------------------------------- */
static int g_tests  = 0;
static int g_failed = 0;
static const char *g_suite = "";

static void suite(const char *name)
{
    g_suite = name;
    printf("\n[%s]\n", name);
}

static void check(const char *desc, int ok, const char *fmt, ...)
{
    g_tests++;
    if (ok) {
        printf("  OK   %s\n", desc);
    } else {
        g_failed++;
        printf("  FAIL %s", desc);
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            printf(" -- ");
            vprintf(fmt, ap);
            va_end(ap);
        }
        printf("\n");
    }
}

#define CHECK(desc, expr) \
    check(desc, (int)(expr), NULL)

#define CHECK_EQ32(desc, got, expected) \
    check(desc, (unsigned long)(got) == (unsigned long)(expected), \
          "got 0x%08lX, expected 0x%08lX", (unsigned long)(got), (unsigned long)(expected))

#define CHECK_EQ16(desc, got, expected) \
    check(desc, (unsigned short)(got) == (unsigned short)(expected), \
          "got 0x%04X, expected 0x%04X", (unsigned short)(got), (unsigned short)(expected))

#define CHECK_EQ8(desc, got, expected) \
    check(desc, (unsigned char)(got) == (unsigned char)(expected), \
          "got 0x%02X, expected 0x%02X", (unsigned char)(got), (unsigned char)(expected))

/* -------------------------------------------------------------------
 * TESTY
 * ------------------------------------------------------------------- */

static void test_loader_segments(void)
{
    suite("Selektory segmentu loadera (DATASEG/CODE16/DATA16)");
    CHECK_EQ32("SEL_DATASEG (0x18) base = g_cs_phys", read_base(0x18), g_cs_phys);
    CHECK_EQ32("SEL_CODE16  (0x20) base = g_cs_phys", read_base(0x20), g_cs_phys);
    CHECK_EQ32("SEL_DATA16  (0x28) base = g_cs_phys", read_base(0x28), g_cs_phys);
}

static void test_app_segments(void)
{
    suite("Selektory aplikacji (APP_CODE / PSP / APP_DATA)");
    CHECK_EQ32("SEL_APP_CODE (0x30) base  = g_app_phys",  read_base(0x30),  g_app_phys);
    CHECK_EQ16("SEL_APP_CODE (0x30) limit = 0xFFFF",      read_limit(0x30), 0xFFFF);
    CHECK_EQ32("SEL_PSP      (0x38) base  = g_psp_phys",  read_base(0x38),  g_psp_phys);
    CHECK_EQ32("SEL_APP_DATA (0x40) base  = g_app_data_phys", read_base(0x40),  g_app_data_phys);
    CHECK_EQ16("SEL_APP_DATA (0x40) limit = 0xFFFF",          read_limit(0x40), 0xFFFF);
}

static void test_thunk(void)
{
    suite("SEL_THUNK (0x88)");
    CHECK_EQ32("base  = g_thunk_phys",      read_base(0x88),  g_thunk_phys);
    CHECK_EQ16("limit = g_thunk_size - 1",  read_limit(0x88), g_thunk_size - 1);
}

static void test_vesa(void)
{
    suite("SEL_VESA (0x90) i 15 okien VESA (0xA0..0x110)");
    CHECK_EQ32("SEL_VESA (0x90) base = g_lfb_phys", read_base(0x90), g_lfb_phys);

    int i;
    for (i = 0; i < 15; i++) {
        unsigned sel = 0xA0 + (unsigned)(i * 8);
        unsigned long  expected_base  = g_lfb_phys + ((unsigned long)i << 16);
        unsigned short expected_limit = (i < 14) ? 0xFFFF : 0x0FFF;
        char desc[64];
        snprintf(desc, sizeof(desc), "VESA win[%2d] (sel=0x%02X) base",  i, sel);
        CHECK_EQ32(desc, read_base(sel), expected_base);
        snprintf(desc, sizeof(desc), "VESA win[%2d] (sel=0x%02X) limit", i, sel);
        CHECK_EQ16(desc, read_limit(sel), expected_limit);
    }
}

static void test_kcb_font(void)
{
    suite("SEL_KCB (0x98) i SEL_FONT (0x118)");
    CHECK_EQ32("SEL_KCB  (0x98)  base = g_kcb_phys",  read_base(0x98),  g_kcb_phys);
    CHECK_EQ32("SEL_FONT (0x118) base = g_font_phys",  read_base(0x118), g_font_phys);
}

static void test_gdt_access(void)
{
    suite("SEL_GDT_ACCESS (0x120)");
    unsigned long expected = g_cs_phys + (unsigned long)g_gdt_off_c;
    CHECK_EQ32("base = g_cs_phys + g_gdt_off_c", read_base(0x120), expected);
}

static void test_bitmaps(void)
{
    suite("SEL_BITMAPS (0x128)");
    CHECK_EQ32("base  = g_bitmaps_phys",   read_base(0x128),   g_bitmaps_phys);
    CHECK_EQ8 ("access = 0x92 (data RW)",  read_access(0x128), 0x92);
}

static void test_dll_entries(void)
{
    suite("DLL code/data selectors (0x48..0x5F)");

    /* DLL 0 (KERNEL) */
    CHECK_EQ32("DLL0 code (0x48) base  = g_dll_code_phys[0]", read_base(0x48),  g_dll_code_phys[0]);
    CHECK_EQ16("DLL0 code (0x48) limit = size-1",             read_limit(0x48), g_dll_code_size[0] - 1);
    CHECK_EQ8 ("DLL0 code (0x48) access = 0x9A (code ER)",    read_access(0x48), 0x9A);
    CHECK_EQ8 ("DLL0 code (0x48) flags  = 0x00 (16-bit)",     fake_gdt[0x48+6], 0x00);
    CHECK_EQ32("DLL0 data (0x50) base  = g_dll_data_phys[0]", read_base(0x50),  g_dll_data_phys[0]);
    CHECK_EQ16("DLL0 data (0x50) limit = 0xFFFF",             read_limit(0x50), 0xFFFF);
    CHECK_EQ8 ("DLL0 data (0x50) access = 0x92 (data RW)",    read_access(0x50), 0x92);

    /* DLL 1 (USER) */
    CHECK_EQ32("DLL1 code (0x58) base  = g_dll_code_phys[1]", read_base(0x58),  g_dll_code_phys[1]);
    CHECK_EQ16("DLL1 code (0x58) limit = size-1",             read_limit(0x58), g_dll_code_size[1] - 1);
    CHECK_EQ32("DLL1 data (0x60) base  = g_dll_data_phys[1]", read_base(0x60),  g_dll_data_phys[1]);

    /* DLL 2 (GDI) - has_data=0, data entry powinno byc wyzerowane (g_ndll=3 wlacza i=2) */
    CHECK_EQ32("DLL2 code (0x68) base  = g_dll_code_phys[2]", read_base(0x68),  g_dll_code_phys[2]);
    CHECK_EQ16("DLL2 code (0x68) limit = size-1",             read_limit(0x68), g_dll_code_size[2] - 1);
    CHECK_EQ8 ("DLL2 data (0x70) zeroed [0] when no data",    fake_gdt[0x70+0], 0x00);
    CHECK_EQ8 ("DLL2 data (0x70) zeroed [2] when no data",    fake_gdt[0x70+2], 0x00);
    CHECK_EQ8 ("DLL2 data (0x70) zeroed [5] when no data",    fake_gdt[0x70+5], 0x00);
}

/* -------------------------------------------------------------------
 * Testy warunkow brzegowych
 * ------------------------------------------------------------------- */

static void test_no_bitmaps(void)
{
    suite("g_bitmaps_phys = 0 -> SEL_BITMAPS nie powinno byc zapisane");
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    g_bitmaps_phys = 0;
    patch_gdt_c();
    CHECK_EQ8("SEL_BITMAPS access NOT written (stays FILL_BYTE)", read_access(0x128), FILL_BYTE);
    g_bitmaps_phys = 0x70000;  /* przywroc */
}

static void test_no_app_data(void)
{
    suite("g_has_data = 0 -> SEL_APP_DATA nie powinno byc zapisane");
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    g_has_data = 0;
    patch_gdt_c();
    CHECK("SEL_APP_DATA base[2] NOT written", fake_gdt[0x40 + 2] == FILL_BYTE);
    CHECK("SEL_APP_DATA base[7] NOT written", fake_gdt[0x40 + 7] == FILL_BYTE);
    g_has_data = 1;  /* przywroc */
}

static void test_base_byte_order(void)
{
    suite("Kolejnosc bajtow bazy w deskryptorze GDT");
    /* Sprawdz kazdy bajt bazy dla g_cs_phys = 0x10000 */
    /* base = 0x00010000 -> [2]=0x00 [3]=0x00 [4]=0x01 [7]=0x00 */
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned long saved = g_cs_phys;
    g_cs_phys = 0x12345678UL;
    patch_gdt_c();
    CHECK_EQ8("DATASEG base byte[2] = 0x78 (bits  7..0)", fake_gdt[0x18+2], 0x78);
    CHECK_EQ8("DATASEG base byte[3] = 0x56 (bits 15..8)", fake_gdt[0x18+3], 0x56);
    CHECK_EQ8("DATASEG base byte[4] = 0x34 (bits 23..16)",fake_gdt[0x18+4], 0x34);
    CHECK_EQ8("DATASEG base byte[7] = 0x12 (bits 31..24)",fake_gdt[0x18+7], 0x12);
    g_cs_phys = saved;
}

static void test_limit_byte_order(void)
{
    suite("Kolejnosc bajtow limitu w deskryptorze GDT");
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned short saved_sz = g_thunk_size;
    g_thunk_size = 0xABCD + 1;  /* limit = 0xABCD */
    patch_gdt_c();
    CHECK_EQ8("THUNK limit byte[0] = 0xCD (bits  7..0)", fake_gdt[0x88+0], 0xCD);
    CHECK_EQ8("THUNK limit byte[1] = 0xAB (bits 15..8)", fake_gdt[0x88+1], 0xAB);
    g_thunk_size = saved_sz;
}

static void test_ndll_zero(void)
{
    suite("g_ndll = 0 -> zadne DLL selektory nie powinny byc zapisane");
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned short saved_ndll = g_ndll;
    g_ndll = 0;
    patch_gdt_c();
    /* 0x48 = DLL0 code - nie powinno byc ruszone */
    CHECK("DLL0 code (0x48) NOT written", fake_gdt[0x48+2] == FILL_BYTE);
    g_ndll = saved_ndll;
}

/* -------------------------------------------------------------------
 * Testy SEL_HEAP (0x528) — XMS GlobalHeap
 * ------------------------------------------------------------------- */

static void test_heap_xms(void)
{
    suite("SEL_HEAP (0x528) z XMS: g_ext_mem_kb=14336 (14MB)");
    /* 14336 KB = 14*1024*1024 B = 0xE00000
     * G=1: enc = (0xE00000/4096) - 1 = 3584 - 1 = 3583 = 0x0DFF
     * lim_lo=0x0DFF, lim_hi=0, byte6 = 0xC0 (G=1 D/B=1)
     */
    CHECK_EQ32("SEL_HEAP (0x528) base = 0x100000",   read_base(0x528),   0x100000UL);
    CHECK_EQ16("SEL_HEAP (0x528) limit = 0x0DFF (G=1 enc)", read_limit(0x528), 0x0DFF);
    CHECK_EQ8 ("SEL_HEAP (0x528) access = 0x92 (data RW)", read_access(0x528), 0x92);
    CHECK_EQ8 ("SEL_HEAP (0x528) byte6  = 0xC0 (G=1 D/B=1)", fake_gdt[0x528+6], 0xC0);
}

static void test_heap_small_xms(void)
{
    suite("SEL_HEAP (0x528) z malym XMS: g_ext_mem_kb=64 (64KB, G=0)");
    /* 64 KB = 0x10000 B; warunek > 0x10000 jest falszywy -> G=0
     * lim_lo = 0x10000 - 1 = 0xFFFF, byte6 = 0x40 (G=0 D/B=1)
     */
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned long saved = g_ext_mem_kb;
    g_ext_mem_kb = 64;
    patch_gdt_c();
    CHECK_EQ32("SEL_HEAP base  = 0x100000",          read_base(0x528),   0x100000UL);
    CHECK_EQ16("SEL_HEAP limit = 0xFFFF (G=0)",       read_limit(0x528), 0xFFFF);
    CHECK_EQ8 ("SEL_HEAP access = 0x92",              read_access(0x528), 0x92);
    CHECK_EQ8 ("SEL_HEAP byte6  = 0x40 (G=0 D/B=1)", fake_gdt[0x528+6], 0x40);
    g_ext_mem_kb = saved;
}

static void test_heap_no_xms(void)
{
    suite("g_ext_mem_kb = 0 -> SEL_HEAP nie powinno byc zapisane");
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned long saved = g_ext_mem_kb;
    g_ext_mem_kb = 0;
    patch_gdt_c();
    CHECK("SEL_HEAP base[2]  NOT written (stays FILL_BYTE)", fake_gdt[0x528+2] == FILL_BYTE);
    CHECK("SEL_HEAP access   NOT written (stays FILL_BYTE)", fake_gdt[0x528+5] == FILL_BYTE);
    CHECK("SEL_HEAP byte6    NOT written (stays FILL_BYTE)", fake_gdt[0x528+6] == FILL_BYTE);
    g_ext_mem_kb = saved;
}

static void test_heap_large_xms(void)
{
    suite("SEL_HEAP z 256MB: g_ext_mem_kb=262144");
    /* 256 MB = 256*1024 KB = 0x10000000 B
     * G=1: enc = (0x10000000/4096) - 1 = 65536 - 1 = 65535 = 0xFFFF
     * lim_lo=0xFFFF, lim_hi=0, byte6 = 0xC0
     */
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    unsigned long saved = g_ext_mem_kb;
    g_ext_mem_kb = 262144UL;  /* 256MB */
    patch_gdt_c();
    CHECK_EQ16("SEL_HEAP limit = 0xFFFF (G=1 256MB)", read_limit(0x528), 0xFFFF);
    CHECK_EQ8 ("SEL_HEAP byte6 = 0xC0",               fake_gdt[0x528+6], 0xC0);
    g_ext_mem_kb = saved;
}

/* -------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_pm_helpers: patch_gdt_c() unit tests ===\n");

    /* Bazowy przebieg: pełna inicjalizacja */
    memset(fake_gdt, FILL_BYTE, sizeof(fake_gdt));
    patch_gdt_c();

    test_loader_segments();
    test_app_segments();
    test_thunk();
    test_vesa();
    test_kcb_font();
    test_gdt_access();
    test_bitmaps();
    test_dll_entries();

    /* SEL_HEAP (0x528) - XMS GlobalHeap */
    test_heap_xms();

    /* Testy warunkow brzegowych (kazdy resetuje bufor i ponownie wywoluje) */
    test_no_bitmaps();
    test_no_app_data();
    test_base_byte_order();
    test_limit_byte_order();
    test_ndll_zero();
    test_heap_small_xms();
    test_heap_no_xms();
    test_heap_large_xms();

    printf("\n=== Wynik: %d/%d testow OK ===\n",
           g_tests - g_failed, g_tests);
    return g_failed ? 1 : 0;
}
