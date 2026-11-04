# Wydajność GDI pod QEMU: analiza spowolnienia

Po przepisaniu USER/GDI (`a63e7e4`) SkiFree i aplikacje okienkowe działały pod
`make run-qemu` w tempie ok. 1 klatki/s. Ten dokument opisuje przyczyny, poprawki,
ślepe uliczki i sposób diagnozy na przyszłość.

## Wyniki

Pomiary z `example_apps/gdiperf` (QEMU 11.1 TCG, `-display none`). Czasy bezwzględne zależą
od hosta; porównywalna między maszynami jest kolumna „% pętli” (czas operacji względem
prostej pętli kopiującej w aplikacji).

| Operacja | przed | wiersze + `-ms` | + wyrównanie XMS | % pętli (po) |
|---|---|---|---|---|
| `BitBlt` pamięć→pamięć 64×64 | 64,7 ms | 2,2 ms | **0,18 ms** | 40% |
| `BitBlt` pamięć→ekran 64×64 | 165 ms | 6,0 ms | **0,44 ms** | 88% |
| `BitBlt` ekran→ekran 64×64 | 173 ms | 6,3 ms | **0,69 ms** | 153% |
| `PatBlt` ekran 64×64 | 55 ms | 1,1 ms | **0,12 ms** | 21% |
| `PatBlt` kreskowany 64×64 | 144 ms | 5,7 ms | **0,45 ms** | 91% |
| `TextOut` ekran, 21 znaków | 42 ms | 8,0 ms | **0,41 ms** | 81% |
| `MoveWindow` z przerysowaniem | 578 ms | 73 ms | **5,9 ms** | 1292% |
| przerysowanie okna | 825 ms | 50 ms | **3,1 ms** | 686% |

SkiFree, po 12 s jazdy: `Dist` 2 m przed poprawkami (stare GUI: 13 m), po nich narciarz
rozpędza się do 27 m/s, a ekran zmienia się ok. 19 razy/s (limit: tick 18,2 Hz). Podczas jazdy
ok. 98% próbek CPU przypada na oczekiwanie na następny tick.

## Przyczyny

### 1. Kod i dane na jednej stronie 4 KB (główna, 12-20x)

`xms_alloc` w `module.c` układał segmenty co 16 bajtów, więc na każdej granicy kod i dane
dzieliły stronę. Dotyczyło to m.in. końca segmentu danych aplikacji, gdzie leży **stos**,
i początku segmentu kodu następnego modułu.

QEMU TCG oznacza strony z przetłumaczonym kodem i każdy zapis do takiej strony idzie wolną
ścieżką (wykrywanie kodu samomodyfikującego się). Trafiał w to każdy `push` i każda zmienna
lokalna, stąd koszt na wywołanie funkcji, a nie na instrukcję. Dlatego prosta pętla
kopiująca bez użycia stosu była szybka, a wszystko inne wolne.

**Poprawka:** `xms_alloc` wyrównuje bloki do 4 KB. Koszt: najwyżej kilkadziesiąt KB pamięci.

Stare GUI też miało ten układ, ale jego pętle pisały bezpośrednio do bufora i używały stosu
znacznie rzadziej. Nowe GDI wołało funkcje na każdy piksel, więc kara zwielokrotniła się
i dopiero wtedy stała się widoczna.

### 2. Rysowanie piksel po pikselu (ok. 25x)

`do_blt`, `span_rop2` i kursor wołały `px_get`/`px_put` na każdy piksel, a każde z nich liczyło
`y*pitch` przez `__U4M` i wyznaczało selektor. Gałąź opisana jako „szybka ścieżka” robiła
dokładnie to samo.

**Poprawka:** operacje masowe idą wierszami (`row_get`, `row_put`, `fill24`, `bits_row`
w `gsurf.c`, `blt_row` i kafel wzoru 8×8 w `gdraw.c`), z podziałem na okna 64 KB
i obsługą piksela ekranu rozciętego między selektory.

### 3. Kursor zdejmowany przy każdej operacji

Rysowanie na ekranie zdejmowało kursor, gdy leżał gdziekolwiek w clipie DC. Przy dużym oknie
każdy `BitBlt`, `TextOut`, a nawet `GetPixel` kasował i rysował od nowa 256 pikseli kursora.

**Poprawka:** wykluczany jest tylko rysowany prostokąt (`draw_open` + `draw_excl`).
Zawężenie ujawniło ukryty błąd: `cursor_exclude` ignorowało wywołanie zagnieżdżone, więc przy
blicie ekran→ekran wykluczenie celu było pomijane. Teraz każde wywołanie sprawdza swój prostokąt.

### 4. Mniejsze

