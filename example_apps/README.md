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
| `shell`    | SHELLTST.EXE | SHELL.DLL: rejestr 3.1 (RegCreateKey/SetValue/QueryValue/EnumKey/DeleteKey), ExtractIcon z NE i .ICO, Drag* |
| `sysdll`   | SYSDLL.EXE   | KEYBOARD.DRV (AnsiToOem/OemToAnsi, VkKeyScan, MapVirtualKey), atomy globalne USER, TOOLHELP, DDEML, VER, MMSYSTEM, COMMDLG |
| `fonts`    | FONTTEST.EXE | Fonty .FON (EnumFonts, CreateFont, GetTextFace/Metrics/Extent, pogrubienie, tło tekstu) |
| `iconic`   | ICONTEST.EXE | Okna zminimalizowane: ikony na dole ekranu, WM_PAINTICON/WM_ICONERASEBKGND, ArrangeIconicWindows, WM_QUERYOPEN, powrót do maksymalizacji, ptMinPosition, WS_MINIMIZE, tytuł ikony, dwuklik przywraca, domyślna ikona klasy bez ikony, LoadIcon z brakującą nazwą |
| `irqregs`  | IRQTEST.EXE  | Regresja: przerwania myszy/klawiatury (IRQ12/IRQ1) nie zmieniają rejestrów przerwanego kodu (dawniej AL=20h po EOI → losowe #GP przy ruchu myszą) |
| `gdiperf`  | GDIPERF.EXE  | Poprawność szybkich ścieżek GDI (ROP-y, granice 64 KB, nakładanie, mono, clip, kursor, tekst, wzory) i regresja wydajności: progi czasu względem pętli kalibracyjnej, niezależne od hosta (`docs/wydajnosc_gdi.md`). Kończy się sama |

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
