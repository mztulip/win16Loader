/* toolhelp.c - TOOLHELP.DLL: informacje o systemie (jedno zadanie, bez listy sterty) */
#include "w16.h"

void API FatalAppExit(WORD, LPCSTR);

typedef struct { DWORD dwSize, dwmsSinceStart, dwmsThisVM; } TIMERINFO;

/* ordinal 80 */
BOOL API TimerCount(TIMERINFO __far *t)
{
    DWORD ms = GetTickCount();
    if (!t) return FALSE;
    t->dwmsSinceStart = ms;
    t->dwmsThisVM = ms;
    return TRUE;
}

/* Handlery wyjatkow / powiadomien - akceptujemy i ignorujemy */
BOOL API InterruptRegister(WORD task, DWORD proc)   { (void)task; (void)proc; return TRUE; }
BOOL API InterruptUnRegister(WORD task)             { (void)task; return TRUE; }
BOOL API NotifyRegister(WORD task, DWORD proc, WORD fl) { (void)task; (void)proc; (void)fl; return TRUE; }
BOOL API NotifyUnregister(WORD task)                { (void)task; return TRUE; }

/* ordinal 77: zakoncz zadanie (u nas jest tylko biezace) */
void API TerminateApp(WORD task, WORD fl)
{
    (void)task; (void)fl;
    dbg("TOOLHELP: TerminateApp\n");
    FatalAppExit(0, "TerminateApp");
}

typedef struct {
    DWORD dwSize;
    char  szModule[10];
    WORD  hModule;
    WORD  wcUsage;
    char  szExePath[256];
    WORD  wNext;
} MODULEENTRY;

/* ordinal 62 */
WORD API ModuleFindHandle(MODULEENTRY __far *me, WORD hmod)
{
    int i, s = 0, k;
    if (!me || !hmod) return 0;
    if (GetModuleFileName(hmod, me->szExePath, sizeof(me->szExePath)) <= 0) return 0;
    for (i = 0; me->szExePath[i]; i++) if (me->szExePath[i] == '\\' || me->szExePath[i] == ':') s = i + 1;
    for (k = 0; k < 9 && me->szExePath[s + k] && me->szExePath[s + k] != '.'; k++) {
        char c = me->szExePath[s + k];
        me->szModule[k] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    me->szModule[k] = 0;
    me->hModule = hmod;
    me->wcUsage = 1;
    me->wNext = 0;
    return hmod;
}

BOOL API ModuleFirst(MODULEENTRY __far *me)               { (void)me; return FALSE; }
BOOL API ModuleNext(MODULEENTRY __far *me)                { (void)me; return FALSE; }
WORD API ModuleFindName(MODULEENTRY __far *me, LPCSTR n)  { (void)me; (void)n; return 0; }

/* Zadania i sterta - bez wyliczania */
BOOL API TaskFirst(LPVOID te)                    { (void)te; return FALSE; }
BOOL API TaskNext(LPVOID te)                     { (void)te; return FALSE; }
WORD API TaskFindHandle(LPVOID te, WORD t)       { (void)te; (void)t; return 0; }
BOOL API GlobalFirst(LPVOID ge, WORD f)          { (void)ge; (void)f; return FALSE; }
BOOL API GlobalNext(LPVOID ge, WORD f)           { (void)ge; (void)f; return FALSE; }
BOOL API GlobalEntryHandle(LPVOID ge, WORD h)    { (void)ge; (void)h; return FALSE; }
BOOL API GlobalEntryModule(LPVOID ge, WORD m, WORD s) { (void)ge; (void)m; (void)s; return FALSE; }
WORD API GlobalHandleToSel(WORD h)               { return h; }

typedef struct { DWORD dwSize; WORD wUserFreePercent, wGDIFreePercent; WORD hUserSegment, hGDISegment; } SYSHEAPINFO;

BOOL API SystemHeapInfo(SYSHEAPINFO __far *s)
{
    if (!s) return FALSE;
    s->wUserFreePercent = 80;
    s->wGDIFreePercent = 80;
    s->hUserSegment = 0;
    s->hGDISegment = 0;
    return TRUE;
}

BOOL API MemManInfo(LPVOID m) { (void)m; return FALSE; }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
