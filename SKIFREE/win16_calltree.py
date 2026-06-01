#!/usr/bin/env python3
"""
win16_calltree.py — Win16 NE executable call tree analyzer

Usage:
    python win16_calltree.py [FILE.EXE]

Requires:
    pip install capstone Pillow   (Pillow only for ne_explorer_gui import)
"""

import sys
import os
import re
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ne_explorer_gui import NEFile, scan_dll_dirs

try:
    from win16_ordinals import WIN16_ORDINALS
except ImportError:
    WIN16_ORDINALS = {}

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False
    X86_OP_IMM = X86_OP_MEM = None

SKIFREE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "SKI.EXE")

# ─── Dark theme ───────────────────────────────────────────────────────────────
C_BG        = "#1e1e1e"
C_BG2       = "#252526"
C_BG3       = "#2d2d30"
C_FG        = "#d4d4d4"
C_FG_DIM    = "#858585"
C_SEL_BG    = "#0e639c"
C_BORDER    = "#474747"
C_HEADER_BG = "#3c3c3c"
C_ACCENT    = "#4ec9b0"
C_BLUE      = "#9cdcfe"
C_ORANGE    = "#ce9178"
C_GREEN     = "#6a9955"
C_YELLOW    = "#dcdcaa"
C_MNEM      = "#569cd6"


# ─── Data model ───────────────────────────────────────────────────────────────

class Func:
    __slots__ = ("seg", "off", "name", "calls", "analyzed")

    def __init__(self, seg, off, name=None):
        self.seg      = seg
        self.off      = off
        self.name     = name or f"sub_{seg+1}_{off:04X}"
        self.calls    = []
        self.analyzed = False

    @property
    def key(self):
        return (self.seg, self.off)


class Call:
    __slots__ = ("kind", "seg", "off", "target", "label", "op")

    def __init__(self, kind, seg=0, off=0, target="", label="", op=""):
        self.kind   = kind    # 'near' | 'far_internal' | 'import' | 'indirect'
        self.seg    = seg
        self.off    = off
        self.target = target  # raw "MODULE.#N"
        self.label  = label   # resolved "FuncName [MODULE.#N]"
        self.op     = op      # operand string for indirect


# ─── Pseudo-C helpers ─────────────────────────────────────────────────────────

_REGS16 = {
    "ax","bx","cx","dx","si","di","sp","bp",
    "al","ah","bl","bh","cl","ch","dl","dh",
    "es","cs","ss","ds","fs","gs",
}

def _is_reg(s):
    return s.strip().lower() in _REGS16

def _mem_to_c(s, regs):
    """Convert a capstone operand string to a C-like expression."""
    s  = s.strip()
    sl = s.lower()

    # plain register
    if sl in _REGS16:
        return regs.get(sl, sl)

    # strip size prefix
    for pfx in ("word ptr ", "byte ptr ", "dword ptr ", "short "):
        if sl.startswith(pfx):
            s  = s[len(pfx):].strip()
            sl = s.lower()
            break

    # plain immediate
    if sl.startswith("0x"):
        try:
            return str(int(sl, 16))
        except ValueError:
            return s
    if sl.lstrip("-+").isdigit():
        return sl

    # [bp ± N]  — stack frame locals / args
    m = re.match(r'\[bp\s*([+-])\s*(0x[0-9a-f]+|\d+)\]', sl)
    if m:
        sign = m.group(1)
        val  = int(m.group(2), 16 if m.group(2).startswith("0x") else 10)
        if sign == "-":
            return f"local_{val:02X}"
        else:
            idx = (val - 4) // 2
            return f"arg{idx}" if idx >= 0 else f"arg_{val:02X}"

    # [reg ± N]
    m = re.match(r'\[(\w+)\s*([+-])\s*(0x[0-9a-f]+|\d+)\]', sl)
    if m:
        reg  = m.group(1).lower()
        sign = m.group(2)
        val  = int(m.group(3), 16 if m.group(3).startswith("0x") else 10)
        base = regs.get(reg, reg)
        return f"{base}[{val}]" if sign == "+" else f"({base} - {val})[0]"

    # [reg]
    m = re.match(r'\[(\w+)\]', sl)
    if m:
        reg = m.group(1).lower()
        return f"*{regs.get(reg, reg)}"

    return s


def _jcc_to_c(mnem, lhs, rhs):
    """Translate a conditional jump mnemonic + cmp operands to a C condition."""
    lhs = lhs or "?"
    rhs = rhs or "?"
    tbl = {
        "je":  f"{lhs} == {rhs}",
        "jz":  f"{lhs} == 0",
        "jne": f"{lhs} != {rhs}",
        "jnz": f"{lhs} != 0",
        "jl":  f"(int16_t){lhs} < (int16_t){rhs}",
        "jle": f"(int16_t){lhs} <= (int16_t){rhs}",
        "jg":  f"(int16_t){lhs} > (int16_t){rhs}",
        "jge": f"(int16_t){lhs} >= (int16_t){rhs}",
        "ja":  f"(uint16_t){lhs} > (uint16_t){rhs}",
        "jae": f"(uint16_t){lhs} >= (uint16_t){rhs}",
        "jb":  f"(uint16_t){lhs} < (uint16_t){rhs}",
        "jbe": f"(uint16_t){lhs} <= (uint16_t){rhs}",
        "js":  f"{lhs} < 0",
        "jns": f"{lhs} >= 0",
        "jc":  "CF",
        "jnc": "!CF",
        "jo":  "OF",
        "jno": "!OF",
    }
    return tbl.get(mnem.lower(), f"/* {mnem} */")


# ─── Analyzer ─────────────────────────────────────────────────────────────────

