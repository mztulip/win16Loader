/* demodll.h - informacje, ktore DLL zebrala w swoim LibEntry */
typedef struct { int calls; WORD hinst, heap, ds; BOOL heap_ok; } DEMOINFO;
void FAR PASCAL DemoInfo(DEMOINFO FAR *out);
