/*
 * shell.c - graficzna powloka przegladu plikow EXE (VESA 640x480x24bpp)
 *
 * ETAP 20 (20b-20f):
 *   20b: skanowanie katalogu *.EXE (INT 21h Find First/Next)
 *   20c: wyswietlenie listy plikow (graficznie, VESA LFB bank-switch)
 *   20d: zaznaczenie - klawisze strzalek
 *   20e: uruchomienie - Enter lub cyfra 1-9
 *   20f: prawdziwe ikony 32x32 z zasobow NE (RT_GROUP_ICON + RT_ICON)
 *
 * Kompilacja: wcc -ml -q shell.c -fo=shell.obj
 * Linkowanie: dolaczany do loader.exe
 *
 * Uwaga: dziala PO inicjalizacji VESA (tryb graficzny 640x480x24bpp).
 * Rysuje bezposrednio do LFB przez bank-switching (0xA000:xxxx + INT 10h 4F05h).
 */

#include <stdio.h>
#include <string.h>
#include <conio.h>  /* getch() */
#include <dos.h>
#include <i86.h>

#define SHELL_MAX_FILES 16

/* Layout ekranu */
#define SCREEN_W     640
#define SCREEN_H     480
#define VESA_PITCH   1920   /* 640 * 3 */
#define BANK_SIZE    65536UL
#define ICON_SZ      32
#define ITEM_H       40     /* wysokosc jednego wpisu */
#define ITEM_X       20     /* poczatek X listy */
#define ITEM_Y0      50     /* Y pierwszego wpisu */
#define TEXT_X_OFF   40     /* offset tekstu od ikony */

/* Kolory RGB (24bpp, kolejnosc w pamieci: B, G, R) */
#define COL_BG_R   0xC0
#define COL_BG_G   0xC0
#define COL_BG_B   0xC0
#define COL_SEL_R  0x00
#define COL_SEL_G  0x00
#define COL_SEL_B  0x80
#define COL_HDR_R  0x0A
#define COL_HDR_G  0x6E
#define COL_HDR_B  0x7E
#define COL_TXT_R  0x00
#define COL_TXT_G  0x00
#define COL_TXT_B  0x00
#define COL_WTXT_R 0xFF
#define COL_WTXT_G 0xFF
#define COL_WTXT_B 0xFF

/* ============================================================
 * Font 8x16 — wbudowany mini font (znaki ASCII 32..127)
 * Uzywamy wskaznika na BIOS font ROM (INT 10h AH=11h AL=30h BH=6)
 * ============================================================ */
static unsigned char __far *g_font = NULL;

static void font_init_shell(void)
{
    union REGPACK rp;
    memset(&rp, 0, sizeof(rp));
    rp.w.ax = 0x1130;
    rp.h.bh = 0x06;  /* 8x16 font */
    intr(0x10, &rp);
    g_font = MK_FP(rp.w.es, rp.w.bp);
}

/* ============================================================
 * VESA bank-switched pixel write
 * VESA mode 0x0112: 640x480x24bpp, LFB, bank size=64KB
 * W real mode piszemy do 0xA000:offset z bank-switchingiem.
 * ============================================================ */
static unsigned short g_cur_bank = 0xFFFF;

static void vesa_set_bank(unsigned short bank)
{
    union REGS r;
    if (bank == g_cur_bank) return;
    g_cur_bank = bank;
    r.x.ax = 0x4F05;
    r.x.bx = 0x0000;  /* window A */
    r.x.dx = bank;
    int86(0x10, &r, &r);
}

static void put_byte(unsigned long flat, unsigned char val)
{
    unsigned short bank = (unsigned short)(flat >> 16);
    unsigned short off  = (unsigned short)flat;
    vesa_set_bank(bank);
    *(unsigned char __far *)MK_FP(0xA000, off) = val;
}

static void put_pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    unsigned long flat;
    unsigned short bank, off;

    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;

    flat = (unsigned long)y * VESA_PITCH + (unsigned long)x * 3;
    bank = (unsigned short)(flat >> 16);
    off  = (unsigned short)flat;

    if (off >= 0xFFFE) {
        /* Piksel na granicy bankow — bajt po bajcie */
        put_byte(flat,     b);
        put_byte(flat + 1, g);
        put_byte(flat + 2, r);
        return;
    }

    vesa_set_bank(bank);
    {
        unsigned char __far *vga = MK_FP(0xA000, off);
        vga[0] = b;
        vga[1] = g;
        vga[2] = r;
    }
}

