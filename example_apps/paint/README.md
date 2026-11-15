# PaintCraft - Nowoczesny zamiennik Paintbrush (PBRUSH) w Pascalu

**PaintCraft** to aplikacja graficzna napisana w języku **Pascal** (Object Pascal / Delphi / Free Pascal Win16) jako nowoczesna reinterpretacja i zamiennik klasycznego programu **Paintbrush (PBRUSH.EXE)** ze środowiska Windows 3.1.

Zamiast kopiować archaiczny, toporny interfejs z lat 80., **PaintCraft** zachowuje pełną funkcjonalność pierwowzoru, wprowadzając nowoczesną ergonomię, estetykę oraz zaawansowaną architekturę rysowania GDI.

---

## 🎨 Czym różni się od Windows 3.1 Paintbrush?

| Cecha | Windows 3.1 Paintbrush (PBRUSH) | Nowy PaintCraft (Pascal) |
|---|---|---|
| **Styl interfejsu** | Archaiczny, 2-kolumnowy, szary pasek z lat 80. | Nowoczesny, zintegrowany pas narzędziowy z czytelnym podświetleniem aktywnego trybu |
| **Płótno i buforowanie** | Bezpośrednie rysowanie po ekranie (migotanie i artefakty) | **Podwójne buforowanie GDI (`CreateCompatibleDC` + `BitBlt`)** – idealna płynność bez migotania |
| **Podgląd kształtów (Live Preview)** | Rysowanie prostych kresek z opóźnieniem | **Pełny podgląd w locie (Rubber-banding)** na buforze pomocniczym (`hBmpTemp`) |
| **Paleta kolorów** | Sztywna, jaskrawa paleta 28 barw VGA | **Nowoczesna paleta 28 harmonijnych barw** (pastelowe, nasycone, odcienie bazowe) |
| **Wskaźnik kolorów** | Dwa oddzielne pola | **Nowoczesny Dual-Color Box** z nakładającymi się próbkami (LPM: Kolor główny, PPM: Kolor tła) |
| **Pasek telemetryczny / stanu** | Brak – brak współrzędnych kursora | **Dynamiczny pasek stanu**: kursor `X/Y px`, rozmiar płótna, aktywne narzędzie |
| **Cofanie zmian (Undo)** | Ograniczone, często zawodne | **Natychmiastowe Undo (`Ctrl+Z`)** z dedykowanym buforem migawki pamięci |
| **Obsługa formatu BMP** | Często ograniczona do przestarzałych formatów | **Bezpieczny odczyt 1, 4, 8 bpp (palety), 24, 32 bpp** oraz zapis BMP 24-bit |
| **Interaktywne zaznaczenie** | Statyczna ramka | **Przesuwanie zaznaczenia myszą, klonowanie (`Ctrl`), schowek (`Ctrl+C/X/V`), filtry na zaznaczeniu** |

---

## 🛠️ Zestaw narzędzi (Pełna funkcjonalność Paintbrush)

