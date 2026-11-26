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
    BOOL  dying;                     /* konczone po wyjatku */
    HTASK parent;
    WORD  prev;                      /* hPrevInstance: dzialajaca kopia tego programu */
    WORD  events;                    /* WaitEvent/PostEvent; 1 na start: kod startowy programu czeka raz */
    HANDLE queue;                    /* SetTaskQueue */
    HINSTANCE libs[8];               /* LoadLibrary: reszte zwalnia koniec zadania */
    BYTE  drive;                     /* dysk i katalog biezacy, gdy DOS ma ustawiony stan innego zadania */
    char  dir[66];
    WORD  depth;                     /* stan thunka INT 3Fh */
    WORD  i3f[I3F_MAX * 3];
} TASK;

static TASK g_t[MAX_TASKS];
static int  g_cur, g_n;
static int  g_dos_owner = -1;        /* zadanie, ktorego katalog jest teraz ustawiony w DOS */

extern void fault_entry(void);

/* Dla task_boot (kasm.asm): pierwsze uruchomienie zadania */
WORD g_boot_cs, g_boot_ip, g_boot_ds, g_boot_psp, g_boot_prev, g_boot_show;

extern void task_switch(WORD *save_ss, WORD *save_sp, WORD new_ss, WORD new_sp);
#pragma aux task_switch parm [ax] [dx] [bx] [cx];
extern void task_boot(void);

static WORD cur_ds(void);
#pragma aux cur_ds = "mov ax, ds" value [ax];

/* Wynik INT E0h (mod_exec_rm w module.c), w RMBUF */
#define MAX_LATE_DLL 16
typedef struct {
    WORD ok, hinst, entry_cs, entry_ip, initsp, stack, heap, heapstart;
    WORD ndll, dll[MAX_LATE_DLL * 4];              /* LibEntry: cs, ip, ds, heap */
} EXECRES;
#define EXEC_RM_INT 0xE0

extern WORD libentry_call(WORD cs, WORD ip, WORD ds, WORD heap, WORD psp);
#pragma aux libentry_call parm [ax] [dx] [bx] [cx] [si] value [ax];

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

static BOOL g_locked;

BOOL task_started(void) { return g_n != 0; }
WORD task_prev(void) { task_init(); return g_t[g_cur].prev; }

/* Inna dzialajaca kopia programu o tej samej nazwie (hInstance) albo 0 */
static WORD other_instance(int self, WORD hinst)
{
    int mi = mod_from_handle(hinst), i;
    if (mi < 0) return 0;
    for (i = MAX_TASKS - 1; i >= 0; i--) {
        int m2;
        if (i == self || !g_t[i].used || g_t[i].dying || g_t[i].hinst == hinst) continue;
        m2 = mod_from_handle(g_t[i].hinst);
        if (m2 >= 0 && !kstricmp((LPCSTR)(MTE(mi) + MT_NAME), (LPCSTR)(MTE(m2) + MT_NAME))) return g_t[i].hinst;
    }
    return 0;
}

void task_lib_loaded(HINSTANCE h)
{
    int i;
    if (!g_n) return;
    for (i = 0; i < 8; i++) if (!g_t[g_cur].libs[i]) { g_t[g_cur].libs[i] = h; return; }
}

void task_lib_freed(HINSTANCE h)
{
    int i;
    if (!g_n) return;
    for (i = 0; i < 8; i++) if (g_t[g_cur].libs[i] == h) { g_t[g_cur].libs[i] = 0; return; }
}

static void set_running(WORD hinst, BOOL on)
{
    int mi = mod_from_handle(hinst);
    if (mi < 0) return;
    if (on) MTW(mi, MT_FLAGS) |= MTF_RUNNING;
    else MTW(mi, MT_FLAGS) &= ~MTF_RUNNING;
}


