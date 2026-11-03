#!/usr/bin/env python3
"""gen_lnk.py - lista eksportow DLL (dyrektywy wlink) z definicji funkcji API.

Szuka w plikach .c definicji "... API Nazwa(" i przypisuje ordinal z .spec Wine
(po nazwie, bez rozrozniania wielkosci liter). Prywatne funkcje i aliasy - w
tabelach PRIVATE/ALIAS ponizej.

Uzycie: tools/gen_lnk.py MODUL plik1.c plik2.c ... > modul.lnk
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imports import load_spec, WINE

# Prywatne eksporty (USER <-> GDI), ordinale poza zakresem Windows
PRIVATE = {
    'GDI': {'GdiMoveCursor': 900, 'GdiShowCursor': 901, 'GdiSetCursor': 902,
            'GdiCursorDefault': 903, 'GdiCreateWindowDC': 910, 'GdiSetDCVis': 911,
            'GdiIntersectVis': 912, 'GdiDCWindow': 913, 'GdiGetRgnRects': 914},
    'USER': {'UserIconFromDIB': 950, 'UserIconSize': 951},
}
# Dodatkowe ordinale wskazujace na te sama funkcje: nazwa_eksportu -> funkcja
ALIAS = {
    'GDI': {'RectVisibleOld': 'RectVisible', 'GDISelectPalette': 'GDISelectPalette'},
    'USER': {},
}
SKIP = {'LibMain'}


def main():
    mod = sys.argv[1].upper()
    spec = load_spec(mod, WINE)
    byname = {}
    for o, v in spec.items():
        byname.setdefault(v[0].lower(), o)
    funcs = []
    for path in sys.argv[2:]:
        for line in open(path, encoding='latin1'):
            m = re.match(r'^[A-Za-z_].*\bAPI\s+(\w+)\s*\(', line)
            if m and not line.rstrip().endswith(';'):
                funcs.append(m.group(1))
    out, used = [f'# {mod} - wygenerowane przez tools/gen_lnk.py'], set()
    missing = []
    for f in funcs:
        if f in SKIP:
            continue
        o = PRIVATE.get(mod, {}).get(f) or byname.get(f.lower())
        if o is None:
            missing.append(f)
            continue
        if o in used:
            continue
        used.add(o)
        out.append(f'export {f.upper()}.{o}')
    for name, target in ALIAS.get(mod, {}).items():
        o = byname.get(name.lower())
        if o is not None and o not in used and target in funcs:
            used.add(o)
            out.append(f'export {name.upper()}.{o}={target.upper()}')
    for f in missing:
        sys.stderr.write(f'gen_lnk: {mod}: brak ordinala dla {f}\n')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
