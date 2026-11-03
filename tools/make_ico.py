#!/usr/bin/env python3
"""make_ico.py - generuje prosta ikone 32x32, 16 kolorow (.ICO, format Windows 3.x).

Uzycie: tools/make_ico.py plik.ico [kolor_tla_indeks]
Wzor: ramka okna z paskiem tytulu i kolorowymi kwadratami (paleta VGA).
"""
import struct, sys

VGA = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
       (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]

def pixel(x, y):
    if x < 2 or x > 29 or y < 3 or y > 28: return None          # przezroczyste
    if x in (2, 29) or y in (3, 28): return 0                   # ramka
    if y < 9: return 4                                          # pasek tytulu
    if 6 <= x <= 13 and 12 <= y <= 18: return 9
    if 18 <= x <= 25 and 12 <= y <= 18: return 10
    if 6 <= x <= 13 and 20 <= y <= 25: return 12
    if 18 <= x <= 25 and 20 <= y <= 25: return 11
    return 15

def main():
    out = sys.argv[1]
    xor = bytearray(); andm = bytearray()
    for y in range(31, -1, -1):                                 # DIB od dolu
        row = bytearray(16); mrow = bytearray(4)
        for x in range(32):
            p = pixel(x, y)
            if p is None:
                mrow[x // 8] |= 0x80 >> (x % 8)
                p = 0
            row[x // 2] |= (p << 4) if x % 2 == 0 else p
        xor += row; andm += mrow
    hdr = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, 4, 0, len(xor) + len(andm), 0, 0, 16, 0)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in VGA)
    img = hdr + pal + xor + andm
    ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(img), 22) + img
    open(out, 'wb').write(ico)

if __name__ == '__main__':
    main()
