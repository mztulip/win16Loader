#!/usr/bin/env python3
"""
gen_icoview_ico.py - Generates a 32x32 16-color Windows icon (ICOVIEW.ICO)
representing the Icon Viewer (a framed pixel-art icon with a magnifying glass).
"""
import struct, os, sys

# Standard 16-color VGA Palette (R, G, B)
VGA = [
    (0,0,0),        # 0: black
    (128,0,0),      # 1: dark red
    (0,128,0),      # 2: dark green
    (128,128,0),    # 3: dark yellow/olive
    (0,0,128),      # 4: dark blue / navy
    (128,0,128),    # 5: dark magenta / purple
    (0,128,128),    # 6: dark cyan / teal
    (192,192,192),  # 7: light gray
    (128,128,128),  # 8: dark gray
    (255,0,0),      # 9: bright red
    (0,255,0),      # 10: bright green
    (255,255,0),    # 11: bright yellow
    (0,0,255),      # 12: bright blue
    (255,0,255),    # 13: bright magenta / pink
    (0,255,255),    # 14: bright cyan
    (255,255,255)   # 15: white
]

grid = [[None for _ in range(32)] for _ in range(32)]

# Drop shadow for the picture frame
for y in range(4, 27):
    for x in range(4, 26):
        grid[y][x] = 8  # dark gray shadow

# Main icon frame card: (1, 1) to (24, 25)
for y in range(1, 26):
    for x in range(1, 25):
        grid[y][x] = 15  # white border/card

# Outer black border
for x in range(1, 25):
    grid[1][x] = 0
    grid[25][x] = 0
for y in range(1, 26):
    grid[y][1] = 0
    grid[y][24] = 0

# Teal title bar inside card (y=3..7, x=3..22)
for y in range(3, 8):
    for x in range(3, 23):
        grid[y][x] = 6  # dark teal

# Draw "ICO" in white on header (y=4..6)
# I (x=5..7)
for x in range(5, 8):
    grid[4][x] = 15
    grid[6][x] = 15
grid[5][6] = 15

# C (x=9..11)
for x in range(9, 12):
    grid[4][x] = 15
    grid[6][x] = 15
grid[5][9] = 15

# O (x=13..15)
for x in range(13, 16):
    grid[4][x] = 15
    grid[6][x] = 15
grid[5][13] = 15
grid[5][15] = 15

# Inner picture canvas (y=9..23, x=3..22): checkerboard sky + colorful pixel art
for y in range(9, 24):
    for x in range(3, 23):
        # Sky / checkerboard
        grid[y][x] = 14 if ((x // 2 + y // 2) % 2 == 0) else 15

# Yellow sun in top-right of canvas
for y in range(10, 14):
    for x in range(17, 21):
        if not ((x in (17, 20)) and (y in (10, 13))):
            grid[y][x] = 11  # bright yellow

# Green hills / mountain
for y in range(16, 24):
    span = y - 15
    for x in range(max(3, 10 - span), min(23, 10 + span + 1)):
        grid[y][x] = 2 if (x + y) % 3 else 10
for y in range(18, 24):
    span = y - 17
    for x in range(max(3, 17 - span), min(23, 17 + span + 1)):
        grid[y][x] = 6 if (x + y) % 2 else 2

# Red flower / accent dot
grid[21][7] = 9
grid[22][7] = 9
grid[21][8] = 11

# Canvas thin border
for x in range(3, 23):
    grid[9][x] = 8
    grid[23][x] = 8
for y in range(9, 24):
    grid[y][3] = 8
    grid[y][22] = 8

# Magnifying glass at bottom-right (center ~ (22, 22), radius ~ 5)
cx, cy = 21, 21
for y in range(15, 28):
    for x in range(15, 28):
        dx = x - cx
        dy = y - cy
        d2 = dx * dx + dy * dy
        if 20 <= d2 <= 32:
            grid[y][x] = 0  # black rim
        elif 13 <= d2 < 20:
            grid[y][x] = 4  # navy inner rim
        elif d2 < 13:
            # Bright cyan glass with white reflection
            if dx <= -1 and dy <= -1 and d2 >= 3:
                grid[y][x] = 15
            else:
                grid[y][x] = 14

# Handle of magnifying glass going to (30, 30)
for t in range(5, 10):
    hx = cx + t
    hy = cy + t
    if 0 <= hx < 32 and 0 <= hy < 32:
        grid[hy][hx] = 3      # olive/gold handle core
        if hx + 1 < 32: grid[hy][hx + 1] = 0
        if hy + 1 < 32: grid[hy + 1][hx] = 0
        if hx - 1 >= 0: grid[hy][hx - 1] = 0
        if hy - 1 >= 0: grid[hy - 1][hx] = 0
for t in range(5, 9):
    hx = cx + t
    hy = cy + t
    if 0 <= hx < 32 and 0 <= hy < 32:
        grid[hy][hx] = 11 if t % 2 == 0 else 3

def write_ico(path):
    # ICONDIR: reserved(2), type=1(2), count=1(2)
    icondir = struct.pack('<HHH', 0, 1, 1)

    # BITMAPINFOHEADER (40 bytes): 32x64 (32 XOR + 32 AND), 4bpp
    bih = struct.pack('<IIIHHIIIIII',
        40,     # biSize
        32,     # biWidth
        64,     # biHeight (2 * 32)
        1,      # biPlanes
        4,      # biBitCount
        0,      # biCompression (BI_RGB)
        640,    # biSizeImage (512 XOR + 128 AND)
        0, 0,   # XPelsPerMeter, YPelsPerMeter
        16, 0   # biClrUsed, biClrImportant
    )

    # Color table (16 * 4 = 64 bytes, BGRA)
    pal = bytearray()
    for r, g, b in VGA:
        pal.extend(struct.pack('<BBBB', b, g, r, 0))

    # XOR bitmap (bottom-up, 32 rows * 16 bytes = 512 bytes)
    xor_bits = bytearray()
    for y in range(31, -1, -1):
        for x in range(0, 32, 2):
            p0 = grid[y][x]
            p1 = grid[y][x + 1]
            c0 = 0 if p0 is None else p0
            c1 = 0 if p1 is None else p1
            xor_bits.append((c0 << 4) | c1)

    # AND mask (bottom-up, 32 rows * 4 bytes = 128 bytes; 1 = transparent, 0 = opaque)
    and_bits = bytearray()
    for y in range(31, -1, -1):
        row = 0
        for x in range(32):
            if grid[y][x] is None:
                row |= (1 << (31 - x))
        and_bits.extend(struct.pack('>I', row))

    img_data = bih + bytes(pal) + bytes(xor_bits) + bytes(and_bits)

    # ICONDIRENTRY (16 bytes)
    entry = struct.pack('<BBBBHHII',
        32, 32,         # width, height
        16, 0,          # colorCount, reserved
        1, 4,           # planes, bitCount
        len(img_data),  # bytesInRes
        6 + 16          # imageOffset
    )

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(icondir + entry + img_data)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'ICOVIEW.ICO')
    write_ico(out)
    print(f"Generated {out}")
