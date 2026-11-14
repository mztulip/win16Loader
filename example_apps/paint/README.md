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
| **Obsługa formatu BMP** | Często ograniczona do przestarzałych formatów | **Natywny, bezpośredni zapis i odczyt standardowych plików BMP 24-bit** |

---

## 🛠️ Zestaw narzędzi (Pełna funkcjonalność Paintbrush)

1. **Ołówek (`Olow`)**: Precyzyjny rysunek 1-pikselowy (idealny do pixel-artu).
2. **Pędzel artystyczny (`Pedz`)**: Gładkie rysowanie ze zmienną średnicą (1, 2, 4, 8, 12 px).
3. **Aerograf / Spray (`Spry`)**: Losowe, naturalne rozpylanie drobinek w promieniu pędzla.
4. **Gumka (`Gumk`)**: Wymazywanie do wybranego koloru tła (prawy przycisk myszy).
5. **Gumka selektywna / kolorowa (`G.Kl`)**: **Kultowe narzędzie z Paintbrusha!** Zamienia wyłącznie wybrany kolor główny na kolor tła pod kursorem gumki, nie ruszając pozostałych kolorów.
6. **Wiadro z farbą (`Wiad`)**: Szybkie wypełnianie powierzchni (`ExtFloodFill`).
7. **Linia prosta (`Lin.`)**: Z podglądem na żywo podczas przeciągania myszą.
8. **Prostokąt (`Pros`)**: Obsługuje 3 style: obrys, wypełniony z obrysem, pełny bez krawędzi.
9. **Zaokrąglony prostokąt (`Zaok`)**: Nowoczesny kształt o zaokrąglonych rogach (`RoundRect`).
10. **Elipsa / Koło (`Elip`)**: Idealne okręgi i elipsy z podglądem w locie.
11. **Wielokąt (`Wiel`)**: Tworzenie figur wielobocznych – kolejne kliknięcia dodają wierzchołki, a dwuklik lub powrót do startu zamyka figurę.
12. **Pipeta (`Pipe`)**: Pobieranie barwy bezpośrednio z rysunku (LPM = kolor główny, PPM = kolor tła).
13. **Narzędzie tekstu (`Teks`)**: Wprowadzanie napisu w wybranym punkcie płótna.
14. **Zaznaczenie (`Zazn`)**: Prostokątna ramka wyboru obszaru.

---

## ⚡ Filtry i operacje na obrazie

- **Cofnij (`Ctrl+Z`)**: Przywrócenie stanu sprzed ostatniej akcji.
- **Wyczyść płótno**: Wypełnienie całego obszaru roboczego kolorem tła.
- **Negatyw (Odwróć kolory)**: Błyskawiczna inwersja barw `PatBlt(DSTINVERT)`.
- **Odbicie w poziomie (Flip H)**: Lustrzane odbicie lewo-prawo.
- **Odbicie w pionie (Flip V)**: Odbicie góra-dół.

---

## 💾 Format plików

Program posiada wbudowany, niezależny enkoder i dekoder formatu **Windows Bitmap (BMP 24-bit)**:
- Zapisuje standardowy nagłówek `BITMAPFILEHEADER` + `BITMAPINFOHEADER` oraz linie pikseli BGR z wyrównaniem do 4 bajtów.
- Pliki zapisane w PaintCraft można natychmiast otworzyć w dowolnym współczesnym programie graficznym (GIMP, Photoshop, Windows Paint) lub przeglądarce.

---

## 🔧 Kompilacja

Projekt kompiluje się przy pomocy Free Pascal dla Win16 (`fpc16` / Borland Pascal / Open Watcom wrc):

```bash
cd example_apps/paint
make
```
