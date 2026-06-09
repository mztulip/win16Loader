/* test_rc.c - ETAP 18: testy jednostkowe rc_loader
 *
 * Uruchamiany bezposrednio pod FreeDOS (bez PM / Win16 DLL).
 * Dwie fazy testow:
 *   Faza 1 (TESTRES.NE): syntetyczne zasoby generowane przez gen_testres.py.
 *   Faza 2 (W16TEST.EXE): prawdziwe zasoby wrc (RT_MENU/DIALOG/ACCEL).
 *
 * Kompilacja:
 *   wcc -ml -q test_rc.c -fo=test_rc.obj
 *   wcl -ml -l=dos -q test_rc.obj serial.obj rc_loader.obj -fe=test_rc.exe
 *
 * Wyjscie: printf (VGA konsola) + serial_puts (COM1 -> QEMU -serial stdio).
 *
 * --- Oczekiwana zawartosc TESTRES.NE (generuje gen_testres.py) ----------
 *
 * RT_STRING blok 1 (block_id=1, stringi ID 0..15):
 *   ID 1 -> "Hello Test"  (len=10)
 *   ID 3 -> "World RC"    (len=8)
 *   pozostale: puste
 * RT_STRING blok 2 (block_id=2, stringi ID 16..31):
 *   ID 17 -> "ABCDE"      (len=5)
 * RT_BITMAP ID 1:
 *   16 B: DE AD BE EF 01 02 03 04 05 06 07 08 09 0A 0B 0C
 * RT_MENU ID 1:
 *   16 B: 10 11 12 ... 1F
 * RT_ACCEL ID 1:
 *   16 B: 20 21 22 ... 2F
 *
 * --- KCB (po rc_load_all) -----------------------------------------------
 *
 * KCB_RSC_DATA_OFF = 32 (po naglowku RC_KCB 32B)
 * RC_KCB offsets (packed):
 *   +18: rsc_nblocks (1B)
 *   +20: rsc_block_ids[0] (2B LE)
 *   +22: rsc_block_ids[1] (2B LE)
 *   +24: rsc_block_sizes[0] (2B LE)
 *   +26: rsc_block_sizes[1] (2B LE)
 *   +32: blok1 dane (48B), +80: blok2 dane (32B)
 *
 * BMP_BUF_HDR = 4 + 86*2 = 176
 * bmp_buf[0..1]: count, [4..5]: offset id=1 = 176, [176..191]: bitmap1 data
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include "serial.h"
#include "rc_loader.h"

/* ---------------------------------------------------------- */
/* Kprintf: stdout + serial (jak w loader.c)                  */
/* ---------------------------------------------------------- */
static void tprintf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    serial_puts(buf);
}

/* ---------------------------------------------------------- */
/* Makra testowe                                               */
/* ---------------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) do { \
    if (cond) { tprintf("PASS: %s\n", desc); g_pass++; } \
    else      { tprintf("FAIL: %s\n", desc); g_fail++; } \
} while(0)

#define RD16(p, off) ((unsigned short)((p)[(off)] | ((unsigned short)(p)[(off)+1] << 8)))

/* ---------------------------------------------------------- */
/* Offsety w KCB (muszą pasowac do RC_KCB w rc_loader.c)      */
/* ---------------------------------------------------------- */
#define KCB_OFF_RSC_NBLOCKS    18u
#define KCB_OFF_RSC_BLOCK_ID0  20u
#define KCB_OFF_RSC_BLOCK_ID1  22u
#define KCB_OFF_RSC_BLOCK_SZ0  24u
#define KCB_OFF_RSC_BLOCK_SZ1  26u
#define KCB_RSC_DATA           32u

/* ---------------------------------------------------------- */
/* Stale zgodne z rc_loader.c                                 */
/* ---------------------------------------------------------- */
#define MAX_BITMAPS    86u
#define BMP_BUF_HDR    (4u + MAX_BITMAPS * 2u)   /* = 176 */
#define BMP_BUF_PARA   3584u

/* ---------------------------------------------------------- */
/* Pomocnicze: wyzeruj bufor far                              */
/* ---------------------------------------------------------- */
static void far_zero(unsigned char __far *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) p[i] = 0;
}

