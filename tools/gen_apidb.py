#!/usr/bin/env python3
"""gen_apidb.py - generuje WIN16API.DB dla loadera (stuby niezaimplementowanych funkcji).

Format linii: "MODUL ORDINAL BAJTY_ARGUMENTOW NAZWA" (pascal: callee zdejmuje
argumenty; cdecl/varargs: 0). Zrodlo: pliki .spec Wine (patrz tools/imports.py).
Uzycie: tools/gen_apidb.py > WIN16API.DB
"""
import sys
from imports import SPEC, WINE, load_spec

out = []
for mod in sorted(SPEC):
    spec = load_spec(mod, WINE)
    for o in sorted(spec):
        name, kind, _r16, nb, _al = spec[o]
        if kind == 'pascal':
            args = nb
        elif kind in ('cdecl', 'varargs'):
            args = 0
        else:
            continue            # stub/equate/variable bez sygnatury
        out.append(f'{mod} {o} {args} {name}\r\n')
sys.stdout.write(''.join(out))
