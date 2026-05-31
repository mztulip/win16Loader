#!/usr/bin/env python3
"""
NE (New Executable) format explorer GUI
For Win16 / Windows 3.1 executables (.exe, .dll)
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import struct
import os
import io
import signal
import sys
import threading
import tty
import termios
from collections import Counter

try:
    from PIL import Image, ImageTk
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

SKIFREE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "SKI.EXE")

# ─── Resource type names ──────────────────────────────────────────────────────
RT_NAMES = {
    0x8001: "RT_CURSOR",    0x8002: "RT_BITMAP",  0x8003: "RT_ICON",
    0x8004: "RT_MENU",      0x8005: "RT_DIALOG",  0x8006: "RT_STRING",
    0x8007: "RT_FONTDIR",   0x8008: "RT_FONT",    0x8009: "RT_ACCELERATOR",
    0x800A: "RT_RCDATA",    0x800B: "RT_MSGTABLE",0x800C: "RT_GROUP_CURSOR",
    0x800E: "RT_GROUP_ICON",0x8010: "RT_VERSION", 0x8011: "RT_DLGINCLUDE",
}

NE_FLAGS = [
    (0x0001, "SINGLEDATA"),  (0x0002, "MULTIPLEDATA"), (0x0008, "FULLSCREEN"),
    (0x0010, "PMAPI"),       (0x0020, "PMCOMPATIBLE"), (0x0040, "LOADERTHREAD"),
    (0x0080, "LOADERR"),     (0x0100, "NODISCARD"),    (0x0200, "NONCONFORMING"),
    (0x0400, "LIBRARY"),     (0x0800, "SELFLOAD"),     (0x1000, "LINKERROR"),
    (0x4000, "PROTECTED"),
]

SEG_FLAGS = [
    (0x0001, "DATA"), (0x0002, "ALLOC"), (0x0004, "LOADED"),
    (0x0008, "ITERATED"), (0x0010, "MOVEABLE"), (0x0020, "SHARED"),
    (0x0040, "PRELOAD"), (0x0080, "ERONLY"), (0x0100, "RELOC"),
    (0x0200, "CONFORM"), (0x0400, "DISCARDABLE"), (0x1000, "DISCARD_PRI"),
]

ACCEL_FLAGS = [
    (0x01, "VIRTKEY"), (0x04, "NOINVERT"),
    (0x08, "SHIFT"),   (0x10, "CONTROL"), (0x20, "ALT"),
]

RELOC_SRC  = {0: "LOBYTE", 2: "SEGMENT", 3: "FAR_ADDR", 5: "OFFSET"}
RELOC_TYPE = {0: "INTERNALREF", 1: "IMPORTORDINAL", 2: "IMPORTNAME", 3: "OSFIXUP"}

# ─── Dark theme palette ───────────────────────────────────────────────────────
C_BG        = "#1e1e1e"
C_BG2       = "#252526"
C_BG3       = "#2d2d30"
C_FG        = "#d4d4d4"
C_FG_DIM    = "#858585"
C_SEL_BG    = "#0e639c"
C_SEL_FG    = "#ffffff"
C_HEADER_BG = "#3c3c3c"
C_BORDER    = "#474747"
C_GREEN_BG  = "#1a2e1a"
C_ACCENT    = "#4ec9b0"


def flags_str(val, flag_list):
    return " | ".join(name for bit, name in flag_list if val & bit) or "0"


# ─── NE file parser ──────────────────────────────────────────────────────────

class NEFile:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        self.path = path
        self._parse()

    def u8(self, off):  return struct.unpack_from("<B", self.data, off)[0]
    def u16(self, off): return struct.unpack_from("<H", self.data, off)[0]
    def u32(self, off): return struct.unpack_from("<I", self.data, off)[0]

    def _parse(self):
        if self.data[:2] != b"MZ":
            raise ValueError("Not an MZ executable")
        self.ne_off = self.u16(0x3C) | (self.u16(0x3E) << 16)
        ne_off2 = self.u16(0x3C)
        if self.data[ne_off2:ne_off2+2] == b"NE":
            self.ne_off = ne_off2
        elif self.data[self.ne_off:self.ne_off+2] != b"NE":
            raise ValueError("NE signature not found")

        n = self.ne_off
        if self.data[n:n+2] != b"NE":
            raise ValueError("NE signature not found at 0x%X" % n)

        self.ne = {
            "linker_ver":       self.u8(n+0x02),
            "linker_rev":       self.u8(n+0x03),
            "entry_table_off":  self.u16(n+0x04),
            "entry_table_size": self.u16(n+0x06),
            "flags":            self.u16(n+0x0C),
            "autodata":         self.u16(n+0x0E),
            "heap_size":        self.u16(n+0x10),
            "stack_size":       self.u16(n+0x12),
            "ne_ip":            self.u16(n+0x14),
            "ne_cs":            self.u16(n+0x16),
            "ne_sp":            self.u16(n+0x18),
            "ne_ss":            self.u16(n+0x1A),
            "cseg":             self.u16(n+0x1C),
            "cmod":             self.u16(n+0x1E),
            "nrnames_size":     self.u16(n+0x20),
            "seg_table_off":    self.u16(n+0x22),
            "res_table_off":    self.u16(n+0x24),
            "rnames_off":       self.u16(n+0x26),
            "modref_off":       self.u16(n+0x28),
            "impnames_off":     self.u16(n+0x2A),
            "nrnames_off":      self.u32(n+0x2C),
            "moveable_entries": self.u16(n+0x30),
            "align_shift":      self.u16(n+0x32),
            "res_count":        self.u16(n+0x34),
            "target_os":        self.u8(n+0x36),
            "os2_flags":        self.u8(n+0x37),
            "win_ver_minor":    self.u8(n+0x3E),
            "win_ver_major":    self.u8(n+0x3F),
        }

        self.segments    = self._parse_segments()
        self.resources   = self._parse_resources()
        self.exports     = self._parse_exports()
        self.nonresident = self._parse_nonresident_names()
        self.imports     = self._parse_imports()
        self.entries     = self._parse_entry_table()
        for seg in self.segments:
            seg["relocs"] = self._parse_segment_relocs(seg)

    def _parse_segments(self):
        n = self.ne_off
        off = n + self.ne["seg_table_off"]
        segs = []
        for i in range(self.ne["cseg"]):
            s_off    = self.u16(off + i*8 + 0)
            cbseg    = self.u16(off + i*8 + 2)
            flags    = self.u16(off + i*8 + 4)
            minalloc = self.u16(off + i*8 + 6)
            align    = self.ne["align_shift"]
            file_pos = s_off << align if s_off else 0
            segs.append({
                "idx": i+1, "file_off_units": s_off, "file_off": file_pos,
                "cbseg": cbseg, "flags": flags, "minalloc": minalloc,
            })
        return segs

    def _res_name_at(self, res_table_base, offset):
        abs_off = res_table_base + offset
        if abs_off >= len(self.data):
            return f"<off={offset:#x}>"
        length = self.u8(abs_off)
        return self.data[abs_off+1:abs_off+1+length].decode("ascii", errors="replace")

    def _parse_resources(self):
        n = self.ne_off
        if self.ne["res_table_off"] == self.ne["rnames_off"]:
            return []
        res_table_base = n + self.ne["res_table_off"]
        off = res_table_base
        align_shift = self.u16(off); off += 2
        resources = []
        while True:
            type_id = self.u16(off); off += 2
            if type_id == 0:
                break
            count = self.u16(off); off += 2
            off += 4  # reserved
            if type_id & 0x8000:
                type_name = RT_NAMES.get(type_id, f"0x{type_id:04X}")
            else:
                type_name = self._res_name_at(res_table_base, type_id)
            for _ in range(count):
                r_off   = self.u16(off) << align_shift; off += 2
                r_len   = self.u16(off) << align_shift; off += 2
                r_flags = self.u16(off); off += 2
                r_id    = self.u16(off); off += 2
                off += 4  # reserved
                if r_id & 0x8000:
                    name = f"#{r_id & 0x7FFF}"
                else:
                    name = self._res_name_at(res_table_base, r_id)
                resources.append({
                    "type_id": type_id, "type_name": type_name,
                    "id": r_id, "name": name,
                    "offset": r_off, "length": r_len, "flags": r_flags,
                })
        return resources

    def _parse_exports(self):
        n = self.ne_off
        off = n + self.ne["rnames_off"]
        names = []
        while True:
            length = self.u8(off); off += 1
            if length == 0:
                break
            name = self.data[off:off+length].decode("ascii", errors="replace"); off += length
            ordinal = self.u16(off); off += 2
            names.append((ordinal, name))
        return names

    def _parse_nonresident_names(self):
        off  = self.ne["nrnames_off"]
        size = self.ne["nrnames_size"]
        if off == 0 or size == 0:
            return []
        end = off + size
        names = []
        while off < end and off < len(self.data):
            length = self.u8(off); off += 1
            if length == 0:
                break
            name = self.data[off:off+length].decode("ascii", errors="replace"); off += length
            ordinal = self.u16(off); off += 2
            names.append((ordinal, name))
        return names

    def _parse_imports(self):
        n = self.ne_off
        mod_off = n + self.ne["modref_off"]
        imp_off = n + self.ne["impnames_off"]
        modules = []
        for i in range(self.ne["cmod"]):
            name_off = self.u16(mod_off + i*2)
            abs_off  = imp_off + name_off
            length   = self.u8(abs_off)
            name = self.data[abs_off+1:abs_off+1+length].decode("ascii", errors="replace")
            modules.append(name)
        return modules

    def _parse_entry_table(self):
        n = self.ne_off
        off = n + self.ne["entry_table_off"]
        end = off + self.ne["entry_table_size"]
        entries = []
        ordinal = 1
        while off < end:
            count = self.u8(off); off += 1
            if count == 0:
                break
            seg_type = self.u8(off); off += 1
            if seg_type == 0x00:
                ordinal += count
            elif seg_type == 0xFF:  # moveable
                for _ in range(count):
                    flags  = self.u8(off);  off += 1
                    off   += 2  # INT 3F
                    seg    = self.u8(off);  off += 1
                    ofs    = self.u16(off); off += 2
                    entries.append({"ordinal": ordinal, "seg": seg, "offset": ofs,
                                    "flags": flags, "type": "mov"})
                    ordinal += 1
            else:  # fixed
                seg = seg_type
                for _ in range(count):
                    flags  = self.u8(off);  off += 1
                    ofs    = self.u16(off); off += 2
                    entries.append({"ordinal": ordinal, "seg": seg, "offset": ofs,
                                    "flags": flags, "type": "fix"})
                    ordinal += 1
        return entries

    def _parse_segment_relocs(self, seg):
        if not (seg["flags"] & 0x0100):
            return []
        if not seg["file_off"] or not seg["cbseg"]:
            return []
        reloc_off = seg["file_off"] + seg["cbseg"]
        if reloc_off + 2 > len(self.data):
            return []
        count = self.u16(reloc_off); reloc_off += 2
        relocs = []
        for _ in range(count):
            if reloc_off + 8 > len(self.data):
                break
            src_type   = self.u8(reloc_off)
            raw_flags  = self.u8(reloc_off + 1)
            seg_offset = self.u16(reloc_off + 2)
            target1    = self.u16(reloc_off + 4)
            target2    = self.u16(reloc_off + 6)
            reloc_off += 8
            reloc_type = raw_flags & 0x03
            additive   = bool(raw_flags & 0x04)
            if reloc_type == 0:
                target_str = f"seg {target1}, off 0x{target2:04X}"
            elif reloc_type == 1:
                mod = self.imports[target1-1] if 0 < target1 <= len(self.imports) else f"mod#{target1}"
                target_str = f"{mod}.#{target2}"
            elif reloc_type == 2:
                imp_base = self.ne_off + self.ne["impnames_off"] + target2
                if imp_base < len(self.data):
                    l  = self.u8(imp_base)
                    nm = self.data[imp_base+1:imp_base+1+l].decode("ascii", errors="replace")
                else:
                    nm = f"?off={target2}"
                mod = self.imports[target1-1] if 0 < target1 <= len(self.imports) else f"mod#{target1}"
                target_str = f"{mod}.{nm}"
            else:
                target_str = f"osfixup #{target1}"
            relocs.append({
                "src_type":   RELOC_SRC.get(src_type, f"0x{src_type:02X}"),
                "reloc_type": RELOC_TYPE.get(reloc_type, f"0x{reloc_type:02X}"),
                "additive":   additive,
                "offset":     seg_offset,
                "target":     target_str,
            })
        return relocs

    def build_import_table(self):
        """Scan all segment relocs → dict { module_name: [func, ...] }."""
        table = {mod: [] for mod in self.imports}
        seen  = {mod: set() for mod in self.imports}
        for seg in self.segments:
            for r in seg["relocs"]:
                if r["reloc_type"] not in ("IMPORTORDINAL", "IMPORTNAME"):
                    continue
                dot = r["target"].find(".")
                if dot < 0:
                    continue
                mod  = r["target"][:dot]
                func = r["target"][dot+1:]
                if mod not in table:
                    table[mod] = []
                    seen[mod]  = set()
                if func not in seen[mod]:
                    seen[mod].add(func)
                    table[mod].append(func)
        return table

    def hex_dump(self, file_off, size, max_bytes=512):
        if not file_off:
            return "<no data in file>"
        data = self.data[file_off:file_off + min(size, max_bytes)]
        lines = []
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            g1    = " ".join(f"{b:02X}" for b in chunk[:8])
            g2    = " ".join(f"{b:02X}" for b in chunk[8:])
            asc   = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"  {file_off+i:08X}  {g1:<23}  {g2:<23}  |{asc:<16}|")
        if size > max_bytes:
            lines.append(f"  ... ({size - max_bytes} more bytes not shown)")
        return "\n".join(lines) if lines else "<empty>"


# ─── GUI ──────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NE Explorer")
        self.geometry("1200x800")
        self.configure(bg=C_BG)
        self.ne = None
        self._apply_dark_theme()
        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self.quit)
        self.bind("<Control-c>", lambda e: self.quit())
        self.bind("<q>",         lambda e: self.quit())
        signal.signal(signal.SIGINT, lambda *_: self.quit())
        self._poll_signals()
        t = threading.Thread(target=self._watch_stdin, daemon=True)
        t.start()

    def _poll_signals(self):
        self.after(200, self._poll_signals)

    def _watch_stdin(self):
        if not sys.stdin.isatty():
            return
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            while True:
                ch = sys.stdin.read(1)
                if ch in ('q', 'Q', '\x03', '\x04'):
                    self.after(0, self.quit)
                    break
        except Exception:
            pass
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)

    # ── Dark theme ────────────────────────────────────────────────────────────

    def _apply_dark_theme(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".",
            background=C_BG, foreground=C_FG,
            fieldbackground=C_BG2, troughcolor=C_BG3,
            bordercolor=C_BORDER, darkcolor=C_BG, lightcolor=C_BG3,
            relief="flat",
        )
        style.configure("TFrame",       background=C_BG)
        style.configure("TLabel",       background=C_BG, foreground=C_FG)
        style.configure("TScrollbar",   background=C_BG3, troughcolor=C_BG2,
                                        arrowcolor=C_FG)
        style.configure("TNotebook",    background=C_BG, tabmargins=[2, 4, 0, 0])
        style.configure("TNotebook.Tab",
            background=C_BG3, foreground=C_FG_DIM, padding=[10, 4], focuscolor=C_BG)
        style.map("TNotebook.Tab",
            background=[("selected", C_BG2), ("active", C_BG2)],
            foreground=[("selected", C_FG),  ("active", C_FG)],
        )
        style.configure("Treeview",
            background=C_BG2, foreground=C_FG,
            fieldbackground=C_BG2, rowheight=20, bordercolor=C_BORDER)
        style.configure("Treeview.Heading",
            background=C_HEADER_BG, foreground=C_FG, relief="flat", borderwidth=0)
        style.map("Treeview",
            background=[("selected", C_SEL_BG)],
            foreground=[("selected", C_SEL_FG)],
        )
        style.map("Treeview.Heading", background=[("active", C_BG3)])
        style.configure("TPanedwindow", background=C_BORDER)

    def _mk_text(self, parent):
        txt = tk.Text(parent,
            font=("Courier", 10), wrap="none",
            bg=C_BG2, fg=C_FG, insertbackground=C_FG,
            selectbackground=C_SEL_BG, selectforeground=C_SEL_FG,
            relief="flat", borderwidth=0,
        )
        txt.tag_configure("header", foreground=C_ACCENT)
        txt.tag_configure("dim",    foreground=C_FG_DIM)
        txt.tag_configure("str",    foreground="#ce9178")
        txt.tag_configure("value",  foreground="#ce9178")
        return txt

    def _mk_text_scroll(self, parent):
        """Pack a scrolled text widget into parent, return the Text."""
        txt = self._mk_text(parent)
        sb_y = ttk.Scrollbar(parent, orient="vertical",   command=txt.yview)
        sb_x = ttk.Scrollbar(parent, orient="horizontal", command=txt.xview)
        txt.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        txt.pack(fill="both", expand=True)
        return txt

    def _mk_tree(self, parent, columns):
        tv = ttk.Treeview(parent, columns=columns, show="headings")
        for col in columns:
            tv.heading(col, text=col)
            tv.column(col, width=120, anchor="w")
        sb_y = ttk.Scrollbar(parent, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(parent, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        return tv

    # ── UI layout ─────────────────────────────────────────────────────────────

    def _build_ui(self):
        menubar = tk.Menu(self, bg=C_BG3, fg=C_FG, activebackground=C_SEL_BG,
                          activeforeground=C_SEL_FG, tearoff=0)
        filemenu = tk.Menu(menubar, bg=C_BG3, fg=C_FG, activebackground=C_SEL_BG,
                           activeforeground=C_SEL_FG, tearoff=0)
        filemenu.add_command(label="Open…", accelerator="Ctrl+O", command=self.open_file)
        filemenu.add_separator()
        filemenu.add_command(label="Quit", command=self.quit)
        menubar.add_cascade(label="File", menu=filemenu)
        self.config(menu=menubar)
        self.bind("<Control-o>", lambda e: self.open_file())

        self.title_var = tk.StringVar(value="No file loaded")
        tk.Label(self, textvariable=self.title_var, anchor="w",
                 font=("TkFixedFont", 10, "bold"),
                 bg=C_BG3, fg=C_ACCENT, pady=4, padx=6,
        ).pack(fill="x")

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=4, pady=4)

        self.tab_overview  = self._make_text_tab("Overview")
        self.tab_segments  = self._make_segments_tab()
        self._make_resources_tab()
        self._make_exports_tab()
        self.tab_imports   = self._make_imports_tab()
        self.tab_entries   = self._make_simple_tab("Entry Table",
            ["Ordinal", "Type", "Seg", "Offset", "Flags", "Name"])

    def _make_text_tab(self, label):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text=label)
        return self._mk_text_scroll(frame)

    def _make_simple_tab(self, label, columns):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text=label)
        return self._mk_tree(frame, columns)

    # ── Segments tab ──────────────────────────────────────────────────────────

    def _make_segments_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Segments")

        pane = ttk.PanedWindow(frame, orient="vertical")
        pane.pack(fill="both", expand=True)

        top = ttk.Frame(pane)
        pane.add(top, weight=2)
        cols = ["#", "File Offset", "Size", "MinAlloc", "Flags"]
        tv = self._mk_tree(top, cols)

        bot = ttk.Frame(pane)
        pane.add(bot, weight=3)
        detail_nb = ttk.Notebook(bot)
        detail_nb.pack(fill="both", expand=True)

        hex_frame   = ttk.Frame(detail_nb)
        reloc_frame = ttk.Frame(detail_nb)
        detail_nb.add(hex_frame,   text="Hex Dump")
        detail_nb.add(reloc_frame, text="Relocations")

        self._seg_hex      = self._mk_text_scroll(hex_frame)
        self._seg_reloc_tv = self._mk_tree(reloc_frame,
            ["Offset", "Src Type", "Reloc Type", "Additive", "Target"])
        self._seg_reloc_tv.column("Target", width=350)

        tv.bind("<<TreeviewSelect>>", self._on_segment_select)
        return tv

    def _on_segment_select(self, event):
        if not self.ne:
            return
        sel = self.tab_segments.selection()
        if not sel:
            return
        idx = int(self.tab_segments.item(sel[0])["values"][0]) - 1
        seg = self.ne.segments[idx]

        txt = self._seg_hex
        txt.config(state="normal")
        txt.delete("1.0", "end")
        txt.insert("end",
            f"  Segment #{seg['idx']}  file offset 0x{seg['file_off']:X}  size 0x{seg['cbseg']:X}\n\n",
            "header")
        txt.insert("end", self.ne.hex_dump(seg["file_off"], seg["cbseg"]))
        txt.config(state="disabled")

        rv = self._seg_reloc_tv
        rv.delete(*rv.get_children())
        for r in seg["relocs"]:
            rv.insert("", "end", values=(
                f"0x{r['offset']:04X}", r["src_type"], r["reloc_type"],
                "+" if r["additive"] else "", r["target"],
            ))
        if not seg["relocs"]:
            rv.insert("", "end", values=("—", "no relocations", "", "", ""))

    # ── Resources tab ─────────────────────────────────────────────────────────

    def _make_resources_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Resources")

        pane = ttk.PanedWindow(frame, orient="horizontal")
        pane.pack(fill="both", expand=True)

        # Left — grouped tree
        left = ttk.Frame(pane)
        pane.add(left, weight=1)

        tv = ttk.Treeview(left, columns=["size"], show="tree headings")
        tv.heading("#0",   text="Resource")
        tv.heading("size", text="Size")
        tv.column("#0",   width=190, anchor="w")
        tv.column("size", width=70,  anchor="e")
        sb_y = ttk.Scrollbar(left, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(left, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        tv.bind("<<TreeviewSelect>>", self._on_resource_select)
        self._res_tree = tv

        # Right — preview + hex
        right = ttk.Frame(pane)
        pane.add(right, weight=3)

        detail_nb = ttk.Notebook(right)
        detail_nb.pack(fill="both", expand=True)
        self._res_detail_nb = detail_nb

        prev_frame = ttk.Frame(detail_nb)
        detail_nb.add(prev_frame, text="Preview")
        self._res_prev = self._mk_text_scroll(prev_frame)

        hex_frame = ttk.Frame(detail_nb)
        detail_nb.add(hex_frame, text="Hex Dump")
        self._res_hex = self._mk_text_scroll(hex_frame)

        self._res_photoimage = None  # prevent GC

    def _on_resource_select(self, event):
        if not self.ne:
            return
        sel = self._res_tree.selection()
        if not sel:
            return
        tags = self._res_tree.item(sel[0], "tags")
        if not tags or not str(tags[0]).startswith("res:"):
            return
        idx  = int(str(tags[0])[4:])
        r    = self.ne.resources[idx]
        data = self.ne.data[r["offset"]:r["offset"] + r["length"]]

        # Hex
        h = self._res_hex
        h.config(state="normal")
        h.delete("1.0", "end")
        h.insert("end",
            f"  {r['type_name']}  {r['name']}  @ 0x{r['offset']:X}  {r['length']} bytes\n\n",
            "header")
        h.insert("end", self.ne.hex_dump(r["offset"], r["length"]))
        h.config(state="disabled")

        # Preview dispatch
        t = r["type_id"]
        if   t == 0x8002:            self._render_bitmap(r, data)
        elif t in (0x8003, 0x8001):  self._render_icon(r, data)
        elif t in (0x800C, 0x800E):  self._render_group_icon(r, data)
        elif t == 0x8006:            self._render_string_table(r, data)
        elif t == 0x8010:            self._render_version(r, data)
        elif t == 0x8009:            self._render_accelerator(r, data)
        elif t == 0x8004:            self._render_menu(r, data)
        elif t == 0x8005:            self._render_dialog(r, data)
        else:
            self._res_show_rich([
                (f"  {r['type_name']}  {r['name']}  {r['length']} bytes\n\n", "header"),
            ])
        self._append_hex_to_preview(r)

    def _append_hex_to_preview(self, r):
        max_b = 512
        txt = self._res_prev
        txt.config(state="normal")
        txt.insert("end",
            f"\n\n─── Hex  @ 0x{r['offset']:X}  {r['length']} bytes"
            + (f"  (first {max_b})" if r["length"] > max_b else "") + " ───\n\n",
            "header")
        txt.insert("end", self.ne.hex_dump(r["offset"], r["length"], max_bytes=max_b))
        txt.config(state="disabled")

    def _res_show_rich(self, parts):
        txt = self._res_prev
        txt.config(state="normal")
        txt.delete("1.0", "end")
        for text, tag in parts:
            txt.insert("end", text, tag) if tag else txt.insert("end", text)
        txt.config(state="disabled")
        self._res_detail_nb.select(0)

    # ── Bitmap / Icon ─────────────────────────────────────────────────────────

    def _dib_to_pil(self, data, is_icon=False):
        bi_size   = struct.unpack_from('<I', data, 0)[0]
        height    = struct.unpack_from('<i', data, 8)[0]
        bit_count = struct.unpack_from('<H', data, 14)[0]
        clr_used  = struct.unpack_from('<I', data, 32)[0]

        real_h = abs(height) // 2 if is_icon else abs(height)

        if bit_count <= 8:
            n_colors = clr_used or (1 << bit_count)
            color_table_size = n_colors * 4
        else:
            color_table_size = 0

        pix_offset = bi_size + color_table_size
        bmp_offset = 14 + pix_offset
        file_size  = 14 + len(data)

        patched = bytearray(data)
        if is_icon:
            struct.pack_into('<i', patched, 8, real_h)

        header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, bmp_offset)
        img = Image.open(io.BytesIO(bytes(header) + bytes(patched)))
        return img.convert("RGBA")

    def _display_image(self, img, r):
        iw, ih = img.size
        max_w, max_h = 640, 480
        scale = min(max_w / max(iw, 1), max_h / max(ih, 1), 8.0)
        scale = max(scale, 0.5)
        new_w = max(1, int(iw * scale))
        new_h = max(1, int(ih * scale))
        img_disp = img.resize((new_w, new_h), Image.NEAREST)

        if img_disp.mode == "RGBA":
            bg = Image.new("RGB", (new_w, new_h), (50, 50, 50))
            bg.paste(img_disp.convert("RGB"), mask=img_disp.getchannel("A"))
            img_disp = bg
        else:
            img_disp = img_disp.convert("RGB")

        photo = ImageTk.PhotoImage(img_disp)
        self._res_photoimage = photo

        txt = self._res_prev
        txt.config(state="normal")
        txt.delete("1.0", "end")
        txt.insert("end",
            f"  {r['type_name']}  {r['name']}  {iw}\u00d7{ih}px  "
            f"{r['length']} bytes  (scale {scale:.1f}\u00d7)\n\n",
            "header")
        txt.image_create("end", image=photo)
        txt.insert("end", "\n")
        txt.config(state="disabled")
        self._res_detail_nb.select(0)

    def _render_bitmap(self, r, data):
        if not HAS_PIL:
            self._res_show_rich([("  PIL not installed: pip install Pillow\n", "dim")])
            return
        try:
            self._display_image(self._dib_to_pil(data, is_icon=False), r)
        except Exception as e:
            self._res_show_rich([(f"  Render error: {e}\n", "dim")])

    def _render_icon(self, r, data):
        if not HAS_PIL:
            self._res_show_rich([("  PIL not installed: pip install Pillow\n", "dim")])
            return
        try:
            self._display_image(self._dib_to_pil(data, is_icon=True), r)
        except Exception as e:
            self._res_show_rich([(f"  Render error: {e}\n", "dim")])

    def _render_group_icon(self, r, data):
        is_cursor   = (r["type_id"] == 0x800C)
        member_type = 0x8001 if is_cursor else 0x8003  # RT_CURSOR or RT_ICON

        try:
            _, _, count = struct.unpack_from('<HHH', data, 0)

            txt = self._res_prev
            txt.config(state="normal")
            txt.delete("1.0", "end")
            txt.insert("end",
                f"  {r['type_name']}  {r['name']}  {count} {'cursors' if is_cursor else 'icons'}\n\n",
                "header")

            self._res_photoimage = []  # list — keep all references alive

            off = 6
            for i in range(count):
                if off + 14 > len(data):
                    break

                if is_cursor:
                    # GRPCURSORDIR entry: WORD w, WORD h (×2 incl mask), WORD planes, WORD bpp, DWORD size, WORD id
                    w, h, planes, bpp, size, res_id = struct.unpack_from('<HHHHIh', data, off)
                    h //= 2  # actual height
                else:
                    # GRPICONDIR entry: BYTE w, BYTE h, BYTE clrCount, BYTE reserved, WORD planes, WORD bpp, DWORD size, WORD id
                    w, h, _, _, planes, bpp, size, res_id = struct.unpack_from('<BBBBHHIh', data, off)
                off += 14

                txt.insert("end", f"  #{i+1}  {w}\u00d7{h}  {bpp}bpp  id=#{res_id}  ", "dim")

                # find matching RT_ICON / RT_CURSOR by ordinal id
                member_data = None
                for res in self.ne.resources:
                    if res["type_id"] == member_type and (res["id"] & 0x7FFF) == res_id:
                        member_data = self.ne.data[res["offset"]:res["offset"] + res["length"]]
                        break

                if member_data is None:
                    txt.insert("end", "(member resource not found)\n", "dim")
                    continue

                if not HAS_PIL:
                    txt.insert("end", "(PIL not installed)\n", "dim")
                    continue

                try:
                    img = self._dib_to_pil(member_data, is_icon=True)
                    iw, ih = img.size
                    # zoom up small icons so they're visible
                    scale = max(1, min(8, 64 // max(iw, ih, 1)))
                    if scale > 1:
                        img = img.resize((iw * scale, ih * scale), Image.NEAREST)
                    # composite RGBA on dark bg
                    bg = Image.new("RGB", img.size, (50, 50, 50))
                    bg.paste(img.convert("RGB"), mask=img.getchannel("A"))
                    photo = ImageTk.PhotoImage(bg)
                    self._res_photoimage.append(photo)
                    txt.image_create("end", image=photo)
                    txt.insert("end", "\n")
                except Exception as e:
                    txt.insert("end", f"(render error: {e})\n", "dim")

            txt.config(state="disabled")
            self._res_detail_nb.select(0)
        except Exception as e:
            self._res_show_rich([(f"  Parse error: {e}\n", "dim")])

    # ── String table ──────────────────────────────────────────────────────────

    def _render_string_table(self, r, data):
        block = (r["id"] & 0x7FFF) if (r["id"] & 0x8000) else 0
        base  = (block - 1) * 16 if block > 0 else 0
        parts = [(f"  RT_STRING  block #{block}  (IDs {base}\u2013{base+15})\n\n", "header")]
        off = 0
        for i in range(16):
            if off >= len(data):
                break
            length = data[off]; off += 1
            if length and off + length <= len(data):
                s = data[off:off+length].decode("latin-1", errors="replace")
                parts += [(f"  [{base+i:4d}]  ", "dim"), (repr(s) + "\n", "str")]
            else:
                parts += [(f"  [{base+i:4d}]  ", "dim"), ("<empty>\n", "dim")]
            off += length
        self._res_show_rich(parts)

    # ── Version ───────────────────────────────────────────────────────────────

    def _render_version(self, r, data):
        parts = [(f"  RT_VERSION  {r['name']}  {r['length']} bytes\n\n", "header")]
        try:
            off = 0
            w_length = struct.unpack_from('<H', data, off)[0]; off += 2
            v_length = struct.unpack_from('<H', data, off)[0]; off += 2
            end = data.index(b'\x00', off)
            key = data[off:end].decode("latin-1"); off = end + 1
            parts += [
                ("  Key:             ", "dim"), (f"{key}\n", None),
                ("  wLength:         ", "dim"), (f"{w_length}\n", None),
                ("  wValueLength:    ", "dim"), (f"{v_length}\n\n", None),
            ]
            if v_length >= 52 and off + 4 <= len(data):
                sig = struct.unpack_from('<I', data, off)[0]
                if sig == 0xFEEF04BD:
                    (_, _, fv_ms, fv_ls, pv_ms, pv_ls,
                     ffmask, fflags, fos, ftype, fsubtype, _, _
                    ) = struct.unpack_from('<IIIIIIIIIIIII', data, off)
                    os_map   = {1:"DOS", 2:"OS2_16", 3:"OS2_32", 4:"NT",
                                0x10001:"WIN16", 0x40004:"WIN32"}
                    type_map = {1:"APP", 2:"DLL", 3:"DRV", 4:"FONT", 5:"VXD", 7:"STATIC_LIB"}
                    parts += [
                        ("  VS_FIXEDFILEINFO\n", "header"),
                        ("  FileVersion:     ", "dim"),
                        (f"{fv_ms>>16}.{fv_ms&0xFFFF}.{fv_ls>>16}.{fv_ls&0xFFFF}\n", None),
                        ("  ProductVersion:  ", "dim"),
                        (f"{pv_ms>>16}.{pv_ms&0xFFFF}.{pv_ls>>16}.{pv_ls&0xFFFF}\n", None),
                        ("  FileOS:          ", "dim"),
                        (f"0x{fos:08X}  {os_map.get(fos, '')}\n", None),
                        ("  FileType:        ", "dim"),
                        (f"0x{ftype:08X}  {type_map.get(ftype, '')}\n", None),
                        ("  FileFlags:       ", "dim"), (f"0x{fflags:08X}\n", None),
                    ]
        except Exception as e:
            parts.append((f"\n  Parse error: {e}\n", "dim"))
        self._res_show_rich(parts)

    # ── Accelerator ───────────────────────────────────────────────────────────

    def _render_accelerator(self, r, data):
        count = len(data) // 5
        parts = [(f"  RT_ACCELERATOR  {r['name']}  {count} entries\n\n", "header"),
                 ("  Flags                Key        Cmd\n", "dim"),
                 ("  " + "─"*40 + "\n",  "dim")]
        off = 0
        while off + 5 <= len(data):
            fvirt = data[off]
            key   = struct.unpack_from('<H', data, off+1)[0]
            cmd   = struct.unpack_from('<H', data, off+3)[0]
            off  += 5
            fname = flags_str(fvirt & 0x7F, ACCEL_FLAGS)
            if fvirt & 0x01:
                key_s = f"VK 0x{key:04X}"
            else:
                key_s = repr(chr(key)) if 32 <= key < 127 else f"0x{key:04X}"
            parts.append((f"  {fname:<22} {key_s:<10} {cmd}\n", None))
            if fvirt & 0x80:
                break
        self._res_show_rich(parts)

    # ── Menu ──────────────────────────────────────────────────────────────────

    def _render_menu(self, r, data):
        MF_POPUP = 0x0010
        MF_END   = 0x0080
        parts    = [(f"  RT_MENU  {r['name']}  {r['length']} bytes\n\n", "header")]
        off      = [0]  # mutable for nested closure

        try:
            if len(data) >= 4 and struct.unpack_from('<H', data, 0)[0] == 0:
                off[0] = 4 + struct.unpack_from('<H', data, 2)[0]

            def read_level(indent):
                while off[0] < len(data):
                    if off[0] + 2 > len(data):
                        break
                    flags = struct.unpack_from('<H', data, off[0])[0]; off[0] += 2
                    if flags & MF_POPUP:
                        end = data.index(b'\x00', off[0])
                        name = data[off[0]:end].decode("latin-1", errors="replace")
                        off[0] = end + 1
                        parts += [("  " + "  "*indent, "dim"), (f"\u25b6 {name}\n", None)]
                        read_level(indent + 1)
                    else:
                        if off[0] + 2 > len(data):
                            break
                        item_id = struct.unpack_from('<H', data, off[0])[0]; off[0] += 2
                        if off[0] < len(data) and data[off[0]] == 0:
                            name = "<separator>"; off[0] += 1
                        else:
                            end = data.index(b'\x00', off[0])
                            name = data[off[0]:end].decode("latin-1", errors="replace")
                            off[0] = end + 1
                        parts += [("  " + "  "*indent + f"[{item_id:4d}]  ", "dim"),
                                   (f"{name}\n", None)]
                    if flags & MF_END:
                        break

            read_level(0)
        except Exception as e:
            parts.append((f"\n  Parse error: {e}\n", "dim"))
        self._res_show_rich(parts)

    # ── Dialog ────────────────────────────────────────────────────────────────

    def _render_dialog(self, r, data):
        """Win16 DLGTEMPLATE: parse header + control list."""
        try:
            if len(data) < 13:
                raise ValueError("too short")
            off = 0
            style    = struct.unpack_from('<I', data, off)[0]; off += 4
            n_items  = struct.unpack_from('<B', data, off)[0]; off += 1
            x, y, cx, cy = struct.unpack_from('<hhhh', data, off); off += 8

            def read_sz():
                nonlocal off
                end = data.index(b'\x00', off)
                s = data[off:end].decode("latin-1", errors="replace")
                off = end + 1
                return s

            menu_name  = read_sz()
            class_name = read_sz()
            caption    = read_sz()

            parts = [(f"  RT_DIALOG  {r['name']}  {r['length']} bytes\n\n", "header"),
                     ("  Caption:    ", "dim"), (f'"{caption}"\n', "str"),
                     ("  Class:      ", "dim"), (f"{class_name or '<default>'}\n", None),
                     ("  Menu:       ", "dim"), (f"{menu_name or '<none>'}\n", None),
                     ("  Pos/Size:   ", "dim"), (f"({x},{y})  {cx}\u00d7{cy}\n", None),
                     ("  Style:      ", "dim"), (f"0x{style:08X}\n", None),
                     ("  Controls:   ", "dim"), (f"{n_items}\n\n", None),
                     ("  Controls\n", "header")]

            # Win16 DLGITEMTEMPLATE: style(4) x(2) y(2) cx(2) cy(2) id(2) class(1) text(sz) [data]
            for _ in range(n_items):
                if off + 13 > len(data):
                    break
                i_style = struct.unpack_from('<I', data, off)[0]; off += 4
                ix, iy, icx, icy = struct.unpack_from('<hhhh', data, off); off += 8
                i_id    = struct.unpack_from('<H', data, off)[0]; off += 2
                i_class = struct.unpack_from('<B', data, off)[0]; off += 1
                CLASS_MAP = {0x80:"BUTTON",0x81:"EDIT",0x82:"STATIC",
                             0x83:"LISTBOX",0x84:"SCROLLBAR",0x85:"COMBOBOX"}
                cname = CLASS_MAP.get(i_class, f"0x{i_class:02X}")
                i_text = read_sz()
                # extra data byte
                extra = struct.unpack_from('<B', data, off)[0]; off += 1 + extra
                parts += [("  ", "dim"),
                           (f"[{i_id:4d}]  {cname:<12} ({ix},{iy}) {icx}\u00d7{icy}  "
                            f'"{i_text}"\n', None)]

        except Exception as e:
            parts = [(f"  RT_DIALOG  {r['name']}\n\n", "header"),
                     (f"  Parse error: {e}\n", "dim")]
        self._res_show_rich(parts)

    # ── Imports tab ───────────────────────────────────────────────────────────

    def _make_imports_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Imports")

        pane = ttk.PanedWindow(frame, orient="horizontal")
        pane.pack(fill="both", expand=True)

        # Left — DLL tree (hierarchical)
        left = ttk.Frame(pane)
        pane.add(left, weight=2)

        tv = ttk.Treeview(left, columns=["info"], show="tree headings")
        tv.heading("#0",   text="Module / Symbol")
        tv.heading("info", text="")
        tv.column("#0",   width=280, anchor="w")
        tv.column("info", width=100, anchor="w")
        sb_y = ttk.Scrollbar(left, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(left, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)

        # Right — summary / stats text
        right = ttk.Frame(pane)
        pane.add(right, weight=1)
        tk.Label(right, text="  Summary",
                 bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("Courier", 9, "bold"), anchor="w",
        ).pack(fill="x")
        self._imp_summary = self._mk_text_scroll(right)

        return tv

    # ── Exports tab ───────────────────────────────────────────────────────────

    def _make_exports_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Exports")

        pane = ttk.PanedWindow(frame, orient="vertical")
        pane.pack(fill="both", expand=True)

        res_frame = ttk.Frame(pane)
        pane.add(res_frame, weight=1)
        tk.Label(res_frame, text="  Resident Names Table",
                 bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("TkFixedFont", 9, "bold"), anchor="w",
        ).pack(fill="x")
        self._tv_res = self._mk_tree(res_frame, ["Ordinal", "Name"])
        self._tv_res.column("Name", width=500)

        nres_frame = ttk.Frame(pane)
        pane.add(nres_frame, weight=1)
        tk.Label(nres_frame, text="  Non-Resident Names Table",
                 bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("TkFixedFont", 9, "bold"), anchor="w",
        ).pack(fill="x")
        self._tv_nres = self._mk_tree(nres_frame, ["Ordinal", "Name"])
        self._tv_nres.column("Name", width=500)

    # ── File open ─────────────────────────────────────────────────────────────

    def open_file(self):
        path = filedialog.askopenfilename(
            title="Open NE executable",
            filetypes=[("Win16 executables", "*.exe *.dll *.drv *.fon"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            self.ne = NEFile(path)
        except Exception as e:
            messagebox.showerror("Error", str(e))
            return
        self.title_var.set(
            f"{os.path.basename(path)}  ({len(self.ne.data):,} bytes)  —  {path}"
        )
        self._populate()

    # ── Populate ──────────────────────────────────────────────────────────────

    def _populate(self):
        self._fill_overview()
        self._fill_segments()
        self._fill_resources()
        self._fill_exports()
        self._fill_imports()
        self._fill_entries()

    def _fill_overview(self):
        ne  = self.ne.ne
        txt = self.tab_overview
        txt.config(state="normal")
        txt.delete("1.0", "end")

        os_map = {1: "OS/2", 2: "Windows", 3: "European DOS 4.x", 4: "Windows 386"}
        target = os_map.get(ne["target_os"], "Unknown (%d)" % ne["target_os"])
        total_relocs = sum(len(s["relocs"]) for s in self.ne.segments)

        sections = [
            ("─── File ────────────────────────────────────────────────────", [
                ("File",             self.ne.path),
                ("File size",        f"{len(self.ne.data):,} bytes  (0x{len(self.ne.data):X})"),
                ("NE header offset", f"0x{self.ne.ne_off:04X}"),
            ]),
            ("─── NE Header ───────────────────────────────────────────────", [
                ("Linker version",  f"{ne['linker_ver']}.{ne['linker_rev']}"),
                ("Target OS",       target),
                ("Windows version", f"{ne['win_ver_major']}.{ne['win_ver_minor']}"),
                ("Flags",           f"0x{ne['flags']:04X}  [{flags_str(ne['flags'], NE_FLAGS)}]"),
                ("Autodata seg",    f"{ne['autodata']}  (DGROUP)"),
                ("Heap size",       f"0x{ne['heap_size']:04X}  ({ne['heap_size']} bytes)"),
                ("Stack size",      f"0x{ne['stack_size']:04X}  ({ne['stack_size']} bytes)"),
                ("Entry CS:IP",     f"{ne['ne_cs']}:{ne['ne_ip']:04X}"),
                ("Stack SS:SP",     f"{ne['ne_ss']}:{ne['ne_sp']:04X}"),
            ]),
            ("─── Tables ──────────────────────────────────────────────────", [
                ("Segments",         str(ne["cseg"])),
                ("Modules imported", str(ne["cmod"])),
                ("Resources",        str(ne["res_count"])),
                ("Moveable entries", str(ne["moveable_entries"])),
                ("Align shift",      f"{ne['align_shift']}  (sector = {1 << ne['align_shift']} bytes)"),
            ]),
            ("─── Table offsets ───────────────────────────────────────────", [
                ("Seg table",   f"NE+0x{ne['seg_table_off']:04X}  = 0x{self.ne.ne_off + ne['seg_table_off']:X}"),
                ("Res table",   f"NE+0x{ne['res_table_off']:04X}  = 0x{self.ne.ne_off + ne['res_table_off']:X}"),
                ("Rnames",      f"NE+0x{ne['rnames_off']:04X}  = 0x{self.ne.ne_off + ne['rnames_off']:X}"),
                ("ModRef",      f"NE+0x{ne['modref_off']:04X}  = 0x{self.ne.ne_off + ne['modref_off']:X}"),
                ("ImpNames",    f"NE+0x{ne['impnames_off']:04X}  = 0x{self.ne.ne_off + ne['impnames_off']:X}"),
                ("NRnames",     f"0x{ne['nrnames_off']:X}  (absolute)  size={ne['nrnames_size']}"),
                ("Entry table", f"NE+0x{ne['entry_table_off']:04X}  size={ne['entry_table_size']}"),
            ]),
            ("─── Summary ─────────────────────────────────────────────────", [
                ("Resident exports",     str(max(0, len(self.ne.exports) - 1))),
                ("Non-resident exports", str(max(0, len(self.ne.nonresident) - 1))),
                ("Imports (modules)",    str(len(self.ne.imports))),
                ("Entry table entries",  str(len(self.ne.entries))),
                ("Resources total",      str(len(self.ne.resources))),
                ("Total relocations",    str(total_relocs)),
            ]),
        ]

        for header, fields in sections:
            txt.insert("end", header + "\n", "header")
            for key, val in fields:
                txt.insert("end", f"  {key:<22}", "dim")
                txt.insert("end", f"{val}\n", "value")
            txt.insert("end", "\n")

        if self.ne.resources:
            txt.insert("end", "─── Resource breakdown ───────────────────────────────────────\n", "header")
            cnt = Counter(r["type_name"] for r in self.ne.resources)
            for t, c in sorted(cnt.items(), key=lambda x: -x[1]):
                txt.insert("end", f"  {t:<28}", "dim")
                txt.insert("end", f"{c}\n", "value")

        txt.config(state="disabled")

    def _fill_segments(self):
        tv = self.tab_segments
        tv.delete(*tv.get_children())
        for s in self.ne.segments:
            kind = "DATA" if (s["flags"] & 0x0001) else "CODE"
            fstr = flags_str(s["flags"], SEG_FLAGS)
            tv.insert("", "end", values=(
                s["idx"],
                f"0x{s['file_off']:X}" if s["file_off"] else "—",
                f"0x{s['cbseg']:X}  ({s['cbseg']})",
                f"0x{s['minalloc']:X}  ({s['minalloc'] or 65536})",
                f"{kind}  [{fstr}]  relocs={len(s['relocs'])}",
            ))
        tv.column("#",           width=30)
        tv.column("File Offset", width=100)
        tv.column("Size",        width=120)
        tv.column("MinAlloc",    width=120)
        tv.column("Flags",       width=450)
        self._seg_hex.config(state="normal")
        self._seg_hex.delete("1.0", "end")
        self._seg_hex.insert("end", "  Select a segment above.\n", "dim")
        self._seg_hex.config(state="disabled")
        self._seg_reloc_tv.delete(*self._seg_reloc_tv.get_children())

    def _fill_resources(self):
        tv = self._res_tree
        tv.delete(*tv.get_children())

        by_type = {}
        for i, r in enumerate(self.ne.resources):
            by_type.setdefault(r["type_name"], []).append((i, r))

        for type_name, items in sorted(by_type.items()):
            parent = tv.insert("", "end",
                text=f"{type_name}  ({len(items)})", open=True)
            for idx, r in items:
                tv.insert(parent, "end",
                    text=r["name"],
                    values=(f"{r['length']:,}",),
                    tags=(f"res:{idx}",),
                )

        # clear detail panels
        for w in (self._res_prev, self._res_hex):
            w.config(state="normal")
            w.delete("1.0", "end")
            w.insert("end", "  Select a resource.\n", "dim")
            w.config(state="disabled")

    def _fill_exports(self):
        tv = self._tv_res
        tv.delete(*tv.get_children())
        for i, (ordinal, name) in enumerate(self.ne.exports):
            tv.insert("", "end", values=("MODULE NAME" if i == 0 else str(ordinal), name))

        tv2 = self._tv_nres
        tv2.delete(*tv2.get_children())
        for i, (ordinal, name) in enumerate(self.ne.nonresident):
            tv2.insert("", "end", values=("MODULE DESC" if i == 0 else str(ordinal), name))
        if not self.ne.nonresident:
            tv2.insert("", "end", values=("—", "<empty>"))

    def _fill_imports(self):
        tv = self.tab_imports
        tv.delete(*tv.get_children())

        table = self.ne.build_import_table()

        total_funcs = 0
        stats = []

        for mod in self.ne.imports:
            raw   = table.get(mod, [])
            # sort: ordinals (#N) numerically, names alphabetically, names after ordinals
            ords  = sorted([f for f in raw if f.startswith("#") and f[1:].isdigit()],
                           key=lambda x: int(x[1:]))
            names = sorted([f for f in raw if not (f.startswith("#") and f[1:].isdigit())])
            funcs = ords + names
            n     = len(funcs)
            total_funcs += n

            parent = tv.insert("", "end",
                text=mod,
                values=(f"{n} import{'s' if n != 1 else ''}",),
                open=n <= 30,  # auto-expand small modules
                tags=("dll",),
            )
            for func in funcs:
                kind = "ordinal" if func.startswith("#") else "name"
                tv.insert(parent, "end", text=f"  {func}", values=(kind,), tags=(kind,))

            stats.append((mod, n, ords, names))

        # colour coding
        tv.tag_configure("dll",     foreground=C_ACCENT)
        tv.tag_configure("ordinal", foreground="#9cdcfe")
        tv.tag_configure("name",    foreground="#ce9178")

        # summary panel
        s = self._imp_summary
        s.config(state="normal")
        s.delete("1.0", "end")
        s.insert("end", f"  {len(self.ne.imports)} DLLs  {total_funcs} imports\n\n", "header")
        for mod, n, ords, names in stats:
            s.insert("end", f"  {mod}\n", "header")
            s.insert("end", f"    ordinals : {len(ords)}\n", "dim")
            if names:
                s.insert("end", f"    by name  : {len(names)}\n", "dim")
            s.insert("end", f"    total    : {n}\n\n", None)
        s.config(state="disabled")

    def _fill_entries(self):
        tv = self.tab_entries
        tv.delete(*tv.get_children())
        name_map = {o: n for o, n in self.ne.exports[1:]}
        name_map.update({o: n for o, n in self.ne.nonresident[1:]})
        for e in self.ne.entries:
            fp = []
            if e["flags"] & 1: fp.append("EXPORTED")
            if e["flags"] & 2: fp.append("SHARED")
            flag_s = f"0x{e['flags']:02X}" + (f"  {' '.join(fp)}" if fp else "")
            iid = tv.insert("", "end", values=(
                e["ordinal"], e["type"], e["seg"],
                f"0x{e['offset']:04X}", flag_s,
                name_map.get(e["ordinal"], ""),
            ))
            if name_map.get(e["ordinal"]):
                tv.item(iid, tags=("named",))
        tv.column("Ordinal", width=70)
        tv.column("Type",    width=50)
        tv.column("Seg",     width=50)
        tv.column("Offset",  width=80)
        tv.column("Flags",   width=160)
        tv.column("Name",    width=350)
        tv.tag_configure("named", background=C_GREEN_BG)


if __name__ == "__main__":
    app = App()
    path = sys.argv[1] if len(sys.argv) > 1 else (SKIFREE_PATH if os.path.isfile(SKIFREE_PATH) else None)
    if path:
        try:
            app.ne = NEFile(path)
            app.title_var.set(
                f"{os.path.basename(path)}  ({len(app.ne.data):,} bytes)  —  {path}"
            )
            app._populate()
        except Exception as e:
            print(f"Error: {e}")
    app.mainloop()
