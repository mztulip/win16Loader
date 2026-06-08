# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

Windows 3.1 / Win16 clone written from scratch, running on top of FreeDOS under Bochs/QEMU. Compatible with the Win16 API. Executables use the **NE (New Executable)** format — predecessor of PE, used by all 16-bit Windows apps.

## Build & Run

Each STEP has its own `Makefile`. Work from within the step directory:

```bash
make            # build everything (compile + create boot.img)
make run-qemu   # run under QEMU (fast, no GUI debugger)
make run        # run under Bochs (with GUI debugger)
make clean
```

**QEMU debug log** (crash diagnosis):
```bash
make run-qemu   # writes to qemu.log
tail -50 qemu.log
```
Flags used: `-no-reboot -no-shutdown -d cpu_reset,int,guest_errors -D qemu.log`

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

## Multitasking model

Win16 uses **cooperative** multitasking — tasks yield only via `GetMessage`/`PeekMessage`/`WaitMessage`. No preemptive timer switches between apps (unlike the 13ring3 kernel which uses IRQ0 round-robin).

## GDI / Graphics stack (STEP15+, aktywny w STEP17)

### Kluczowe pliki

| Plik | Rola |
|------|------|
| `STEP17/gdi.c` | GDI.EXE: PatBlt, BitBlt, TextOut, CreateCompatibleDC, SelectObject |
| `STEP17/user.c` | USER.EXE: okna, kolejka komunikatów, BeginPaint/EndPaint, DispatchMessage |
| `STEP17/loader.c` | Ładowanie NE, inicjalizacja PM, IDT, PS/2 |
| `STEP17/pm_call.asm` | Thunk INT 3Fh, IRQ1 (keyboard), IRQ12 (mouse), PIC maski |
| `STEP15/skifree_graphics_analysis.txt` | **Pełna analiza** stosu graficznego SKI.EXE: DC map, trace gondola/skoczek, atlas 86 spriteów, adresy kluczowych funkcji |

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

### Dirty sprite background (WIP)

Skoczek zmienia sprite przy zmianie kierunku. Gdy nowy sprite jest mniejszy niż poprzedni,
piksele starego sprite'a poza nowym bounding boxem zostają na ekranie.

Windows 3.1 rozwiązanie: `WM_ERASEBKGND` (WHITE_BRUSH) przed każdym `WM_PAINT`.
Problem: SKI.EXE nie używa `DispatchMessage` → `WM_ERASEBKGND` w `DispatchMessage` nie odpala.
Następny krok: znaleźć gdzie/jak SKI.EXE dispatchuje WM_PAINT (prawdopodobnie bezpośrednio przez pętlę GetMessage → WndProc call).
