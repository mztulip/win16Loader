/* ddeml.c - DDEML.DLL: biblioteka DDE bez innych procesow
 *
 * Jest tylko jedna aplikacja, wiec nie ma z kim rozmawiac: DdeConnect zawsze
 * zwraca 0 (DMLERR_NO_CONV_ESTABLISHED), a uchwyty napisow to indeksy
 * prywatnej tablicy (zeby DdeQueryString/DdeCmpStringHandles dzialaly).
 */
#include "w16.h"

#define DMLERR_NO_ERROR            0
#define DMLERR_NO_CONV_ESTABLISHED 0x400A

#define MAX_HSZ 64
static char g_hsz[MAX_HSZ][40];
static WORD g_err;

static int hsz_idx(DWORD h) { return (h >= 1 && h <= MAX_HSZ && g_hsz[h - 1][0]) ? (int)h - 1 : -1; }

/* ordinal 2 */
WORD API DdeInitialize(DWORD __far *inst, DWORD cb, DWORD cmd, DWORD res)
{
    (void)cb; (void)cmd; (void)res;
    if (inst) *inst = 1;
    return DMLERR_NO_ERROR;
}

BOOL API DdeUninitialize(DWORD inst) { (void)inst; return TRUE; }

DWORD API DdeConnect(DWORD inst, DWORD svc, DWORD topic, LPVOID cc)
{
    (void)inst; (void)svc; (void)topic; (void)cc;
    g_err = DMLERR_NO_CONV_ESTABLISHED;
    return 0;
}

DWORD API DdeConnectList(DWORD inst, DWORD svc, DWORD topic, DWORD l, LPVOID cc)
{
    (void)inst; (void)svc; (void)topic; (void)l; (void)cc;
    g_err = DMLERR_NO_CONV_ESTABLISHED;
    return 0;
}
DWORD API DdeQueryNextServer(DWORD l, DWORD prev)   { (void)l; (void)prev; return 0; }
BOOL  API DdeDisconnectList(DWORD l)                 { (void)l; return TRUE; }
BOOL  API DdeDisconnect(DWORD c)                     { (void)c; return TRUE; }
WORD  API DdeQueryConvInfo(DWORD c, DWORD t, LPVOID ci) { (void)c; (void)t; (void)ci; return 0; }
BOOL  API DdeSetUserHandle(DWORD c, DWORD id, DWORD u)  { (void)c; (void)id; (void)u; return FALSE; }

DWORD API DdeClientTransaction(LPVOID data, DWORD n, DWORD conv, DWORD item, WORD fmt, WORD type,
                               DWORD tmo, DWORD __far *res)
{
    (void)data; (void)n; (void)conv; (void)item; (void)fmt; (void)type; (void)tmo;
    if (res) *res = 0;
    g_err = DMLERR_NO_CONV_ESTABLISHED;
    return 0;
}

BOOL  API DdeAbandonTransaction(DWORD i, DWORD c, DWORD t) { (void)i; (void)c; (void)t; return TRUE; }
BOOL  API DdePostAdvise(DWORD i, DWORD t, DWORD it)        { (void)i; (void)t; (void)it; return TRUE; }

/* Uchwyty danych: blok GlobalAlloc (uchwyt = selektor) */
DWORD API DdeCreateDataHandle(DWORD inst, BYTE __far *src, DWORD cb, DWORD off, DWORD item, WORD fmt, WORD cmd)
{
    WORD h;
    (void)inst; (void)item; (void)fmt; (void)cmd;
    if (cb > 0xFFF0UL) return 0;
    h = GlobalAlloc(0x42, cb ? cb : 1);
    if (h && src && cb) w_memcpy(GlobalLock(h), src + (WORD)off, (WORD)cb);
    return h;
}
DWORD API DdeAddData(DWORD h, BYTE __far *src, DWORD cb, DWORD off)
{
    if (h && src && off + cb <= 0xFFF0UL) w_memcpy((BYTE __far *)GlobalLock((WORD)h) + (WORD)off, src, (WORD)cb);
    return h;
}
DWORD API DdeGetData(DWORD h, BYTE __far *dst, DWORD cb, DWORD off)
{
    if (h && dst) w_memcpy(dst, (BYTE __far *)GlobalLock((WORD)h) + (WORD)off, (WORD)cb);
    return cb;
}
LPVOID API DdeAccessData(DWORD h, DWORD __far *cb) { if (cb) *cb = 0; return h ? GlobalLock((WORD)h) : 0; }
BOOL  API DdeUnaccessData(DWORD h)                 { (void)h; return TRUE; }
BOOL  API DdeFreeDataHandle(DWORD h)               { if (h) GlobalFree((WORD)h); return TRUE; }
WORD  API DdeGetLastError(DWORD inst)              { WORD e = g_err; (void)inst; g_err = 0; return e; }

DWORD API DdeCreateStringHandle(DWORD inst, LPCSTR s, int cp)
{
    int i;
    (void)inst; (void)cp;
    if (!s || !s[0]) return 0;
    for (i = 0; i < MAX_HSZ; i++) if (g_hsz[i][0] && !w_stricmp(g_hsz[i], s)) return i + 1;
    for (i = 0; i < MAX_HSZ; i++) if (!g_hsz[i][0]) { w_strncpy(g_hsz[i], s, sizeof(g_hsz[i])); return i + 1; }
    return 0;
}
BOOL  API DdeFreeStringHandle(DWORD inst, DWORD h) { (void)inst; (void)h; return TRUE; }
BOOL  API DdeKeepStringHandle(DWORD inst, DWORD h) { (void)inst; (void)h; return TRUE; }

DWORD API DdeQueryString(DWORD inst, DWORD h, LPSTR buf, DWORD n, int cp)
{
    int i = hsz_idx(h);
    (void)inst; (void)cp;
    if (i < 0) { if (buf && n) buf[0] = 0; return 0; }
    if (buf && n) w_strncpy(buf, g_hsz[i], (int)(n < 0x7FFF ? n : 0x7FFF));
    return w_strlen(g_hsz[i]);
}

int API DdeCmpStringHandles(DWORD a, DWORD b)
{
    int ia = hsz_idx(a), ib = hsz_idx(b), r;
    if (ia < 0 || ib < 0) return a < b ? -1 : a > b ? 1 : 0;
    r = w_stricmp(g_hsz[ia], g_hsz[ib]);
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}

BOOL  API DdeEnableCallback(DWORD inst, DWORD c, WORD cmd) { (void)inst; (void)c; (void)cmd; return TRUE; }
DWORD API DdeNameService(DWORD inst, DWORD s1, DWORD s2, WORD cmd) { (void)inst; (void)s1; (void)s2; (void)cmd; return 1; }
DWORD API DdeReconnect(DWORD c) { (void)c; return 0; }

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