- GDI w modelu `-ms` zamiast `-mm`: wywołania wewnętrzne near. Sam ten krok daje ok. 10%.
- `__U4M`/`__U4D` poza gorącymi ścieżkami (`mul16`, `ROOM24`, `MUL3`).

## Ślepe uliczki

Żeby ich nie powtarzać; wszystkie wyglądały przekonująco, a pomiar je obalił.

- **`-d int` w `run-qemu`.** Flaga była tam od dawna i nie ona spowodowała spowolnienie.
  Stała się jednak problemem po przyspieszeniu (zob. niżej).
- **Zegar IRQ0.** Komentarz „100 Hz, +10 ms” był nieaktualny; kod dolicza 55 ms przy 18 Hz.
- **Brak `HLT` w `PeekMessage`.** Stary USER usypiał tam CPU, nowy kręcił się w pętli. Nie było to
  przyczyną spowolnienia: dodanie `HLT` nic nie poprawiło (ale zob. niżej).
- **`dt = 0` przy szybkiej pętli.** SkiFree samo ogranicza klatki (`[0xb50] = 40 ms`).
- **Koszt far call w TCG.** Spodziewałem się wiele po `-ms`, a wyszło ok. 10%.
- **Liczenie zmian ekranu.** Licznik pokazywał „25 klatek/s”, gdy gra stała w miejscu. Mierz
  postęp gry (`Dist`), nie zmiany pikseli.

## Skutek uboczny przyspieszenia: pętla `PeekMessage` i rozmiar logu

Gdy rysowanie przestało zajmować cały czas, SkiFree zaczęło czekać w `PeekMessage` +
`GetCurrentTime` tysiące razy na sekundę. Przy `-d int` każde wywołanie (INT 3Fh) to pełny stan
CPU w `qemu.log`: zmierzone **45,6 MB/s**, czyli ok. 2,7 GB na minutę gry.

- `PeekMessage` po 64 pustych wywołaniach w jednym ticku robi `HLT` (do następnego przerwania).
  Gra rysuje najwyżej raz na tick, więc nic nie traci, a host nie jest zajęty w 100%.
  Aplikacja wykonująca pracę między wywołaniami zwykle nie dochodzi do limitu; najgorszy przypadek
  to ok. 1150 pustych `PeekMessage`/s.
- `make run-qemu` domyślnie nie loguje przerwań; włącza je `QEMU_D=cpu_reset,int,guest_errors`.

Po poprawce log z `-d int` rośnie o ok. 2,3 MB/s, a bez tej flagi w ogóle. Zużycie CPU hosta
przez QEMU podczas gry spadło z 99% do 11%.

## Jak to diagnozować

```bash
python3 tools/run_examples.py gdiperf          # czasy operacji w logu COM1
tools/qemu_prof.py --run "LOADER.EXE SKI.EXE" --mouse --secs 20   # profil kodu gościa
```

`qemu_prof.py` próbkuje CS:IP przez monitor QEMU i mapuje je na symbole z `*.map`. EIP jest
znany tylko na granicy bloku TCG, a funkcje statyczne liczą się do najbliższego symbolu przed
nimi, więc wynik pokazuje kierunek, nie dokładne procenty. Żeby sprofilować jedną operację,
zbuduj `gdiperf` z `-DGP_LOOP=1` (TextOut), `2` (przerysowanie) lub `3` (ruch okna).

Sygnał ostrzegawczy: operacja używająca stosu jest o rząd wielkości wolniejsza, niż wynika
z liczby instrukcji, a pętla bez stosu jest szybka. Wtedy sprawdź układ stron kodu i danych.

## Zabezpieczenie przed regresją

`example_apps/gdiperf` sprawdza poprawność (ROP-y, granice 64 KB, nakładanie, mono, clip,
kursor, tekst, wzory) i progi czasu względne wobec pętli kalibracyjnej, więc niezależne od
szybkości hosta. Zweryfikowane przez cofnięcie poprawek: bez wyrównania XMS pada 10 progów, bez
`blt_row` 4 progi, bez poprawki zagnieżdżonego `cursor_exclude` test „cel nad kursorem”.

## Czego nie sprawdzono

- **Prawdziwe okno QEMU i Bochs.** Liczby pochodzą z `-display none`. Kara za współdzieloną stronę
  wynika z TCG; na prawdziwym CPU i pod Bochsem jej wielkość nie była mierzona.
- **Pamięć konwencjonalna.** Dane `pm_call.asm` leżą przy kodzie, a thunki i KCB blisko siebie.
  Wywołanie API kosztuje dziś ok. 5 µs (przed wyrównaniem XMS ok. 24 µs), więc nie jest to pilne.