1. **Ołówek (`Olow`)**: Precyzyjny rysunek 1-pikselowy (stała grubość 1 px, idealna do pixel-artu).
2. **Pędzel artystyczny (`Pedz`)**: Gładkie rysowanie ze zmienną średnicą (1, 2, 4, 8, 12 px).
3. **Aerograf / Spray (`Spry`)**: Losowe, naturalne rozpylanie drobinek w promieniu pędzla.
4. **Gumka (`Gumk`)**: Wymazywanie do wybranego koloru tła (prawy przycisk myszy).
5. **Gumka selektywna / kolorowa (`G.Kl`)**: **Kultowe narzędzie z Paintbrusha!** Zamienia wyłącznie wybrany kolor główny na kolor tła pod kursorem gumki, nie ruszając pozostałych kolorów.
6. **Wiadro z farbą (`Wiad`)**: Szybkie wypełnianie powierzchni (`ExtFloodFill`) wybranym kolorem.
7. **Linia prosta (`Lin.`)**: Z podglądem na żywo podczas przeciągania myszą.
8. **Prostokąt (`Pros`)**: Obsługuje 3 style: obrys, wypełniony z obrysem, pełny bez krawędzi.
9. **Zaokrąglony prostokąt (`Zaok`)**: Nowoczesny kształt o zaokrąglonych rogach (`RoundRect`).
10. **Elipsa / Koło (`Elip`)**: Idealne okręgi i elipsy z podglądem w locie.
11. **Wielokąt (`Wiel`)**: Tworzenie figur wielobocznych – kolejne kliknięcia dodają wierzchołki, a dwuklik lub powrót do startu zamyka figurę.
12. **Pipeta (`Pipe`)**: Pobieranie barwy bezpośrednio z rysunku (LPM = kolor główny, PPM = kolor tła).
13. **Narzędzie tekstu (`Teks`)**: Wprowadzanie napisu w wybranym punkcie płótna.
14. **Zaznaczenie (`Zazn`)**:
    - Przeciągnięcie zaznaczonego fragmentu kursorem myszy przemieszcza wycinek.
    - Przytrzymanie klawisza `Ctrl` podczas przeciągania działa jak stempel / klonowanie.
    - Kliknięcie poza obszarem utrwala fragment na płótnie.
    - Obsługa skrótów edycyjnych: Wytnij (`Ctrl+X`), Kopiuj (`Ctrl+C`), Wklej (`Ctrl+V`), Zaznacz wszystko (`Ctrl+A`), Usuń (`Del`).

---

## ⚡ Filtry i operacje na obrazie

- **Cofnij (`Ctrl+Z`)**: Przywrócenie stanu sprzed ostatniej akcji.
- **Rozmiar płótna (`Ctrl+E`)**: Zmiana wymiarów płótna przez dialog ze wsparciem dla własnych wymiarów oraz gotowych presetów (`320x200`, `640x480`, `800x600`, `Default`). Dostępne z menu `Image`, `Edit`, skrótem `Ctrl+E` oraz przyciskiem `Canvas Size...` na pasku. Zachowuje istniejącą zawartość rysunku.
- **Wyczyść płótno**: Wypełnienie całego obszaru roboczego kolorem tła (lub wyczyszczenie zaznaczenia klawiszem `Del`).
- **Negatyw (Odwróć kolory)**: Błyskawiczna inwersja barw `PatBlt(DSTINVERT)` (całego płótna lub aktywnego zaznaczenia).
- **Odbicie w poziomie (Flip H)**: Lustrzane odbicie lewo-prawo (płótna lub zaznaczenia).
- **Odbicie w pionie (Flip V)**: Odbicie góra-dół (płótna lub zaznaczenia).

---

## 💾 Format plików

Program posiada wbudowany, bezpieczny dla stosu (zero-stack allocation w DS) enkoder i dekoder formatu **Windows Bitmap (BMP)**:
- **Odczyt**: obsługa formatów 1 bpp (monochromatyczny), 4 bpp (16 kolorów), 8 bpp (256 kolorów) z tablicami `RGBQUAD` oraz 24 i 32 bpp True Color. Obsługuje orientację od dołu (bottom-up) oraz od góry (top-down z ujemną wysokością).
- **Zapis**: standardowy, w pełni kompatybilny format 24-bit True Color.
- **Obsługa argumentów linii poleceń**: `PBRUSH.EXE <sciezka.bmp>` automatycznie otwiera plik przy starcie (pełna integracja z menedżerem plików Calmiry).

---

## 🔧 Kompilacja i instalacja w systemie

Projekt dostarcza implementację w C (`paint.c`) kompilowaną przez Open Watcom oraz referencyjną implementację w Pascalu (`PAINT.PAS`). Na dysku twardym Calmiry instalowany jest jako oficjalny **`C:\WINDOWS\PBRUSH.EXE`**.

```bash
cd example_apps/paint
make
```
