#!/usr/bin/env python3
"""imports.py - lista importow NE (modul.ordinal) z nazwami i rozmiarem argumentow.

Nazwy/sygnatury bierze z plikow .spec Wine (dlls/*16/*.spec).
Uzycie:  tools/imports.py [--wine DIR] [--impl] PLIK.EXE [PLIK.DLL ...]
  --impl  oznacz funkcje juz eksportowane przez nasze KERNEL/USER/GDI (z Makefile)
"""
import os, re, struct, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
WINE = os.path.join(HERE, '..', '..', 'wine', 'dlls')

SPEC = {
    'KERNEL': 'krnl386.exe16', 'USER': 'user.exe16', 'GDI': 'gdi.exe16',
    'SHELL': 'shell.dll16', 'DDEML': 'ddeml.dll16', 'TOOLHELP': 'toolhelp.dll16',
    'VER': 'ver.dll16', 'MMSYSTEM': 'mmsystem.dll16', 'COMMDLG': 'commdlg.dll16',
    'KEYBOARD': 'keyboard.drv16', 'WIN87EM': 'win87em.dll16', 'SOUND': 'sound.drv16',
    'LZEXPAND': 'lzexpand.dll16', 'CTL3D': 'ctl3d.dll16', 'CTL3DV2': 'ctl3dv2.dll16',
    'SYSTEM': 'system.drv16', 'MOUSE': 'mouse.drv16', 'DISPLAY': 'display.drv16',
}
ARGSZ = {'word': 2, 's_word': 2, 'long': 4, 'ptr': 4, 'segptr': 4, 'str': 4,
         'segstr': 4, 'wstr': 4}


def load_spec(mod, wine):
    d = SPEC.get(mod)
    res = {}
    if not d:
        return res
    path = os.path.join(wine, d, d + '.spec')
    if not os.path.exists(path):
        return res
    for line in open(path, encoding='latin1'):
        line = line.split('#')[0].strip()
        m = re.match(r'(\d+)\s+(\w+)\s+((?:-\S+\s+)*)([^\s(]+)\s*(?:\(([^)]*)\))?(?:\s|$)', line)
        if not m:
            continue
        o, kind, flags, name, args = m.groups()
        if kind in ('pascal', 'cdecl', 'varargs'):
            al = args.split() if args else []
            nb = sum(ARGSZ.get(a, 0) for a in al)
            res[int(o)] = (name, kind, '-ret16' in flags, nb, ' '.join(al))
        elif kind in ('stub', 'equate', 'variable'):
            res[int(o)] = (name, kind, False, None, '')
    return res


def ne_imports(path):
    b = open(path, 'rb').read()
    ne = struct.unpack_from('<I', b, 0x3C)[0]
    u16 = lambda o: struct.unpack_from('<H', b, o)[0]
    nseg, nmod = u16(ne + 0x1C), u16(ne + 0x1E)
    segtab, modtab, imptab = ne + u16(ne + 0x22), ne + u16(ne + 0x28), ne + u16(ne + 0x2A)
    align = u16(ne + 0x32)
    pstr = lambda o: b[o + 1:o + 1 + b[o]].decode('latin1')
    mods = [pstr(imptab + u16(modtab + 2 * i)) for i in range(nmod)]
    imp = collections.defaultdict(set)
    for i in range(nseg):
        o = segtab + 8 * i
        sec, ln, fl = u16(o), u16(o + 2), u16(o + 4)
        if not (fl & 0x100 and sec):
            continue
        ro = (sec << align) + (ln or 0x10000)
        for r in range(u16(ro)):
            e = ro + 2 + 8 * r
            t = b[e + 1] & 3
            if t == 1:
                imp[mods[u16(e + 4) - 1].upper()].add(u16(e + 6))
            elif t == 2:
                imp[mods[u16(e + 4) - 1].upper()].add(pstr(imptab + u16(e + 6)))
    return imp


def implemented():
    """Ordinale eksportowane przez nasze DLL (z regul wlink w Makefile)."""
    done = collections.defaultdict(set)
    mk = os.path.join(HERE, '..', 'Makefile')
    cur = None
    for line in open(mk):
        m = re.match(r'^(\w+)\.exe:', line)
        if m:
            cur = m.group(1).upper()
        for e in re.findall(r'export\s+\w+\.(\d+)', line):
            if cur:
                done[cur].add(int(e))
    # pliki .lnk modulow w podkatalogach (np. kernel/kernel.lnk)
    import glob
    for lnk in glob.glob(os.path.join(HERE, '..', '*', '*.lnk')):
        mod = os.path.splitext(os.path.basename(lnk))[0].upper()
        for line in open(lnk):
            for e in re.findall(r'export\s+\w+\.(\d+)', line):
                done[mod].add(int(e))
    return done


def main():
    args = sys.argv[1:]
    wine, impl = WINE, False
    if '--wine' in args:
        i = args.index('--wine'); wine = args[i + 1]; del args[i:i + 2]
    if '--impl' in args:
        args.remove('--impl'); impl = True
    have = implemented() if impl else {}
    allimp = collections.defaultdict(set)
    for p in args:
        for m, s in ne_imports(p).items():
            allimp[m] |= s
    total = missing = 0
    for m in sorted(allimp):
        spec = load_spec(m, wine)
        items = sorted(allimp[m], key=lambda x: (isinstance(x, str), x))
        print(f'== {m} ({len(items)})')
        for o in items:
            total += 1
            name, kind, r16, nb, al = spec.get(o, ('?', '?', False, None, '')) if isinstance(o, int) else (o, 'name', False, None, '')
            ok = isinstance(o, int) and o in have.get(m, set())
            if impl and not ok:
                missing += 1
            mark = ('+ ' if ok else '- ') if impl else ''
            nbs = '?' if nb is None else str(nb)
            print(f'  {mark}{o:>4} {name:<28} {kind:<7} args={nbs:<3} ({al})')
    if impl:
        print(f'# razem {total}, brakuje {missing}')


if __name__ == '__main__':
    main()
