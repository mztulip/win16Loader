#!/usr/bin/env python3
"""
NE (New Executable) format explorer GUI
For Win16 / Windows 3.1 executables (.exe, .dll)
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import struct
import os
from collections import Counter

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

    # ── Segments ──────────────────────────────────────────────────────────────

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

    # ── Resources ─────────────────────────────────────────────────────────────

    def _res_name_at(self, res_table_base, offset):
        """Read Pascal string at res_table_base + offset."""
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

    # ── Names tables ──────────────────────────────────────────────────────────

    def _parse_exports(self):
        """Resident names table — first entry is module name, rest are exports."""
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
        """Non-resident names table (absolute file offset)."""
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
            if seg_type == 0x00:  # unused
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
            else:  # fixed segment
                seg = seg_type
                for _ in range(count):
                    flags  = self.u8(off);  off += 1
                    ofs    = self.u16(off); off += 2
                    entries.append({"ordinal": ordinal, "seg": seg, "offset": ofs,
                                    "flags": flags, "type": "fix"})
                    ordinal += 1
        return entries

    # ── Relocations ───────────────────────────────────────────────────────────

    def _parse_segment_relocs(self, seg):
        """Parse NE relocation records appended after segment data."""
        if not (seg["flags"] & 0x0100):   # RELOC flag
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
            if reloc_type == 0:   # INTERNALREF
                target_str = f"seg {target1}, off 0x{target2:04X}"
            elif reloc_type == 1: # IMPORTORDINAL
                mod = self.imports[target1-1] if 0 < target1 <= len(self.imports) else f"mod#{target1}"
                target_str = f"{mod}.#{target2}"
            elif reloc_type == 2: # IMPORTNAME
                imp_base = self.ne_off + self.ne["impnames_off"] + target2
                if imp_base < len(self.data):
                    l  = self.u8(imp_base)
                    nm = self.data[imp_base+1:imp_base+1+l].decode("ascii", errors="replace")
                else:
                    nm = f"?off={target2}"
                mod = self.imports[target1-1] if 0 < target1 <= len(self.imports) else f"mod#{target1}"
                target_str = f"{mod}.{nm}"
            else:                 # OSFIXUP
                target_str = f"osfixup #{target1}"
            relocs.append({
                "src_type":   RELOC_SRC.get(src_type, f"0x{src_type:02X}"),
                "reloc_type": RELOC_TYPE.get(reloc_type, f"0x{reloc_type:02X}"),
                "additive":   additive,
                "offset":     seg_offset,
                "target":     target_str,
            })
        return relocs

    # ── Hex dump ──────────────────────────────────────────────────────────────

    def hex_dump(self, file_off, size, max_bytes=512):
        if not file_off:
            return "<no data in file>"
        data = self.data[file_off:file_off + min(size, max_bytes)]
        lines = []
        for i in range(0, len(data), 16):
            chunk      = data[i:i+16]
            hex_part   = " ".join(f"{b:02X}" for b in chunk)
            ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"  {file_off+i:08X}  {hex_part:<47}  {ascii_part}")
        if size > max_bytes:
            lines.append(f"  ... ({size - max_bytes} more bytes not shown)")
        return "\n".join(lines) if lines else "<empty segment>"


# ─── GUI ──────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NE Explorer")
        self.geometry("1100x750")
        self.configure(bg=C_BG)
        self.ne = None
        self._apply_dark_theme()
        self._build_ui()

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
            background=C_BG3, foreground=C_FG_DIM,
            padding=[10, 4], focuscolor=C_BG,
        )
        style.map("TNotebook.Tab",
            background=[("selected", C_BG2), ("active", C_BG2)],
            foreground=[("selected", C_FG),  ("active", C_FG)],
        )
        style.configure("Treeview",
            background=C_BG2, foreground=C_FG,
            fieldbackground=C_BG2, rowheight=20,
            bordercolor=C_BORDER,
        )
        style.configure("Treeview.Heading",
            background=C_HEADER_BG, foreground=C_FG,
            relief="flat", borderwidth=0,
        )
        style.map("Treeview",
            background=[("selected", C_SEL_BG)],
            foreground=[("selected", C_SEL_FG)],
        )
        style.map("Treeview.Heading",
            background=[("active", C_BG3)],
        )
        style.configure("TPanedwindow", background=C_BORDER)

    def _text_widget(self, parent):
        txt = tk.Text(parent,
            font=("TkFixedFont", 10), wrap="none",
            bg=C_BG2, fg=C_FG, insertbackground=C_FG,
            selectbackground=C_SEL_BG, selectforeground=C_SEL_FG,
            relief="flat", borderwidth=0,
        )
        txt.tag_configure("header",  foreground=C_ACCENT)
        txt.tag_configure("dim",     foreground=C_FG_DIM)
        txt.tag_configure("value",   foreground="#ce9178")
        return txt

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
        self.tab_resources = self._make_tree_tab("Resources",
            ["Type", "ID", "File Offset", "Size", "Flags"])
        self.tab_exports   = self._make_exports_tab()
        self.tab_imports   = self._make_tree_tab("Imports",
            ["#", "Module"])
        self.tab_entries   = self._make_tree_tab("Entry Table",
            ["Ordinal", "Type", "Seg", "Offset", "Flags", "Name"])

    def _make_text_tab(self, label):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text=label)
        txt = self._text_widget(frame)
        sb_y = ttk.Scrollbar(frame, orient="vertical",   command=txt.yview)
        sb_x = ttk.Scrollbar(frame, orient="horizontal", command=txt.xview)
        txt.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        txt.pack(fill="both", expand=True)
        return txt

    def _make_tree_tab(self, label, columns):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text=label)
        tv = ttk.Treeview(frame, columns=columns, show="headings")
        for col in columns:
            tv.heading(col, text=col)
            tv.column(col, width=120, anchor="w")
        sb_y = ttk.Scrollbar(frame, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(frame, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        return tv

    # ── Segments tab: treeview + detail panel ─────────────────────────────────

    def _make_segments_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Segments")

        pane = ttk.PanedWindow(frame, orient="vertical")
        pane.pack(fill="both", expand=True)

        # Top: segment list
        top = ttk.Frame(pane)
        pane.add(top, weight=2)
        cols = ["#", "File Offset", "Size", "MinAlloc", "Flags"]
        tv = ttk.Treeview(top, columns=cols, show="headings")
        for col in cols:
            tv.heading(col, text=col)
            tv.column(col, width=120, anchor="w")
        sb_y = ttk.Scrollbar(top, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(top, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)

        # Bottom: detail (hex + relocs)
        bot = ttk.Frame(pane)
        pane.add(bot, weight=3)

        detail_nb = ttk.Notebook(bot)
        detail_nb.pack(fill="both", expand=True)

        hex_frame   = ttk.Frame(detail_nb)
        reloc_frame = ttk.Frame(detail_nb)
        detail_nb.add(hex_frame,   text="Hex Dump")
        detail_nb.add(reloc_frame, text="Relocations")

        self._seg_hex  = self._text_widget_in(hex_frame)
        self._seg_reloc_tv = self._reloc_tree_in(reloc_frame)

        tv.bind("<<TreeviewSelect>>", self._on_segment_select)
        return tv

    def _text_widget_in(self, parent):
        txt = self._text_widget(parent)
        sb_y = ttk.Scrollbar(parent, orient="vertical",   command=txt.yview)
        sb_x = ttk.Scrollbar(parent, orient="horizontal", command=txt.xview)
        txt.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        txt.pack(fill="both", expand=True)
        return txt

    def _reloc_tree_in(self, parent):
        cols = ["Offset", "Src Type", "Reloc Type", "Additive", "Target"]
        tv = ttk.Treeview(parent, columns=cols, show="headings")
        for col in cols:
            tv.heading(col, text=col)
            tv.column(col, width=120, anchor="w")
        tv.column("Target", width=350)
        sb_y = ttk.Scrollbar(parent, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(parent, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        return tv

    def _on_segment_select(self, event):
        if not self.ne:
            return
        sel = self.tab_segments.selection()
        if not sel:
            return
        idx = int(self.tab_segments.item(sel[0])["values"][0]) - 1
        seg = self.ne.segments[idx]

        # Hex dump
        txt = self._seg_hex
        txt.config(state="normal")
        txt.delete("1.0", "end")
        txt.insert("end", f"  Segment #{seg['idx']}  —  file offset 0x{seg['file_off']:X}  size 0x{seg['cbseg']:X}\n\n", "header")
        txt.insert("end", self.ne.hex_dump(seg["file_off"], seg["cbseg"]))
        txt.config(state="disabled")

        # Relocations
        rv = self._seg_reloc_tv
        rv.delete(*rv.get_children())
        for r in seg["relocs"]:
            rv.insert("", "end", values=(
                f"0x{r['offset']:04X}",
                r["src_type"],
                r["reloc_type"],
                "+" if r["additive"] else "",
                r["target"],
            ))
        if not seg["relocs"]:
            rv.insert("", "end", values=("—", "no relocations", "", "", ""))

    # ── Exports tab: resident + non-resident ──────────────────────────────────

    def _make_exports_tab(self):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text="Exports")

        pane = ttk.PanedWindow(frame, orient="vertical")
        pane.pack(fill="both", expand=True)

        # Resident names
        res_frame = ttk.Frame(pane)
        pane.add(res_frame, weight=1)
        tk.Label(res_frame, text="  Resident Names Table",
                 bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("TkFixedFont", 9, "bold"), anchor="w",
        ).pack(fill="x")
        cols = ["Ordinal", "Name"]
        tv_res = ttk.Treeview(res_frame, columns=cols, show="headings")
        for col in cols:
            tv_res.heading(col, text=col)
            tv_res.column(col, width=120, anchor="w")
        tv_res.column("Name", width=500)
        sb = ttk.Scrollbar(res_frame, orient="vertical", command=tv_res.yview)
        tv_res.config(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        tv_res.pack(fill="both", expand=True)

        # Non-resident names
        nres_frame = ttk.Frame(pane)
        pane.add(nres_frame, weight=1)
        tk.Label(nres_frame, text="  Non-Resident Names Table",
                 bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("TkFixedFont", 9, "bold"), anchor="w",
        ).pack(fill="x")
        tv_nres = ttk.Treeview(nres_frame, columns=cols, show="headings")
        for col in cols:
            tv_nres.heading(col, text=col)
            tv_nres.column(col, width=120, anchor="w")
        tv_nres.column("Name", width=500)
        sb2 = ttk.Scrollbar(nres_frame, orient="vertical", command=tv_nres.yview)
        tv_nres.config(yscrollcommand=sb2.set)
        sb2.pack(side="right", fill="y")
        tv_nres.pack(fill="both", expand=True)

        self._tv_res  = tv_res
        self._tv_nres = tv_nres
        return tv_res   # tab_exports points to resident treeview (compat)

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

    # ── Populate tabs ─────────────────────────────────────────────────────────

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
                ("Segments",          str(ne["cseg"])),
                ("Modules imported",  str(ne["cmod"])),
                ("Resources",         str(ne["res_count"])),
                ("Moveable entries",  str(ne["moveable_entries"])),
                ("Align shift",       f"{ne['align_shift']}  (sector = {1 << ne['align_shift']} bytes)"),
            ]),
            ("─── Table offsets ───────────────────────────────────────────", [
                ("Seg table",    f"NE+0x{ne['seg_table_off']:04X}  = 0x{self.ne.ne_off + ne['seg_table_off']:X}"),
                ("Res table",    f"NE+0x{ne['res_table_off']:04X}  = 0x{self.ne.ne_off + ne['res_table_off']:X}"),
                ("Rnames",       f"NE+0x{ne['rnames_off']:04X}  = 0x{self.ne.ne_off + ne['rnames_off']:X}"),
                ("ModRef",       f"NE+0x{ne['modref_off']:04X}  = 0x{self.ne.ne_off + ne['modref_off']:X}"),
                ("ImpNames",     f"NE+0x{ne['impnames_off']:04X}  = 0x{self.ne.ne_off + ne['impnames_off']:X}"),
                ("NRnames",      f"0x{ne['nrnames_off']:X}  (absolute)  size={ne['nrnames_size']}"),
                ("Entry table",  f"NE+0x{ne['entry_table_off']:04X}  size={ne['entry_table_size']}"),
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
            nrel = len(s["relocs"])
            tv.insert("", "end", values=(
                s["idx"],
                f"0x{s['file_off']:X}" if s["file_off"] else "—",
                f"0x{s['cbseg']:X}  ({s['cbseg']})",
                f"0x{s['minalloc']:X}  ({s['minalloc'] or 65536})",
                f"{kind}  [{fstr}]  relocs={nrel}",
            ))
        tv.column("#",           width=30)
        tv.column("File Offset", width=100)
        tv.column("Size",        width=120)
        tv.column("MinAlloc",    width=120)
        tv.column("Flags",       width=450)

        # Clear detail panels
        self._seg_hex.config(state="normal")
        self._seg_hex.delete("1.0", "end")
        self._seg_hex.insert("end", "  Select a segment above to view its hex dump.", "dim")
        self._seg_hex.config(state="disabled")
        self._seg_reloc_tv.delete(*self._seg_reloc_tv.get_children())

    def _fill_resources(self):
        tv = self.tab_resources
        tv.delete(*tv.get_children())
        for r in self.ne.resources:
            tv.insert("", "end", values=(
                r["type_name"],
                r["name"],
                f"0x{r['offset']:X}",
                f"0x{r['length']:X}  ({r['length']})",
                f"0x{r['flags']:04X}",
            ))
        tv.column("Type",        width=160)
        tv.column("ID",          width=100)
        tv.column("File Offset", width=100)
        tv.column("Size",        width=120)
        tv.column("Flags",       width=80)

    def _fill_exports(self):
        # Resident names
        tv = self._tv_res
        tv.delete(*tv.get_children())
        for i, (ordinal, name) in enumerate(self.ne.exports):
            label = "MODULE NAME" if i == 0 else str(ordinal)
            tv.insert("", "end", values=(label, name))

        # Non-resident names
        tv2 = self._tv_nres
        tv2.delete(*tv2.get_children())
        for i, (ordinal, name) in enumerate(self.ne.nonresident):
            label = "MODULE DESC" if i == 0 else str(ordinal)
            tv2.insert("", "end", values=(label, name))
        if not self.ne.nonresident:
            tv2.insert("", "end", values=("—", "<empty>"))

    def _fill_imports(self):
        tv = self.tab_imports
        tv.delete(*tv.get_children())
        for i, mod in enumerate(self.ne.imports):
            tv.insert("", "end", values=(i+1, mod))
        tv.column("#",      width=40)
        tv.column("Module", width=400)

    def _fill_entries(self):
        tv = self.tab_entries
        tv.delete(*tv.get_children())
        # Build name lookup from both resident and non-resident exports
        name_map = {ord_: name for ord_, name in self.ne.exports[1:]}
        name_map.update({ord_: name for ord_, name in self.ne.nonresident[1:]})
        for e in self.ne.entries:
            flag_parts = []
            if e["flags"] & 1: flag_parts.append("EXPORTED")
            if e["flags"] & 2: flag_parts.append("SHARED")
            flag_str = f"0x{e['flags']:02X}" + (f"  {' '.join(flag_parts)}" if flag_parts else "")
            name = name_map.get(e["ordinal"], "")
            iid = tv.insert("", "end", values=(
                e["ordinal"],
                e["type"],
                e["seg"],
                f"0x{e['offset']:04X}",
                flag_str,
                name,
            ))
            if name:
                tv.item(iid, tags=("named",))
        tv.column("Ordinal", width=70)
        tv.column("Type",    width=50)
        tv.column("Seg",     width=50)
        tv.column("Offset",  width=80)
        tv.column("Flags",   width=160)
        tv.column("Name",    width=350)
        tv.tag_configure("named", background=C_GREEN_BG)


if __name__ == "__main__":
    import sys
    app = App()
    if len(sys.argv) > 1:
        try:
            app.ne = NEFile(sys.argv[1])
            app.title_var.set(
                f"{os.path.basename(sys.argv[1])}  —  {sys.argv[1]}"
            )
            app._populate()
        except Exception as e:
            print(f"Error: {e}")
    app.mainloop()