/* Szybkie wypelnianie prostokata */
static void fill_rect(int x, int y, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int row, col;
    for (row = 0; row < h; row++) {
        unsigned long flat = (unsigned long)(y + row) * VESA_PITCH + (unsigned long)x * 3;
        unsigned short bank = (unsigned short)(flat >> 16);
        unsigned short off  = (unsigned short)flat;
        unsigned char __far *vga;

        vesa_set_bank(bank);
        vga = MK_FP(0xA000, off);

        for (col = 0; col < w; col++) {
            if (off >= 0xFFFE) {
                /* piksel na granicy — uzyj put_pixel (wolny, ale rzadki) */
                put_pixel(x + col, y + row, r, g, b);
                flat = (unsigned long)(y + row) * VESA_PITCH + (unsigned long)(x + col + 1) * 3;
                bank = (unsigned short)(flat >> 16);
                off  = (unsigned short)flat;
                vesa_set_bank(bank);
                vga = MK_FP(0xA000, off);
                continue;
            }
            vga[0] = b;
            vga[1] = g;
            vga[2] = r;
            off += 3;
            if (off < 3) {
                /* off zawinelo sie przez 0xFFFF — przelacz bank */
                bank++;
                vesa_set_bank(bank);
                vga = MK_FP(0xA000, off);
            } else {
                vga += 3;
            }
        }
    }
}

/* Rysuj znak 8x16 z BIOS font ROM */
static void draw_char(int x, int y, char ch,
                      unsigned char fr, unsigned char fg, unsigned char fb)
{
    unsigned char __far *glyph;
    int row, col;

    if (!g_font) return;
    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) return;

    glyph = g_font + (unsigned char)ch * 16;
    for (row = 0; row < 16; row++) {
        unsigned char bits = glyph[row];
        for (col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                put_pixel(x + col, y + row, fr, fg, fb);
        }
    }
}

/* Rysuj tekst */
static void draw_text(int x, int y, const char *s,
                      unsigned char fr, unsigned char fg, unsigned char fb)
{
    while (*s) {
        draw_char(x, y, *s, fr, fg, fb);
        x += 8;
        s++;
    }
}

/* ============================================================
 * Ekstrakcja ikon z plikow NE (20f)
 *
 * Format ikony 32x32 4bpp:
 *   BITMAPINFOHEADER (40B) + paleta (16*4=64B) + XOR mask (512B) + AND mask (128B)
 *   Razem: 744 bajty
 * ============================================================ */

/* Rozmiar surowych danych ikony 32x32 4bpp */
#define ICON_DATA_SIZE  744
/* 40 BIH + 64 paleta + 512 XOR + 128 AND */
#define ICON_BIH_SIZE   40
#define ICON_PAL_OFF    40
#define ICON_PAL_SIZE   64     /* 16 kolorow * 4 bajty (BGRX) */
#define ICON_XOR_OFF    104    /* 40 + 64 */
#define ICON_XOR_SIZE   512    /* 32 * 32 / 2 */
#define ICON_AND_OFF    616    /* 104 + 512 */
#define ICON_AND_SIZE   128    /* 32 * 32 / 8 */

/* Standardowa paleta VGA 16 kolorow (fallback gdy ikona nie ma palety) */
static const unsigned char s_vga16[16][3] = {
    {  0,  0,  0}, {128,  0,  0}, {  0,128,  0}, {128,128,  0},
    {  0,  0,128}, {128,  0,128}, {  0,128,128}, {192,192,192},
    {128,128,128}, {255,  0,  0}, {  0,255,  0}, {255,255,  0},
    {  0,  0,255}, {255,  0,255}, {  0,255,255}, {255,255,255}
};

/* Tablica znalezionych plikow EXE */
static struct {
    char name[13];                   /* 8.3 uppercase, null-terminated */
    unsigned char icon[ICON_DATA_SIZE]; /* surowe dane ikony (BIH+pal+XOR+AND) */
    int has_icon;                    /* 1 = wczytano ikone z NE */
} g_shell_files[SHELL_MAX_FILES];

static int g_shell_n   = 0;
static int g_shell_sel = 0;

/* DLL-e loadera - pomijamy w liscie shell */
static const char * const s_skip[] = {
    "LOADER.EXE", "KERNEL.EXE", "USER.EXE", "GDI.EXE", NULL
};

static int is_skip(const char *name)
{
    int i;
    for (i = 0; s_skip[i]; i++)
        if (strcmp(name, s_skip[i]) == 0) return 1;
    return 0;
}

