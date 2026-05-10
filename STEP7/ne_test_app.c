/*
 * ne_test_app.c - STEP7: apka testujaca DLL z DGROUP
 *
 * Weryfikacja: OutputDebugString (z KERNEL.EXE, ordinal 1) dziala
 * po patchowaniu INTERNALREF fixupow w kodzie DLL.
 * Jesli g_call_count w KERNEL.DGROUP jest poprawnie inkrementowany,
 * to fixupy INTERNALREF i segment danych DLL dzialaja poprawnie.
 *
 * Kompilacja (Makefile):
 *   wcc -ms -q -zl -s ne_test_app.c -fo=ne_test_app.obj
 *   wlink system windows name ne_test.exe file ne_test_app.obj
 *         import OutputDebugString_ KERNEL.1
 *         option nodefaultlibs option start=app_entry_ option quiet
 */

extern void __far __pascal OutputDebugString(const char __far *s);

static char g_msg1[] = "STEP7: Hello from ne_test_app!\n";
static char g_msg2[] = "STEP7: INTERNALREF fixup test passed!\n";
static char g_msg3[] = "STEP7: app done.\n";

void __far app_entry(void)
{
    OutputDebugString(g_msg1);
    OutputDebugString(g_msg2);
    OutputDebugString(g_msg3);
}
