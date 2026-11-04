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
static int  g_dying = -1;            /* zadanie konczone po wyjatku */
static BOOL g_fault;

extern void fault_entry(void);

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

static void set_running(WORD hinst, BOOL on)
{
    int mi = mod_from_handle(hinst);
    if (mi < 0) return;
    if (on) MTW(mi, MT_FLAGS) |= MTF_RUNNING;
    else MTW(mi, MT_FLAGS) &= ~MTF_RUNNING;
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
    set_running(t->hinst, TRUE);
    {   /* wyjatek konczacy zadanie dziala na wlasnym stosie: stos zadania moze byc zepsuty */
        WORD st = (WORD)GlobalAlloc(0x40, 0x2000);
        kmem_set_owner(st, 0);
        kmem_set_owner(t->h, 0);
        KCBW(KCB_EXC_STACK) = st;
        *(DWORD __far *)MK_FP(SEL_KCB, KCB_EXC_CB) = (DWORD)(void __far *)fault_entry;
    }
}

/* Zadanie, ktoremu nalezy blok przydzielony wlasnie teraz: tylko gdy API wywolal sam program */
WORD task_owner(void)
{
    if (!g_n || KCBW(KCB_CALLER_DS) != g_t[g_cur].hinst) return 0;
    return g_t[g_cur].h;
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
    KCBW(KCB_NTASKS) = (i == g_dying) ? 0 : g_n;
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
    int n;
    task_init();
    if (g_n <= 1) { pm_int21(ax); return; }
    t = &g_t[g_cur];
    {   /* okna, kolejka i klasy: USER, obiekty: GDI (KERNEL nie importuje tych modulow) */
        typedef void (API *USEREXIT)(WORD, WORD, BOOL);
        typedef void (API *GDIEXIT)(WORD);
        HMODULE u = GetModuleHandle("USER"), g = GetModuleHandle("GDI");
        USEREXIT fu = u ? (USEREXIT)GetProcAddress(u, (LPCSTR)MK_FP(0, 952)) : 0;
        GDIEXIT fg = g ? (GDIEXIT)GetProcAddress(g, (LPCSTR)MK_FP(0, 915)) : 0;
        if (fu) fu(t->h, t->hinst, g_fault);
        if (fg) fg(t->h);
    }
    kdos_close_task(t->h);
    kmem_free_task(t->h);
    set_running(t->hinst, FALSE);
    if (t->psp != SEL_PSP) GlobalFree(t->psp);
    GlobalFree(t->h);
    t->used = FALSE;
    g_n--;
    g_dying = -1;
    g_fault = FALSE;
    n = next_task();
    *(WORD __far *)MK_FP(SEL_DATA16, KCBW(KCB_I3F_DEPTH)) = 0;
    t->depth = 0;
    switch_to(n);                                 /* stos konczonego zadania nikomu nie wroci */
}

/* Wyjatek przy wielu zadaniach (kasm.asm: fault_entry, na stosie KCB_EXC_STACK): konczy tylko
 * winne zadanie. Komunikat po sprzataniu, inne programy dzialaja w tym czasie dalej. */
void task_fault_c(void)
{
    static char msg[80];
    typedef int (API *MSGBOX)(WORD, LPCSTR, LPCSTR, WORD);
    HMODULE u;
    MSGBOX mb;
    int mi;
    *(WORD __far *)MK_FP(SEL_DATA16, KCBW(KCB_I3F_DEPTH)) = 0;
    KCBW(KCB_NTASKS) = 0;                          /* wyjatek przy sprzataniu = panika */
    g_dying = g_cur;
    g_fault = TRUE;
    mi = mod_from_handle(g_t[g_cur].hinst);
    kstrcpy((LPSTR)msg, "Program ");
    if (mi >= 0) kstrcpy((LPSTR)msg + kstrlen((LPCSTR)msg), (LPCSTR)(MTE(mi) + MT_NAME));
    kstrcpy((LPSTR)msg + kstrlen((LPCSTR)msg), " wykonal niedozwolona operacje i zostal zamkniety.");
    dbg_str("KERNEL: wyjatek konczy zadanie: ", (LPCSTR)msg);
    u = GetModuleHandle("USER");
    {
        typedef void (API *USEREXIT)(WORD, WORD, BOOL);
        USEREXIT fu = u ? (USEREXIT)GetProcAddress(u, (LPCSTR)MK_FP(0, 952)) : 0;
        if (fu) fu(g_t[g_cur].h, g_t[g_cur].hinst, TRUE);
    }
    mb = u ? (MSGBOX)GetProcAddress(u, (LPCSTR)MK_FP(0, 1)) : 0;
    if (mb) mb(0, (LPCSTR)msg, (LPCSTR)"Blad programu", 0x10);
    task_exit(0x4CFF);
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
    kmem_set_owner(t->h, 0);                             /* nie naleza do uruchamiajacego */
    kmem_set_owner(t->psp, 0);
    set_running(t->hinst, TRUE);
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
    KCBW(KCB_NTASKS) = g_n;
    dbg_str("KERNEL: WinExec ", (LPCSTR)prog);
    Yield();                                             /* nowe zadanie dostaje CPU od razu */
    return t->hinst;
}
