/* rc_loader.h - generyczny loader zasobow NE
 * ETAP 18: zastepuje hardcodowane load_ski_strings/load_ski_bitmaps z STEP17.
 *
 * Kompilacja: wcc -ml -q rc_loader.c -fo=rc_loader.obj
 * Linkowanie:  dolaczony do loader.exe (wlink ... rc_loader.obj)
 */
#ifndef RC_LOADER_H
#define RC_LOADER_H

/* Typy zasobow NE (bit15=1 -> ID liczbowy) */
#define RT_CURSOR   0x8001u
#define RT_BITMAP   0x8002u
#define RT_ICON     0x8003u
#define RT_MENU     0x8004u
#define RT_DIALOG   0x8005u
#define RT_STRING   0x8006u
#define RT_ACCEL    0x8009u

/* rc_init: otworz plik NE i zapamietaj pozycje tablicy zasobow.
 * Musi byc wywolana przed rc_load_all / find_ne_resource.
 * Jesli plik nie istnieje lub nie jest NE - dalsza praca jest no-op. */
void rc_init(const char *filename);

/* rc_load_all: wczytaj wszystkie zasoby z otwartego pliku:
 *   RT_STRING  (0x8006) -> KCB[32..255] (format KCB_LAYOUT jak w STEP17)
 *   RT_BITMAP  (0x8002) -> bufor SEL_BITMAPS (format jak w STEP17)
 *   RT_MENU    (0x8004) -> RSC_HEAP (wewnetrzny bufor 8KB)
 *   RT_DIALOG  (0x8005) -> RSC_HEAP
 *   RT_ACCEL   (0x8009) -> RSC_HEAP
 * Parametry:
 *   kcb_phys - adres fizyczny KCB (g_kcb_phys)
 *   bmp_phys - adres fizyczny bufora bitmap (g_bitmaps_phys, 0 = pomijaj) */
int rc_load_all(unsigned long kcb_phys, unsigned long bmp_phys);

/* rc_get: zwroc far ptr do danych zasobu wczytanego do RSC_HEAP.
 * type: RT_MENU / RT_DIALOG / RT_ACCEL
 * id:   numer zasobu (MAKEINTRESOURCE n)
 * Zwraca NULL jesli zasob nie zostal wczytany. */
unsigned char __far *rc_get(unsigned short type, unsigned short id);

/* rc_size: zwroc rozmiar zasobu z RSC_HEAP w bajtach (0 = nie znaleziono). */
unsigned short rc_size(unsigned short type, unsigned short id);

/* find_ne_resource: niskopoziomowy lookup jednego zasobu w tablicy NE.
 * Wymaga wczesniejszego wywolania rc_init().
 * type_id: np. RT_BITMAP (0x8002), RT_STRING (0x8006)
 * res_id:  numer zasobu (rnID & 0x7FFF)
 * Zwraca 1 jesli znaleziony; out_file_off = offset w pliku, out_size = rozmiar (bajty).
 * Zwraca 0 jesli nie znaleziono lub rc_init() nie zostalo wywolane poprawnie. */
int find_ne_resource(unsigned short type_id, unsigned short res_id,
                     long *out_file_off, unsigned short *out_size);

#endif /* RC_LOADER_H */