class CallTreeAnalyzer:
    MAX_DEPTH = 30
    MAX_INSNS = 1500   # per function (budget)

    def __init__(self, ne, ordinal_maps):
        self.ne            = ne
        self.ordinal_maps  = ordinal_maps
        self.funcs         = {}   # key -> Func
        self._seg_data     = {}
        self._reloc_maps   = {}
        self.wndproc_keys  = set()  # populated by find_wndprocs()

    # ── helpers ───────────────────────────────────────────────────────────────

    def _seg_data(self, idx):
        if idx not in self.__dict__.get("_sdata", {}):
            if not hasattr(self, "_sdata"):
                self._sdata = {}
            seg = self.ne.segments[idx]
            if seg["file_off"] and seg["cbseg"]:
                self._sdata[idx] = bytes(
                    self.ne.data[seg["file_off"]: seg["file_off"] + seg["cbseg"]]
                )
            else:
                self._sdata[idx] = b""
        return self._sdata[idx]

    def seg_data(self, idx):
        if not hasattr(self, "_sdata"):
            self._sdata = {}
        if idx not in self._sdata:
            seg = self.ne.segments[idx]
            if seg["file_off"] and seg["cbseg"]:
                self._sdata[idx] = bytes(
                    self.ne.data[seg["file_off"]: seg["file_off"] + seg["cbseg"]]
                )
            else:
                self._sdata[idx] = b""
        return self._sdata[idx]

    def reloc_map(self, idx):
        if idx not in self._reloc_maps:
            m = {}
            for r in self.ne.segments[idx]["relocs"]:
                if r["reloc_type"] in ("IMPORTORDINAL", "IMPORTNAME"):
                    # store at both instruction offset and operand offset (+1 for 9A)
                    m[r["offset"]]     = r["target"]
                    m[r["offset"] - 1] = r["target"]
            self._reloc_maps[idx] = m
        return self._reloc_maps[idx]

    def resolve(self, raw):
        """raw = "MODULE.#N"  →  "FuncName [MODULE.#N]" or raw"""
        dot = raw.find(".")
        if dot < 0:
            return raw
        mod  = raw[:dot].upper()
        func = raw[dot + 1:]
        if func.startswith("#") and func[1:].isdigit():
            name = self.ordinal_maps.get(mod, {}).get(int(func[1:]))
            if name:
                return f"{name}  [{mod}.{func}]"
        return raw

    # ── recursive analysis ────────────────────────────────────────────────────

    def analyze(self, seg_idx, off, depth=0):
        key = (seg_idx, off)
        if key in self.funcs:
            return self.funcs[key]
        if depth > self.MAX_DEPTH:
            f = Func(seg_idx, off)
            f.name += "  [depth limit]"
            f.analyzed = True
            self.funcs[key] = f
            return f

        f = Func(seg_idx, off)
        self.funcs[key] = f     # register early — breaks cycles

        data = self.seg_data(seg_idx)
        rmap = self.reloc_map(seg_idx)
        if not data or off >= len(data):
            f.analyzed = True
            return f

        cs = Cs(CS_ARCH_X86, CS_MODE_16)
        cs.detail = True

        visited  = set()
        worklist = [off]
        n_insns  = 0
        calls    = []

        while worklist and n_insns < self.MAX_INSNS:
            ip = worklist.pop(0)
            if ip in visited or ip < 0 or ip >= len(data):
                continue

            chunk = data[ip: min(ip + 512, len(data))]
            for insn in cs.disasm(chunk, ip):
                if insn.address in visited or n_insns >= self.MAX_INSNS:
                    break
                visited.add(insn.address)
                n_insns += 1
                m = insn.mnemonic.lower()

                if m in ("ret", "retf", "retn", "iret"):
                    break

                elif m == "call":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            tgt = int(op, 16) & 0xFFFF
                            calls.append(Call("near", seg=seg_idx, off=tgt))
                            self.analyze(seg_idx, tgt, depth + 1)
                        except ValueError:
                            pass
                    else:
                        calls.append(Call("indirect", op=op))

                elif m == "lcall":
                    raw = rmap.get(insn.address + 1) or rmap.get(insn.address)
                    if raw:
                        calls.append(Call("import",
                                          target=raw,
                                          label=self.resolve(raw)))
                    else:
                        calls.append(Call("indirect", op=insn.op_str))

                elif m == "jmp":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            worklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass
                    break

                elif m[0] == "j":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            worklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass

        # deduplicate (preserve order)
        seen   = set()
        unique = []
        for c in calls:
            ck = (c.kind, c.seg, c.off, c.target, c.op)
            if ck not in seen:
                seen.add(ck)
                unique.append(c)
        f.calls    = unique
        f.analyzed = True
        return f

    # ── disassembly text ──────────────────────────────────────────────────────

    def disassemble(self, seg_idx, off, max_insns=80):
        """Return list of (addr, mnem, ops, comment) tuples."""
        data = self.seg_data(seg_idx)
        rmap = self.reloc_map(seg_idx)
        if not data or off >= len(data):
            return []

        cs = Cs(CS_ARCH_X86, CS_MODE_16)
        cs.detail = True
        rows     = []
        visited  = set()
        worklist = [off]
        n        = 0

        while worklist and n < max_insns:
            ip = worklist.pop(0)
            if ip in visited or ip < 0 or ip >= len(data):
                continue
            chunk = data[ip: min(ip + 256, len(data))]
            for insn in cs.disasm(chunk, ip):
                if insn.address in visited or n >= max_insns:
                    break
                visited.add(insn.address)
                n += 1
                m   = insn.mnemonic.lower()
                cmt = ""
                if m == "lcall":
                    raw = rmap.get(insn.address + 1) or rmap.get(insn.address)
                    if raw:
                        cmt = self.resolve(raw)
                rows.append((insn.address, insn.mnemonic, insn.op_str, cmt))
                if m in ("ret", "retf", "retn", "iret"):
                    break
                if m == "jmp":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            worklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass
                    break
                if m[0] == "j":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            worklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass

        return rows

    # ── full segment scan ─────────────────────────────────────────────────────

    PROLOGUES = [
        b'\x55\x89\xe5',   # push bp / mov bp, sp
        b'\x55\x8b\xec',   # push bp / mov bp, sp  (alternate encoding)
        b'\x55\x56',       # push bp / push si
        b'\x55\x57',       # push bp / push di
        b'\x56\x57',       # push si / push di
    ]

    def scan_all(self, progress_cb=None):
        """
        Find function entry points by:
          1. NE entry table
          2. Prologue pattern scan of all CODE segments
          3. All CALL near targets already discovered
        Then analyze each candidate and return sorted list of Funcs.
        """
        candidates = set()

        # 1. Entry table
        name_map = {o: n for o, n in self.ne.exports[1:]}
        name_map.update({o: n for o, n in self.ne.nonresident[1:]})
        for e in self.ne.entries:
            si = e["seg"] - 1
            if 0 <= si < len(self.ne.segments):
                if not (self.ne.segments[si]["flags"] & 0x0001):
                    candidates.add((si, e["offset"]))

        # 2. Prologue scan
        for si in range(len(self.ne.segments)):
            seg = self.ne.segments[si]
            if seg["flags"] & 0x0001:   # skip DATA
                continue
            data = self.seg_data(si)
            for i in range(len(data) - 3):
                for p in self.PROLOGUES:
                    if data[i: i + len(p)] == p:
                        candidates.add((si, i))
                        break

        # 3. Already-known call targets (from entry analysis)
        candidates.update(self.funcs.keys())

        total = len(candidates)
        funcs = []
        for n, (si, off) in enumerate(sorted(candidates)):
            if progress_cb:
                progress_cb(n + 1, total)
            funcs.append(self.analyze(si, off))

        # apply names from entry table
        for e in self.ne.entries:
            si  = e["seg"] - 1
            off = e["offset"]
            fn  = self.funcs.get((si, off))
            if fn and e["ordinal"] in name_map:
                fn.name = name_map[e["ordinal"]]

        return sorted(funcs, key=lambda f: (f.seg, f.off))

    # ── WndProc detection ─────────────────────────────────────────────────────

    def find_wndprocs(self):
        """
        Return set of (seg, off) keys that are likely WndProc candidates.

        Heuristic 1 — DefWindowProc: any function that directly calls
        USER.DefWindowProc is almost certainly a WndProc.

        Heuristic 2 — RegisterClass: disassemble functions that call
        USER.RegisterClass and look for MOV instructions carrying an immediate
        value that matches a known function entry offset (lpfnWndProc field at
        offset +2 in WNDCLASS is filled with offset WndProc just before the
        lcall).
        """
        user_map = self.ordinal_maps.get("USER", {})

        # reverse-map name → ordinal target string "USER.#N"
        defwnd_targets = set()
        regcls_targets = set()
        for n, name in user_map.items():
            if name == "DefWindowProc":
                defwnd_targets.add(f"USER.#{n}")
            if name == "RegisterClass":
                regcls_targets.add(f"USER.#{n}")

        wndproc_keys = set()

        for fn in self.funcs.values():
            for c in fn.calls:
                if c.kind != "import":
                    continue
                if c.target in defwnd_targets:
                    wndproc_keys.add(fn.key)
                    break

        for fn in self.funcs.values():
            for c in fn.calls:
                if c.kind == "import" and c.target in regcls_targets:
                    wndproc_keys.update(self._track_wndproc_in_function(fn))
                    break

        return wndproc_keys

    def _track_wndproc_in_function(self, fn):
        """
        Disassemble *fn* linearly, find every `lcall RegisterClass`, then look
        back up to 40 instructions for `mov …, imm16` where the immediate
        matches a known function entry offset in the same segment.
        """
        if not HAS_CAPSTONE:
            return set()
        candidates  = set()
        data        = self.seg_data(fn.seg)
        rmap        = self.reloc_map(fn.seg)
        if not data or fn.off >= len(data):
            return candidates

        cs = Cs(CS_ARCH_X86, CS_MODE_16)
        cs.detail = True

        insns   = []
        visited = set()
        wklist  = [fn.off]

        while wklist:
            ip = wklist.pop(0)
            if ip in visited or ip < 0 or ip >= len(data):
                continue
            chunk = data[ip: min(ip + 512, len(data))]
            for insn in cs.disasm(chunk, ip):
                if insn.address in visited or len(insns) >= self.MAX_INSNS:
                    break
                visited.add(insn.address)
                insns.append(insn)
                m = insn.mnemonic.lower()
                if m in ("ret", "retf", "retn", "iret"):
                    break
                if m == "jmp":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            wklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass
                    break
                if m[0] == "j":
                    op = insn.op_str.strip()
                    if op.startswith("0x"):
                        try:
                            wklist.append(int(op, 16) & 0xFFFF)
                        except ValueError:
                            pass

        # offsets of all known functions in the same segment
        known_offsets = {off for (seg, off) in self.funcs if seg == fn.seg}

        user_map = self.ordinal_maps.get("USER", {})

        for i, insn in enumerate(insns):
            if insn.mnemonic.lower() != "lcall":
                continue
            raw = rmap.get(insn.address + 1) or rmap.get(insn.address)
            if not raw:
                continue
            dot = raw.find(".")
            if dot < 0 or raw[:dot].upper() != "USER":
                continue
            func_part = raw[dot + 1:]
            if not (func_part.startswith("#") and func_part[1:].isdigit()):
                continue
            if user_map.get(int(func_part[1:])) != "RegisterClass":
                continue

            # found RegisterClass call — look back for MOV imm
            for j in range(max(0, i - 40), i):
                prev = insns[j]
                if prev.mnemonic.lower() != "mov":
                    continue
                for op in prev.operands:
                    if op.type == X86_OP_IMM:
                        imm = op.imm & 0xFFFF
                        if imm in known_offsets:
                            candidates.add((fn.seg, imm))

        return candidates

    # ── pseudo-C lifter ───────────────────────────────────────────────────────

    def decompile(self, seg_idx, off, max_insns=200):
        """
        Lift 16-bit x86 to pseudo-C.
        Not a real decompiler — uses goto for all control flow — but gives a
        readable skeleton: assignments, conditions, function calls with args.
        """
        rows = self.disassemble(seg_idx, off, max_insns=max_insns)
        if not rows:
            return "/* no code */"

        fn    = self.funcs.get((seg_idx, off))
        fname = fn.name if fn else f"sub_{seg_idx+1}_{off:04X}"
        fname_c = re.sub(r'[^a-zA-Z0-9_]', '_', fname)

        # collect jump targets → need labels
        addr_set     = {r[0] for r in rows}
        jump_targets = set()
        for addr, mnem, ops, _ in rows:
            if (mnem[0].lower() == 'j' or mnem.lower() == 'loop') \
                    and ops.strip().startswith("0x"):
                try:
                    jump_targets.add(int(ops.strip(), 16))
                except ValueError:
                    pass

        lines = [
            f"/* {fname}  seg{seg_idx+1}:{off:04X} */",
            f"void {fname_c}(void)",
            "{",
        ]

        I         = "    "
        regs      = {}   # reg → C expression
        cmp_lhs   = None
        cmp_rhs   = None
        arg_stack = []   # pending push args before a call

        for addr, mnem, ops, cmt in rows:
            m  = mnem.lower()
            op = ops.strip()

            if addr in jump_targets:
                lines.append(f"  lbl_{addr:04X}:")

            # ── prologue / epilogue (skip) ──
            if m == "push" and op.lower() == "bp":
                continue
            if m == "mov" and re.match(r'bp\s*,\s*sp', op.lower()):
                continue
            if m == "pop" and op.lower() == "bp":
                continue
            if m in ("leave", "enter", "nop"):
                continue

            # ── local variable space ──
            if m == "sub" and op.lower().startswith("sp,"):
                rhs_s = op.split(",", 1)[1].strip()
                try:
                    n = int(rhs_s, 16 if rhs_s.startswith("0x") else 10)
                    count = n // 2
                    lines.append(f"{I}// {count} local variable(s)")
                except ValueError:
                    pass
                continue

            # ── return ──
            if m in ("ret", "retn", "retf"):
                rv = regs.get("ax")
                if rv and rv != "ax":
                    lines.append(f"{I}return {rv};")
                else:
                    lines.append(f"{I}return;")
                continue

            # ── unconditional jump ──
            if m == "jmp":
                if op.startswith("0x"):
                    try:
                        tgt = int(op, 16)
                        lines.append(f"{I}goto lbl_{tgt:04X};")
                    except ValueError:
                        lines.append(f"{I}// jmp {op}")
                else:
                    lines.append(f"{I}// jmp {op}  /* indirect */")
                continue

            # ── conditional jump ──
            if m[0] == "j" and m != "jmp":
                cond = _jcc_to_c(m, cmp_lhs, cmp_rhs)
                if op.startswith("0x"):
                    try:
                        tgt = int(op, 16)
                        lines.append(f"{I}if ({cond}) goto lbl_{tgt:04X};")
                    except ValueError:
                        lines.append(f"{I}// {mnem} {op}")
                else:
                    lines.append(f"{I}// {mnem} {op}")
                continue

            # ── cmp / test ──
            if m == "cmp":
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    cmp_lhs = _mem_to_c(parts[0], regs)
                    cmp_rhs = _mem_to_c(parts[1], regs)
                lines.append(f"{I}// cmp {cmp_lhs}, {cmp_rhs}")
                continue

            if m == "test":
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    cmp_lhs = _mem_to_c(parts[0], regs)
                    cmp_rhs = "0"
                lines.append(f"{I}// test {op}")
                continue

            # ── mov / movzx / movsx ──
            if m in ("mov", "movzx", "movsx"):
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    dst_s, src_s = parts
                    dst_e = _mem_to_c(dst_s, regs)
                    src_e = _mem_to_c(src_s, regs)
                    if _is_reg(dst_s):
                        regs[dst_s.lower()] = src_e
                    lines.append(f"{I}{dst_e} = {src_e};")
                else:
                    lines.append(f"{I}// {mnem} {op}")
                continue

            # ── lea ──
            if m == "lea":
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    dst_s, src_s = parts
                    inner = src_s.strip()
                    if inner.startswith("[") and inner.endswith("]"):
                        src_e = "&" + _mem_to_c(inner, regs)
                    else:
                        src_e = f"&({inner})"
                    dst_e = _mem_to_c(dst_s, regs)
                    if _is_reg(dst_s):
                        regs[dst_s.lower()] = src_e
                    lines.append(f"{I}{dst_e} = {src_e};")
                else:
                    lines.append(f"{I}// lea {op}")
                continue

            # ── push (accumulate call args) ──
            if m == "push":
                arg_stack.append(_mem_to_c(op, regs))
                continue

            # ── pop ──
            if m == "pop":
                if arg_stack:
                    arg_stack.pop()
                if _is_reg(op):
                    regs.pop(op.lower(), None)
                continue

            # ── add sp, N — cdecl caller stack clean ──
            if m == "add" and op.lower().startswith("sp,"):
                rhs_s = op.split(",", 1)[1].strip()
                try:
                    n = int(rhs_s, 16 if rhs_s.startswith("0x") else 10)
                    for _ in range(min(n // 2, len(arg_stack))):
                        arg_stack.pop()
                except ValueError:
                    pass
                continue

            # ── near call ──
            if m == "call":
                args   = ", ".join(reversed(arg_stack))
                arg_stack.clear()
                if op.startswith("0x"):
                    try:
                        tgt    = int(op, 16)
                        callee = self.funcs.get((seg_idx, tgt))
                        cname  = re.sub(r'[^a-zA-Z0-9_]', '_',
                                        callee.name if callee
                                        else f"sub_{seg_idx+1}_{tgt:04X}")
                        lines.append(f"{I}{cname}({args});")
                    except ValueError:
                        lines.append(f"{I}// call {op}")
                else:
                    lines.append(f"{I}(* {op})({args});")
                regs.pop("ax", None)
                regs.pop("dx", None)
                continue

            # ── far call (import) ──
            if m == "lcall":
                args = ", ".join(reversed(arg_stack))
                arg_stack.clear()
                if cmt:
                    cname = re.sub(r'[^a-zA-Z0-9_]', '_',
                                   cmt.split("[")[0].strip())
                    lines.append(f"{I}{cname}({args});")
                else:
                    lines.append(f"{I}// lcall {op}({args})")
                regs.pop("ax", None)
                regs.pop("dx", None)
                continue

            # ── arithmetic / logical ──
            _arith = {
                "add": "+=", "sub": "-=",
                "or":  "|=", "and": "&=", "xor": "^=",
                "shl": "<<=", "shr": ">>=", "sar": ">>=",
                "imul": "*=", "mul": "*=",
            }
            if m in _arith:
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    dst_e = _mem_to_c(parts[0], regs)
                    src_e = _mem_to_c(parts[1], regs)
                    lines.append(f"{I}{dst_e} {_arith[m]} {src_e};")
                    if _is_reg(parts[0]):
                        regs.pop(parts[0].lower(), None)
                else:
                    lines.append(f"{I}// {mnem} {op}")
                continue

            # ── inc / dec / neg / not ──
            if m in ("inc", "dec", "neg", "not"):
                expr = _mem_to_c(op, regs)
                if m == "inc":
                    lines.append(f"{I}{expr}++;")
                elif m == "dec":
                    lines.append(f"{I}{expr}--;")
                elif m == "neg":
                    lines.append(f"{I}{expr} = -{expr};")
                elif m == "not":
                    lines.append(f"{I}{expr} = ~{expr};")
                if _is_reg(op):
                    regs.pop(op.lower(), None)
                continue

            # ── xchg ──
            if m == "xchg":
                parts = [p.strip() for p in op.split(",", 1)]
                if len(parts) == 2:
                    a, b = _mem_to_c(parts[0], regs), _mem_to_c(parts[1], regs)
                    lines.append(f"{I}swap({a}, {b});")
                continue

            # ── everything else → comment ──
            suffix = f"  /* {cmt} */" if cmt else ""
            lines.append(f"{I}// {mnem} {op}{suffix}")

        lines.append("}")
        return "\n".join(lines)

    # ── miasm IR lifter ───────────────────────────────────────────────────────

    def decompile_miasm(self, seg_idx, off):
        """
        Lift 16-bit x86 to miasm IR and format as pseudo-C.
        Requires: pip install miasm
        """
        try:
            from miasm.analysis.machine import Machine
            from miasm.core.locationdb import LocationDB
            from miasm.core.bin_stream import bin_stream_str
            from miasm.expression.expression import ExprCond, ExprLoc
        except ImportError:
            return ("// miasm not installed\n"
                    "// pip install miasm")
        except Exception as e:
            return (f"// miasm failed to load: {e}\n"
                    f"// If on Python ≥ 3.12, patch venv sembuilder.py\n"
                    f"// (see win16_calltree.py comments)")

        try:
            machine = Machine("x86_16")
        except Exception as e:
            return f"// Machine('x86_16') failed: {e}"

        data = self.seg_data(seg_idx)
        if not data or off >= len(data):
            return "/* no data */"

        fn      = self.funcs.get((seg_idx, off))
        fname   = fn.name if fn else f"sub_{seg_idx+1}_{off:04X}"
        fname_c = re.sub(r'[^a-zA-Z0-9_]', '_', fname)

        loc_db = LocationDB()

        try:
            bs    = bin_stream_str(data)
            mdis  = machine.dis_engine(bs, loc_db=loc_db)
            asmcfg = mdis.dis_multiblock(off)
        except Exception as e:
            return f"/* miasm disasm error: {e} */"

        try:
            lifter = machine.lifter_model_call(loc_db)
            ircfg  = lifter.new_ircfg_from_asmcfg(asmcfg)
        except Exception as e:
            return f"/* miasm lift error: {e} */"

        # x86 flags we don't want cluttering the output
        _SKIP = {"zf","nf","pf","cf","af","of","tf","df",
                 "if_x86","iopl","vm","vif","vip","ac",
                 "exception_flags","pfmem08","reg_float_st0"}

        def _loc_addr(lbl):
            try:
                return loc_db.get_location_offset(lbl)
            except Exception:
                return None

        def _lbl_name(lbl):
            a = _loc_addr(lbl)
            return f"lbl_{a:04X}" if a is not None else str(lbl)

        def _fmt_irdst(src):
            if isinstance(src, ExprCond):
                cond  = str(src.cond)
                true_ = _lbl_name(src.src1.loc_key) if isinstance(src.src1, ExprLoc) else str(src.src1)
                fals_ = _lbl_name(src.src2.loc_key) if isinstance(src.src2, ExprLoc) else str(src.src2)
                return f"if ({cond}) goto {true_}; else goto {fals_};"
            if isinstance(src, ExprLoc):
                return f"goto {_lbl_name(src.loc_key)};"
            return f"goto {src};  // indirect"

        lines = [
            f"/* {fname}  seg{seg_idx+1}:{off:04X}  [miasm IR] */",
            f"void {fname_c}(void) {{",
        ]

        # Sort blocks by address; unresolved blocks go last
        sorted_lbls = sorted(
            ircfg.blocks.keys(),
            key=lambda l: (_loc_addr(l) is None, _loc_addr(l) or 0),
        )

        for lbl in sorted_lbls:
            irblock = ircfg.blocks[lbl]
            lines.append(f"  {_lbl_name(lbl)}:")

            prev_instr_off = None
            for assignblk in irblock:
                # Show original ASM instruction as comment
                instr = getattr(assignblk, "instr", None)
                if instr is not None:
                    ioff = getattr(instr, "offset", None)
                    if ioff is not None and ioff != prev_instr_off:
                        prev_instr_off = ioff
                        lines.append(f"    // {ioff:04X}: {instr}")

                for dst, src in assignblk.items():
                    dst_s = str(dst)
                    src_s = str(src)
                    # skip identity and flags
                    if dst_s == src_s:
                        continue
                    if dst_s.lower() in _SKIP:
                        continue
                    if dst_s.startswith("exception"):
                        continue
                    if dst_s == "IRDst":
                        lines.append(f"    {_fmt_irdst(src)}")
                        continue
                    lines.append(f"    {dst_s} = {src_s};")

        lines.append("}")
        return "\n".join(lines)

    # ── text / dot export ─────────────────────────────────────────────────────

    def tree_as_text(self, root_fn, max_depth=10):
        lines = []

        def walk(fn, prefix, depth, visited):
            if depth > max_depth:
                return
            tag = ""
            if fn.key in visited:
                tag = " [↺]"
            lines.append(f"{prefix}{fn.name}  (seg{fn.seg+1}:{fn.off:04X}){tag}")
            if fn.key in visited or not fn.calls:
                return
            visited = visited | {fn.key}
            for i, c in enumerate(fn.calls):
                last = (i == len(fn.calls) - 1)
                br   = "└── " if last else "├── "
                cont = "    " if last else "│   "
                if c.kind == "import":
                    lines.append(f"{prefix}{br}{c.label}")
                elif c.kind == "indirect":
                    lines.append(f"{prefix}{br}call {c.op}  [indirect]")
                else:
                    child = self.funcs.get((c.seg, c.off))
                    if child:
                        walk(child, prefix + cont, depth + 1, visited)

        walk(root_fn, "", 0, set())
        return "\n".join(lines)

    def as_dot(self):
        lines = ['digraph calltree {',
                 '  rankdir=LR;',
                 '  node [fontname="Courier" fontsize=10 style=filled fillcolor="#252526" '
                 'fontcolor="#d4d4d4" color="#474747"];',
                 '  edge [color="#858585"];']
        def nid(fn):
            return f"f_{fn.seg}_{fn.off:04X}"

        for fn in self.funcs.values():
            label = fn.name.replace('"', '\\"')
            if fn.key in self.wndproc_keys:
                label += "  [WndProc?]"
                extra = 'fillcolor="#3a2000" fontcolor="#ff8c00" color="#ff8c00"'
            else:
                extra = ''
            lines.append(f'  {nid(fn)} [label="{label}" shape=box {extra}];')

        added_edges = set()
        for fn in self.funcs.values():
            for c in fn.calls:
                if c.kind == "import":
                    imp_id = "imp_" + c.target.replace(".", "_").replace("#", "ord")
                    edge   = (nid(fn), imp_id)
                    if edge not in added_edges:
                        added_edges.add(edge)
                        lbl = c.label.replace('"', '\\"').split("[")[0].strip()
                        lines.append(
                            f'  {imp_id} [label="{lbl}" shape=ellipse '
                            f'fillcolor="#1a2e1a" fontcolor="#b5cea8"];'
                        )
                        lines.append(f'  {nid(fn)} -> {imp_id};')
                elif c.kind in ("near", "far_internal"):
                    child = self.funcs.get((c.seg, c.off))
                    if child:
                        edge = (nid(fn), nid(child))
                        if edge not in added_edges:
                            added_edges.add(edge)
                            lines.append(f'  {nid(fn)} -> {nid(child)};')

        lines.append("}")
        return "\n".join(lines)


# ─── GUI ──────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Win16 Call Tree")
        self.geometry("1300x850")
        self.configure(bg=C_BG)
        self.ne            = None
        self.analyzer      = None
        self.root_fn       = None
        self._wndproc_keys = set()
        self._cur_fn_key   = None   # (seg_idx, off) last selected
        self._view_mode    = "asm"  # "asm" | "c"
        self._apply_theme()
        self._build_ui()

    def _apply_theme(self):
        s = ttk.Style(self)
        s.theme_use("clam")
        s.configure(".", background=C_BG, foreground=C_FG,
            fieldbackground=C_BG2, troughcolor=C_BG3,
            bordercolor=C_BORDER, relief="flat",
            darkcolor=C_BG, lightcolor=C_BG3)
        s.configure("TFrame",    background=C_BG)
        s.configure("TLabel",    background=C_BG, foreground=C_FG)
        s.configure("TScrollbar", background=C_BG3, troughcolor=C_BG2, arrowcolor=C_FG)
        s.configure("TPanedwindow", background=C_BORDER)
        s.configure("Treeview",
            background=C_BG2, foreground=C_FG, fieldbackground=C_BG2, rowheight=20)
        s.configure("Treeview.Heading",
            background=C_HEADER_BG, foreground=C_FG, relief="flat")
        s.map("Treeview",
            background=[("selected", C_SEL_BG)],
            foreground=[("selected", "#ffffff")])

    def _build_ui(self):
        # ── menu ──────────────────────────────────────────────────────────────
        mb = tk.Menu(self, bg=C_BG3, fg=C_FG, tearoff=0)
        fm = tk.Menu(mb, bg=C_BG3, fg=C_FG, activebackground=C_SEL_BG,
                     activeforeground="#fff", tearoff=0)
        fm.add_command(label="Open…",            accelerator="Ctrl+O", command=self.open_file)
        fm.add_separator()
        fm.add_command(label="Export tree as text…",    command=self.export_text)
        fm.add_command(label="Export as DOT (graphviz)…", command=self.export_dot)
        fm.add_separator()
        fm.add_command(label="Quit", command=self.quit)
        mb.add_cascade(label="File", menu=fm)
        self.config(menu=mb)
        self.bind("<Control-o>", lambda e: self.open_file())
        self.bind("<q>",         lambda e: self.quit())

        # ── toolbar ───────────────────────────────────────────────────────────
        toolbar = tk.Frame(self, bg=C_BG3)
        toolbar.pack(fill="x")

        self.status = tk.StringVar(value="No file loaded  —  Ctrl+O to open")
        tk.Label(toolbar, textvariable=self.status, anchor="w",
                 font=("Courier", 10, "bold"),
                 bg=C_BG3, fg=C_ACCENT, pady=4, padx=6).pack(side="left", fill="x", expand=True)

        self.scan_btn = tk.Button(toolbar, text="Scan All Functions",
            bg=C_BG3, fg=C_ACCENT, activebackground=C_SEL_BG,
            activeforeground="#fff", relief="flat", padx=10, pady=3,
            font=("Courier", 9, "bold"),
            command=self._do_scan_all, state="disabled")
        self.scan_btn.pack(side="right", padx=6, pady=2)

        tk.Button(toolbar, text="Expand All",
            bg=C_BG3, fg=C_FG, activebackground=C_SEL_BG,
            activeforeground="#fff", relief="flat", padx=10, pady=3,
            font=("Courier", 9),
            command=self._expand_all).pack(side="right", padx=2, pady=2)

        tk.Button(toolbar, text="Collapse All",
            bg=C_BG3, fg=C_FG, activebackground=C_SEL_BG,
            activeforeground="#fff", relief="flat", padx=10, pady=3,
            font=("Courier", 9),
            command=self._collapse_all).pack(side="right", padx=2, pady=2)

        # ── main pane ─────────────────────────────────────────────────────────
        pane = ttk.PanedWindow(self, orient="horizontal")
        pane.pack(fill="both", expand=True, padx=4, pady=4)

        # Left — call tree
        left = ttk.Frame(pane)
        pane.add(left, weight=2)
        tk.Label(left, text="  Call Tree", bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("Courier", 9, "bold"), anchor="w").pack(fill="x")

        tv = ttk.Treeview(left, columns=["addr"], show="tree headings")
        tv.heading("#0",   text="Function / Import")
        tv.heading("addr", text="Address")
        tv.column("#0",   width=330, anchor="w")
        tv.column("addr", width=110, anchor="w")
        sb_y = ttk.Scrollbar(left, orient="vertical",   command=tv.yview)
        sb_x = ttk.Scrollbar(left, orient="horizontal", command=tv.xview)
        tv.config(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)
        sb_y.pack(side="right",  fill="y")
        sb_x.pack(side="bottom", fill="x")
        tv.pack(fill="both", expand=True)
        tv.bind("<<TreeviewSelect>>", self._on_select)
        tv.tag_configure("entry",    foreground=C_ACCENT)
        tv.tag_configure("internal", foreground=C_BLUE)
        tv.tag_configure("import",   foreground=C_ORANGE)
        tv.tag_configure("indirect", foreground=C_FG_DIM)
        tv.tag_configure("cycle",    foreground=C_FG_DIM)
        tv.tag_configure("wndproc",  foreground="#ff8c00")
        self.tv = tv

        # Right — disassembly / C
        right = ttk.Frame(pane)
        pane.add(right, weight=3)

        right_hdr = tk.Frame(right, bg=C_HEADER_BG)
        right_hdr.pack(fill="x")
        tk.Label(right_hdr, text="  Disassembly", bg=C_HEADER_BG, fg=C_ACCENT,
                 font=("Courier", 9, "bold"), anchor="w").pack(side="left")

        _btn_kw = dict(font=("Courier", 9, "bold"),
                       activebackground=C_SEL_BG, activeforeground="#fff",
                       padx=6, pady=1)
        self._btn_miasm = tk.Button(right_hdr, text=" C miasm ", bg=C_BG3, fg=C_FG,
                                    relief="flat", **_btn_kw,
                                    command=lambda: self._set_view_mode("miasm"))
        self._btn_c     = tk.Button(right_hdr, text=" C ", bg=C_BG3, fg=C_FG,
                                    relief="flat", **_btn_kw,
                                    command=lambda: self._set_view_mode("c"))
        self._btn_asm   = tk.Button(right_hdr, text=" ASM ", bg=C_SEL_BG, fg="#fff",
                                    relief="sunken", **_btn_kw,
                                    command=lambda: self._set_view_mode("asm"))
        self._btn_miasm.pack(side="right", padx=2, pady=2)
        self._btn_c.pack(side="right",     padx=2, pady=2)
        self._btn_asm.pack(side="right",   padx=2, pady=2)

        txt = tk.Text(right, font=("Courier", 10), wrap="none",
                      bg=C_BG2, fg=C_FG, relief="flat",
                      selectbackground=C_SEL_BG, selectforeground="#fff",
                      insertbackground=C_FG)
        txt.tag_configure("hdr",     foreground=C_ACCENT)
        txt.tag_configure("addr",    foreground=C_FG_DIM)
        txt.tag_configure("mnem",    foreground=C_MNEM)
        txt.tag_configure("ops",     foreground=C_FG)
        txt.tag_configure("cmt",     foreground=C_GREEN)
        txt.tag_configure("kw",      foreground=C_MNEM)    # C keywords
        txt.tag_configure("call_c",  foreground=C_YELLOW)  # function calls in C view
        txt.tag_configure("comment", foreground=C_GREEN)   # // comments
        sb2y = ttk.Scrollbar(right, orient="vertical",   command=txt.yview)
        sb2x = ttk.Scrollbar(right, orient="horizontal", command=txt.xview)
        txt.config(yscrollcommand=sb2y.set, xscrollcommand=sb2x.set)
        sb2y.pack(side="right",  fill="y")
        sb2x.pack(side="bottom", fill="x")
        txt.pack(fill="both", expand=True)
        self.asm = txt

    # ── file open ─────────────────────────────────────────────────────────────

    def open_file(self):
        path = filedialog.askopenfilename(
            title="Open Win16 NE executable",
            filetypes=[("Win16 executables", "*.exe *.dll *.drv"), ("All files", "*.*")],
        )
        if path:
            self._load(path)

    def _load(self, path):
        if not HAS_CAPSTONE:
            messagebox.showerror("Missing dependency",
                "capstone is required:\n\n  pip install capstone")
            return
        self.status.set(f"Loading {os.path.basename(path)}…")
        self.update()
        try:
            self.ne = NEFile(path)
        except Exception as e:
            self.status.set(f"Error: {e}")
            return

        # build ordinal map: built-in + scan same dir
        omap = {k: dict(v) for k, v in WIN16_ORDINALS.items()}
        for mod, names in scan_dll_dirs([os.path.dirname(path)]).items():
            omap.setdefault(mod, {}).update(names)

        self.analyzer      = CallTreeAnalyzer(self.ne, omap)
        self._wndproc_keys = set()

        # entry point from NE header (CS is 1-based)
        ne    = self.ne.ne
        si    = ne["ne_cs"] - 1
        off   = ne["ne_ip"]

        if si < 0 or si >= len(self.ne.segments):
            self.status.set("Invalid entry point in NE header")
            return

        self.status.set(f"Analyzing {os.path.basename(path)}  —  entry seg{si+1}:{off:04X} …")
        self.update()

        # analyze from entry point
        self.root_fn = self.analyzer.analyze(si, off)
        self.root_fn.name = "Entry"

        # also analyze exported functions from entry table and name them
        name_map = {o: n for o, n in self.ne.exports[1:]}
        name_map.update({o: n for o, n in self.ne.nonresident[1:]})
        for e in self.ne.entries:
            eidx = e["seg"] - 1
            eoff = e["offset"]
            if 0 <= eidx < len(self.ne.segments):
                fn = self.analyzer.analyze(eidx, eoff)
                if e["ordinal"] in name_map:
                    fn.name = name_map[e["ordinal"]]

        self._populate_tree()
        self.scan_btn.config(state="normal")
        n = len(self.analyzer.funcs)
        self.status.set(
            f"{os.path.basename(path)}  —  {n} functions from entry  "
            f"(seg{si+1}:{off:04X})  —  click 'Scan All Functions' for full scan"
        )

    # ── scan all ──────────────────────────────────────────────────────────────

    def _do_scan_all(self):
        if not self.analyzer:
            return
        self.scan_btn.config(state="disabled")
        self.status.set("Scanning all CODE segments for function prologues…")
        self.update()

        done   = [0]
        total  = [1]

        def progress(n, t):
            done[0]  = n
            total[0] = t
            if n % 20 == 0:
                self.status.set(f"Analyzing functions… {n}/{t}")
                self.update()

        all_funcs = self.analyzer.scan_all(progress_cb=progress)
        self._wndproc_keys = self.analyzer.find_wndprocs()
        self.analyzer.wndproc_keys = self._wndproc_keys
        self._populate_tree(all_funcs)
        n     = len(all_funcs)
        n_wnd = len(self._wndproc_keys)
        wnd_info = f"  {n_wnd} WndProc candidate{'s' if n_wnd != 1 else ''}" if n_wnd else ""
        self.status.set(
            f"Full scan complete — {n} functions found{wnd_info}"
        )

    # ── populate tree ─────────────────────────────────────────────────────────

    def _populate_tree(self, all_funcs=None):
        tv = self.tv
        tv.delete(*tv.get_children())
        if not self.root_fn:
            return

        # always show entry call tree first
        self._insert_node("", self.root_fn, set(), is_root=True)

        if not all_funcs:
            return

        # separator
        tv.insert("", "end", text="─── All Functions ───────────────────────",
                  values=("",), tags=("sep",), open=False)
        tv.tag_configure("sep", foreground=C_FG_DIM)

        # group by segment
        by_seg = {}
        for fn in all_funcs:
            by_seg.setdefault(fn.seg, []).append(fn)

        entry_key = self.root_fn.key

        for si in sorted(by_seg):
            seg     = self.analyzer.ne.segments[si]
            is_data = bool(seg["flags"] & 0x0001)
            seg_hdr = tv.insert("", "end",
                text=f"  Segment {si+1}  ({'DATA' if is_data else 'CODE'})  "
                     f"{len(by_seg[si])} functions",
                values=("",), tags=("seg_hdr",), open=True)
            tv.tag_configure("seg_hdr", foreground=C_YELLOW)

            for fn in by_seg[si]:
                n_calls   = len([c for c in fn.calls if c.kind != "indirect"])
                n_imports = len([c for c in fn.calls if c.kind == "import"])
                n_ind     = len([c for c in fn.calls if c.kind == "indirect"])
                info = f"{n_calls} calls"
                if n_imports:
                    info += f"  {n_imports} imports"
                if n_ind:
                    info += f"  {n_ind} indirect"
                if fn.key == entry_key:
                    tag = "entry"
                elif fn.key in self._wndproc_keys:
                    tag = "wndproc"
                else:
                    tag = "internal"
                label = fn.name
                if fn.key in self._wndproc_keys:
                    label += "  [WndProc?]"
                fn_iid = tv.insert(seg_hdr, "end",
                    text=label,
                    values=(f"seg{fn.seg+1}:{fn.off:04X}  [{info}]",),
                    tags=(tag,), open=False)
                # add calls as children (collapsed)
                for c in fn.calls:
                    if c.kind == "import":
                        tv.insert(fn_iid, "end",
                            text=c.label or c.target, values=("",), tags=("import",))
                    elif c.kind == "indirect":
                        tv.insert(fn_iid, "end",
                            text=f"call {c.op}", values=("indirect",), tags=("indirect",))
                    else:
                        child = self.analyzer.funcs.get((c.seg, c.off))
                        if child:
                            tv.insert(fn_iid, "end",
                                text=child.name,
                                values=(f"seg{child.seg+1}:{child.off:04X}",),
                                tags=("internal",))

    def _insert_node(self, parent_iid, fn, visited, depth=0, is_root=False):
        addr  = f"seg{fn.seg+1}:{fn.off:04X}"
        cycle = fn.key in visited
        is_wnd = fn.key in self._wndproc_keys
        if is_root:
            tag = "entry"
        elif is_wnd:
            tag = "wndproc"
        else:
            tag = "internal"
        suffix = ("  [↺]" if cycle else "") + ("  [WndProc?]" if is_wnd else "")
        tv  = self.tv
        iid = tv.insert(parent_iid, "end",
                    text=fn.name + suffix,
                    values=(addr,),
                    tags=(tag,),
                    open=(depth < 2 and not cycle))

        if cycle or not fn.calls:
            return

        new_visited = visited | {fn.key}
        seen_here   = set()

        for c in fn.calls:
            if c.kind == "import":
                key2 = ("import", c.target)
                if key2 in seen_here:
                    continue
                seen_here.add(key2)
                tv.insert(iid, "end",
                    text=c.label or c.target,
                    values=("",),
                    tags=("import",))

            elif c.kind == "indirect":
                tv.insert(iid, "end",
                    text=f"call {c.op}",
                    values=("indirect",),
                    tags=("indirect",))

            else:  # near / far_internal
                key2 = (c.seg, c.off)
                if key2 in seen_here:
                    continue
                seen_here.add(key2)
                child = self.analyzer.funcs.get(key2)
                if child:
                    self._insert_node(iid, child, new_visited, depth + 1)

    # ── view mode toggle ──────────────────────────────────────────────────────

    def _set_view_mode(self, mode):
        self._view_mode = mode
        btns = {"asm": self._btn_asm, "c": self._btn_c, "miasm": self._btn_miasm}
        for m, btn in btns.items():
            if m == mode:
                btn.config(relief="sunken", bg=C_SEL_BG, fg="#fff")
            else:
                btn.config(relief="flat", bg=C_BG3, fg=C_FG)
        self._render_view()

    # ── expand / collapse ─────────────────────────────────────────────────────

    def _expand_all(self):
        def expand(iid):
            self.tv.item(iid, open=True)
            for child in self.tv.get_children(iid):
                expand(child)
        for iid in self.tv.get_children():
            expand(iid)

    def _collapse_all(self):
        def collapse(iid):
            self.tv.item(iid, open=False)
            for child in self.tv.get_children(iid):
                collapse(child)
        for iid in self.tv.get_children():
            collapse(iid)

    # ── disassembly / C panel ─────────────────────────────────────────────────

    def _on_select(self, event):
        sel = self.tv.selection()
        if not sel:
            return
        vals = self.tv.item(sel[0], "values")
        if not vals or not vals[0] or vals[0] == "indirect":
            return
        addr = vals[0].split()[0]   # strip trailing "[N calls …]"
        try:
            seg_s, off_s = addr.split(":")
            si  = int(seg_s[3:]) - 1
            off = int(off_s, 16)
        except Exception:
            return
        self._cur_fn_key = (si, off)
        self._render_view()

    def _render_view(self):
        if not self._cur_fn_key or not self.analyzer:
            return
        si, off = self._cur_fn_key
        fn = self.analyzer.funcs.get((si, off))
        if self._view_mode == "c":
            self._show_c(si, off, fn)
        elif self._view_mode == "miasm":
            self._show_miasm(si, off, fn)
        else:
            self._show_asm(si, off, fn)

    def _show_asm(self, si, off, fn):
        name = fn.name if fn else f"seg{si+1}:{off:04X}"
        rows = self.analyzer.disassemble(si, off)
        txt  = self.asm
        txt.config(state="normal")
        txt.delete("1.0", "end")
        txt.insert("end", f"  {name}  (seg{si+1}:{off:04X})\n\n", "hdr")
        for a, mnem, ops, cmt in rows:
            txt.insert("end", f"  {a:04X}  ", "addr")
            txt.insert("end", f"{mnem:<8} ", "mnem")
            txt.insert("end", ops, "ops")
            if cmt:
                txt.insert("end", f"   ; {cmt}", "cmt")
            txt.insert("end", "\n")
        txt.config(state="disabled")

    def _show_c(self, si, off, fn):
        code = self.analyzer.decompile(si, off)
        txt  = self.asm
        txt.config(state="normal")
        txt.delete("1.0", "end")

        _KW = re.compile(
            r'\b(void|return|if|goto|int|uint16_t|int16_t|swap|unsigned)\b')
        _CALL = re.compile(r'\b([A-Za-z_]\w*)\s*(?=\()')
        _CMT  = re.compile(r'(//.*)')
        _LBL  = re.compile(r'^(\s*lbl_[0-9a-fA-F]+:)')

        for line in code.splitlines():
            # label lines
            lm = _LBL.match(line)
            if lm:
                txt.insert("end", line + "\n", "addr")
                continue
            # comment lines
            if line.strip().startswith("//"):
                txt.insert("end", line + "\n", "comment")
                continue
            # first line (/* … */) → header
            if line.startswith("/*"):
                txt.insert("end", line + "\n", "hdr")
                continue
            # function signature
            if re.match(r'^void ', line):
                for tok in re.split(r'(\bvoid\b)', line):
                    if tok == "void":
                        txt.insert("end", tok, "kw")
                    else:
                        txt.insert("end", tok)
                txt.insert("end", "\n")
                continue
            # normal line — highlight keywords, call names, inline comments
            rest = line
            pos  = 0
            # find inline comment first
            cmt_m = _CMT.search(rest)
            code_part = rest[:cmt_m.start()] if cmt_m else rest
            # tokenise code part
            for m in re.finditer(r'(\b(?:void|return|if|goto|int|uint16_t|int16_t|unsigned)\b)'
                                  r'|([A-Za-z_]\w*(?=\s*\())'
                                  r'|(\S+|\s+)', code_part):
                tok = m.group(0)
                if m.group(1):
                    txt.insert("end", tok, "kw")
                elif m.group(2):
                    txt.insert("end", tok, "call_c")
                else:
                    txt.insert("end", tok, "ops")
            if cmt_m:
                txt.insert("end", cmt_m.group(1), "comment")
            txt.insert("end", "\n")

        txt.config(state="disabled")

    def _show_miasm(self, si, off, fn):
        txt = self.asm
        txt.config(state="normal")
        txt.delete("1.0", "end")
        name = fn.name if fn else f"seg{si+1}:{off:04X}"
        txt.insert("end", f"  Computing miasm IR for {name}…\n", "hdr")
        txt.config(state="disabled")
        self.update()

        code = self.analyzer.decompile_miasm(si, off)

        # colour rules:
        #   lbl_XXXX:            → addr (dim)
        #   // XXXX: asm_instr   → asm comment (green, slightly dimmer)
        #   if / goto / else     → keyword (blue)
        #   REGISTER = EXPR;     → register name in accent, rest ops
        #   /* … */              → hdr (header)
        _LBL    = re.compile(r'^\s+lbl_[0-9a-fA-F]+:')
        _ASMCMT = re.compile(r'^(\s+//\s+[0-9a-fA-F]{4}:)(.*)')
        _IRDST  = re.compile(r'^\s+(if\s.*|goto\s.*)')
        _ASSIGN = re.compile(r'^(\s+)(\S+)(\s*=\s*)(.*)$')

        txt.config(state="normal")
        txt.delete("1.0", "end")

        for line in code.splitlines():
            # /* … */ header
            if line.startswith("/*"):
                txt.insert("end", line + "\n", "hdr")
                continue
            # void … {
            if re.match(r'^void |^}$', line):
                for tok in re.split(r'(\bvoid\b)', line):
                    txt.insert("end", tok, "kw" if tok == "void" else "ops")
                txt.insert("end", "\n")
                continue
            # label
            if _LBL.match(line):
                txt.insert("end", line + "\n", "addr")
                continue
            # // XXXX: asm instruction
            am = _ASMCMT.match(line)
            if am:
                txt.insert("end", am.group(1), "addr")
                txt.insert("end", am.group(2) + "\n", "comment")
                continue
            # if (…) goto …; else goto …;  /  goto …;
            if _IRDST.match(line):
                for tok in re.split(r'(\bif\b|\bgoto\b|\belse\b)', line):
                    if tok in ("if", "goto", "else"):
                        txt.insert("end", tok, "kw")
                    else:
                        txt.insert("end", tok, "ops")
                txt.insert("end", "\n")
                continue
            # REG = EXPR;
            am = _ASSIGN.match(line)
            if am:
                ind, dst, eq, src = am.group(1), am.group(2), am.group(3), am.group(4)
                txt.insert("end", ind)
                txt.insert("end", dst, "mnem")   # blue — register / memory dst
                txt.insert("end", eq,  "ops")
                txt.insert("end", src + "\n", "ops")
                continue
            # fallback
            txt.insert("end", line + "\n", "ops")

        txt.config(state="disabled")

    # ── export ────────────────────────────────────────────────────────────────

    def export_text(self):
        if not self.root_fn:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text", "*.txt"), ("All", "*.*")])
        if not path:
            return
        with open(path, "w") as f:
            f.write(self.analyzer.tree_as_text(self.root_fn))
        self.status.set(f"Saved: {path}")

    def export_dot(self):
        if not self.analyzer:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".dot",
            filetypes=[("Graphviz DOT", "*.dot"), ("All", "*.*")])
        if not path:
            return
        with open(path, "w") as f:
            f.write(self.analyzer.as_dot())
        self.status.set(f"Saved: {path}  (render with: dot -Tsvg {os.path.basename(path)} -o out.svg)")


# ─── entry ────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    app = App()
    path = sys.argv[1] if len(sys.argv) > 1 else (
        SKIFREE_PATH if os.path.isfile(SKIFREE_PATH) else None
    )
    if path:
        app.after(100, lambda: app._load(path))
    app.mainloop()
