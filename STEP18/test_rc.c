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
    CHECK("blok1: str0 pusta",         blk1[0]  == 0x00);
    CHECK("blok1: str1 len==10",       blk1[1]  == 10);
    CHECK("blok1: str1[0]=='H'",       blk1[2]  == 'H');
    CHECK("blok1: str1[4]=='o'",       blk1[6]  == 'o');
    CHECK("blok1: str1[9]=='t'",       blk1[11] == 't');
    CHECK("blok1: str2 pusta",         blk1[12] == 0x00);
    CHECK("blok1: str3 len==8",        blk1[13] == 8);
    CHECK("blok1: str3[0]=='W'",       blk1[14] == 'W');
    CHECK("blok1: str3[7]=='C'",       blk1[21] == 'C');

    /* --- RT_STRING blok 2: stringi 16..31 ---
     * blok2[0]   = 0x00 (str16: pusta)
     * blok2[1]   = 0x05 (str17: len=5)
     * blok2[2..6]= "ABCDE"
     */
    tprintf("\n-- RT_STRING blok 2 --\n");
    blk2 = kcb + KCB_RSC_DATA + 48u;   /* po bloku 1 */
    CHECK("blok2: str16 pusta",        blk2[0] == 0x00);
    CHECK("blok2: str17 len==5",       blk2[1] == 5);
    CHECK("blok2: str17[0]=='A'",      blk2[2] == 'A');
    CHECK("blok2: str17[4]=='E'",      blk2[6] == 'E');

    /* --- RT_BITMAP ---
     * bmp[0..1]: liczba bitmap
     * bmp[4..5]: offset id=1 = BMP_BUF_HDR = 176
     * bmp[176..191]: dane bitmap1
     */
    tprintf("\n-- RT_BITMAP --\n");
    CHECK("bmp count == 1",           RD16(bmp, 0) == 1);
    CHECK("bmp offset[0] == 176",     RD16(bmp, 4) == BMP_BUF_HDR);
    CHECK("bitmap1[0]==0xDE",         bmp[BMP_BUF_HDR + 0] == 0xDE);
    CHECK("bitmap1[1]==0xAD",         bmp[BMP_BUF_HDR + 1] == 0xAD);
    CHECK("bitmap1[2]==0xBE",         bmp[BMP_BUF_HDR + 2] == 0xBE);
    CHECK("bitmap1[3]==0xEF",         bmp[BMP_BUF_HDR + 3] == 0xEF);
    CHECK("bitmap1[7]==0x04",         bmp[BMP_BUF_HDR + 7] == 0x04);
    CHECK("bitmap1[15]==0x0C",        bmp[BMP_BUF_HDR + 15] == 0x0C);

    /* --- RT_MENU (RSC_HEAP) --- */
    tprintf("\n-- RT_MENU (RSC_HEAP) --\n");
    {
        unsigned char __far *m  = rc_get(RT_MENU, 1);
        unsigned short       sz = rc_size(RT_MENU, 1);
        CHECK("RT_MENU id=1 != NULL", m  != NULL);
        CHECK("RT_MENU id=1 size==16", sz == 16);
        if (m) {
            CHECK("menu1[0]==0x10",   m[0]  == 0x10);
            CHECK("menu1[7]==0x17",   m[7]  == 0x17);
            CHECK("menu1[15]==0x1F",  m[15] == 0x1F);
        } else {
            g_fail += 3;
        }
    }

    /* --- RT_ACCEL (RSC_HEAP) --- */
    tprintf("\n-- RT_ACCEL (RSC_HEAP) --\n");
    {
        unsigned char __far *a  = rc_get(RT_ACCEL, 1);
        unsigned short       sz = rc_size(RT_ACCEL, 1);
        CHECK("RT_ACCEL id=1 != NULL",  a  != NULL);
        CHECK("RT_ACCEL id=1 size==16", sz == 16);
        if (a) {
            CHECK("accel1[0]==0x20",   a[0]  == 0x20);
            CHECK("accel1[7]==0x27",   a[7]  == 0x27);
            CHECK("accel1[15]==0x2F",  a[15] == 0x2F);
        } else {
            g_fail += 3;
        }
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
     *   bytes[20..]: drugi popup "Help" z "About" (id=201)
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

    /* --- RT_ACCEL: tabela akceleratorow Win16 ---
     * Format: fVirt(1B) + key(2B) + cmd(2B) = 5 bajtow na wpis
     * Ostatni wpis: fVirt & 0x80 (FLASTKEY) */
    tprintf("\n-- W16TEST RT_ACCEL (wrc template) --\n");
    {
        unsigned char __far *acc = rc_get(RT_ACCEL, 102);
        unsigned short       sz  = rc_size(RT_ACCEL, 102);
        CHECK("W16TEST RT_ACCEL != NULL",        acc != NULL);
        CHECK("W16TEST RT_ACCEL size==10",        sz  == 10);
        if (acc) {
            /* Wpis 1: fVirt=NOINVERT(0x02), key=1(^A), cmd=201(ID_HELP_ABOUT) */
            CHECK("W16TEST accel[0]==0x02 (NOINVERT)", acc[0] == 0x02);
            CHECK("W16TEST accel key1==1 (^A)",         RD16(acc, 1) == 1);
            CHECK("W16TEST accel cmd1==201",             RD16(acc, 3) == 201);
            /* Wpis 2: fVirt=FLASTKEY|NOINVERT(0x82), key=17(^Q), cmd=200(ID_FILE_EXIT) */
            CHECK("W16TEST accel[5]==0x82 (FLASTKEY)", acc[5] == 0x82);
            CHECK("W16TEST accel key2==17 (^Q)",        RD16(acc, 6) == 17);
            CHECK("W16TEST accel cmd2==200",             RD16(acc, 8) == 200);
        } else { g_fail += 7; }
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
