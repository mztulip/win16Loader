# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

This is a reference Win16 NE-format example application from Transmission Zero, used as a test target for the win16Loader STEP18 NE loader. It demonstrates the canonical structure of a Win16 Windows 3.x GUI application.

## Building

### With Visual C++ 1.5x (MSVC, Windows)
```
nmake /f Win16App.mak          # release
nmake /f Win16App.mak DEBUG=1  # debug
```
Requires MSVC 1.5x environment (`MSVCVARS.BAT`) or Windows Server 2003 DDK (add `bin16` to `%PATH%`, `inc16` to `%INCLUDE%`, `lib16` to `%LIB%`).

### With Open Watcom (Linux/cross)
```bash
WATCOM=/opt/watcom PATH=/opt/watcom/binl64:$PATH INCLUDE=/opt/watcom/h \
  wcl -ml -l=windows -zWs -q WinMain.c MainWnd.c AboutDlg.c -fe=Win16App.exe
```
The `-zWs` flag enables smart callbacks (loads DS from SS — required for all window/dialog procedures). Optimizations must be disabled (`-od`); with optimizations the app crashes on startup.

## Architecture

The app follows the standard Win16 message-loop pattern:

| File | Role |
|------|------|
| `WinMain.c` | Entry point: registers class, creates window, runs `GetMessage` loop |
| `MainWnd.c` | `MainWndProc` — handles `WM_COMMAND`, `WM_GETMINMAXINFO`, `WM_DESTROY`; `RegisterMainWindowClass`/`CreateMainWindow` |
| `AboutDlg.c` | Modal "About" dialog via `DialogBox` + `AboutDialogProc` |
| `Globals.h` | Single global: `extern HINSTANCE g_hInstance` |
| `Resource.rc` | Menu (`IDR_MAINMENU`), dialog (`IDD_ABOUTDIALOG`), icon, accelerator, `VERSIONINFO` |
| `Win16App.def` | Module definition: `EXETYPE WINDOWS 3.00`, `CODE MOVEABLE PRELOAD DISCARDABLE`, `DATA MOVEABLE PRELOAD MULTIPLE`, stack=4096, heap=1024 |

## Smart Callbacks

All window/dialog procedures use "smart callbacks": DS is loaded from SS on entry, so `MakeProcInstance` thunks are not needed and callbacks don't have to appear in the `.def` exports. In MSVC this is `/GA /GEs /GEm`; in Open Watcom it is `-zWs`.

## Key Win16 API patterns used

- `RegisterClass` / `CreateWindowEx` / `ShowWindow` / `UpdateWindow`
- `GetMessage` / `TranslateAccelerator` / `TranslateMessage` / `DispatchMessage`
- `DialogBox` (modal) — no `MakeProcInstance` needed due to smart callbacks
- `GetSystemMenu` / `InsertMenu` — adds "About" to system menu
- `WM_GETMINMAXINFO` with `MINMAXINFO FAR*` — note required `FAR` pointer (16-bit segmented model)

## NE binary structure (for loader development)

The compiled `Win16App.exe` is an NE (New Executable) with:
- Small/large memory model (one code segment + one data segment for small)
- `MOVEABLE PRELOAD DISCARDABLE` code segment
- `MOVEABLE PRELOAD MULTIPLE` data segment (one copy per task)
- Imports from `USER`, `GDI`, `KERNEL` (resolved via ordinals at load time)
- Resources embedded in the NE resource table (icon, menu, dialog, accelerator)

This binary is the primary test case for the STEP18 NE loader in the parent `win16Loader/` project.