/* ======================================================== */
/* main                                                      */
/* ======================================================== */
int main(void)
{
    unsigned kcb_seg, bmp_seg;
    unsigned long kcb_phys, bmp_phys;
    unsigned char __far *kcb;
    unsigned char __far *bmp;
    unsigned char __far *blk1;
    unsigned char __far *blk2;

    serial_init();
    tprintf("\n=== test_rc: ETAP 18 rc_loader ===\n");

    /* Alokacja buforow DOS.
     * KCB: 256 B (16 paragrafow)
     * BMP: BMP_BUF_PARA paragrafow = 56 KB */
    if (_dos_allocmem(16u, &kcb_seg) != 0) {
        tprintf("ERR: _dos_allocmem KCB\n");
        return 1;
    }
    if (_dos_allocmem(BMP_BUF_PARA, &bmp_seg) != 0) {
        tprintf("ERR: _dos_allocmem BMP\n");
        _dos_freemem(kcb_seg);
        return 1;
    }
    kcb_phys = (unsigned long)kcb_seg * 16UL;
    bmp_phys = (unsigned long)bmp_seg * 16UL;
    kcb = (unsigned char __far *)MK_FP(kcb_seg, 0);
    bmp = (unsigned char __far *)MK_FP(bmp_seg, 0);
    far_zero(kcb, 256u);
    far_zero(bmp, (unsigned)(BMP_BUF_HDR + 64u)); /* tylko naglowek + troche danych */

    /* ======================================================
     * Test 1: find_ne_resource (przed rc_load_all)
     * ====================================================== */
    tprintf("\n-- find_ne_resource --\n");
    rc_init("TESTRES.NE");
    {
        long  off;
        unsigned short sz;
        int r;

        r = find_ne_resource(RT_BITMAP, 1, &off, &sz);
        CHECK("find RT_BITMAP id=1 found",   r  == 1);
        CHECK("find RT_BITMAP id=1 size=16", sz == 16);

        r = find_ne_resource(RT_STRING, 1, &off, &sz);
        CHECK("find RT_STRING blok1 found",  r  == 1);
        CHECK("find RT_STRING blok1 size=48",sz == 48);

        r = find_ne_resource(RT_MENU, 1, &off, &sz);
        CHECK("find RT_MENU id=1 found",     r  == 1);
        CHECK("find RT_MENU id=1 size=16",   sz == 16);

        r = find_ne_resource(RT_ACCEL, 1, &off, &sz);
        CHECK("find RT_ACCEL id=1 found",    r  == 1);

        r = find_ne_resource(RT_BITMAP, 99, &off, &sz);
        CHECK("find RT_BITMAP id=99 not found", r == 0);
    }

    /* ======================================================
     * Test 2: rc_load_all -> KCB + BMP buffer
     * ====================================================== */
    tprintf("\n-- rc_load_all --\n");
    rc_init("TESTRES.NE");
    rc_load_all(kcb_phys, bmp_phys);

    /* --- KCB naglowek --- */
    tprintf("\n-- KCB header --\n");
    CHECK("rsc_nblocks == 2",          kcb[KCB_OFF_RSC_NBLOCKS] == 2);
    CHECK("rsc_block_ids[0] == 1",     RD16(kcb, KCB_OFF_RSC_BLOCK_ID0) == 1);
    CHECK("rsc_block_ids[1] == 2",     RD16(kcb, KCB_OFF_RSC_BLOCK_ID1) == 2);
    CHECK("rsc_block_sizes[0] == 48",  RD16(kcb, KCB_OFF_RSC_BLOCK_SZ0) == 48);
    CHECK("rsc_block_sizes[1] == 32",  RD16(kcb, KCB_OFF_RSC_BLOCK_SZ1) == 32);

    /* --- RT_STRING blok 1: stringi 0..15 ---
     * blok1[0]    = 0x00 (str0: pusta)
     * blok1[1]    = 0x0A (str1: len=10)
     * blok1[2..11]= "Hello Test"
     * blok1[12]   = 0x00 (str2: pusta)
     * blok1[13]   = 0x08 (str3: len=8)
     * blok1[14..21]= "World RC"
     */
    tprintf("\n-- RT_STRING blok 1 --\n");
    blk1 = kcb + KCB_RSC_DATA;
    {
        /* Oczekiwane 48 bajtow bloku 1 (stringi ID 0..15):
         * [0]     = 0x00         str0: pusta
         * [1]     = 0x0A         str1: len=10
         * [2..11] = "Hello Test"
         * [12]    = 0x00         str2: pusta
         * [13]    = 0x08         str3: len=8
         * [14..21]= "World RC"
         * [22..47]= 0x00 * 26   str4..str15: puste
         */
        static const unsigned char expected_blk1[48] = {
            0x00,                                           /* str0: pusta */
            0x0A,'H','e','l','l','o',' ','T','e','s','t',  /* str1: "Hello Test" */
            0x00,                                           /* str2: pusta */
            0x08,'W','o','r','l','d',' ','R','C',           /* str3: "World RC" */
            /* str4..str15: puste (26 bajtow) */
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        unsigned i;
        int all_ok = 1;
        for (i = 0; i < 48u; i++)
            if (blk1[i] != expected_blk1[i]) { all_ok = 0; break; }
        CHECK("blok1: wszystkie 48B poprawne", all_ok);
    }

    /* --- RT_STRING blok 2: stringi 16..31 ---
     * blok2[0]   = 0x00         str16: pusta
     * blok2[1]   = 0x05         str17: len=5
     * blok2[2..6]= "ABCDE"
     * blok2[7..31]= 0x00 * 25  str18..str31: puste
     */
    tprintf("\n-- RT_STRING blok 2 --\n");
    blk2 = kcb + KCB_RSC_DATA + 48u;   /* po bloku 1 */
    {
        static const unsigned char expected_blk2[32] = {
            0x00,                           /* str16: pusta */
            0x05,'A','B','C','D','E',        /* str17: "ABCDE" */
            /* str18..str31: puste (25 bajtow) */
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        unsigned i;
        int all_ok = 1;
        for (i = 0; i < 32u; i++)
            if (blk2[i] != expected_blk2[i]) { all_ok = 0; break; }
        CHECK("blok2: wszystkie 32B poprawne", all_ok);
    }

    /* --- RT_BITMAP ---
     * bmp[0..1]: liczba bitmap
     * bmp[4..5]: offset id=1 = BMP_BUF_HDR = 176
     * bmp[176..191]: dane bitmap1
     */
    tprintf("\n-- RT_BITMAP --\n");
    {
        /* oczekiwana zawartosc: DE AD BE EF 01 02 03 04 05 06 07 08 09 0A 0B 0C */
        static const unsigned char expected_bmp[16] = {
            0xDE,0xAD,0xBE,0xEF, 0x01,0x02,0x03,0x04,
            0x05,0x06,0x07,0x08, 0x09,0x0A,0x0B,0x0C
        };
        unsigned char __far *p = bmp + BMP_BUF_HDR;
        unsigned i;
        int all_ok = 1;
        CHECK("bmp count == 1",       RD16(bmp, 0) == 1);
        CHECK("bmp offset[0] == 176", RD16(bmp, 4) == BMP_BUF_HDR);
        for (i = 0; i < 16u; i++)
            if (p[i] != expected_bmp[i]) { all_ok = 0; break; }
        CHECK("bitmap1 wszystkie 16B (DE AD BE EF 01..0C)", all_ok);
    }

    /* --- RT_MENU (RSC_HEAP) - weryfikacja wszystkich 16 bajtow --- */
    tprintf("\n-- RT_MENU (RSC_HEAP) --\n");
    {
        unsigned char __far *m  = rc_get(RT_MENU, 1);
        unsigned short       sz = rc_size(RT_MENU, 1);
        unsigned             i;
        int                  all_ok = 1;
        CHECK("RT_MENU id=1 != NULL",  m  != NULL);
        CHECK("RT_MENU id=1 size==16", sz == 16);
        if (m) {
            for (i = 0; i < 16u; i++)
                if (m[i] != (unsigned char)(0x10u + i)) { all_ok = 0; break; }
            CHECK("RT_MENU wszystkie 16B (0x10..0x1F)", all_ok);
        } else { g_fail++; }
    }

    /* --- RT_ACCEL (RSC_HEAP) - weryfikacja wszystkich 16 bajtow --- */
    tprintf("\n-- RT_ACCEL (RSC_HEAP) --\n");
    {
        unsigned char __far *a  = rc_get(RT_ACCEL, 1);
        unsigned short       sz = rc_size(RT_ACCEL, 1);
        unsigned             i;
        int                  all_ok = 1;
        CHECK("RT_ACCEL id=1 != NULL",  a  != NULL);
        CHECK("RT_ACCEL id=1 size==16", sz == 16);
        if (a) {
            for (i = 0; i < 16u; i++)
                if (a[i] != (unsigned char)(0x20u + i)) { all_ok = 0; break; }
            CHECK("RT_ACCEL wszystkie 16B (0x20..0x2F)", all_ok);
        } else { g_fail++; }
    }

    /* --- rc_get dla nieznanego zasobu --- */
    tprintf("\n-- rc_get brakujacy zasob --\n");
    CHECK("RT_MENU id=99 == NULL",  rc_get(RT_MENU, 99)  == NULL);
    CHECK("RT_ACCEL id=2 == NULL",  rc_get(RT_ACCEL, 2)  == NULL);
    CHECK("rc_size brakujacy == 0", rc_size(RT_MENU, 99) == 0);

    /* ======================================================
     * Faza 2: prawdziwe zasoby wrc z W16TEST.EXE
     *
     * W16TEST.EXE (STEP18/win16test/w16test.exe) zawiera:
     *   RT_MENU   id=101 size=38  - MENUITEMTEMPLATEHEADER + "File/Exit, Help/About"
     *   RT_DIALOG id=103 size=72  - DLGTEMPLATE "About" (2 items, 160x60)
     *   RT_ACCEL  id=102 size=10  - 2 akceleratory: ^A(ID=201), ^Q(ID=200)
     *
     * --- Struktura binarnych szablonow Win16 ---
     *
     * RT_MENU (MENUITEMTEMPLATEHEADER + MENUITEMTEMPLATE):
     *   bytes[0..3] = {0,0,0,0}   MENUITEMTEMPLATEHEADER: versionNumber=0, offset=0
     *   bytes[4..5] = {0x10,0x00} mtOption = MF_POPUP (0x10) = popup "File"
     *   bytes[6..10]= "File\0"
     *   bytes[11..12]={0x80,0x00} MF_END (ostatni element popupu)
     *   bytes[13..14]={200,0}     mtID = 200 = ID_FILE_EXIT
     *   bytes[15..19]="Exit\0"
     *   bytes[20..21]={0x90,0x00} MF_POPUP|MF_END (ostatni na top-level)
 *   bytes[22..26]= "Help\0"
     *
     * RT_DIALOG (DLGTEMPLATE Win16):
     *   bytes[0..3]  = {0x00,0x00,0xC8,0x80}  dtStyle=WS_POPUP|WS_CAPTION|WS_SYSMENU
     *   byte[4]      = 2                        dtItemCount=2 (LTEXT+DEFPUSHBUTTON)
     *   bytes[5..6]  = {0,0}                   dtX=0
     *   bytes[7..8]  = {0,0}                   dtY=0
     *   bytes[9..10] = {160,0}                 dtCX=160
     *   bytes[11..12]= {60,0}                  dtCY=60
     *   byte[13]     = 0                        dtMenuName="" (brak)
     *   byte[14]     = 0                        dtClassName="" (domyslna)
     *   bytes[15..19]= "About"                  dtCaptionText
     *
     * RT_ACCEL (5 bajtow na wpis: fVirt(B) + key(W) + cmd(W)):
     *   bytes[0]    = 0x02         fVirt = NOINVERT
     *   bytes[1..2] = {1,0}        key   = 1 = ^A (ASCII)
     *   bytes[3..4] = {201,0}      cmd   = 201 = ID_HELP_ABOUT
     *   bytes[5]    = 0x82         fVirt = FLASTKEY(0x80)|NOINVERT(0x02)
     *   bytes[6..7] = {17,0}       key   = 17 = ^Q (ASCII)
     *   bytes[8..9] = {200,0}      cmd   = 200 = ID_FILE_EXIT
     * ============================================================ */
    tprintf("\n=== Faza 2: W16TEST.EXE (prawdziwe zasoby wrc) ===\n");

    /* --- find_ne_resource w W16TEST.EXE --- */
    tprintf("\n-- W16TEST find_ne_resource --\n");
    far_zero(kcb, 256u);
    rc_init("W16TEST.EXE");
    {
        long off;
        unsigned short sz;
        int r;
        r = find_ne_resource(RT_MENU, 101, &off, &sz);
        CHECK("W16TEST find RT_MENU id=101",   r  == 1);
        CHECK("W16TEST RT_MENU size==38",       sz == 38);
        r = find_ne_resource(RT_DIALOG, 103, &off, &sz);
        CHECK("W16TEST find RT_DIALOG id=103", r  == 1);
        CHECK("W16TEST RT_DIALOG size==72",     sz == 72);
        r = find_ne_resource(RT_ACCEL, 102, &off, &sz);
        CHECK("W16TEST find RT_ACCEL id=102",  r  == 1);
        CHECK("W16TEST RT_ACCEL size==10",      sz == 10);
    }

    /* --- rc_load_all na W16TEST.EXE --- */
    tprintf("\n-- W16TEST rc_load_all --\n");
    far_zero(kcb, 256u);
    rc_init("W16TEST.EXE");
    rc_load_all(kcb_phys, bmp_phys);

    /* Brak RT_STRING w W16TEST */
    CHECK("W16TEST rsc_nblocks==0", kcb[KCB_OFF_RSC_NBLOCKS] == 0);

    /* --- RT_MENU: szablon MENUITEMTEMPLATEHEADER --- */
    tprintf("\n-- W16TEST RT_MENU (wrc template) --\n");
    {
        unsigned char __far *m  = rc_get(RT_MENU, 101);
        unsigned short       sz = rc_size(RT_MENU, 101);
        CHECK("W16TEST RT_MENU != NULL",        m  != NULL);
        CHECK("W16TEST RT_MENU size==38",        sz == 38);
        if (m) {
            /* MENUITEMTEMPLATEHEADER: versionNumber=0, offset=0 */
            CHECK("W16TEST menu[0]==0 (ver lo)", m[0] == 0x00);
            CHECK("W16TEST menu[1]==0 (ver hi)", m[1] == 0x00);
            CHECK("W16TEST menu[2]==0 (off lo)", m[2] == 0x00);
            CHECK("W16TEST menu[3]==0 (off hi)", m[3] == 0x00);
            /* Pierwszy MENUITEMTEMPLATE: mtOption=MF_POPUP(0x10) dla "File" */
            CHECK("W16TEST menu[4]==0x10 (MF_POPUP)", m[4] == 0x10);
            /* mtString = "File\0": bytes[6..10] */
            CHECK("W16TEST menu 'F'ile",  m[6]  == 'F');
            CHECK("W16TEST menu 'E'xit",  m[15] == 'E');
        } else { g_fail += 7; }
    }

    /* --- RT_DIALOG: DLGTEMPLATE Win16 --- */
    tprintf("\n-- W16TEST RT_DIALOG (wrc template) --\n");
    {
        unsigned char __far *dlg = rc_get(RT_DIALOG, 103);
        unsigned short       sz  = rc_size(RT_DIALOG, 103);
        CHECK("W16TEST RT_DIALOG != NULL",       dlg != NULL);
        CHECK("W16TEST RT_DIALOG size==72",       sz  == 72);
        if (dlg) {
            /* dtStyle = WS_POPUP|WS_CAPTION|WS_SYSMENU = 0x80C80000
             * LE bytes: 0x00, 0x00, 0xC8, 0x80 */
            CHECK("W16TEST dlg style[2]==0xC8",  dlg[2] == 0xC8);
            CHECK("W16TEST dlg style[3]==0x80",  dlg[3] == 0x80);
            /* dtItemCount = 2 (LTEXT + DEFPUSHBUTTON) */
            CHECK("W16TEST dlg dtItemCount==2",   dlg[4] == 2);
            /* dtCX = 160 (szerokosc dialogu) */
            CHECK("W16TEST dlg dtCX==160",        RD16(dlg, 9)  == 160);
            /* dtCY = 60 (wysokosc dialogu) */
            CHECK("W16TEST dlg dtCY==60",         RD16(dlg, 11) == 60);
            /* dtCaptionText = "About..." (byte[15]='A') */
            CHECK("W16TEST dlg caption[0]=='A'",  dlg[15] == 'A');
        } else { g_fail += 6; }
    }

    /* --- RT_DIALOG: dodatkowe sprawdzenie napisu "Win16 Test App" w itemach ---
     * Po naglowku DLGTEMPLATE i stringach (caption "About\0") nastepuje
     * pierwsza pozycja DLGITEMTEMPLATE: tekst "Win16 Test App" gdzies w danych. */
    tprintf("\n-- W16TEST RT_DIALOG: szukanie tekstu w itemach --\n");
    {
        unsigned char __far *dlg = rc_get(RT_DIALOG, 103);
        unsigned             i;
        int found = 0;
        if (dlg) {
            /* Szukaj "Win16" gdziekolwiek w szablonie (szybsze niz parsowanie itemow) */
            for (i = 0; i + 4u < 72u; i++) {
                if (dlg[i]=='W' && dlg[i+1]=='i' && dlg[i+2]=='n' &&
                    dlg[i+3]=='1' && dlg[i+4]=='6') { found = 1; break; }
            }
            CHECK("W16TEST dlg zawiera tekst 'Win16'", found);
            /* Szukaj "OK" (DEFPUSHBUTTON caption) */
            found = 0;
            for (i = 0; i + 1u < 72u; i++) {
                if (dlg[i]=='O' && dlg[i+1]=='K') { found = 1; break; }
            }
            CHECK("W16TEST dlg zawiera tekst 'OK'", found);
        } else { g_fail += 2; }
    }

    /* --- RT_ACCEL: weryfikacja wszystkich 10 bajtow ---
     * Format: fVirt(1B) + key(2B) + cmd(2B) = 5 bajtow na wpis
     * Ostatni wpis: fVirt & 0x80 (FLASTKEY)
     * Oczekiwane: 02 01 00 C9 00  82 11 00 C8 00 */
    tprintf("\n-- W16TEST RT_ACCEL (wrc template) --\n");
    {
        static const unsigned char expected_accel[10] = {
            0x02, 0x01, 0x00, 0xC9, 0x00,   /* ^A, ID=201=0xC9 */
            0x82, 0x11, 0x00, 0xC8, 0x00    /* ^Q, ID=200=0xC8, FLASTKEY */
        };
        unsigned char __far *acc = rc_get(RT_ACCEL, 102);
        unsigned short       sz  = rc_size(RT_ACCEL, 102);
        unsigned             i;
        int                  all_ok = 1;
        CHECK("W16TEST RT_ACCEL != NULL",  acc != NULL);
        CHECK("W16TEST RT_ACCEL size==10", sz  == 10);
        if (acc) {
            for (i = 0; i < 10u; i++)
                if (acc[i] != expected_accel[i]) { all_ok = 0; break; }
            CHECK("W16TEST accel wszystkie 10B poprawne", all_ok);
            /* Semantyczne: fVirt, key, cmd wpis 1 */
            CHECK("W16TEST accel[0]==NOINVERT(0x02)", acc[0] == 0x02);
            CHECK("W16TEST accel key1==1 (^A)",        RD16(acc, 1) == 1);
            CHECK("W16TEST accel cmd1==201",            RD16(acc, 3) == 201);
            /* Semantyczne: fVirt, key, cmd wpis 2 */
            CHECK("W16TEST accel[5]==FLASTKEY(0x82)", acc[5] == 0x82);
            CHECK("W16TEST accel key2==17 (^Q)",       RD16(acc, 6) == 17);
            CHECK("W16TEST accel cmd2==200",            RD16(acc, 8) == 200);
        } else { g_fail += 8; }
    }

    /* --- RT_MENU: weryfikacja napisow "File", "Exit", "Help", "About" --- */
    tprintf("\n-- W16TEST RT_MENU: nazwy pozycji --\n");
    {
        unsigned char __far *m = rc_get(RT_MENU, 101);
        if (m) {
            /* "File\0" zaczyna sie od bajtu[6] */
            CHECK("W16TEST menu popup1='F'ile",  m[6]=='F' && m[7]=='i' && m[8]=='l' && m[9]=='e' && m[10]==0);
            /* "Exit\0": mtID=200 w bytes[13..14], string od bajtu[15] */
            CHECK("W16TEST menu item 'E'xit",    m[15]=='E' && m[16]=='x' && m[17]=='i' && m[18]=='t' && m[19]==0);
            /* Drugi popup "Help" zaczyna sie od bajtu[20]: mtOption=MF_POPUP|MF_END=0x90
             * (MF_END=0x80 bo ostatni element na poziomie top-level) */
            CHECK("W16TEST menu popup2 MF_POPUP|MF_END", (m[20] & 0x10) && m[21]==0x00);
            CHECK("W16TEST menu popup2='H'elp",  m[22]=='H' && m[23]=='e' && m[24]=='l' && m[25]=='p' && m[26]==0);
        } else { g_fail += 4; }
    }

    /* ======================================================
     * Podsumowanie
     * ====================================================== */
    _dos_freemem(kcb_seg);
    _dos_freemem(bmp_seg);

    tprintf("\n=== WYNIK: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    if (g_fail == 0)
        tprintf("=== ALL PASSED ===\n");
    else
        tprintf("=== FAILED ===\n");

    return g_fail;
}
