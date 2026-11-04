/* ktask.c - KERNEL: zadania (kooperacyjne). Kazde zadanie ma wlasny
 * stos (w swoim DGROUP), PSP i kopie stanu thunka INT 3Fh. Przelaczenie tylko w Yield
 * i tym, co go wola (USER: GetMessage/PeekMessage/WaitMessage). */
#include "kernel.h"

#define MAX_TASKS 16
#define I3F_MAX   32                 /* INT3F_MAX_DEPTH w pm_call.asm */

typedef struct {
    BOOL  used;
    HTASK h;                         /* TDB */
    WORD  hinst, psp, show;
    WORD  ss, sp;                    /* zapisany stos, gdy zadanie nie dziala */
    WORD  stack, initsp, heap, heapstart;
    WORD  entry_cs, entry_ip;
    WORD  depth;                     /* stan thunka INT 3Fh */
    WORD  i3f[I3F_MAX * 3];
} TASK;

static TASK g_t[MAX_TASKS];
static int  g_cur, g_n;

/* Dla task_boot (kasm.asm): pierwsze uruchomienie zadania */
WORD g_boot_cs, g_boot_ip, g_boot_ds, g_boot_psp;

extern void task_switch(WORD *save_ss, WORD *save_sp, WORD new_ss, WORD new_sp);
#pragma aux task_switch parm [ax] [dx] [bx] [cx];
extern void task_boot(void);

static WORD cur_ds(void);
#pragma aux cur_ds = "mov ax, ds" value [ax];

/* Wynik INT E0h (mod_exec_rm w module.c), w RMBUF */
typedef struct { WORD ok, hinst, entry_cs, entry_ip, initsp, stack, heap, heapstart; } EXECRES;
#define EXEC_RM_INT 0xE0

static HTASK tdb_new(WORD hinst)
{
    HTASK h = (HTASK)GlobalAlloc(0x40, 256);
    BYTE __far *tdb;
    int mi;
    if (!h) return 0;
    tdb = (BYTE __far *)MK_FP(h, 0);
    *(WORD __far *)(tdb + 0x1C) = hinst;                 /* TDB_hInstance */
    mi = mod_from_handle(hinst);
    if (mi >= 0) {
        *(WORD __far *)(tdb + 0x1E) = MTW(mi, MT_HMOD);  /* TDB_hModule */
        kstrncpy((LPSTR)(tdb + 0xF2), (LPCSTR)(MTE(mi) + MT_NAME), 8);
    }
    tdb[0xFA] = 'T'; tdb[0xFB] = 'D';
    return h;
}

/* Zadanie startowe: uruchomil je loader, jego dane sa w KCB */
void task_init(void)
{
    TASK *t = &g_t[0];
    if (g_n) return;
    t->h = tdb_new(KCBW(KCB_APP_HINST));
    if (!t->h) return;
    t->used = TRUE;
    t->hinst = KCBW(KCB_APP_HINST);
    t->psp = SEL_PSP;
    t->show = 1;
    t->initsp = KCBW(KCB_APP_SP);
    t->stack = KCBW(KCB_APP_STACK);
    t->heap = KCBW(KCB_APP_HEAP);
    t->heapstart = KCBW(KCB_APP_HEAPSTART);
    g_cur = 0;
    g_n = 1;
}

static void switch_to(int i)
{
    TASK *o = &g_t[g_cur], *n = &g_t[i];
    WORD __far *dp = (WORD __far *)MK_FP(SEL_DATA16, KCBW(KCB_I3F_DEPTH));
    BYTE __far *st = (BYTE __far *)MK_FP(SEL_DATA16, KCBW(KCB_I3F_STACK));
    if (i == g_cur) return;
    o->depth = *dp;
    kmemcpy((LPVOID)o->i3f, st, o->depth * 6);
    *dp = n->depth;
    kmemcpy(st, (LPVOID)n->i3f, n->depth * 6);
    KCBW(KCB_APP_HINST) = n->hinst;
    KCBW(KCB_APP_SP) = n->initsp;
    KCBW(KCB_APP_STACK) = n->stack;
    KCBW(KCB_APP_HEAP) = n->heap;
    KCBW(KCB_APP_HEAPSTART) = n->heapstart;
    g_boot_cs = n->entry_cs; g_boot_ip = n->entry_ip;
    g_boot_ds = n->hinst; g_boot_psp = n->psp;
    g_cur = i;
    task_switch(&o->ss, &o->sp, n->ss, n->sp);
}

static int next_task(void)
{
    int i;
    for (i = 1; i <= MAX_TASKS; i++) {
        int k = (g_cur + i) % MAX_TASKS;
        if (g_t[k].used) return k;
    }
    return g_cur;
}

/* ordinal 29 */
void API Yield(void)
{
    task_init();
    switch_to(next_task());
}

/* ordinal 150 */
void API DirectedYield(HTASK h)
{
    int i;
    task_init();
    for (i = 0; i < MAX_TASKS; i++)
        if (g_t[i].used && g_t[i].h == h) { switch_to(i); return; }
}

/* ordinal 36 */
HTASK API GetCurrentTask(void)  { task_init(); return g_t[g_cur].h; }
/* ordinal 37 */
WORD  API GetCurrentPDB(void)   { task_init(); return g_t[g_cur].psp; }
/* ordinal 152 */
int   API GetNumTasks(void)     { task_init(); return g_n; }

