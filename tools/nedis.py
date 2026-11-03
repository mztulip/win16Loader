#!/usr/bin/env python3
"""nedis.py - deasemblacja segmentu pliku NE (ndisasm) z adnotacja fixupow.

Uzycie: tools/nedis.py PLIK SEGMENT [OFFSET] [--before N] [--after N]
  SEGMENT: numer segmentu (1-based); OFFSET hex: pokaz okno wokol adresu.
Importy w adnotacjach maja nazwy z .spec Wine (patrz imports.py).
"""
import struct, subprocess, sys, tempfile, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imports import load_spec, WINE


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opts = dict(a[2:].split('=') for a in sys.argv[1:] if a.startswith('--') and '=' in a)
    path, segn = args[0], int(args[1])
    center = int(args[2], 16) if len(args) > 2 else None
    before = int(opts.get('before', 0x30), 0)
    after = int(opts.get('after', 0x40), 0)
    b = open(path, 'rb').read()
    ne = struct.unpack_from('<I', b, 0x3C)[0]
    u16 = lambda o: struct.unpack_from('<H', b, o)[0]
    segtab, modtab, imptab = ne + u16(ne + 0x22), ne + u16(ne + 0x28), ne + u16(ne + 0x2A)
    nmod, align = u16(ne + 0x1E), u16(ne + 0x32)
    pstr = lambda o: b[o + 1:o + 1 + b[o]].decode('latin1')
    mods = [pstr(imptab + u16(modtab + 2 * i)) for i in range(nmod)]
    o = segtab + 8 * (segn - 1)
    sec, ln, fl = u16(o), u16(o + 2), u16(o + 4)
    ln = ln or 0x10000
    data = bytearray(b[sec << align: (sec << align) + ln])
    notes = {}
    specs = {}
    if fl & 0x100:
        ro = (sec << align) + ln
        for r in range(u16(ro)):
            e = ro + 2 + 8 * r
            at, rt, off, t1, t2 = b[e], b[e + 1], u16(e + 2), u16(e + 4), u16(e + 6)
            kind = rt & 3
            if kind == 1:
                m = mods[t1 - 1].upper()
                if m not in specs:
                    specs[m] = load_spec(m, WINE)
                nm = specs[m].get(t2, ('?',))[0]
                txt = f'{m}.{t2} {nm}'
            elif kind == 0:
                txt = f'seg{t1}:{t2:04X}' if t1 != 0xFF else f'entry#{t2}'
            else:
                continue
            # lancuch (non-additive)
            cur, n = off, 0
            while cur < len(data) - 1 and n < 4096:
                notes[cur] = f'{txt} (type {at})'
                if rt & 4:
                    break
                nxt = data[cur] | (data[cur + 1] << 8)
                if nxt == 0xFFFF or nxt == cur:
                    break
                cur, n = nxt, n + 1
    with tempfile.NamedTemporaryFile(delete=False) as t:
        t.write(data)
    out = subprocess.run(['ndisasm', '-b16', t.name], capture_output=True, text=True).stdout
    os.unlink(t.name)
    for line in out.splitlines():
        addr = int(line[:8], 16)
        if center is not None and not (center - before <= addr <= center + after):
            continue
        n = line.split()
        size = len(n[1]) // 2 if len(n) > 1 else 1
        ann = [notes[a] for a in range(addr, addr + size) if a in notes]
        mark = '>>' if addr == center else '  '
        print(mark + line + ('   ; ' + ', '.join(ann) if ann else ''))


if __name__ == '__main__':
    main()
