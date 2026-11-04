# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

Windows 3.1 / Win16 clone written from scratch, running on top of FreeDOS under Bochs/QEMU. Compatible with the Win16 API. Executables use the **NE (New Executable)** format — predecessor of PE, used by all 16-bit Windows apps.

## Build & Run

Development happens in the **repo root** (latest code, single `Makefile`). Historical
snapshots STEP1..STEP20 live in `steps/` (frozen, each with its own `Makefile` —
do not develop there; new work is NOT split into new STEP directories). Work from the root:

```bash
make            # build everything (compile + create boot.img)
make run-qemu   # run under QEMU (fast, no GUI debugger)
make run        # run under Bochs (with GUI debugger)
make run-calmira  # Calmira II na drugim dysku (calmira.img = C:)
make OLDGUI=1 boot.img   # stare user.exe/gdi.exe (tylko SkiFree)
make clean
python3 tools/run_examples.py   # testy aplikacji przykladowych (example_apps/) w QEMU bez okna
```

Headless: `tools/qemu_headless.py --fd boot.img [--hd calmira.img] --run "LOADER.EXE X.EXE" KROKI`
(`w` czekaj, `p` zrzut PNG, `k` klawisz, `a x,y` mysz bezwzglednie, `m dx,dy`, `c` klik).
Dodatkowy krok `d<sek>` = czekaj na linie `TEST ... DONE` w logu COM1.
Opcja loadera `-t` = trace wywolan API na COM1 (`>mod off cs:ip`, nazwy z map: `*/*.map`).
Profil kodu goscia: `tools/qemu_prof.py --run "LOADER.EXE SKI.EXE" --mouse --secs 20`
(CS:IP z monitora QEMU -> symbole z map); analiza wydajnosci: `docs/wydajnosc_gdi.md`.

**QEMU debug log** (crash diagnosis):
```bash
make run-qemu   # writes to qemu.log
tail -50 qemu.log
```
Flags used: `-no-reboot -no-shutdown -d cpu_reset,guest_errors -D qemu.log`
Z logiem przerwan: `make run-qemu QEMU_D=cpu_reset,int,guest_errors` (kazde wywolanie API = pelny stan CPU w logu).

**Compile individual files:**
```bash
nasm -f bin win16.asm -o win16.com                          # NASM .COM
WATCOM=/opt/watcom PATH=/opt/watcom/binl64:$PATH INCLUDE=/opt/watcom/h \
  wcl -l=dos -ms -q foo.c -fe=foo.exe                       # DOS .EXE (Open Watcom)
wcl -ml -l=windows -q foo.c                                 # Win16 NE .EXE
```

**Copy file onto boot floppy:**
```bash
mcopy -i boot.img file.com ::file.com
mdir  -i boot.img
```

## Tools & Paths

| Tool | Path / command |
|------|---------------|
| Open Watcom | `/opt/watcom/binl64/wcl`, `/opt/watcom/binl64/wlink` |
| WATCOM env | `WATCOM=/opt/watcom INCLUDE=/opt/watcom/h` (required for stdio.h etc.) |
| FreeDOS floppies | `FD14-FloppyEdition/144m/x86BOOT.img` (1.44MB, base for boot.img) |
| NASM | system `nasm` |
| Bochs BIOS | `/usr/share/bochs/BIOS-bochs-latest` |
| Bochs VGA BIOS | `/usr/share/bochs/VGABIOS-lgpl-latest.bin` (note: `.bin` suffix required) |

## Architecture

### Memory model (Win16 / NE)

Win16 uses **16-bit segmented** protected mode. Each segment is a GDT/LDT descriptor with a base and 64KB max limit. Apps are compiled with Open Watcom memory models:
- `-ms` small (1 code + 1 data segment)
- `-ml` large (multiple code segments, multiple data)
- `-mm` medium

### STEP2: Protected Mode switcher (win16.asm)

Key pattern — **GDT has 5 descriptors** patched at runtime with `cs_phys = CS << 4`:

| Selector | Name | Description |
|----------|------|-------------|
| 0x08 | CODE32 | flat 32-bit code (base=0) — for PM execution |
| 0x10 | DATA32 | flat 32-bit data (base=0) — for VGA at 0xB8000 |
| 0x18 | DATASEG | base=cs_phys, limit=64KB — access to COM file variables |
| 0x20 | CODE16 | base=cs_phys, 16-bit — for return to real mode |
| 0x28 | DATA16 | base=cs_phys, 16-bit — for return to real mode |