/* Stos dla wyjatku konczacego zadanie: KERNEL dziala na CPL=0, a SS musi miec DPL == CPL.
 * GlobalAlloc daje bloki z DPL=3 (selektory aplikacji z RPL=3, jak w Windows), wiec
 * zaladowanie takiego bloku do SS w obsludze wyjatku dawalo #GP w kolko. */
WORD API SelectorAccessRights(WORD sel, WORD op, WORD rights);
static WORD exc_stack_alloc(void)
{
    WORD st = (WORD)GlobalAlloc(0x40, 0x2000);
    if (st) SelectorAccessRights(st, 1, 0x92);
    return st;
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
    t->events = 1;
    t->initsp = KCBW(KCB_APP_SP);
    t->stack = KCBW(KCB_APP_STACK);
    t->heap = KCBW(KCB_APP_HEAP);
    t->heapstart = KCBW(KCB_APP_HEAPSTART);
    g_cur = 0;
    g_n = 1;
    set_running(t->hinst, TRUE);
    {   /* wyjatek konczacy zadanie dziala na wlasnym stosie: stos zadania moze byc zepsuty */
        WORD st = exc_stack_alloc();
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

/* Przed kazdym wywolaniem DOS (kdos.c). Pierwsze wywolanie: DOS ma juz stan startowego zadania. */
void task_dos_sync(void)
{
    static BOOL busy;
    if (!g_n || busy || g_dos_owner == g_cur) return;
    busy = TRUE;
    if (g_dos_owner >= 0) dos_dir_save(&g_t[g_dos_owner].drive, (LPSTR)g_t[g_dos_owner].dir);
    if (g_dos_owner != -1) dos_dir_restore(g_t[g_cur].drive, (LPCSTR)g_t[g_cur].dir);
    g_dos_owner = g_cur;
    busy = FALSE;
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
    g_boot_ds = n->hinst; g_boot_psp = n->psp; g_boot_prev = n->prev;
    g_boot_show = n->show;
    g_cur = i;
    KCBW(KCB_NTASKS) = n->dying ? 0 : g_n;                /* wyjatek przy sprzataniu = panika */
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
    if (g_locked) return;
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
/* ordinal 30: FALSE, gdy bylo zdarzenie; inaczej czeka na PostEvent, pozwalajac dzialac innym */
BOOL API WaitEvent(HTASK h)
{
    TASK *t;
    task_init();
    (void)h;
    t = &g_t[g_cur];
    if (t->events) { t->events--; return FALSE; }
    while (!t->events && g_n > 1) Yield();
    if (t->events) t->events--;
    return TRUE;
}

/* ordinal 31 */
void API PostEvent(HTASK h)
{
    int i;
    task_init();
    for (i = 0; i < MAX_TASKS; i++)
        if (g_t[i].used && (g_t[i].h == h || (!h && i == g_cur))) { g_t[i].events++; return; }
}

/* ordinal 33 */
HTASK API LockCurrentTask(BOOL f)
{
    task_init();
    g_locked = f;
    return f ? g_t[g_cur].h : 0;
}
/* ordinal 122 */
HTASK API IsTaskLocked(void) { task_init(); return g_locked ? g_t[g_cur].h : 0; }

static TASK *task_of(HTASK h)
{
    int i;
    task_init();
    if (!h) return &g_t[g_cur];
    for (i = 0; i < MAX_TASKS; i++) if (g_t[i].used && g_t[i].h == h) return &g_t[i];
    return 0;
}

/* ordinal 34 */
HANDLE API SetTaskQueue(HTASK h, HANDLE q)
{
    TASK *t = task_of(h);
    HANDLE o;
    if (!t) return 0;
    o = t->queue; t->queue = q;
    return o;
}
/* ordinal 35 */
HANDLE API GetTaskQueue(HTASK h) { TASK *t = task_of(h); return t ? t->queue : 0; }

/* ordinal 900 (prywatny, TOOLHELP): dane zadania nr i lub nastepnego. Wynik: nr + 1, 0 = koniec.
 * out: hTask, hParent, hInst, hModule, SS, SP, pStackTop, pStackMin, pStackBottom, events, queue */
WORD API TaskEnum(WORD i, WORD __far *out, LPSTR name)
{
    int mi;
    task_init();
    while (i < MAX_TASKS && !g_t[i].used) i++;
    if (i >= MAX_TASKS) return 0;
    {
        TASK *t = &g_t[i];
        WORD __far *inst = (WORD __far *)MK_FP(t->hinst, 0);
        mi = mod_from_handle(t->hinst);
        out[0] = t->h; out[1] = t->parent; out[2] = t->hinst;
        out[3] = mi >= 0 ? MTW(mi, MT_HMOD) : 0;
        out[4] = t->hinst; out[5] = t->sp;
        out[6] = inst[0x0A / 2]; out[7] = inst[0x0C / 2]; out[8] = inst[0x0E / 2];
        out[9] = t->events; out[10] = t->queue;
        if (mi >= 0) kstrncpy(name, (LPCSTR)(MTE(mi) + MT_NAME), 10);
    }
    return i + 1;
}

/* ordinal 130: powiadomienia o zmianach plikow. Nic ich jeszcze nie wysyla, ale wywolanie sie udaje. */
FARPROC API FileCDR(FARPROC proc)  { (void)proc; return (FARPROC)MK_FP(0, 1); }

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
    if (g_n <= 1) {
        /* Bez dyspozytora (KCB_INT_CB = 0) INT 21h idzie prosto do pm_call.asm i wraca do loadera
         * w trybie rzeczywistym. Przez dyspozytor wrocilby tu (4Ch -> task_exit) bez konca. */
        KCBW(KCB_INT_CB + 2) = 0;
        pm_int21(ax);
        return;
    }
    t = &g_t[g_cur];
    g_locked = FALSE;
    {   /* biblioteki z LoadLibrary, ktorych zadanie nie zwolnilo */
        int k;
        for (k = 0; k < 8; k++) if (t->libs[k]) FreeLibrary(t->libs[k]);
    }
    {   /* okna, kolejka i klasy: USER, obiekty: GDI (KERNEL nie importuje tych modulow) */
        typedef void (API *USEREXIT)(WORD, WORD, BOOL, WORD);
        typedef void (API *GDIEXIT)(WORD);
        HMODULE u = GetModuleHandle("USER"), g = GetModuleHandle("GDI");
        USEREXIT fu = u ? (USEREXIT)GetProcAddress(u, (LPCSTR)MK_FP(0, 952)) : 0;
        GDIEXIT fg = g ? (GDIEXIT)GetProcAddress(g, (LPCSTR)MK_FP(0, 915)) : 0;
        if (fu) fu(t->h, t->hinst, t->dying, other_instance(g_cur, t->hinst));
        if (fg) fg(t->h);
    }
    kdos_close_task(t->h);
    kmem_free_task(t->h);
    set_running(t->hinst, FALSE);
    if (g_dos_owner == g_cur) g_dos_owner = -2;          /* stan DOS niczyj: nie zapisuj */
    if (t->psp != SEL_PSP) GlobalFree(t->psp);
    GlobalFree(t->h);
    t->used = FALSE;
    g_n--;
    t->dying = FALSE;
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
    g_t[g_cur].dying = TRUE;
    {   /* ten stos zostaje na czas komunikatu, inne zadanie moze w tym czasie tez zawiesc */
        WORD st = exc_stack_alloc();
        if (st) { kmem_set_owner(st, 0); KCBW(KCB_EXC_STACK) = st; }
    }
    mi = mod_from_handle(g_t[g_cur].hinst);
    kstrcpy((LPSTR)msg, "Program ");
    if (mi >= 0) kstrcpy((LPSTR)msg + kstrlen((LPCSTR)msg), (LPCSTR)(MTE(mi) + MT_NAME));
    kstrcpy((LPSTR)msg + kstrlen((LPCSTR)msg), " wykonal niedozwolona operacje i zostal zamkniety.");
    dbg_str("KERNEL: wyjatek konczy zadanie: ", (LPCSTR)msg);
    dbg_puts("  numer="); dbg_hex(KCBW(KCB_EXC_INFO), 2);
    dbg_puts(" CS:IP="); dbg_hex(KCBW(KCB_EXC_INFO + 4), 4); dbg_puts(":"); dbg_hex(KCBW(KCB_EXC_INFO + 2), 4); dbg_puts("\n");
    u = GetModuleHandle("USER");
    {
        typedef void (API *USEREXIT)(WORD, WORD, BOOL, WORD);
        USEREXIT fu = u ? (USEREXIT)GetProcAddress(u, (LPCSTR)MK_FP(0, 952)) : 0;
        if (fu) fu(g_t[g_cur].h, g_t[g_cur].hinst, TRUE, other_instance(g_cur, g_t[g_cur].hinst));
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

/* Wywolaj loader w trybie rzeczywistym (mode 0 = program, 1 = DLL). Wynik w RMBUF. */
static void exec_rm(WORD mode, LPCSTR path)
{
    RMREGS __far *r = rmregs();
    WORD __far *in = (WORD __far *)MK_FP(SEL_RMBUF, RMBUF_DATA);
    kdos_init();
    kmemset(r, 0, sizeof(RMREGS));
    in[0] = mode;
    kstrcpy((LPSTR)(in + 1), path);
    rm_int(EXEC_RM_INT, r);
}

/* LibEntry nowo zaladowanych DLL, w kolejnosci zaleznosci. Tak jak przy starcie systemu:
 * DS=DGROUP, DI=hInstance, CX=rozmiar sterty, ES:SI=linia polecen. FALSE: ktoras zwrocila blad
 * albo jest ich za duzo. Lista musi byc skopiowana: LibEntry moze uzyc RMBUF (DOS). */
static BOOL run_libentries(EXECRES __far *res)
{
    static WORD list[MAX_LATE_DLL * 4];
    WORD n = res->ndll, i;
    BOOL ok = TRUE;
    if (n > MAX_LATE_DLL) { dbg_puts("KERNEL: za duzo nowych DLL\n"); return FALSE; }
    kmemcpy((LPVOID)list, res->dll, n * 8);
    for (i = 0; i < n; i++) {
        int mi = mod_from_handle(list[i * 4 + 2]);
        if (!libentry_call(list[i * 4], list[i * 4 + 1], list[i * 4 + 2], list[i * 4 + 3], GetCurrentPDB())) {
            dbg_puts("KERNEL: LibEntry zwrocilo blad\n");
            if (mi >= 0) MTW(mi, MT_FLAGS) |= MTF_VIRTUAL;   /* drugi LoadLibrary nie odda jej jako dobrej */
            ok = FALSE;
        }
    }
    return ok;
}

/* ordinal 95: jeszcze nie w pamieci = zaladuj. Wynik < 32 = blad (2 brak pliku, 20 LibEntry) */
HINSTANCE API LoadLibrary(LPCSTR name)
{
    static char path[96];
    EXECRES __far *res = (EXECRES __far *)MK_FP(SEL_RMBUF, RMBUF_DATA);
    int mi = mod_from_name(name);
    dbg_str("KERNEL: LoadLibrary ", name);
    if (mi >= 0) {
        if (MTW(mi, MT_FLAGS) & MTF_VIRTUAL) return 2;
    } else {
        kstrcpy((LPSTR)path, name);
        if (!has_ext((LPCSTR)path)) kstrcpy((LPSTR)path + kstrlen((LPCSTR)path), ".DLL");
        exec_rm(1, (LPCSTR)path);
        if (!res->ok) return res->hinst ? res->hinst : 2;
        mi = res->hinst;
        if (!run_libentries(res)) return 20;
    }
    if (!(MTW(mi, MT_FLAGS) & MTF_SYSTEM)) {
        MTW(mi, MT_USAGE)++;
        task_lib_loaded(MTW(mi, MT_DGROUP) ? MTW(mi, MT_DGROUP) : MTW(mi, MT_HMOD));
    }
    return MTW(mi, MT_DGROUP) ? MTW(mi, MT_DGROUP) : MTW(mi, MT_HMOD);
}

/* ordinal 166. Wynik > 32 = hInstance, inaczej kod bledu jak w Windows (2 = brak pliku) */
WORD API WinExec(LPCSTR cmd, WORD show)
{
    static char prog[80];
    EXECRES __far *res = (EXECRES __far *)MK_FP(SEL_RMBUF, RMBUF_DATA);
    LPCSTR args;
    int i, slot = -1, n;
    TASK *t;
    BYTE __far *psp;
    WORD __far *f;
    task_init();
    dbg_str("KERNEL: WinExec cmd: ", cmd);
    args = split_cmd(cmd, (LPSTR)prog, sizeof(prog) - 4);
    if (!prog[0]) return 2;
    if (!has_ext((LPCSTR)prog)) kstrcpy((LPSTR)prog + kstrlen((LPCSTR)prog), ".EXE");
    for (i = 0; i < MAX_TASKS; i++) if (!g_t[i].used) { slot = i; break; }
    if (slot < 0) return 8;                              /* za malo pamieci */
    exec_rm(0, (LPCSTR)prog);
    if (!res->ok) {
        dbg_str("KERNEL: WinExec nie udany ", (LPCSTR)prog);
        return res->hinst ? res->hinst : 2;              /* hinst = kod bledu */
    }
    if (res->ok == 2) {
        /* Program DOS zakonczyl dzialanie w trybie pelnoekranowym (Standard Mode) */
        HMODULE u = GetModuleHandle("USER");
        if (u) {
            typedef void (API *INVALRECT)(WORD, LPVOID, BOOL);
            typedef void (API *RSTCUR)(void);
            INVALRECT inv = (INVALRECT)GetProcAddress(u, (LPCSTR)MK_FP(0, 125));
            RSTCUR urst = (RSTCUR)GetProcAddress(u, (LPCSTR)MK_FP(0, 953));
            if (inv) inv(0, (LPVOID)0, TRUE);
            if (urst) urst();
        }
        task_dos_sync();
        dbg_str("KERNEL: WinExec DOS zakonczony pomyslnie ", (LPCSTR)prog);
        return res->hinst ? res->hinst : 33;
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
    t->events = 1;
    task_dos_sync();                                     /* nowe zadanie dziedziczy katalog */
    dos_dir_save(&t->drive, (LPSTR)t->dir);
    if (!run_libentries(res)) return 20;                 /* jak LoadLibrary: blad DLL */
    t->parent = g_t[g_cur].h;
    t->prev = other_instance(slot, t->hinst);
    t->h = tdb_new(t->hinst);
    t->psp = (WORD)GlobalAlloc(0x40, 256);
    if (!t->h || !t->psp) return 8;
    kmem_set_owner(t->h, 0);                             /* nie naleza do uruchamiajacego */
    kmem_set_owner(t->psp, 0);
    set_running(t->hinst, TRUE);
    psp = (BYTE __far *)MK_FP(t->psp, 0);
    kmemcpy(psp, MK_FP(SEL_PSP, 0), 0x80);
    n = kstrlen(args);
    if (n > 124) n = 124;
    if (n > 0) {
        psp[0x80] = (BYTE)(n + 1);
        psp[0x81] = ' ';
        kmemcpy(psp + 0x82, args, n);
        psp[0x82 + n] = 0;
        psp[0x83 + n] = 0x0D;
    } else {
        psp[0x80] = 0;
        psp[0x81] = 0;
        psp[0x82] = 0x0D;
    }
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
