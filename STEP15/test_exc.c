/*
 * test_exc.c - test ekranu wyjatku (ETAP 15)
 * Celowo wywoluje dzielenie przez zero (#DE, exception 0x00).
 */

extern void __far __pascal OutputDebugString(const char __far *s);
extern void __far __pascal PostQuitMessage(int exitCode);

void __far app_entry(void)
{
    OutputDebugString("test_exc: START - zaraz exception #DE (div by zero)\r\n");

    /* Dzielenie przez zero -> IDT[0x00] -> exc_stub_00 -> panic_screen */
    __asm { xor  bx, bx }
    __asm { xor  ax, ax }
    __asm { xor  dx, dx }
    __asm { div  bx     }   /* #DE: divide by zero */

    /* Tu nie powinno dotrzec */
    OutputDebugString("test_exc: ERROR - nie powinno tu dotrzec!\r\n");
    PostQuitMessage(1);
}
