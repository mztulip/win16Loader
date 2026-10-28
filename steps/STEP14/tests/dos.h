/*
 * tests/dos.h - mock Watcom <dos.h> dla testow hostowych (GCC/Linux)
 *
 * pm_helpers.c uzyla #include <dos.h> dla MK_FP i __far.
 * Ten plik zastepuje systemowy dos.h gdy kompilujemy z -Itests/.
 *
 * MK_FP ignoruje segment (seg) i indeksuje bezposrednio w buforze GDT,
 * poniewaz w testach g_gdt_off_c = 0 wiec offset = sel.
 */
#ifndef _MOCK_DOS_H
#define _MOCK_DOS_H

#define __far
#define __cdecl

/* Wskaznik do fałszywego bufora GDT; ustawiany przez test przed wywolaniem patch_gdt_c() */
extern unsigned char *_mock_gdt_base;

/* MK_FP(seg, off): ignorujemy seg, zwracamy _mock_gdt_base + off */
#define MK_FP(seg, off)  ((void *)(_mock_gdt_base + (unsigned)(off)))
#define FP_SEG(fp)       0
#define FP_OFF(fp)       0

#endif /* _MOCK_DOS_H */