**Critical bug pattern** (already fixed): far jump from 16-bit code to PM requires `db 0x66, 0xEA` (operand-size prefix + far jmp opcode), NOT just `db 0xEA`. Without 0x66, the CPU reads only 16-bit offset → wrong selector → #GP → triple fault.

**Return to real mode sequence:**
1. Load CODE16/DATA16 selectors (base=cs_phys)
2. `db 0xEA; dd rm_entry; dw SEL_CODE16` — far jump (32-bit offset, SEL_CODE16 has base=cs_phys so offset is org-relative)
3. Clear PE bit in CR0
4. `jmp far [rm_jmp]` — indirect far jump with patched real-mode CS
5. Restore DS/ES/SS/SP, `sti`, INT 21h exit

**Variable access in PM:** In PM with DS=DATASEG (base=cs_phys), `[var]` uses the org-relative offset, resolving to physical address cs_phys+offset. For VGA, use ES=DATA32 (base=0), then `[es:0xB8000]`.

### Planned modules (see TODO.txt)

```
FreeDOS → WIN16.COM → KERNEL.EXE (NE loader, GlobalAlloc, LoadModule)
                    → USER.EXE   (RegisterClass, CreateWindow, message queue)
                    → GDI.EXE    (HDC, TextOut, Rectangle)
                    → app.exe    (NE format, Win16 API)
```

### NE Executable format (upcoming STEP3)

```
MZ stub → NE header (at MZ.e_lfanew) → Segment Table → Resource Table → Module Table → Entry Table
```
Each segment entry: offset (in file alignment units), size, flags, min_alloc. Must set up LDT selectors per segment and apply fixups (relocations).

## Moduly systemowe (stan biezacy)