/* Odczytaj ikone 32x32 4bpp z pliku NE.
 * Zwraca 1 jesli udalo sie wczytac, 0 jesli brak ikony. */
static int extract_icon(const char *filename, unsigned char *out_icon)
{
    FILE *f;
    unsigned short ne_off_w;
    unsigned long  ne_off;
    unsigned short rsctab_off, align_shift;
    long rsctab_pos;
    unsigned short best_nid = 0;

    f = fopen(filename, "rb");
    if (!f) return 0;

    /* Sprawdz MZ */
    {
        unsigned short mz_sig;
        if (fread(&mz_sig, 2, 1, f) != 1 || mz_sig != 0x5A4D) { fclose(f); return 0; }
    }

    /* NE offset z MZ.e_lfanew */
    fseek(f, 0x3C, SEEK_SET);
    if (fread(&ne_off_w, 2, 1, f) != 1) { fclose(f); return 0; }
    ne_off = (unsigned long)ne_off_w;

    /* Sprawdz NE signature */
    fseek(f, ne_off, SEEK_SET);
    {
        unsigned short ne_sig;
        if (fread(&ne_sig, 2, 1, f) != 1 || ne_sig != 0x454E) { fclose(f); return 0; }
    }

    /* Odczytaj rsctab offset (pole na ne_off + 0x24) */
    fseek(f, ne_off + 0x24, SEEK_SET);
    if (fread(&rsctab_off, 2, 1, f) != 1) { fclose(f); return 0; }
    rsctab_pos = ne_off + (long)rsctab_off;

    /* rscAlignShift */
    fseek(f, rsctab_pos, SEEK_SET);
    if (fread(&align_shift, 2, 1, f) != 1) { fclose(f); return 0; }

    /* 1) Szukaj RT_GROUP_ICON (0x800E) w tablicy zasobow */
    fseek(f, rsctab_pos + 2L, SEEK_SET);
    for (;;) {
        unsigned short rt_id, rt_count, j;
        if (fread(&rt_id, 2, 1, f) != 1 || rt_id == 0) break;
        if (fread(&rt_count, 2, 1, f) != 1) break;
        fseek(f, 4L, SEEK_CUR);  /* rt_proc */

        if (rt_id == 0x800Eu) {
            /* Znaleziono GROUP_ICON — czytaj pierwszy wpis */
            for (j = 0; j < rt_count && best_nid == 0; j++) {
                unsigned short rn_offset, rn_length, rn_flags, rn_id;
                long grp_off;
                unsigned short grp_reserved, grp_type, grp_count;
                unsigned short k;

                if (fread(&rn_offset, 2, 1, f) != 1) goto done;
                if (fread(&rn_length, 2, 1, f) != 1) goto done;
                if (fread(&rn_flags,  2, 1, f) != 1) goto done;
                if (fread(&rn_id,     2, 1, f) != 1) goto done;
                fseek(f, 4L, SEEK_CUR);

                /* Odczytaj zawartosc GRPICONDIR */
                grp_off = (long)rn_offset << align_shift;
                {
                    long save = ftell(f);
                    fseek(f, grp_off, SEEK_SET);
                    if (fread(&grp_reserved, 2, 1, f) != 1) { fseek(f, save, SEEK_SET); continue; }
                    if (fread(&grp_type,     2, 1, f) != 1) { fseek(f, save, SEEK_SET); continue; }
                    if (fread(&grp_count,    2, 1, f) != 1) { fseek(f, save, SEEK_SET); continue; }

                    /* Szukaj najlepszego wpisu: preferuj 32x32 4bpp (16 kolorow) */
                    for (k = 0; k < grp_count; k++) {
                        unsigned char bw, bh, bcol, bres;
                        unsigned short bplanes, bbits;
                        unsigned long  bsize;
                        unsigned short bnid;

                        if (fread(&bw,  1, 1, f) != 1) break;
                        if (fread(&bh,  1, 1, f) != 1) break;
                        if (fread(&bcol, 1, 1, f) != 1) break;
                        if (fread(&bres, 1, 1, f) != 1) break;
                        if (fread(&bplanes, 2, 1, f) != 1) break;
                        if (fread(&bbits,   2, 1, f) != 1) break;
                        if (fread(&bsize,   4, 1, f) != 1) break;
                        if (fread(&bnid,    2, 1, f) != 1) break;

                        /* Preferuj 32x32 4bpp */
                        if (bw == 32 && bh == 32 && bbits == 4) {
                            best_nid = bnid;
                            break;
                        }
                        /* Fallback: jakikolwiek 32x32 */
                        if (bw == 32 && bh == 32 && best_nid == 0) {
                            best_nid = bnid;
                        }
                    }
                    fseek(f, save, SEEK_SET);
                }
            }
            /* Pomin pozostale wpisy GROUP_ICON */
            for (; j < rt_count; j++)
                fseek(f, 12L, SEEK_CUR);
        } else {
            /* Pomin wszystkie wpisy tego typu */
            for (j = 0; j < rt_count; j++)
                fseek(f, 12L, SEEK_CUR);
        }
    }

    if (best_nid == 0) goto done;

    /* 2) Szukaj RT_ICON (0x8003) z id == best_nid */
    fseek(f, rsctab_pos + 2L, SEEK_SET);
    for (;;) {
        unsigned short rt_id, rt_count, j;
        if (fread(&rt_id, 2, 1, f) != 1 || rt_id == 0) break;
        if (fread(&rt_count, 2, 1, f) != 1) break;
        fseek(f, 4L, SEEK_CUR);

        if (rt_id == 0x8003u) {
            for (j = 0; j < rt_count; j++) {
                unsigned short rn_offset, rn_length, rn_flags, rn_id;
                if (fread(&rn_offset, 2, 1, f) != 1) goto done;
                if (fread(&rn_length, 2, 1, f) != 1) goto done;
                if (fread(&rn_flags,  2, 1, f) != 1) goto done;
                if (fread(&rn_id,     2, 1, f) != 1) goto done;
                fseek(f, 4L, SEEK_CUR);

                if ((rn_id & 0x7FFFu) == best_nid) {
                    long icon_off = (long)rn_offset << align_shift;
                    unsigned short icon_len = (unsigned short)((unsigned long)rn_length << align_shift);
                    fseek(f, icon_off, SEEK_SET);
                    if (icon_len > ICON_DATA_SIZE) icon_len = ICON_DATA_SIZE;
                    memset(out_icon, 0, ICON_DATA_SIZE);
                    fread(out_icon, 1, icon_len, f);
                    fclose(f);
                    return 1;
                }
            }
        } else {
            for (j = 0; j < rt_count; j++)
                fseek(f, 12L, SEEK_CUR);
        }
    }

done:
    fclose(f);
    return 0;
}

