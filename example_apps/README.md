# Aplikacje przykładowe / testowe

Każda nowa funkcjonalność loadera (KERNEL/USER/GDI, biblioteki systemowe) ma tu
aplikację Win16, która ją ćwiczy. Aplikacje same sprawdzają wyniki i wypisują je
przez `OutputDebugString` na port szeregowy (`testlog.h`):

```
TEST CTLTEST EM_REPLACESEL: PASS
TEST CTLTEST DONE pass=36 fail=0
```

| Katalog    | Plik         | Co testuje |
|------------|--------------|------------|
| `controls` | CTLTEST.EXE  | Kontrolki USER w dialogu z zasobu: BUTTON (check/radio/3-state/group), EDIT (jedno- i wieloliniowy, EM_*), LISTBOX (sortowanie, LB_*), COMBOBOX (lista i z polem edycji), SCROLLBAR (SB_CTL), STATIC/SS_ICON, *DlgItem* |
| `odmenu`   | ODMENU.EXE   | Menu z zasobu i tworzone w kodzie, MF_OWNERDRAW (WM_MEASUREITEM/WM_DRAWITEM), podmenu, Check/Enable/Modify/Insert/DeleteMenu, TrackPopupMenu |
| `shell`    | SHELLTST.EXE | SHELL.DLL: rejestr (RegCreateKey/SetValue/QueryValue/EnumKey/DeleteKey), ExtractIcon z NE i .ICO, Drag* |
| `sysdll`   | SYSDLL.EXE   | KEYBOARD.DRV (AnsiToOem/OemToAnsi, VkKeyScan, MapVirtualKey), atomy globalne USER, TOOLHELP, DDEML, VER, MMSYSTEM, COMMDLG |
| `fonts`    | FONTTEST.EXE | Fonty .FON (EnumFonts, CreateFont, GetTextFace/Metrics/Extent, pogrubienie, tło tekstu) |
| `iconic`   | ICONTEST.EXE | Okna zminimalizowane: ikony na dole ekranu, WM_PAINTICON/WM_ICONERASEBKGND, ArrangeIconicWindows, WM_QUERYOPEN, powrót do maksymalizacji, ptMinPosition, WS_MINIMIZE, tytuł ikony, dwuklik przywraca, domyślna ikona klasy bez ikony, LoadIcon z brakującą nazwą |
| `irqregs`  | IRQTEST.EXE  | Regresja: przerwania myszy/klawiatury (IRQ12/IRQ1) nie zmieniają rejestrów przerwanego kodu (dawniej AL=20h po EOI → losowe #GP przy ruchu myszą) |
| `gdiperf`  | GDIPERF.EXE  | Poprawność szybkich ścieżek GDI (ROP-y, granice 64 KB, nakładanie, mono, clip, kursor, tekst, wzory) i regresja wydajności: progi czasu względem pętli kalibracyjnej, niezależne od hosta (`docs/wydajnosc_gdi.md`). Kończy się sama |
| `dosirq`   | DOSIRQ.EXE   | Regresja: mysz i klawiatura działają w trakcie wywołań DOS (dawniej bajt czekający w 8042 po powrocie do PM zatrzymywał kursor na zawsze) |
| `dlgkeys`  | DLGKEYS.EXE | Kody WM_GETDLGCODE kontrolek (VCL po nich decyduje, czy Enter idzie do przycisku domyślnego), Enter i Esc w IsDialogMessage, zaznaczanie całego tekstu przy Tab, WM_NEXTDLGCTL, pierwszym fokusie combo i CB_SETCURSEL. |
| `firstsize`| FIRSTSIZ.EXE | Pierwszy WM_SIZE/WM_MOVE okna ukrytego, także przy pokazaniu przez SetWindowPos(SWP_SHOWWINDOW), tak jak VCL Delphi pokazuje kontrolki; dokładnie raz. |
| `menuhover`| MENUHOV.EXE  | Regresja: przejazd myszą bez klikania z otwartego podmenu na pozycje rodzica, także z drugiego poziomu (`WM_MENUSELECT`) |
| `dosprompt`| DOSPROMP.EXE | Regresja: przy jednej stacji dysk B: nie istnieje (`GetDriveType` = 0, ścieżka zwraca błąd), a brak dyskietki nie wiesza systemu (DOS prosił o klawisz przy zamaskowanej klawiaturze) |
| `tasks`    | TASKTEST.EXE | Zadania jak w Win16: WinExec, Yield, własny stos i PSP, koniec dziecka nie kończy rodzica (`taskchild` = program potomny) |
| `libdemo`  | LIBDEMO.EXE  | LibEntry bibliotek ładowanych w trakcie pracy: import statyczny programu z `WinExec` i `LoadLibrary`, jedno wywołanie, sterta DLL, DLL zwracająca błąd. Wyniki pokazuje w oknie |
| `nameimp`  | NAMEIMP.EXE  | Import po nazwie z KERNEL: stałe (`__AHIncr`, `__AHShift`, `__WinFlags`, selektory) oraz `Catch`/`Throw`, także przez zagnieżdżone `SendMessage` |
| `paint`    | PBRUSH.EXE   | PaintCraft Studio (odpowiednik Windows Paintbrush): podwójne buforowanie GDI, interaktywne zaznaczanie, filtry, obsługa BMP (1/4/8/24/32 bpp) |
| `notepad`  | NOTEPAD.EXE  | Klasyczny Notatnik Win16 (wieloliniowy edytor tekstu, menu, dialogi COMMDLG) |
| `charmap`  | CHARMAP.EXE  | Klasyczna Tablica Znaków Win16 (siatka 16x14, podgląd, schowek CF_TEXT, wybór czcionki) |
| `hexview`  | HEXVIEW.EXE  | Przeglądarka plików binarnych (offset, bajty hex, zdekodowany ASCII, skok do adresu, podświetlanie, skojarzenia .sys/.bin/.dat) |

## Budowanie i uruchamianie

```bash
make -C example_apps/controls          # jedna aplikacja (Open Watcom, /opt/watcom)
make example_apps                      # wszystkie
python3 tools/run_examples.py          # wszystkie testy w QEMU bez okna (+ SKI i Win1App jako testy dymne)
python3 tools/run_examples.py shell    # wybrane
```

`run_examples.py` kopiuje aplikacje na kopię `boot.img`, uruchamia każdą przez
`LOADER.EXE`, robi zrzut ekranu (`qemu_out/examples/<nazwa>.png`) i podsumowuje
linie `TEST`. Kod wyjścia 1 = jakiś test nie przeszedł, brak `DONE` albo wyjątek.

## Nowa aplikacja

1. Katalog `example_apps/<nazwa>/` z `<APP>.c`, `<APP>.rc` i `Makefile`:
   ```make
   APP  = NAZWA
   OBJS = nazwa.obj
   LIBS = shell            # opcjonalnie: biblioteki importów z /opt/watcom/lib286/win
   include ../common.mk
   ```
2. `#include "testlog.h"`, ustaw `t_app = "NAZWA";`, sprawdzaj przez
   `CHECK` / `CHECK_INT` / `CHECK_STR`, na końcu `t_done()`.
3. Dopisz wpis do `APPS` w `tools/run_examples.py` (katalog, plik, kroki: `w` czekaj,
   `d` czekaj na `DONE`, `p` zrzut, `k` klawisz, `a` mysz bezwzględnie, `c` klik).
