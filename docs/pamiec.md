# Architektura pamięci win16Loader

Dokument opisuje organizację pamięci w projekcie **win16Loader**, podział na przestrzeń konwencjonalną DOS (< 640 KB) i rozszerzoną XMS (> 1 MB), mechanizmy alokacji i zwalniania pamięci oraz porównanie z Windows 3.1.

---

## 1. Dwuwarstwowy model pamięci

System pracuje w hybrydowym modelu łączącym tryb rzeczywisty (Real Mode 16-bit) oraz tryb chroniony (Protected Mode 16-bit 80286/i486):

```
+------------------------------------------------------------+ 0x00000000
| Pamięć konwencjonalna DOS (0 .. 640 KB)                    |
| - Wektory przerwań BIOS / BDA (0x00000 - 0x004FF)          |
| - Jądro FreeDOS (KERNEL.SYS, tablice CDS/SFT)              |
| - Powłoka COMMAND.COM                                      |
| - LOADER.EXE (kod, dane, DGROUP, stos)                     |
| - Struktury systemowe: Fake PSP, KCB, IDT, thunki          |
| - Dynamiczne bufory ładowania: g_tmp_seg (64KB), chunk (32KB)|
| - WOLNA PRZESTRZEŃ DLA APLIKACJI DOS (np. MARIO.EXE)       |
+------------------------------------------------------------+ 0x000A0000 (640 KB)
| Pamięć górna UMA / VGA / BIOS ROM                          |
| - Bufor tekstowy EGA/VGA (0xB8000)                         |
| - BIOS ROM / VGA BIOS                                      |
+------------------------------------------------------------+ 0x00100000 (1 MB)
| Pamięć rozszerzona XMS (> 1 MB)                            |
| - Segmenty kodu i danych modułów Windows (NE):             |
|   KERNEL, USER, GDI, DLL-e systemowe, Calmira, aplikacje   |
| - Zasoby aplikacji (RT_BITMAP, RT_MENU, ikony, dialogi)   |
| - Dynamiczna sterta GlobalAlloc / GlobalFree (kmem.c)      |
+------------------------------------------------------------+ Koniec RAM (16/32 MB)
```

---

## 2. Pamięć konwencjonalna (< 640 KB)

Pamięć poniżej 640 KB jest zarządzana przez strukturę łańcucha bloków **MCB (Memory Control Blocks)** DOS-a.

### Aktualna mapa bloków MCB (stan podczas działania Calmiry)

| Adres MCB | Typ | Właściciel | Rozmiar (paragrafy / KB) | Przeznaczenie |
|---|---|---|---|---|
| `028E` | M | 0008 (DOS) | 570 para (~8 KB) | Jądro FreeDOS (część danych) |
| `04C9` | M | 0008 (DOS) | 2637 para (~41 KB) | Jądro FreeDOS (kod i struktury I/O) |
| `0F17` | M | 0000 (FREE)| 6 para (0 KB) | Odstęp wyrównania |
| `0F1E` | M | 0F1F (COMMAND) | 4499 para (~70 KB) | Rezydentna część FreeDOS COMMAND.COM |
| `20BE` | M | 20BF (LOADER) | 7190 para (~112 KB) | Plik wykonywalny `LOADER.EXE` (kod, dane DGROUP) |
| `3CD5` | M | 20BF (LOADER) | 512 para (8 KB) | Bufor thunkow API (`g_thunk_seg`) |
| `3ED6` | M | 20BF (LOADER) | 1024 para (16 KB) | Bufor zasobów RSC_HEAP |
| `42D7` | M | 20BF (LOADER) | 128 para (2 KB) | Tablica IDT trybu chronionego (256 wektorów) |
| `4358` | M | 20BF (LOADER) | 256 para (4 KB) | Tablica modułów MODTAB (`g_modtab_seg`) |
| `4459` | M | 20BF (LOADER) | 512 para (8 KB) | Pierwszy chunk sterty dalekiej OpenWatcom (hash) |
| `465A` | M | 20BF (LOADER) | 16 para (0.25 KB) | Fake PSP dla aplikacji Win16 |
| `466B` | M | 20BF (LOADER) | 128 para (2 KB) | Blok Kontrolny Jądra (KCB - Kernel Control Block) |
| `46EC` | M | 20BF (LOADER) | 1024 para (16 KB) | Dedykowane tablice sterty modulow |
| `4AED` | M | 20BF (LOADER) | 512 para (8 KB) | Tablice ordynali DLL systemowych |
| **`4CEE`** | **M** | **0000 (FREE)** | **21168 para (330 KB)** | **Wolna pamięć konwencjonalna oddawana DOS** |
| `9F9F` | Z | 0F1F (COMMAND) | 32 para (0.5 KB) | Końcowy blok środowiska powłoki |

### Uruchamianie programów DOS (`dos_exec` w `module.c`)
Gdy użytkownik uruchamia pełnoekranowy program DOS (np. `C:\MARIO\MARIO.EXE` lub znakówkę `COMMAND.COM`):
1. Loader zamyka kursor myszy PS/2 i przełącza kartę graficzną w tryb tekstowy VGA 80x25 (`INT 10h AH=00h AL=03h`).
2. Tymczasowo zwalniane są górne bufory transferowe:
   - `g_tmp_seg` (0x1000 paragrafów = 64 KB)
   - `g_chunk_seg` (CHUNK = 32 KB)
