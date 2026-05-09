/*
 * ne_test_app.c - STEP5: apka importujaca OutputDebugString z KERNEL.EXE
 *
 * Kompilacja (Makefile):
 *   wcc -ms -q -zl -s ne_test_app.c -fo=ne_test_app.obj
 *   wlink system windows name ne_test.exe file ne_test_app.obj
 *         import OutputDebugString_ KERNEL.1
 *         option nodefaultlibs option start=app_entry_ option quiet
 *
 * Loader przed wywolaniem ustawia:
 *   CS = SEL_APP_CODE, DS = SEL_APP_DATA, ES = SEL_VGA
 *   Fixup w segmencie kodu: call far [KERNEL.1] -> SEL_KERNEL_CODE:0
 */

/* Import z KERNEL.EXE - ordinal 1, Watcom register convention.
 * far pointer: AX=offset, DX=segment */
extern void __far OutputDebugString(const char __far *s);

static char g_hello[] = "Hello from STEP5 app! Import dziala!\n";

void __far app_entry(void)
{
    OutputDebugString(g_hello);
    OutputDebugString("app_entry: koniec.\n");
}