/* Rysuj ikone 32x32 4bpp na ekranie */
static void draw_icon_32(const unsigned char *icon_data, int sx, int sy)
{
    const unsigned char *pal  = icon_data + ICON_PAL_OFF;
    const unsigned char *xor  = icon_data + ICON_XOR_OFF;
    const unsigned char *amask = icon_data + ICON_AND_OFF;
    int row, col;

    /* BMP: wiersze od dolu do gory */
    for (row = 0; row < ICON_SZ; row++) {
        int src_row = ICON_SZ - 1 - row;  /* BMP bottom-up */
        const unsigned char *xor_row = xor + src_row * (ICON_SZ / 2);
        const unsigned char *and_row = amask + src_row * (ICON_SZ / 8);

        for (col = 0; col < ICON_SZ; col++) {
            unsigned char and_bit, xor_nib;
            unsigned char r, g, b;

            /* AND mask: 1 bit na piksel, MSB first */
            and_bit = (and_row[col / 8] >> (7 - (col % 8))) & 1;
            if (and_bit) continue;  /* przezroczysty */

            /* XOR mask: 4 bity na piksel, packed (high nibble = lewy piksel) */
            if (col & 1)
                xor_nib = xor_row[col / 2] & 0x0F;
            else
                xor_nib = (xor_row[col / 2] >> 4) & 0x0F;

            /* Kolor z palety ikony (BGRX format) */
            b = pal[xor_nib * 4 + 0];
            g = pal[xor_nib * 4 + 1];
            r = pal[xor_nib * 4 + 2];

            put_pixel(sx + col, sy + row, r, g, b);
        }
    }
}

/* Rysuj domyslna ikone (niebieski prostokat z bialym "E") */
static void draw_default_icon(int sx, int sy)
{
    fill_rect(sx, sy, ICON_SZ, ICON_SZ, 0x00, 0x00, 0xC0);
    /* Biala ramka */
    fill_rect(sx, sy, ICON_SZ, 1, 0xFF, 0xFF, 0xFF);
    fill_rect(sx, sy + ICON_SZ - 1, ICON_SZ, 1, 0xFF, 0xFF, 0xFF);
    fill_rect(sx, sy, 1, ICON_SZ, 0xFF, 0xFF, 0xFF);
    fill_rect(sx + ICON_SZ - 1, sy, 1, ICON_SZ, 0xFF, 0xFF, 0xFF);
    /* Litera "E" na srodku */
    draw_char(sx + 12, sy + 8, 'E', 0xFF, 0xFF, 0xFF);
}

