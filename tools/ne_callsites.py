#!/usr/bin/env python3
"""ne_callsites.py - gdzie program NE wola dany import (analiza statyczna, bez GUI).

Parsuje relokacje segmentow NE, znajduje miejsca, w ktore loader wpisuje adres importu
MODUL.ordinal (wlacznie z lancuchami relokacji), i pokazuje deasemblacje (capstone, 16 bit)
kodu przed i po kazdym wywolaniu.

Uzycie: tools/ne_callsites.py PLIK.EXE USER.70 [USER.17 ...] [--before 40] [--after 16]
        tools/ne_callsites.py PLIK.EXE --list            # wszystkie importy z liczba wywolan
"""
import argparse
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16


class NE:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        ne = struct.unpack_from('<I', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise SystemExit('to nie jest plik NE')
        self.ne = ne
        nseg = struct.unpack_from('<H', d, ne + 0x1C)[0]
        nmod = struct.unpack_from('<H', d, ne + 0x1E)[0]
        segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
        modref = ne + struct.unpack_from('<H', d, ne + 0x28)[0]
        impnam = ne + struct.unpack_from('<H', d, ne + 0x2A)[0]
        self.align = 1 << (struct.unpack_from('<H', d, ne + 0x32)[0] or 9)
        self.mods = []
        for i in range(nmod):
            off = struct.unpack_from('<H', d, modref + i * 2)[0]
            n = d[impnam + off]
            self.mods.append(d[impnam + off + 1:impnam + off + 1 + n].decode('latin1').upper())
        self.impnam = impnam
        self.segs = []
        for i in range(nseg):
            sec, size, flags, minalloc = struct.unpack_from('<HHHH', d, segtab + i * 8)
            size = size or 0x10000
            start = sec * self.align
            data = d[start:start + size] if sec else b''
            relocs = []
            if sec and flags & 0x100:
                p = start + size
                n = struct.unpack_from('<H', d, p)[0]
                for k in range(n):
                    relocs.append(d[p + 2 + k * 8:p + 10 + k * 8])
            self.segs.append({'flags': flags, 'data': data, 'relocs': relocs})

    def import_sites(self):
        """[(modul, ordinal_albo_nazwa, nr_segmentu(1..), offset)]"""
        out = []
        for si, s in enumerate(self.segs, 1):
            data = s['data']
            for r in s['relocs']:
                atype, rtype, off, a, b = struct.unpack('<BBHHH', r)
                kind = rtype & 3
                if kind not in (1, 2):
                    continue
                mod = self.mods[a - 1] if 0 < a <= len(self.mods) else f'?{a}'
                if kind == 1:
                    what = b
                else:
                    n = self.d[self.impnam + b]
                    what = self.d[self.impnam + b + 1:self.impnam + b + 1 + n].decode('latin1')
                additive = rtype & 4
                seen = set()
                while True:                       # lancuch relokacji (nieaddytywnych)
                    if off in seen or off + 2 > len(data):
                        break
                    seen.add(off)
                    out.append((mod, what, si, off))
                    if additive:
                        break
                    nxt = struct.unpack_from('<H', data, off)[0]
                    if nxt == 0xFFFF:
                        break
                    off = nxt
        return out


def disasm(seg, off, before, after, mark):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    data = seg['data']
    # Szukamy poczatku, od ktorego deasemblacja trafia w instrukcje zawierajaca 'off'
    lines = None
    for start in range(max(0, off - before), off):
        ins = list(md.disasm(data[start:off + after], start))
        if any(i.address <= off < i.address + i.size and i.address <= mark < i.address + i.size + 2
               for i in ins):
            lines = ins
            break
    if lines is None:
        lines = list(md.disasm(data[off - 1:off + after], off - 1))
    for i in lines:
        tag = '  <==' if i.address <= off < i.address + i.size else ''
        print(f'    {i.address:04X}: {i.bytes.hex():<14} {i.mnemonic:<7} {i.op_str}{tag}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('imports', nargs='*', help='MODUL.ordinal albo MODUL.Nazwa')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--before', type=int, default=40)
    ap.add_argument('--after', type=int, default=16)
    a = ap.parse_args()
    ne = NE(a.exe)
    sites = ne.import_sites()
    if a.list or not a.imports:
        cnt = {}
        for m, w, si, off in sites:
            cnt[(m, w)] = cnt.get((m, w), 0) + 1
        for (m, w), n in sorted(cnt.items(), key=lambda x: (x[0][0], str(x[0][1]))):
            print(f'{m}.{w}: {n}')
        return
    for want in a.imports:
        m, w = want.upper().split('.', 1)
        w = int(w) if w.isdigit() else w
        hits = [(si, off) for mm, ww, si, off in sites
                if mm == m and (ww == w or (isinstance(ww, str) and str(ww).upper() == str(w)))]
        print(f'== {want}: {len(hits)} miejsc')
        for si, off in hits:
            print(f'  segment {si} offset {off - 1:04X} (fixup @ {off:04X})')
            disasm(ne.segs[si - 1], off, a.before, a.after, off - 1)


if __name__ == '__main__':
    main()
