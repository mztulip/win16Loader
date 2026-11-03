#!/usr/bin/env python3
"""check_args.py - sprawdz, czy eksporty naszych DLL zdejmuja tyle bajtow
argumentow (retf N), ile podaje .spec Wine. Blad tu = rozjechany stos apki.

Uzycie: tools/check_args.py kernel.exe:KERNEL user.exe:USER gdi.exe:GDI
"""
import struct, subprocess, sys, tempfile, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imports import load_spec, WINE


def exports(b):
    ne = struct.unpack_from('<I', b, 0x3C)[0]
    u16 = lambda o: struct.unpack_from('<H', b, o)[0]
    p, ordn, out = ne + u16(ne + 4), 1, {}
    while b[p]:
        cnt, sg = b[p], b[p + 1]
        p += 2
        if sg == 0:
            ordn += cnt
            continue
        for _ in range(cnt):
            if sg == 0xFF:
                out[ordn] = (b[p + 3], u16(p + 4)); p += 6
            else:
                out[ordn] = (sg, u16(p + 1)); p += 3
            ordn += 1
    return out


def seg_data(b, n):
    ne = struct.unpack_from('<I', b, 0x3C)[0]
    u16 = lambda o: struct.unpack_from('<H', b, o)[0]
    st, al = ne + u16(ne + 0x22), u16(ne + 0x32)
    o = st + 8 * (n - 1)
    sec, ln = u16(o), u16(o + 2) or 0x10000
    return b[sec << al:(sec << al) + ln]


_dis_cache = {}


def disasm(code):
    """Cala sekcja: adres -> (dlugosc, tekst)."""
    key = id(code)
    if key in _dis_cache:
        return _dis_cache[key]
    with tempfile.NamedTemporaryFile(delete=False) as t:
        t.write(code)
    out = subprocess.run(['ndisasm', '-b16', t.name], capture_output=True, text=True).stdout
    os.unlink(t.name)
    ins = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        ins[int(parts[0], 16)] = (len(parts[1]) // 2, parts[2])
    _dis_cache[key] = ins
    return ins


def retf_values(code, off):
    """Wszystkie retf N osiagalne od off (skoki tak, call nie). None = brak."""
    ins = disasm(code)
    # ndisasm synchronizuje sie od 0 - jesli off nie jest poczatkiem instrukcji, deasembluj od off
    if off not in ins:
        sub = code[off:]
        with tempfile.NamedTemporaryFile(delete=False) as t:
            t.write(sub)
        out = subprocess.run(['ndisasm', '-b16', '-o', str(off), t.name],
                             capture_output=True, text=True).stdout
        os.unlink(t.name)
        for line in out.splitlines():
            parts = line.split(None, 2)
            if len(parts) == 3:
                ins.setdefault(int(parts[0], 16), (len(parts[1]) // 2, parts[2]))
    seen, work, vals = set(), [off], set()
    while work and len(seen) < 4000:
        a = work.pop()
        while a in ins and a not in seen:
            seen.add(a)
            ln, txt = ins[a]
            m = re.match(r'retf\s*(?:word\s+)?(0x[0-9a-f]+)?\s*$', txt)
            if m:
                vals.add(int(m.group(1), 16) if m.group(1) else 0)
                break
            if txt.startswith(('ret', 'iret', 'jmp far', 'jmp word far', 'jmp dword')):
                break
            m = re.match(r'(j\w+)\s+(?:short\s+|near\s+)?(0x[0-9a-f]+)$', txt)
            if m:
                tgt = int(m.group(2), 16)
                if m.group(1) == 'jmp':
                    a = tgt
                    continue
                work.append(tgt)
            elif txt.startswith('jmp'):
                break                      # skok posredni
            a += ln
    return vals or None


def main():
    bad = 0
    for arg in sys.argv[1:]:
        path, mod = arg.split(':')
        b = open(path, 'rb').read()
        spec = load_spec(mod, WINE)
        segs = {}
        for o, (sg, off) in sorted(exports(b).items()):
            if sg == 0xFE or o not in spec or spec[o][1] != 'pascal':
                continue
            if sg not in segs:
                segs[sg] = seg_data(b, sg)
            vals = retf_values(segs[sg], off)
            want = spec[o][3]
            if vals is None:
                print(f'?  {mod}.{o} {spec[o][0]}: brak retf (asm/rejestrowe?)')
            elif vals != {want}:
                bad += 1
                print(f'!! {mod}.{o} {spec[o][0]}: retf {sorted(vals)}, oczekiwano {want} ({spec[o][4]})')
    print(f'# bledow: {bad}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