/* 20b: skanuj katalog biezacy + 20f: ekstrahuj ikony */
static void shell_scan(void)
{
    struct find_t ft;
    unsigned rc;
    g_shell_n = 0;
    rc = _dos_findfirst("*.EXE", _A_NORMAL | _A_RDONLY | _A_ARCH, &ft);
    while (rc == 0 && g_shell_n < SHELL_MAX_FILES) {
        if (!is_skip(ft.name)) {
            int idx = g_shell_n;
            strncpy(g_shell_files[idx].name, ft.name, 12);
            g_shell_files[idx].name[12] = '\0';
            g_shell_files[idx].has_icon =
                extract_icon(g_shell_files[idx].name, g_shell_files[idx].icon);
            g_shell_n++;
        }
        rc = _dos_findnext(&ft);
    }
}

/* 20c: narysuj liste graficznie (VESA) */
static void shell_draw(void)
{
    int i;

    /* Tlo szare */
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG_R, COL_BG_G, COL_BG_B);

    /* Pasek naglowka */
    fill_rect(0, 0, SCREEN_W, 30, COL_HDR_R, COL_HDR_G, COL_HDR_B);
    draw_text(12, 7, "Win16 Shell", COL_WTXT_R, COL_WTXT_G, COL_WTXT_B);
    draw_text(180, 7, "Arrows: select  Enter: run  1-9: quick  Esc: DOS",
              COL_WTXT_R, COL_WTXT_G, COL_WTXT_B);

    if (g_shell_n == 0) {
        draw_text(ITEM_X, ITEM_Y0, "(no EXE files found)", COL_TXT_R, COL_TXT_G, COL_TXT_B);
        return;
    }

    for (i = 0; i < g_shell_n; i++) {
        int y = ITEM_Y0 + i * ITEM_H;
        int is_sel = (i == g_shell_sel);
        char label[20];

        /* Tlo zaznaczonego wpisu */
        if (is_sel) {
            fill_rect(ITEM_X - 4, y - 2, SCREEN_W - 2 * ITEM_X + 8, ITEM_H - 2,
                      COL_SEL_R, COL_SEL_G, COL_SEL_B);
        }

        /* Ikona */
        if (g_shell_files[i].has_icon)
            draw_icon_32(g_shell_files[i].icon, ITEM_X, y + 2);
        else
            draw_default_icon(ITEM_X, y + 2);

        /* Nazwa pliku */
        sprintf(label, "[%d] %s", i + 1, g_shell_files[i].name);
        draw_text(ITEM_X + TEXT_X_OFF, y + 10, label,
                  is_sel ? COL_WTXT_R : COL_TXT_R,
                  is_sel ? COL_WTXT_G : COL_TXT_G,
                  is_sel ? COL_WTXT_B : COL_TXT_B);
    }
}

/*
 * shell_run() - glowna petla powloki (graficzna).
 * Musi byc wywolana PO vesa_init().
 * Zwraca wskaznik na nazwe wybranego pliku.
 */
const char *shell_run(void)
{
    int ch;

    g_cur_bank = 0xFFFF;   /* tryb VESA ustawiony od nowa (powrot z apki) */
    font_init_shell();
    shell_scan();

    if (g_shell_n == 0) {
        return "WIN1APP.EXE";
    }

    g_shell_sel = 0;
    for (;;) {
        shell_draw();
        ch = getch();

        if (ch == 0 || ch == 0xE0) {
            ch = getch();
            if (ch == 0x48) {  /* strzalka w gore */
                if (g_shell_sel > 0) g_shell_sel--;
            } else if (ch == 0x50) {  /* strzalka w dol */
                if (g_shell_sel < g_shell_n - 1) g_shell_sel++;
            }
        } else if (ch == 0x1B) {   /* Esc: wyjscie do DOS */
            return NULL;
        } else if (ch == '\r' || ch == '\n') {
            break;
        } else if (ch >= '1' && ch <= '9') {
            int idx = ch - '1';
            if (idx < g_shell_n) {
                g_shell_sel = idx;
                break;
            }
        }
    }

    return g_shell_files[g_shell_sel].name;
}
