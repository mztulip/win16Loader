# CHARMAP - Tablica znaków (Win16 / Open Watcom C)

Natywna, 16-bitowa implementacja klasycznego narzędzia **Tablica Znaków** (*Character Map*) dla środowiska Win16Loader / Windows 3.1 / Calmira II.

## Funkcjonalności
* **Siatka znaków:** 16×14 komórek wyświetlających pełny zestaw kodów ANSI/OEM (od 32 do 255).
* **Wybór czcionki:** Rozwijana lista (ComboBox) z dynamiczną enumeracją fontów GDI (`EnumFonts`).
* **Podgląd powiększenia:** Panel z powiększonym rysunkiem aktualnie wybranego znaku, jego kodem dziesiętnym/szesnastkowym oraz skrótem `Alt+0NNN`.
* **Kopiowanie do schowka:**
  * Kliknięcie lub nawigacja strzałkami wybiera znak.
  * Przycisk **Wybierz** lub dwuklik dodaje znak do pola edycyjnego.
  * Przycisk **Kopiuj** umieszcza tekst w systemowym schowku Windows (`CF_TEXT`), skąd można go wklejać do Notatnika, edytora Write itp.
* **Natywna ikona zasobu:** Ikona 32×32 (`CHARMAP.ICO`) osadzona w pliku NE, dzięki czemu Calmira automatycznie wyświetla właściwą ikonę w zasobniku systemowym (Applet Tray).

## Budowanie
```bash
make -C example_apps/charmap
```
Plik wynikowy `CHARMAP.EXE` jest automatycznie kopiowany do `C:\WINDOWS\CHARMAP.EXE` podczas budowania `calmira.img`.
