/* module.h - ogolny loader modulow NE (aplikacja + DLL) do pamieci rozszerzonej
 *
 * Kazdy modul dostaje kolejne selektory GDT dla swoich segmentow
 * (segment n -> first_sel + (n-1)*8), segmenty laduja sie do XMS (>1MB),
 * a w PM dostepna jest tablica modulow MODTAB (SEL_MODTAB) - patrz MT_* nizej.
 *
 * Kompilacja: wcc -ml (czesc loader.exe, tryb rzeczywisty)
 */
#ifndef MODULE_H
#define MODULE_H

#define MAX_MODS        32
#define MAX_DLLINIT     16      /* = MAX_DLLINIT w pm_call.asm */

/* Stale selektory w rozszerzeniu GDT (patrz pm_call.asm) */
#define SEL_MODTAB      0x530
#define SEL_RM_0000     0x540   /* __0000H: okno 64KB od adresu 0 */
#define SEL_RM_0040     0x548   /* __0040H: BIOS data area */
#define SEL_RM_A000     0x550
#define SEL_RM_B000     0x558
#define SEL_RM_B800     0x560
#define SEL_RM_C000     0x568
#define SEL_RM_D000     0x570
#define SEL_RM_E000     0x578
#define SEL_RM_F000     0x580
#define SEL_THUNK_SEL   0x88    /* segment thunkow INT 3F */
#define SEL_RMBUF       0x538   /* bufor transferowy DOS (RMREGS + dane), 64KB */
#define SEL_THUNK_DATA  0x588   /* alias danych segmentu thunkow (KERNEL tworzy thunki) */
#define SEL_MOD_FIRST   0x600   /* pierwszy selektor segmentow modulow */
#define GDT_TOTAL_SIZE  0x4000  /* = GDT_TOTAL_SIZE w pm_call.asm */

/* Wpis MODTAB (128 B) - czytany w PM przez KERNEL i int3f_handler */
#define MT_SIZE         128
#define MT_NAME         0       /* char[10] nazwa modulu (wielkie litery) */
#define MT_DGROUP       10      /* WORD selektor DGROUP (= hInstance), 0 = brak */
#define MT_FIRST_SEL    12      /* WORD selektor segmentu 1 */
#define MT_NSEGS        14      /* WORD liczba segmentow */
#define MT_HMOD         16      /* WORD selektor kopii naglowka NE (= hModule) */
#define MT_NRES_OFF     18      /* WORD offset tablicy nonresident names w kopii NE (0=brak) */
#define MT_FLAGS        20      /* WORD MTF_* */
#define MT_PATH         22      /* char[80] pelna sciezka pliku */
#define MT_RES_SEL      102     /* WORD selektor bloku zasobow (0=brak) */

#define MTF_DLL         0x0001
#define MTF_VIRTUAL     0x0002  /* brak pliku - wszystkie importy to stuby */
#define MTF_SYSTEM      0x0004  /* nasz KERNEL/USER/GDI */

/* KCB rozszerzenie (KCB ma teraz 1KB) */
#define KCB_DYN_MAX_OFF   512   /* WORD max selektor dla GlobalAlloc */
#define KCB_NMODS_OFF     514   /* WORD liczba modulow w MODTAB */
#define KCB_RMINT_OFF     516   /* WORD offset rm_int_service w SEL_CODE16 */
#define KCB_RMBUF_SEG     518   /* WORD segment RM bufora RMBUF */
#define KCB_CALLER_DS     520   /* WORD DS wolajacego ostatnie API (int3f_handler) */
#define KCB_THUNK_NEXT    522   /* WORD nastepny wolny offset w segmencie thunkow */
#define KCB_THUNK_LIMIT   524   /* WORD rozmiar segmentu thunkow */
#define KCB_ENV_SEG       526   /* WORD segment RM srodowiska DOS */
#define KCB_APP_STACK     528   /* WORD rozmiar stosu apki (ne_stack) */
#define KCB_APP_SP        530   /* WORD poczatkowy SP apki */
#define KCB_APP_HEAP      532   /* WORD rozmiar local heap apki (ne_heap) */
#define KCB_APP_HEAPSTART 534   /* WORD offset poczatku local heap w DGROUP apki */
#define KCB_TRACE         536   /* WORD !=0: trace wywolan API (opcja -t loadera) */
#define KCB_WINDIR        600   /* char[64] katalog Windows (WIN.INI) */
#define KCB_SYSDIR        664   /* char[64] katalog systemowy (loader) */
#define KCB_CMDLINE       728   /* char[128] argumenty apki */

typedef struct {
    unsigned short sel;
    unsigned long  base;
    unsigned long  limit;
    unsigned char  access;
    unsigned char  big;
} GDT_PATCH;

extern GDT_PATCH      g_gdtp[];
extern unsigned short g_ngdtp;

/* Dla pm_call.asm */
extern unsigned short g_entry_ip, g_entry_cs, g_app_ds, g_init_sp;
extern unsigned short g_ndllinit;
extern unsigned short g_rmbuf_seg;
extern unsigned short g_app_stack, g_app_heap, g_app_heapstart;
extern unsigned short g_dllinit[MAX_DLLINIT * 4];

/* Inicjalizacja (raz): katalog systemowy (z argv[0]), bufory DOS */
int  mod_init(const char *argv0, unsigned long thunk_phys, unsigned thunk_seg,
              unsigned thunk_size);
/* Przed kazdym uruchomieniem apki: wyczysc moduly, XMS, thunki */
void mod_reset(void);
/* Zaladuj apke (i rekurencyjnie jej DLL). Zwraca 0 = OK. */
int  mod_load_app(const char *path);
/* Pierwszy wolny adres XMS po modulach (dla GlobalAlloc) */
unsigned long  mod_xms_end(void);
/* Pierwszy wolny selektor po modulach (dla GlobalAlloc) */
unsigned short mod_next_sel(void);
/* Nastepny wolny offset w segmencie thunkow */
unsigned short mod_thunk_next(void);
/* Katalog systemowy (z argv[0]) */
const char    *mod_sysdir(void);

#endif /* MODULE_H */
