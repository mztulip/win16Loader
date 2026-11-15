# HEXVIEW - Win16 Hex Viewer

Natywna 16-bitowa przeglądarka plików binarnych (Hex Viewer) dla Win16Loader i środowiska Calmira II / Windows 3.1.

## Cechy aplikacji:
- Czytelny podgląd zawartości plików w stałym formacie (czcionka Fixedsys):
  - 8-cyfrowy adres szesnastkowy (Offset)
  - 16 bajtów w zapisie heksadecymalnym w dwóch grupach po 8 bajtów
  - Dekodowany tekst ASCII (znaki drukowalne lub kropki)
- Dynamiczne wczytywanie danych w trakcie odrysowywania (`_llseek` + `_lread`), co pozwala na płynne przeglądanie plików o dowolnym rozmiarze bez konieczności alokacji dużych buforów w pamięci RAM
- Paski przewijania (pionowy i poziomy) ze skalowaniem dużych plików
- Interaktywne zaznaczanie i podświetlanie wybranego bajtu (synchronizacja kolumny HEX <-> ASCII)
- Pasek stanu na dole okna wyświetlający:
  - Adres wybranego bajtu (hex i dec)
  - Wartość wybranego bajtu (hex, dec oraz znak ASCII)
  - Łączny rozmiar pliku w bajtach
- Pasek menu i skróty klawiszowe:
  - **Plik**: Otwórz (`Ctrl+O`), Zamknij, Zakończ (`Alt+F4`)
  - **Edycja**: Skocz pod adres... (`Ctrl+G`), Kopiuj wiersz (`Ctrl+C`)
  - **Pomoc**: O programie...
- Dedykowana ikona 16-kolorowa `HEXVIEW.ICO`
- Skojarzenia systemowe w Calmira II i `REG.TXT` / `WIN.INI` dla rozszerzeń `.sys`, `.bin`, `.dat`, `.rom`, `.raw`
