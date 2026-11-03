/* testlog.h - wspolny log testow aplikacji przykladowych
 *
 * Wyniki ida przez OutputDebugString na port szeregowy (KERNEL loadera), w formacie:
 *   TEST <APP> <nazwa>: PASS | FAIL (<szczegoly>)
 *   TEST <APP> DONE pass=N fail=M
 * tools/run_examples.py zbiera te linie z logu QEMU.
 */
#ifndef TESTLOG_H
#define TESTLOG_H

#include <windows.h>
#include <string.h>

static int t_pass, t_fail;
static const char *t_app = "APP";

static void t_log(const char *name, BOOL ok, const char *detail)
{
    char buf[200];
    wsprintf(buf, "TEST %s %s: %s%s%s%s\r\n", (LPSTR)t_app, (LPSTR)name, (LPSTR)(ok ? "PASS" : "FAIL"),
             (LPSTR)(detail && *detail ? " (" : ""), (LPSTR)(detail ? detail : ""), (LPSTR)(detail && *detail ? ")" : ""));
    OutputDebugString(buf);
    if (ok) t_pass++; else t_fail++;
}

/* CHECK(warunek, "nazwa") */
#define CHECK(c, name) t_log(name, (c) ? TRUE : FALSE, #c)

/* CHECK_INT(wartosc, oczekiwana, "nazwa") - wypisuje obie liczby przy bledzie */
#define CHECK_INT(v, exp, name) do { long _v = (long)(v), _e = (long)(exp); char _d[64]; \
    wsprintf(_d, "%ld == %ld", _v, _e); t_log(name, _v == _e, _d); } while (0)

/* CHECK_STR(s, oczekiwany, "nazwa") */
#define CHECK_STR(s, exp, name) do { char _d[120]; wsprintf(_d, "'%s' == '%s'", (LPSTR)(s), (LPSTR)(exp)); \
    t_log(name, lstrcmp((LPSTR)(s), (LPSTR)(exp)) == 0, _d); } while (0)

static void t_done(void)
{
    char buf[80];
    wsprintf(buf, "TEST %s DONE pass=%d fail=%d\r\n", (LPSTR)t_app, t_pass, t_fail);
    OutputDebugString(buf);
}

#endif /* TESTLOG_H */
