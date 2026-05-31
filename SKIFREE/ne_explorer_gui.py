#!/usr/bin/env python3
"""
NE (New Executable) format explorer GUI
For Win16 / Windows 3.1 executables (.exe, .dll)
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import struct
import os

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


def flags_str(val, flag_list):
    return " | ".join(name for bit, name in flag_list if val & bit) or "0"


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
        # handle both 2-byte and 4-byte e_lfanew
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

        self.segments   = self._parse_segments()
        self.resources  = self._parse_resources()
        self.exports    = self._parse_exports()
        self.imports    = self._parse_imports()
        self.entries    = self._parse_entry_table()

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

    def _parse_resources(self):
        n = self.ne_off
        if self.ne["res_table_off"] == self.ne["rnames_off"]:
            return []  # empty resource table
        off = n + self.ne["res_table_off"]
        align_shift = self.u16(off); off += 2
        resources = []
        while True:
            type_id = self.u16(off); off += 2
            if type_id == 0:
                break
            count = self.u16(off); off += 2
            off += 4  # reserved
            type_name = RT_NAMES.get(type_id, "0x%04X" % type_id)
            for _ in range(count):
                r_off  = self.u16(off) << align_shift; off += 2
                r_len  = self.u16(off) << align_shift; off += 2
                r_flags= self.u16(off); off += 2
                r_id   = self.u16(off); off += 2
                off += 4  # reserved
                name = ("#%d" % (r_id & 0x7FFF)) if (r_id & 0x8000) else "str"
                resources.append({
                    "type_id": type_id, "type_name": type_name,
                    "id": r_id, "name": name,
                    "offset": r_off, "length": r_len, "flags": r_flags,
                })
        return resources

    def _parse_exports(self):
        """Parse resident names table — first entry is module name, rest are exports."""
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
        return names  # [(ordinal, name), ...]

    def _parse_imports(self):
        n = self.ne_off
        # Module reference table: array of u16 offsets into imported names table
        mod_off  = n + self.ne["modref_off"]
        imp_off  = n + self.ne["impnames_off"]
        modules = []
        for i in range(self.ne["cmod"]):
            name_off = self.u16(mod_off + i*2)
            abs_off  = imp_off + name_off
            length = self.u8(abs_off)
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
                    entries.append({"ordinal": ordinal, "seg": seg, "offset": ofs, "flags": flags, "type": "mov"})
                    ordinal += 1
            else:  # fixed segment
                seg = seg_type
                for _ in range(count):
                    flags  = self.u8(off);  off += 1
                    ofs    = self.u16(off); off += 2
                    entries.append({"ordinal": ordinal, "seg": seg, "offset": ofs, "flags": flags, "type": "fix"})
                    ordinal += 1
        return entries


# ─── GUI ──────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NE Explorer")
        self.geometry("1000x700")
        self.ne = None
        self._build_ui()

    def _build_ui(self):
        # Menu
        menubar = tk.Menu(self)
        filemenu = tk.Menu(menubar, tearoff=0)
        filemenu.add_command(label="Open…", accelerator="Ctrl+O", command=self.open_file)
        filemenu.add_separator()
        filemenu.add_command(label="Quit", command=self.quit)
        menubar.add_cascade(label="File", menu=filemenu)
        self.config(menu=menubar)
        self.bind("<Control-o>", lambda e: self.open_file())

        # Title bar
        self.title_var = tk.StringVar(value="No file loaded")
        tk.Label(self, textvariable=self.title_var, anchor="w",
                 font=("TkFixedFont", 10, "bold")).pack(fill="x", padx=6, pady=2)

        # Notebook tabs
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=4, pady=4)

        self.tab_overview  = self._make_text_tab("Overview")
        self.tab_segments  = self._make_tree_tab("Segments",
            ["#", "File Offset", "Size", "MinAlloc", "Flags"])
        self.tab_resources = self._make_tree_tab("Resources",
            ["Type", "ID", "File Offset", "Size", "Flags"])
        self.tab_exports   = self._make_tree_tab("Exports",
            ["Ordinal", "Name"])
        self.tab_imports   = self._make_tree_tab("Imports",
            ["#", "Module"])
        self.tab_entries   = self._make_tree_tab("Entry Table",
            ["Ordinal", "Type", "Seg", "Offset", "Flags"])

    def _make_text_tab(self, label):
        frame = ttk.Frame(self.nb)
        self.nb.add(frame, text=label)
        txt = tk.Text(frame, font=("TkFixedFont", 10), wrap="none")
        sb_y = ttk.Scrollbar(frame, orient="vertical",   command=txt.yview)
        sb_x = ttk.Scrollbar(frame, orient="horizontal", command=txt.xview)
        txt.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right", fill="y")
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
        sb_y.pack(side="right", fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        return tv

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
        self.title_var.set(f"{os.path.basename(path)}  ({len(self.ne.data):,} bytes)  —  {path}")
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
        ne = self.ne.ne
        txt = self.tab_overview
        txt.config(state="normal")
        txt.delete("1.0", "end")

        os_map = {1: "OS/2", 2: "Windows", 3: "European DOS 4.x", 4: "Windows 386"}
        target = os_map.get(ne["target_os"], "Unknown (%d)" % ne["target_os"])

        lines = [
            f"File:             {self.ne.path}",
            f"File size:        {len(self.ne.data):,} bytes  (0x{len(self.ne.data):X})",
            f"NE header offset: 0x{self.ne.ne_off:04X}",
            "",
            "─── NE Header ────────────────────────────────────────────────",
            f"Linker version:   {ne['linker_ver']}.{ne['linker_rev']}",
            f"Target OS:        {target}",
            f"Windows version:  {ne['win_ver_major']}.{ne['win_ver_minor']}",
            f"Flags:            0x{ne['flags']:04X}  [{flags_str(ne['flags'], NE_FLAGS)}]",
            f"Autodata seg:     {ne['autodata']}  (DGROUP)",
            f"Heap size:        0x{ne['heap_size']:04X}  ({ne['heap_size']} bytes)",
            f"Stack size:       0x{ne['stack_size']:04X}  ({ne['stack_size']} bytes)",
            f"Entry CS:IP:      {ne['ne_cs']}:{ne['ne_ip']:04X}",
            f"Stack SS:SP:      {ne['ne_ss']}:{ne['ne_sp']:04X}",
            "",
            "─── Tables ───────────────────────────────────────────────────",
            f"Segments:         {ne['cseg']}",
            f"Modules imported: {ne['cmod']}",
            f"Resources:        {ne['res_count']}",
            f"Moveable entries: {ne['moveable_entries']}",
            f"Align shift:      {ne['align_shift']}  (sector = {1 << ne['align_shift']} bytes)",
            "",
            f"Seg table off:    NE+0x{ne['seg_table_off']:04X}  = 0x{self.ne.ne_off + ne['seg_table_off']:X}",
            f"Res table off:    NE+0x{ne['res_table_off']:04X}  = 0x{self.ne.ne_off + ne['res_table_off']:X}",
            f"Rnames off:       NE+0x{ne['rnames_off']:04X}  = 0x{self.ne.ne_off + ne['rnames_off']:X}",
            f"ModRef off:       NE+0x{ne['modref_off']:04X}  = 0x{self.ne.ne_off + ne['modref_off']:X}",
            f"ImpNames off:     NE+0x{ne['impnames_off']:04X}  = 0x{self.ne.ne_off + ne['impnames_off']:X}",
            f"NRnames off:      0x{ne['nrnames_off']:X}  (absolute)  size={ne['nrnames_size']}",
            f"Entry table off:  NE+0x{ne['entry_table_off']:04X}  size={ne['entry_table_size']}",
            "",
            "─── Summary ──────────────────────────────────────────────────",
            f"Exports (resident names): {max(0, len(self.ne.exports)-1)}",
            f"Imports (modules):        {len(self.ne.imports)}",
            f"Entry table entries:      {len(self.ne.entries)}",
            f"Resources total:          {len(self.ne.resources)}",
        ]

        # Resource type breakdown
        if self.ne.resources:
            from collections import Counter
            cnt = Counter(r["type_name"] for r in self.ne.resources)
            lines.append("")
            lines.append("─── Resource breakdown ───────────────────────────────────────")
            for t, c in sorted(cnt.items(), key=lambda x: -x[1]):
                lines.append(f"  {t:<25} {c}")

        txt.insert("end", "\n".join(lines))
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
                f"{kind}  [{fstr}]",
            ))
        tv.column("#",           width=30)
        tv.column("File Offset", width=100)
        tv.column("Size",        width=120)
        tv.column("MinAlloc",    width=120)
        tv.column("Flags",       width=400)

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
        tv.column("ID",          width=80)
        tv.column("File Offset", width=100)
        tv.column("Size",        width=120)
        tv.column("Flags",       width=80)

    def _fill_exports(self):
        tv = self.tab_exports
        tv.delete(*tv.get_children())
        for i, (ordinal, name) in enumerate(self.ne.exports):
            label = "MODULE NAME" if i == 0 else str(ordinal)
            tv.insert("", "end", values=(label, name))
        tv.column("Ordinal", width=80)
        tv.column("Name",    width=600)

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
        # Build name lookup from exports
        name_map = {ord_: name for ord_, name in self.ne.exports[1:]}
        for e in self.ne.entries:
            name = name_map.get(e["ordinal"], "")
            tv.insert("", "end", values=(
                e["ordinal"],
                e["type"],
                e["seg"],
                f"0x{e['offset']:04X}",
                f"0x{e['flags']:02X}  {'EXPORTED' if e['flags']&1 else ''}{'SHARED' if e['flags']&2 else ''}",
            ))
            if name:
                tv.item(tv.get_children()[-1], tags=("named",))
                tv.set(tv.get_children()[-1], "Flags",
                       f"0x{e['flags']:02X}  {name}")
        tv.column("Ordinal", width=70)
        tv.column("Type",    width=50)
        tv.column("Seg",     width=50)
        tv.column("Offset",  width=80)
        tv.column("Flags",   width=400)
        tv.tag_configure("named", background="#e8f4e8")


if __name__ == "__main__":
    import sys
    app = App()
    if len(sys.argv) > 1:
        try:
            app.ne = NEFile(sys.argv[1])
            app.title_var.set(f"{os.path.basename(sys.argv[1])}  —  {sys.argv[1]}")
            app._populate()
        except Exception as e:
            print(f"Error: {e}")
    app.mainloop()
