#!/usr/bin/env python3
"""trace_names.py - nazwy funkcji w trace API loadera (opcja -t).

Linie trace: "> mm oooo cccc:iiii" (mm = indeks modulu, oooo = offset funkcji,
cccc:iiii = adres powrotu w apce). Indeksy/pliki modulow z linii loadera
"Modul NAZWA [mm] z PLIK: ...". Pliki DLL szukane w --dir (domyslnie katalog
repo) po nazwie pliku z logu.

Uzycie: tools/trace_names.py serial.log [--dir .] [--all]
  bez --all: zwija powtorzenia kolejnych identycznych wywolan
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imports import load_spec, WINE
from check_args import exports


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    d = '.'
    for a in sys.argv[1:]:
        if a.startswith('--dir='):
            d = a[6:]
    show_all = '--all' in sys.argv
    log = open(args[0], encoding='latin1').read().splitlines()
    mods = {}
    for l in log:
        m = re.match(r'Modul (\S+) \[(\d+)\] z (\S+):', l)
        if m:
            mods[int(m.group(2))] = (m.group(1), m.group(3))
    names = {}
    for idx, (name, path) in mods.items():
        fn = os.path.join(d, os.path.basename(path.replace('\\', '/')).lower())
        if not os.path.exists(fn):
            fn = os.path.join(d, os.path.basename(path.replace('\\', '/')))
        if not os.path.exists(fn):
            continue
        spec = load_spec(name, WINE)
        for o, (sg, off) in exports(open(fn, 'rb').read()).items():
            names[(idx, off)] = f'{name}.{o} {spec.get(o, ("?",))[0]}'
    prev, rep = None, 0
    for l in log:
        m = re.match(r'>([0-9A-F]{2}) ([0-9A-F]{4}) ([0-9A-F]{4}:[0-9A-F]{4})', l)
        if not m:
            if prev and rep:
                print(f'    ... x{rep + 1}')
            prev, rep = None, 0
            print(l)
            continue
        idx, off = int(m.group(1), 16), int(m.group(2), 16)
        nm = names.get((idx, off), f'{mods.get(idx, ("?",))[0]}:{off:04X}')
        line = f'> {nm}  <- {m.group(3)}'
        if not show_all and line == prev:
            rep += 1
            continue
        if prev and rep:
            print(f'    ... x{rep + 1}')
        rep = 0
        prev = line
        print(line)


if __name__ == '__main__':
    main()
