# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

Przykładowa aplikacja Win16 zaadaptowana do kompilacji **Open Watcom** (oryginalnie: Microsoft C 4 + Windows 1 SDK). Używana jako materiał referencyjny dla projektu Win16 OS clone w `../i486_win16/`. Wynikowy NE executable działa pod Windows 3.1 (nie Windows 1.x — patrz sekcja "Co zmieniono").

## Build

```bash
make          # kompiluje Win1App.exe
make clean
```

Wymaga Open Watcom w `/opt/watcom` (binaria w `binl64`).

Toolchain:
- `wcc` — kompilator 16-bit, `-ms -zW -bt=windows` (small model, Windows prolog/epilog)
- `wlink @Win1App.lnk` — linker, `system windows` + `library clibs` + `windows.lib` (auto)
- `wrc` — resource compiler

## Pliki

| Plik | Rola |
|------|------|
| `WinMain.c` | Entry point `WinMain`, rejestracja klasy, tworzenie okna, message loop |
| `MainWnd.c` | `MAINWNDPROC` — WM_COMMAND, WM_SYSCOMMAND, WM_DESTROY |
| `AboutDlg.c` | `ABOUTDIALOGPROC`, `ShowAboutDialog` przez `DialogBox` |
| `Resource.rc` | Menu, dialog About, akceleratory (ikona wykomentowana — format Win1.x niekompatybilny z wrc) |
| `Win1App.lnk` | Linker script: `system windows`, exporty, biblioteki |
| `Makefile` | Build dla Open Watcom |
| `Win1App` | Oryginalny Makefile dla Microsoft C 4 (zachowany jako referencja) |
| `Win1App.def` | Oryginalny moduł definition dla Microsoft C 4 (zachowany jako referencja) |

## Ważne: konwencja nazw symboli Watcom

Watcom z `FAR PASCAL` generuje nazwy **UPPERCASE bez podkreślnika** (Pascal convention):
- `MainWndProc` → symbol `MAINWNDPROC` w `.obj`
- `AboutDialogProc` → symbol `ABOUTDIALOGPROC` w `.obj`

Dlatego w `Win1App.lnk` exporty są jako `MAINWNDPROC`/`ABOUTDIALOGPROC`, nie `MainWndProc_`.

Funkcje `__cdecl` (domyślne w C) mają podkreślnik na końcu: `ShowAboutDialog_`.

## Co zmieniono względem oryginału (port z MS C 4 → Watcom)

| Zmiana | Powód |
|--------|-------|
| K&R → ANSI deklaracje funkcji | Watcom wymaga ANSI C |
| `WS_TILEDWINDOW` → `WS_OVERLAPPEDWINDOW` | `WS_TILEDWINDOW` to alias Win 1.x, brak w nagłówkach Win3.x Watcoma |
| `ChangeMenu` → `AppendMenu` (2×) | `ChangeMenu` to API Windows 1.x/2.x, usunięte w Win3.x |
| Usunięto `MakeProcInstance`/`FreeProcInstance` | Watcom generuje własny prolog/epilog naprawiający DS — thunki zbędne |
| `WORD wParam` → `unsigned wParam` w WndProc/DlgProc | Watcom definiuje `WNDPROC` z `unsigned` (UINT), nie `WORD` (unsigned short) |
| `Resource.rc`: `"resource.h"` → `"Resource.h"` | Linux: case-sensitive filesystem |
| Ikona wykomentowana w `Resource.rc` | `App.ico` w formacie Win 1.x — `wrc` nie obsługuje |
| Nowy `Makefile` i `Win1App.lnk` | Build system dla Open Watcom |

## Relevance to Win16 Clone (`../i486_win16/`)

- Wzorzec exportów `WndProc`/`DialogProc` — NE loader musi je obsłużyć
- `DialogBox` + `EndDialog` — nie zaimplementowane jeszcze w USER.EXE (STEP13)
- `LoadAccelerators`/`TranslateAccelerator` — nie zaimplementowane w USER.EXE
- `GetSystemMenu`/`AppendMenu` — nie zaimplementowane w USER.EXE
- Startup Watcoma (`clibs`) wywołuje `InitTask` (KERNEL.91) i `WaitEvent` (KERNEL.30) — oba są w STEP13
