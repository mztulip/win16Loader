/* kernel.h - wspolne definicje KERNEL.EXE (Win16 KERNEL, 16-bit PM)
 *
 * Kompilacja: wcc -ms -zu -zl -s (DLL: DS = DGROUP KERNEL, SS = stos apki != DS,
 * wiec wskazniki do zmiennych lokalnych musza byc far).
 */
#ifndef KERNEL_H
#define KERNEL_H

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef WORD           HANDLE;
typedef WORD           HGLOBAL;
typedef WORD           HLOCAL;
typedef WORD           HINSTANCE;
typedef WORD           HMODULE;
typedef WORD           HRSRC;
typedef WORD           HTASK;
typedef int            HFILE;
typedef char __far    *LPSTR;
typedef const char __far *LPCSTR;
typedef void __far    *LPVOID;
typedef void (__far __pascal *FARPROC)(void);

#define API     __far __pascal
#define NULL    0
#define TRUE    1
#define FALSE   0
#define HFILE_ERROR  (-1)

/* Uwaga (bledy Watcoma): (DWORD)(seg)<<16 | (WORD)off rzutowane na far ptr przy seg=0
 * daje DS:off (jak konwersja near->far), a wersja z '+' generuje 'lea' + 'adc' z
 * przypadkowym CF. Obie polowy jawnie jako (DWORD)(WORD) i '|' - dziala. */
#define MK_FP(seg, off) ((void __far *)(((DWORD)(WORD)(seg) << 16) | (DWORD)(WORD)(off)))
#define FP_SEG(p)       ((WORD)((DWORD)(void __far *)(p) >> 16))
#define FP_OFF(p)       ((WORD)(DWORD)(void __far *)(p))
#define HIWORD(l)       ((WORD)((DWORD)(l) >> 16))
#define LOWORD(l)       ((WORD)(DWORD)(l))
#define MAKELONG(lo, hi) ((DWORD)(WORD)(lo) | ((DWORD)(WORD)(hi) << 16))

/* ---- Selektory (pm_call.asm, module.h) ---- */
#define SEL_CODE16      0x20
#define SEL_DATA16      0x28
#define SEL_PSP         0x38
#define SEL_THUNK       0x88
#define SEL_KCB         0x98
#define SEL_GDT_ACCESS  0x120
#define SEL_MODTAB      0x530
#define SEL_RMBUF       0x538
#define SEL_THUNK_DATA  0x588

/* ---- KCB ---- */
#define KCB_APP_HINST     0
#define KCB_NEXT_DYN_SEL  2
#define KCB_HEAP_PHYS     4
#define KCB_HEAP_NEXT     8
#define KCB_HEAP_END      12
#define KCB_TICK_MS       28
#define KCB_DYN_MAX_OFF   512
#define KCB_RMINT_OFF     516
#define KCB_RMBUF_SEG     518
#define KCB_CALLER_DS     520
#define KCB_THUNK_NEXT    522
#define KCB_THUNK_LIMIT   524
#define KCB_ENV_SEG       526
#define KCB_APP_STACK     528
#define KCB_APP_SP        530
#define KCB_APP_HEAP      532
#define KCB_APP_HEAPSTART 534
#define KCB_WINDIR        600
#define KCB_SYSDIR        664
#define KCB_CMDLINE       728

#define KCBW(off)   (*(WORD  __far *)MK_FP(SEL_KCB, (off)))
#define KCBD(off)   (*(DWORD __far *)MK_FP(SEL_KCB, (off)))
#define KCBS(off)   ((char __far *)MK_FP(SEL_KCB, (off)))

/* ---- MODTAB (module.h) ---- */
#define MAX_MODS        32
#define MT_SIZE         128
#define MT_NAME         0
#define MT_DGROUP       10
#define MT_FIRST_SEL    12
#define MT_NSEGS        14
#define MT_HMOD         16
#define MT_NRES_OFF     18
#define MT_FLAGS        20
#define MT_PATH         22
#define MTF_DLL         0x0001
#define MTF_VIRTUAL     0x0002
#define MTF_SYSTEM      0x0004
#define MTE(i)          ((BYTE __far *)MK_FP(SEL_MODTAB, (i) * MT_SIZE))
#define MTW(i, off)     (*(WORD __far *)(MTE(i) + (off)))

/* ---- RMREGS w SEL_RMBUF:0, dane od RMBUF_DATA ---- */
typedef struct {
    WORD ax, bx, cx, dx, si, di, bp, ds, es, flags;
} RMREGS;
#define RMBUF_DATA      0x100   /* poczatek obszaru danych w RMBUF */
#define RMBUF_DATA_SIZE 0xFE00u
#define RMBUF_DTA       0x40    /* DTA dla FindFirst/Next (43 B) */
#define CF_FLAG         0x0001

/* ---- Pomocnicze asm ---- */
WORD get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax] modify [ax];
WORD get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify [ax];
BYTE io_inb(WORD port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al] modify [al];
void io_outb(WORD port, BYTE v);
#pragma aux io_outb = "out dx, al" parm [dx] [al] modify [];

/* INT 21h w PM (int21_handler w pm_call.asm: AH=4Ch konczy apke) */
void pm_int21(WORD ax);
#pragma aux pm_int21 = "int 21h" parm [ax] modify [ax bx cx dx];

/* ---- kmain.c ---- */
LPSTR API lstrcat(LPSTR d, LPCSTR s);
LPSTR API lstrcpy(LPSTR d, LPCSTR s);
void  dbg_puts(LPCSTR s);
void  dbg_hex(DWORD v, int digits);
void  dbg_dec(DWORD v);
void  dbg_str(LPCSTR label, LPCSTR s);   /* "label'str'\n" */
int   kstrlen(LPCSTR s);
void  kstrcpy(LPSTR d, LPCSTR s);
void  kstrncpy(LPSTR d, LPCSTR s, int n);
int   kstricmp(LPCSTR a, LPCSTR b);
int   kstrnicmp(LPCSTR a, LPCSTR b, int n);
char  kupper(char c);
void  kmemcpy(LPVOID d, const void __far *s, WORD n);
void  kmemset(LPVOID d, BYTE v, WORD n);

/* ---- kmem.c ---- */
WORD  sel_alloc_run(WORD n);
void  sel_free(WORD sel);
void  desc_set(WORD sel, DWORD base, DWORD limit, BYTE access);
DWORD desc_base(WORD sel);
DWORD desc_limit(WORD sel);
WORD  alias_selector(DWORD base, DWORD size, BYTE access);
HGLOBAL API GlobalAlloc(WORD flags, DWORD size);
HGLOBAL API GlobalFree(HGLOBAL h);
DWORD API GlobalSize(HGLOBAL h);
void  kmem_init(void);
void  local_init_app(void);

/* ---- kdos.c ---- */
int   rm_int(BYTE intno, RMREGS __far *r);   /* r jest kopiowane do/z RMBUF */
RMREGS __far *rmregs(void);
int   rm_int21(void);                         /* uzywa RMREGS w RMBUF */
HFILE API _lopen(LPCSTR path, int mode);
HFILE API _lclose(HFILE h);
DWORD API _hread(HFILE h, void __far *buf, DWORD n);
long  API _llseek(HFILE h, long off, int origin);
void  kdos_init(void);

/* ---- kmod.c ---- */
int   mod_from_handle(WORD h);               /* indeks MODTAB albo -1 */
int   mod_from_name(LPCSTR name);
DWORD res_phys(int mi, HRSRC h, DWORD __far *size);

#endif /* KERNEL_H */
