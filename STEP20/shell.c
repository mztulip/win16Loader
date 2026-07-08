/*
 * shell.c - prosta powloka przegladu plikow EXE (text mode, real mode)
 *
 * ETAP 20 (20b-20e):
 *   20b: skanowanie katalogu *.EXE (INT 21h Find First/Next przez _dos_findfirst)
 *   20c: wyswietlenie listy plikow (cprintf/clrscr, tryb tekstowy DOS)
 *   20d: zaznaczenie - klawisze strzalek
 *   20e: uruchomienie - Enter lub cyfra 1-9
 *
 * Kompilacja: wcc -ml -q shell.c -fo=shell.obj
 * Linkowanie: dolaczany do loader.exe
 *
 * Uwaga: dziala PRZED inicjalizacja VESA (tryb tekstowy DOS).
 * Po shell_run() loader wraca do grafiki przez vesa_init().
 */

#include <stdio.h>
#include <string.h>
#include <conio.h>  /* getch() */
#include <dos.h>

#define SHELL_MAX_FILES 16

/* Tablica znalezionych plikow EXE */
static struct {
    char name[13];   /* 8.3 uppercase, null-terminated */
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

/* 20b: skanuj katalog biezacy (root dyskietki) - *.EXE */
static void shell_scan(void)
{
    struct find_t ft;
    unsigned rc;
    g_shell_n = 0;
    rc = _dos_findfirst("*.EXE", _A_NORMAL | _A_RDONLY | _A_ARCH, &ft);
    while (rc == 0 && g_shell_n < SHELL_MAX_FILES) {
        if (!is_skip(ft.name)) {
            strncpy(g_shell_files[g_shell_n].name, ft.name, 12);
            g_shell_files[g_shell_n].name[12] = '\0';
            g_shell_n++;
        }
        rc = _dos_findnext(&ft);
    }
}

/* 20c: narysuj liste w trybie tekstowym */
static void shell_draw(void)
{
    int i;
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    printf("=== Win16 Shell - wybierz aplikacje ===\r\n");
    printf("Strzalki: wybor  |  Enter: uruchom  |  1-9: szybki wybor\r\n");
    printf("-------------------------------------------\r\n");
    if (g_shell_n == 0) {
        printf("  (brak plikow EXE)\r\n");
        return;
    }
    for (i = 0; i < g_shell_n; i++) {
        if (i == g_shell_sel)
            printf(" >> [%d] %-12s <<\r\n", i + 1, g_shell_files[i].name);
        else
            printf("    [%d] %-12s\r\n",   i + 1, g_shell_files[i].name);
    }
    printf("-------------------------------------------\r\n");
}

/*
 * shell_run() - glowna petla powloki.
 * Zwraca wskaznik na nazwe wybranego pliku (statyczny bufor, wazny przez
 * caly czas dzialania loadera). Nigdy nie zwraca NULL.
 */
const char *shell_run(void)
{
    int ch;

    shell_scan();

    if (g_shell_n == 0) {
        printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        printf("Shell: brak plikow EXE na dysku.\r\n");
        printf("Uruchamiam domyslny WIN1APP.EXE...\r\n");
        return "WIN1APP.EXE";
    }

    g_shell_sel = 0;
    for (;;) {
        shell_draw();
        ch = getch();

        if (ch == 0 || ch == 0xE0) {
            /* klawisz rozszerzony: 0x00/0xE0 + kod */
            ch = getch();
            if (ch == 0x48) {  /* strzalka w gore */
                if (g_shell_sel > 0) g_shell_sel--;
            } else if (ch == 0x50) {  /* strzalka w dol */
                if (g_shell_sel < g_shell_n - 1) g_shell_sel++;
            }
        } else if (ch == '\r' || ch == '\n') {
            /* Enter - uruchom zaznaczony plik */
            break;
        } else if (ch >= '1' && ch <= '9') {
            /* cyfra 1-9: bezposredni wybor */
            int idx = ch - '1';
            if (idx < g_shell_n) {
                g_shell_sel = idx;
                break;
            }
        }
        /* inne klawisze: ignoruj */
    }

    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    printf("Shell: uruchamiam %s...\r\n", g_shell_files[g_shell_sel].name);
    return g_shell_files[g_shell_sel].name;
}
