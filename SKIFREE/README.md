# SKIFREE

[SkiFree](https://ski.ihoc.net/) is a classic Windows 3.1 skiing game written by
**Chris Pirih** and originally shipped with Windows Entertainment Pack 3 (1991).
This directory contains the original `SKI.EXE` (Win16 NE format) used as the
primary test target for the win16Loader project.

- **Author's page:** https://ski.ihoc.net/

---

## NE Explorer

`ne_explorer_gui.py` is a GUI tool for inspecting NE (New Executable) binaries —
the 16-bit executable format used by Windows 3.x apps (`.exe`, `.dll`, `.drv`).

It auto-opens `SKI.EXE` on startup if it is present in the same directory.

### Features

- **Overview** — full NE header dump with all table offsets
- **Segments** — per-segment hex dump and relocation records on click
- **Resources** — grouped tree with image preview (bitmaps, icons, cursors),
  decoded string tables, version info, accelerator tables, menu and dialog templates
- **Exports** — resident and non-resident names tables
- **Imports** — hierarchical DLL → symbol tree; ordinals resolved to names
  using built-in Win16 API tables (GDI, USER, KERNEL, MMSYSTEM, …)
- **Entry Table** — ordinals with resolved export names

### Requirements

```
Python >= 3.9
Pillow          # image rendering (bitmaps, icons)
tkinter         # usually bundled with Python; on Debian/Ubuntu: python3-tk
```

Install dependencies:

```bash
pip install Pillow
# or
pip install -r requirements.txt
```

### Running

```bash
python ne_explorer_gui.py           # opens SKI.EXE automatically
python ne_explorer_gui.py FILE.EXE  # open a specific NE executable
```

**Keyboard shortcuts:**

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open file |
| `q` | Quit (also works from the terminal) |
| `Ctrl+C` | Quit (terminal or window) |

### Resolving import ordinals from custom DLLs

Built-in ordinal tables cover the standard Win16 system DLLs (GDI, USER, KERNEL,
KEYBOARD, MMSYSTEM, COMMDLG, SHELL, WINSOCK). For other DLLs use
**File → Add DLL directory…** to point the tool at a directory containing the
relevant `.exe`/`.dll` files — their export tables will be parsed automatically.
