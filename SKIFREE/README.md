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

---

## Call Tree Analyzer

`win16_calltree.py` performs static analysis of a Win16 NE executable and
visualizes the call graph starting from the entry point.

```bash
python win16_calltree.py           # opens SKI.EXE automatically
python win16_calltree.py FILE.EXE
```

### Setup (virtualenv — recommended)

```bash
python -m venv venv
source venv/bin/activate        # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

Then run:

```bash
python win16_calltree.py
```

### Features

- Recursive disassembly using **capstone** (16-bit x86)
- Follows `CALL near` and `CALL far` (import calls resolved from reloc table)
- Marks indirect calls (`call ax`, `call [bx]`) as unknown — can't be followed statically
- Import calls resolved to function names via built-in Win16 tables
- **Scan All Functions** — prologue scan of all CODE segments
- **WndProc detection** — DefWindowProc heuristic + RegisterClass tracking;
  candidates shown in orange `[WndProc?]`
- Three view modes for any selected function:
  - **[ASM]** — annotated capstone disassembly
  - **[C]** — built-in pseudo-C lifter (fast, no extra deps)
  - **[C miasm]** — miasm IR (precise semantics: `@16[SP]`, real flag usage)
- **File → Export tree as text…** — indented ASCII tree
- **File → Export as DOT…** — graphviz format, render with:
  ```bash
  dot -Tsvg calltree.dot -o calltree.svg
  ```

### Limitations

- Indirect calls (`call reg` / `call [mem]`) cannot be followed statically
- Switch/jump tables are not resolved
- ~70% of the call graph is typically recoverable for a simple game like SkiFree

---

### miasm — Python 3.12+ patch

`[C miasm]` requires **miasm** (`pip install miasm`).
miasm 0.1.5 (latest) uses the deprecated `ast.Str` / `ast.Num` API removed in
Python 3.12. A patch is provided in `patches/miasm_python312.patch`.

**Apply once after installing miasm:**

```bash
# find the installed sembuilder.py
SEMBUILDER=$(python -c "import miasm.core.sembuilder as m; print(m.__file__)")
echo $SEMBUILDER

# apply the patch
patch "$SEMBUILDER" patches/miasm_python312.patch
```

**What the patch fixes** (`miasm/core/sembuilder.py`):

| Old (Python ≤ 3.11) | New (Python ≥ 3.12) |
|----------------------|----------------------|
| `ast.Num(n=size)` | `ast.Constant(value=size)` |
| `isinstance(x, ast.Str)` | `isinstance(x, ast.Constant) and isinstance(x.value, str)` |

---

### Resolving import ordinals from custom DLLs

Built-in ordinal tables cover the standard Win16 system DLLs (GDI, USER, KERNEL,
KEYBOARD, MMSYSTEM, COMMDLG, SHELL, WINSOCK). For other DLLs use
**File → Add DLL directory…** to point the tool at a directory containing the
relevant `.exe`/`.dll` files — their export tables will be parsed automatically.