Uruchamia `LOADER.EXE` (loader.c + module.c, tryb rzeczywisty -ml): laduje aplikacje NE i
rekurencyjnie jej DLL do XMS, selektory GDT od 0x600 (segment n -> first_sel+(n-1)*8),
wpisy MODTAB (0x530), thunki INT 3Fh dla importow; z katalogu apki preloaduje `*.DLL`,
z katalogu systemowego (`A:\`) `*.FON`. Potem przechodzi do PM i woła LibEntry DLL + wejscie apki.

| Katalog | Modul | Model | Uwagi |
|---------|-------|-------|-------|
| `kernel/` | KERNEL.EXE | -ms -zu | pamiec (XMS, selektory, LocalHeap), DOS3Call z tlumaczeniem wskaznikow, INI, moduly/zasoby, INT 21h z PM |
| `gdi/` | GDI.EXE | -ms -zu | DC, obiekty, ROP2/ROP3, regiony, bitmapy DIB/DDB, fonty .FON (FNT 2/3), kursor myszy (prywatne Gdi* 900-914) |
| `user/` | USER.EXE | -mm -zu | okna, klasy, kolejka, malowanie NC w stylu 3.1, menu (owner-draw), dialogi, kontrolki (uctl.c), schowek, DCE (dce_update), ikony okien zminimalizowanych jak w 3.1 (uicon.c: miejsca na dole rodzica, ArrangeIconicWindows, tytul #32772, WM_PAINTICON), prywatne UserIcon* 950-951 |
| `dlls/` | SHELL.DLL, KEYBOARD.DRV, TOOLHELP.DLL, DDEML.DLL, VER.DLL, MMSYSTEM.DLL, COMMDLG.DLL | -ms -zu | rejestr 3.1 w `<windir>\REG.TXT`, ExtractIcon, ANSI<->OEM; reszta zwraca "brak/anuluj" |
| `example_apps/` | testy | aplikacje | po jednej na funkcjonalnosc, wyniki `TEST ...` na COM1 (README.md) |

Eksporty: `tools/gen_lnk.py MODUL pliki.c` (ordynaly z .spec Wine, prywatne w PRIVATE),
weryfikacja `retf N` vs .spec: `tools/check_args.py plik:MODUL` (KERNEL.354 = falszywy alarm).
Calmira II (`../win31_apps/cal3101p`) dziala: pulpit, pasek zadan, menu Start, dialogi.
Brak: wielozadaniowosci (ShellExecute/WinExec zwracaja blad), COMMDLG (zawsze "Anuluj"), DDE.

### Wyglad: wlasny styl, NIE kopiujemy Windows

Zgodnosc dotyczy API (komunikaty, metryki, zachowanie), nie wygladu. Styl projektu:
pulpit ciemny teal (0x1C5058), ramka 1 px czarna + 3 px jasny teal (0x88C8D0), pasek tytulu
0x0A6E7E z bialym tytulem od lewej (font Fixedsys = VGA), przyciski 0x60A8B4 ze znakami
`=` (menu systemowe), `-`, `+`, `X` (zamknij); pasek menu 0x88C0C8, menu rozwijane 0xC0E4E8
z ramka 0x004048, zaznaczenie 0x0A6E7E. Paleta kolorow systemowych: `g_col` w `user/umisc.c`
i `[colors]` w `windir/WIN.INI`. Nie zmieniac na kolory/ksztalty Windows bez prosby uzytkownika.

### Pulapki wydajnosci (`docs/wydajnosc_gdi.md`) - nie powtarzac

- **Kod i dane nie moga dzielic strony 4KB** (`xms_alloc` wyrownuje do 4KB): w QEMU TCG kazdy zapis
  do takiej strony, w tym kazdy `push`, idzie wolna sciezka. Roznica 12-20x w calym GUI.
- **Masowe operacje GDI ida wierszami** (`row_get`/`row_put`/`bits_row`), nie przez `px_get`/`px_put`.
- **Kursor wykluczaj tylko dla rysowanego prostokata** (`draw_open` + `draw_excl`), nie dla calego clipu.
- Zmiana progow w `example_apps/gdiperf` wymaga uzasadnienia: to regresja wydajnosci.

### Pulapki (Watcom / Win16) - juz naprawione, nie powtarzac

- **MK_FP/MAKELONG**: tylko `((DWORD)(WORD)seg << 16) | (DWORD)(WORD)off`. Wersja `|` bez
  `(DWORD)` przy seg=0 daje DS:off; wersja z `+` generuje `lea` + `adc` z losowym CF.
- **rt386**: moduly -mm wolaja `__U4M/__U4D` far -> `kernel/rt386f.obj` (`-DFARCODE`, retf).
- **`__InitRtns/__FiniRtns`** (stuby dla libentry.obj) musza konczyc sie `ret`, nie `retf`.
- **Prologi eksportow** (jak Wine NE_FixupSegmentPrologs): flaga 2 -> `mov ax,DGROUP`;
  eksport (flaga 1) modulu MULTIPLEDATA -> `nop nop nop`; nieeksportowanych nie ruszac.
- **DLGPROC zwraca BOOL** (tylko AX) - wynik obcinac do WORD. GetSystemDebugState = DWORD.
- **-zu (SS != DS)**: wskazniki do zmiennych lokalnych musza byc far; dlugosci > 0x7FFF
  przekazywane jako `int` robia sie ujemne (bufor EDIT = 0x7FF0).
- **WM_SIZE/WM_MOVE** wysylane przy pierwszym ShowWindow (WF_NEEDSIZE), nie w CreateWindow.
- **CREATESTRUCT**: lpszName @22, lpszClass @26 (CS_NAME_OFF).

## Multitasking model

Win16 uses **cooperative** multitasking — tasks yield only via `GetMessage`/`PeekMessage`/`WaitMessage`. No preemptive timer switches between apps (unlike the 13ring3 kernel which uses IRQ0 round-robin).

## Stare GDI (gdi.c, `OLDGUI=1`) - historia SkiFree

### Kluczowe pliki

| Plik | Rola |
|------|------|
| `gdi.c` | GDI.EXE: PatBlt, BitBlt, TextOut, CreateCompatibleDC, SelectObject |
| `user.c` | USER.EXE: okna, kolejka komunikatów, BeginPaint/EndPaint, DispatchMessage |
| `loader.c` | Ładowanie NE, inicjalizacja PM, IDT, PS/2 |
| `pm_call.asm` | Thunk INT 3Fh, IRQ1 (keyboard), IRQ12 (mouse), PIC maski |
| `SKIFREE/skifree_graphics_analysis.txt` | **Pełna analiza** stosu graficznego SKI.EXE: DC map, trace gondola/skoczek, atlas 86 spriteów, adresy kluczowych funkcji |

### GDI BitBlt — przypadki (gdi.c)

| Przypadek | Warunek | Opis |
|-----------|---------|------|
| 1 | sprite(1..86) → memDC, SRCCOPY | decode_4bpp → bufor BGRA; transparent = alpha=0 (pominięty) |
| 1b | sprite → memDC, NOTSRCCOPY | maska odwrotna do DC3/DC5 |
| 2B | memDC → memDC, SRCCOPY | kopiuj bufor; transparent src = pomiń dst |
| B | SRCAND/SRCPAINT memDC→memDC | compositing (maska+kolor) |
| C | memDC → screen(1), SRCCOPY | alpha=1 → LFB; alpha zerowane po odczycie (auto-clear) |
| 4 | sprite(1..86) → screen(1) | blit_sprite_hbm bezpośrednio na LFB |

### Child window exclusion (status bar x=500..640, y=0..68)

Wszystkie ścieżki do LFB chronią child windows przez **per-pixel `excl_contains()`**:
- `PatBlt(screenDC)` — per-pixel exclusion (zastąpiło `clip_screen_x`)
- `CaseC` — per-pixel exclusion
- `blit_sprite_hbm` (Case 4) — per-pixel exclusion
- `draw_char_gdi` (TextOut screenDC/windowDC) — per-pixel exclusion
- `ExclRect get_child_excl()` / `excl_contains()` — helpery w gdi.c

KCB layout dla child windows (SEL_KCB=0x98): `wnd_ox[8]`@208, `wnd_oy[8]`@224, `wnd_w[8]`@240, `wnd_h[8]`@256.

### TextOut — tryb OPAQUE (Windows 3.1 default)

`draw_char_gdi`: bit fontu=0 → `draw_pixel(0xFF,0xFF,0xFF)` (białe tło), bit=1 → kolor fg.
Eliminuje nawarstwianie napisów (trail effect).

### Znane zachowanie SKI.EXE (z trace)

- **NIGDY** nie wywołuje `PatBlt(hdc=1)` bezpośrednio na screenDC
- **NIGDY** nie wywołuje `DispatchMessage` (game loop bezpośrednio przez SendMessage/WndProc)
- Rendering przez DC6 (intermediate memDC): `PatBlt(DC6,white)` → `Case2B(atlas→DC6)` → `CaseC(DC6→screen)`
- `BeginPaint` może nie być wywoływany — SKI.EXE używa prawdopodobnie `GetDC`
- DC map: DC2=small sprites, DC3=small masks, DC4=large sprites, DC5=large masks, DC6=intermediate

### DC6 lifecycle (sub_09B3 w SKI.EXE)

`sub_09B3` wywoływana przed każdym entity. Zachowanie:
- Jeśli DC6 bitmap wystarczająco duży → **reuse bez zerowania** (skok do 0xa64)
- Jeśli za mały → `CreateCompatibleBitmap` (nowy, zerowany) + `SelectObject(DC6, new_hbm)`
- Rozmiary zaokrąglane do granicy 64: `(dim & 0xC0) + 0x40`

**Bug**: po reuse DC6 ma stale `alpha=1` piksele z poprzedniego entity.
Case2B (atlas→DC6) kopiuje tylko opaque piksele, pomija transparent → stale alpha=1 zostaje
→ Case C zapisuje je na ekran → flag/NPC widoczny przez przezroczyste obszary skoczka.

(Opisany wcześniej fix "zeruj `dp[3]` dla transparent src" nigdy nie trafił do kodu.)

### Ślad narciarza przy zmianie pozy (naprawione, gdi.c Case2B)

Gdy nowa poza narciarza ma ten sam rozmiar co stara, SKI.EXE **nie** robi `PatBlt(DC6)` —
tylko `BitBlt(DC6 ← DC2, SRCCOPY)` + Case C. W prawdziwym GDI białe tło sprite'a zamazuje
starą pozę. U nas transparent src był pomijany → w DC6 zostawało alpha=0 → Case C nie
rysował → stara poza zostawała na ekranie.

**Fix**: w Case2B dla transparent src (`sp[3]==0`): jeśli dst ma alpha=0 (nic tu nie
narysowano w tej kompozycji) → wpisz biel z alpha=1 (jak SRCCOPY); jeśli alpha=1
(PatBlt / inny sprite) → nie zmieniaj (nakładanie obiektów działa jak dotąd).
Atlas (DC2/DC4) budowany jest przez Case 1 (src = prawdziwy sprite), nie Case2B — nietknięty.

## Debug wyjątków (DEBUG_STACK)

`pm_call.asm` przy #DE/#UD/#NP/#SS/#GP/#PF zrzuca do `exc_dbg` rejestry (AX..BP, DS, ES,
SS, FLAGS, limit SS), 256 słów stosu od SP w chwili błędu (przycięte do limitu SS, pomijane
przy #SS) i łańcuch ramek BP (nieparzysty zapisany BP = far frame Win16 → ret CS:IP).
`panic.c` wypisuje całość na serial, na ekran paniki pierwsze 48 słów.
Domyślnie włączone; `make clean && make DEBUG_STACK=0` wyłącza.
Trace GDI (BB/PB/SO na serial): `#define TC_MAX` w gdi.c (0 = wyłączony).