/* ordinal 320 */
BOOL API IsTask(HTASK h)
{
    int i;
    task_init();
    for (i = 0; i < MAX_TASKS; i++) if (h && g_t[i].used && g_t[i].h == h) return TRUE;
    return FALSE;
}

WORD task_show(void) { task_init(); return g_t[g_cur].show; }
BOOL task_is_first(void) { task_init(); return g_cur == 0; }

/* Koniec biezacego zadania. Ostatnie konczy caly system (powrot do DOS). */
void task_exit(WORD ax)
{
    TASK *t;
    WORD dummy_ss, dummy_sp;
    int n;
    task_init();
    if (g_n <= 1) { pm_int21(ax); return; }
    t = &g_t[g_cur];
    {   /* okna, timery, kolejka, klasy zadania: USER (nie ma importu KERNEL -> USER) */
        HMODULE u = GetModuleHandle("USER");
        typedef void (API *TASKEXITPROC)(WORD, WORD);
        TASKEXITPROC f = u ? (TASKEXITPROC)GetProcAddress(u, (LPCSTR)MK_FP(0, 952)) : 0;
        if (f) f(t->h, t->hinst);
    }
    if (t->psp != SEL_PSP) GlobalFree(t->psp);
    GlobalFree(t->h);
    t->used = FALSE;
    g_n--;
    n = next_task();
    *(WORD __far *)MK_FP(SEL_DATA16, KCBW(KCB_I3F_DEPTH)) = 0;
    t->depth = 0;
    /* switch_to zapisze stan w nieuzywanym wpisie, stos konczonego zadania nikomu nie wroci */
    (void)dummy_ss; (void)dummy_sp;
    switch_to(n);
}

/* Slowo programu z cmd do out, reszta to argumenty. Zwraca wskaznik na argumenty. */
static LPCSTR split_cmd(LPCSTR cmd, LPSTR out, int max)
{
    int n = 0;
    while (*cmd == ' ') cmd++;
    while (*cmd && *cmd != ' ' && n < max - 1) out[n++] = *cmd++;
    out[n] = 0;
    while (*cmd == ' ') cmd++;
    return cmd;
}

static BOOL has_ext(LPCSTR s)
{
    LPCSTR dot = 0;
    for (; *s; s++) {
        if (*s == '.') dot = s;
        if (*s == '\\' || *s == ':') dot = 0;
    }
    return dot != 0;
}

/* ordinal 166. Wynik > 32 = hInstance, inaczej kod bledu jak w Windows (2 = brak pliku) */
WORD API WinExec(LPCSTR cmd, WORD show)
{
    static char prog[80];
    RMREGS __far *r = rmregs();
    EXECRES __far *res = (EXECRES __far *)MK_FP(SEL_RMBUF, RMBUF_DATA);
    LPCSTR args;
    int i, slot = -1, n;
    TASK *t;
    BYTE __far *psp;
    WORD __far *f;
    task_init();
    args = split_cmd(cmd, (LPSTR)prog, sizeof(prog) - 4);
    if (!prog[0]) return 2;
    if (!has_ext((LPCSTR)prog)) kstrcpy((LPSTR)prog + kstrlen((LPCSTR)prog), ".EXE");
    for (i = 0; i < MAX_TASKS; i++) if (!g_t[i].used) { slot = i; break; }
    if (slot < 0) return 8;                              /* za malo pamieci */
    kdos_init();
    kmemset(r, 0, sizeof(RMREGS));
    kstrcpy((LPSTR)MK_FP(SEL_RMBUF, RMBUF_DATA), (LPCSTR)prog);
    rm_int(EXEC_RM_INT, r);
    if (!res->ok) {
        dbg_str("KERNEL: WinExec nie udany ", (LPCSTR)prog);
        return res->hinst ? res->hinst : 2;              /* hinst = kod bledu */
    }
    t = &g_t[slot];
    kmemset((LPVOID)t, 0, sizeof(TASK));
    t->hinst = res->hinst;
    t->entry_cs = res->entry_cs;
    t->entry_ip = res->entry_ip;
    t->initsp = res->initsp;
    t->stack = res->stack;
    t->heap = res->heap;
    t->heapstart = res->heapstart;
    t->show = show;
    t->h = tdb_new(t->hinst);
    t->psp = (WORD)GlobalAlloc(0x40, 256);
    if (!t->h || !t->psp) return 8;
    psp = (BYTE __far *)MK_FP(t->psp, 0);
    kmemcpy(psp, MK_FP(SEL_PSP, 0), 0x80);
    n = kstrlen(args);
    if (n > 125) n = 125;
    psp[0x80] = (BYTE)(n ? n + 1 : 0);
    psp[0x81] = ' ';
    kmemcpy(psp + (n ? 0x82 : 0x81), args, n);
    psp[(n ? 0x82 : 0x81) + n] = 0x0D;
    /* Ramka jak po task_switch: GS FS ES DS DI SI BP FLAGS, adres powrotu = task_boot */
    t->ss = t->hinst;
    t->sp = t->initsp - 18;
    f = (WORD __far *)MK_FP(t->ss, t->sp);
    f[0] = 0; f[1] = 0; f[2] = 0;
    f[3] = cur_ds();
    f[4] = 0; f[5] = 0; f[6] = 0;
    f[7] = 0x0202;
    f[8] = (WORD)task_boot;
    t->used = TRUE;
    g_n++;
    dbg_str("KERNEL: WinExec ", (LPCSTR)prog);
    Yield();                                             /* nowe zadanie dostaje CPU od razu */
    return t->hinst;
}
