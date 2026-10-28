/*
 * test14e.c - ETAP 14e: test GlobalAlloc > 64KB z XMS
 *
 * Testy A (podstawowe):
 *  1. GlobalAlloc(1MB/512KB/256KB) zwraca selektor != 0
 *  2. Kazdy selektor: zapis calych 64KB wzorcem, weryfikacja odczytu
 *  3. Po zapisie h2+h3 wzorzec h1 nienaruszony (brak aliasingu)
 *  4. Kolejnosc slotow GDT (+8 per alokacja)
 *  5. GlobalAlloc(0) -> selektor != 0
 *
 * Test B (sweep 1MB):
 *  6. 16 alokacji × 64KB = 1MB lacznie
 *     - fill wzorcem kazdy blok (seed = indeks bloku)
 *     - weryfikacja wszystkich 16 blokow naraz
 *     Pokrywa caly 1MB obszar XMS, wykrywa przekroczenie granic blokow.
 *
 * Wzorzec 64KB: pat[off] = seed ^ (u8)off ^ (u8)(off>>8)
 *   Unikalny dla kazdej pary (blok, offset) -> wykrywa GPF, aliasing, bledna baze.
 */

extern void     __far __pascal OutputDebugString(const char __far *s);
extern unsigned __far __pascal GlobalAlloc(unsigned wFlags, unsigned long dwBytes);
extern void     __far __pascal PostQuitMessage(int exitCode);

#define GMEM_FIXED 0x0000

#define MK_FP(seg, off) \
    ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(off)))

/* ---------------------------------------------------------------
 * Liczniki i raportowanie
 * --------------------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;

static void report(int ok, const char *desc)
{
    if (ok) { OutputDebugString("14e PASS: "); g_pass++; }
    else     { OutputDebugString("14e FAIL: "); g_fail++; }
    OutputDebugString(desc);
    OutputDebugString("\n");
}

/* Hex bez biblioteki C (Watcom -zl) */
static void phex16(char *buf4, unsigned short v)
{
    int i;
    for (i = 3; i >= 0; i--) { buf4[i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
    buf4[4] = '\0';
}

static void print_hex(unsigned short v)
{
    char buf[5];
    phex16(buf, v);
    OutputDebugString(buf);
}

/* ---------------------------------------------------------------
 * fill_pattern: zapisz cale 64KB selektora wzorcem
 *   wzorzec[off] = seed ^ (unsigned char)off ^ (unsigned char)(off >> 8)
 *   Kazdy offset daje inna wartosc (w granicach bajtu), kazdy seed inna serie.
 * --------------------------------------------------------------- */
static void fill_pattern(unsigned sel, unsigned char seed)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(sel, 0);
    unsigned short i = 0;
    do {
        p[i] = (unsigned char)(seed ^ (unsigned char)i ^ (unsigned char)(i >> 8));
        i++;
    } while (i != 0);  /* owinięcie z 0xFFFF -> 0x0000 = koniec petli */
}

/* ---------------------------------------------------------------
 * check_pattern: weryfikuj 64KB - zwraca liczbe bledow (0 = OK)
 * --------------------------------------------------------------- */
static unsigned short check_pattern(unsigned sel, unsigned char seed)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(sel, 0);
    unsigned short errors = 0;
    unsigned short i = 0;
    do {
        unsigned char expected = (unsigned char)(seed ^ (unsigned char)i ^ (unsigned char)(i >> 8));
        if (p[i] != expected) {
            if (errors == 0) {
                /* Wyswietl pierwszy blad */
                OutputDebugString("14e ERR: off=0x");
                print_hex(i);
                OutputDebugString(" got=0x");
                {char b[5]; b[0]='0';b[1]='0';b[2]="0123456789ABCDEF"[p[i]>>4];b[3]="0123456789ABCDEF"[p[i]&0xF];b[4]=0; OutputDebugString(b);}
                OutputDebugString("\n");
            }
            errors++;
        }
        i++;
    } while (i != 0);
    return errors;
}

/* ---------------------------------------------------------------
 * test_1mb_sweep: 16 blokow × 64KB = 1MB
 *
 * Schemat:
 *   1. Alokuj 16 × GlobalAlloc(GMEM_FIXED, 64*1024)
 *   2. Fill kazdy blok wzorcem seed=i (i=0..15)
 *   3. Weryfikuj wszystkie 16 blokow (po zapisaniu ostatniego)
 *      -> GPF jesli jakikolwiek deskryptor GDT jest bledny
 *      -> blad wzorca jesli bloki zachodza na siebie
 * --------------------------------------------------------------- */
#define SWEEP_BLOCKS 16