3. Dzięki temu cały obszar od adresu `0x4CEE` do `0x9F9F` tworzy **jeden ciągły blok o rozmiarze ~330 KB**.
4. Wywoływane jest przerwanie wykonania `INT 21h AH=4Bh`.
5. Po zakończeniu programu DOS (kod wyjścia `AH=4Dh`) loader:
   - Ponownie alokuje bufory `g_tmp_seg` i `g_chunk_seg` na samej górze pamięci DOS (`dos_set_strategy(2)` – Last Fit).
   - Aktualizuje selektor `SEL_RMBUF` w GDT.
   - Wznawia tryb graficzny VESA LFB 1024x768x32bpp (`vesa_set_lfb`).
   - Przywraca obsługę myszy PS/2 i wraca natychmiast do pulpitu Calmiry w trybie chronionym.

---

## 3. Pamięć rozszerzona XMS (> 1 MB)

Wszystkie aplikacje Windows 16-bit oraz ich segmenty działają wyłącznie w pamięci powyżej 1 MB.

### A. Ładowanie modułów NE (`module.c`)
* Każdy segment kodu i danych pliku NE (New Executable) jest alokowany sekwencyjnie w XMS przez funkcję `xms_alloc`.
* Wyrównanie stron: każdy segment umieszczany jest na osobnych granicach stron 4 KB (`(next + 4095) & ~4095`), co eliminuje spowolnienia emulacji w QEMU związane z fałszywym wykrywaniem kodu samomodyfikującego się przy wspólnym kodzie i stosie.
* Kopiowanie danych z plików do pamięci XMS odbywa się w trybie rzeczywistym za pomocą przerwania BIOS `INT 15h AH=87h` (blokowe przenoszenie pamięci z użyciem tymczasowych deskryptorów GDT).

### B. Mechanizm Replay Cache (`ALLOCLOG`)
* Segmenty załadowanych programów Windows nie są kompaktowane w locie przy zamykaniu programu.
* Zamiast tego loader zapisuje adresy fizyczne XMS oraz przydzielone selektory GDT w strukturze `ALLOCLOG`.
* Przy ponownym uruchomieniu tego samego programu (np. wygaszacz ekranu `WIN31SS.SCR` uruchamiany wielokrotnie) moduł jest odtwarzany pod **dokładnie tymi samymi adresami fizycznymi i z tymi samymi selektorami**. Zapobiega to wyciekom selektorów GDT i pamięci XMS.

### C. Dynamiczna sterta Windows (`GlobalAlloc` / `GlobalFree` w `kernel/kmem.c`)
Aplikacje Win16 alokują pamięć dynamiczną w czasie działania za pomocą API Windows:
* **Lista bloków wolnych z łączeniem sąsiadów (Coalescing)**:
  Funkcja `xms_give` automatycznie łączy zwalniany blok z sąsiadującym blokiem poprzedzającym i następującym, zapobiegając fragmentacji przestrzeni XMS.
* **Zarządzanie selektorami GDT**:
  Selektory dla bloków `GlobalAlloc` (`g_sel_base` .. `g_sel_max`) są dynamicznie przydzielane, a po `GlobalFree` zerowane i zwracane do puli wielokrotnego użytku (`sel_free`).
* **Automatyczne sprzątanie zadań (`kmem_free_task`)**:
  Gdy aplikacja kończy działanie (`task_exit` / `DOS3Call 4Ch`), jądro KERNEL automatycznie skanuje tablicę selektorów i zwalnia wszystkie bloki XMS należące do danego zadania (z wyjątkiem bloków z flagą `GMEM_DDESHARE`). Moduł USER niszczy powiązane okna, timery i kolejki komunikatów.

---

## 4. Porównanie z Windows 3.1

| Cecha | Windows 3.1 (Tryb Standardowy 286) | Windows 3.1 (Tryb 386 Rozszerzony) | win16Loader (nasz projekt) |
|---|---|---|---|
| **Architektura procesora** | 16-bit Protected Mode (80286+) | 32-bit Paging + Virtual 8086 (80386+) | 16-bit Protected Mode (i486) |
| **Wolna pamięć DOS** | ~550–580 KB | ~580–615 KB | **~330 KB** |
| **Sposób uruchamiania DOS** | Swapowanie Windows na dysk (`~dswap.tmp`) | Wirtualne Maszyny DOS (V86 VM) | Bezpośredni powrót do Real Mode (w RAM) |
| **Czas przełączania do DOS** | Kilka do kilkunastu sekund (odczyt z dysku) | Natychmiast (okno lub pełny ekran) | **Natychmiast (< 0.1 s, brak dysku)** |
| **Położenie jądra Windows** | Pamięć konwencjonalna + XMS | Pamięć rozszerzona (System VM) | **Pamięć rozszerzona XMS (> 1 MB)** |
| **Położenie jądra DOS** | HMA (`DOS=HIGH` z HIMEM.SYS) | HMA + sterowniki w UMB | Pamięć konwencjonalna (< 640 KB) |

### Dlaczego nasz projekt ma ~330 KB, a nie 600 KB?
Windows 3.1 w trybie standardowym (286) osiągał 580 KB tylko dlatego, że w momencie uruchomienia DOS-a **kompletnie usuwał Windowsa z pamięci RAM i zapisywał go na dysk**.

W **win16Loader** priorytetem jest natychmiastowa responsywność bez mielenia dyskiem. Zarówno jądro FreeDOS, powłoka `COMMAND.COM`, jak i `LOADER.EXE` rezydują na stałe w pamięci konwencjonalnej, a aplikacje Windows czekają w nienaruszonym stanie w pamięci XMS. Dostępne 330 KB w zupełności wystarcza do uruchamiania gier i narzędzi DOS bez konieczności powolnego zrzucania stanu systemu na dysk.