static void test_1mb_sweep(void)
{
    static unsigned sels[SWEEP_BLOCKS];  /* static: nie na stosie (SS!=DS w DLL) */
    unsigned char i;
    unsigned short all_ok;

    OutputDebugString("14e INFO: sweep 1MB (16x64KB)...\n");

    /* Faza 1: alokuj wszystkie bloki */
    for (i = 0; i < SWEEP_BLOCKS; i++) {
        sels[i] = GlobalAlloc(GMEM_FIXED, 64UL * 1024UL);
        if (sels[i] == 0) {
            report(0, "sweep: GlobalAlloc(64KB) zwrocil 0");
            return;
        }
    }
    report(1, "sweep: 16 alokacji 64KB OK");

    /* Faza 2: fill wzorcem (seed = indeks bloku) */
    for (i = 0; i < SWEEP_BLOCKS; i++)
        fill_pattern(sels[i], i);
    report(1, "sweep: fill 16x64KB bez GPF");

    /* Faza 3: weryfikacja wszystkich blokow */
    all_ok = 1;
    for (i = 0; i < SWEEP_BLOCKS; i++) {
        unsigned short errs = check_pattern(sels[i], i);
        if (errs != 0) {
            all_ok = 0;
            OutputDebugString("14e ERR: sweep blok ");
            { char b[3]; b[0]='0'+(char)(i/10); b[1]='0'+(char)(i%10); b[2]=0; OutputDebugString(b); }
            OutputDebugString(" ma bledy\n");
        }
    }
    report(all_ok, "sweep: weryfikacja 16x64KB (1MB lacznie) bez bledow");

    /* Dodatkowa kontrola: bloki mają selektory co 8 */
    all_ok = 1;
    for (i = 1; i < SWEEP_BLOCKS; i++) {
        if (sels[i] != sels[i-1] + 8) { all_ok = 0; break; }
    }
    report(all_ok, "sweep: selektory rosna co 8 (16 kolejnych slotow GDT)");
}

/* ---------------------------------------------------------------
 * app_entry
 * --------------------------------------------------------------- */
void __far app_entry(void)
{
    unsigned h1, h2, h3;
    char buf[5];

    OutputDebugString("=== ETAP 14e: GlobalAlloc XMS pattern test ===\n");

    /* ------- Test 1: alokacja 1MB ---------------------------------------- */
    h1 = GlobalAlloc(GMEM_FIXED, 1024UL * 1024UL);
    report(h1 != 0, "GlobalAlloc(1MB) sel != 0");
    if (h1 == 0) goto summary;

    /* ------- Test 2: zapis + weryfikacja wzorca 64KB w h1 ----------------- */
    OutputDebugString("14e INFO: fill h1 (seed=0xA5)...\n");
    fill_pattern(h1, 0xA5);
    {
        unsigned short errs = check_pattern(h1, 0xA5);
        report(errs == 0, "h1: wzorzec 64KB bez bledow");
    }

    /* ------- Test 3: alokacja 512KB --------------------------------------- */
    h2 = GlobalAlloc(GMEM_FIXED, 512UL * 1024UL);
    report(h2 != 0, "GlobalAlloc(512KB) sel != 0");
    if (h2 == 0) goto summary;

    /* ------- Test 4: h2 wzorzec 64KB ------------------------------------- */
    OutputDebugString("14e INFO: fill h2 (seed=0x5A)...\n");
    fill_pattern(h2, 0x5A);
    {
        unsigned short errs = check_pattern(h2, 0x5A);
        report(errs == 0, "h2: wzorzec 64KB bez bledow");
    }

    /* ------- Test 5: h1 wzorzec nienaruszony po zapisie h2 ---------------- */
    {
        unsigned short errs = check_pattern(h1, 0xA5);
        report(errs == 0, "h1 wzorzec nienaruszony po fill h2 (brak aliasingu)");
    }

    /* ------- Test 6: alokacja 256KB --------------------------------------- */
    h3 = GlobalAlloc(GMEM_FIXED, 256UL * 1024UL);
    report(h3 != 0, "GlobalAlloc(256KB) sel != 0");
    if (h3 == 0) goto summary;

    /* ------- Test 7: h3 wzorzec 64KB ------------------------------------- */
    OutputDebugString("14e INFO: fill h3 (seed=0x3C)...\n");
    fill_pattern(h3, 0x3C);
    {
        unsigned short errs = check_pattern(h3, 0x3C);
        report(errs == 0, "h3: wzorzec 64KB bez bledow");
    }

    /* ------- Test 8: h1 i h2 wzorce nienaruszone po zapisie h3 ----------- */
    {
        unsigned short e1 = check_pattern(h1, 0xA5);
        unsigned short e2 = check_pattern(h2, 0x5A);
        report(e1 == 0, "h1 wzorzec nienaruszony po fill h3");
        report(e2 == 0, "h2 wzorzec nienaruszony po fill h3");
    }

    /* ------- Test 9: selektory rosna po 8 (kolejne sloty GDT) ------------ */
    report(h2 == h1 + 8, "h2 == h1+8 (kolejny slot GDT)");
    report(h3 == h2 + 8, "h3 == h2+8 (kolejny slot GDT)");

    /* ------- Test 10: GlobalAlloc(0) -> min 16B -------------------------- */
    {
        unsigned h0 = GlobalAlloc(GMEM_FIXED, 0UL);
        report(h0 != 0, "GlobalAlloc(0) -> min 16B, sel != 0");
    }

    /* ------- Test B: sweep 1MB (16 × 64KB) -------------------------------- */
    test_1mb_sweep();

    /* ------- Info --------------------------------------------------------- */
    OutputDebugString("14e INFO: h1=0x"); print_hex(h1);
    OutputDebugString(" h2=0x");          print_hex(h2);
    OutputDebugString(" h3=0x");          print_hex(h3);
    OutputDebugString("\n");

summary:
    {
        int total = g_pass + g_fail;
        OutputDebugString("14e DONE: ");
        buf[0] = '0' + (char)(g_pass / 10);
        buf[1] = '0' + (char)(g_pass % 10);
        buf[2] = '/'; buf[3] = '\0';
        OutputDebugString(buf);
        buf[0] = '0' + (char)(total / 10);
        buf[1] = '0' + (char)(total % 10);
        buf[2] = '\0';
        OutputDebugString(buf);
        OutputDebugString(" OK\n");
    }

    PostQuitMessage(0);
}
