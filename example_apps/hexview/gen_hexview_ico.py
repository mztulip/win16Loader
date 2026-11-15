#!/usr/bin/env python3
"""
gen_hexview_ico.py - Generates a 32x32 16-color Windows icon (HEXVIEW.ICO)
representing the Hex Viewer (a document with 'HEX' header and binary/hex grid).
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

# Draw a document card from (2, 2) to (29, 29)
# Drop shadow on bottom and right
for y in range(5, 30):
    for x in range(5, 30):
        grid[y][x] = 8 # dark gray drop shadow

# Main paper card: (2, 2) to (27, 27)
for y in range(2, 28):
    for x in range(2, 28):
        grid[y][x] = 15 # white paper

# Card border (black)
for x in range(2, 28):
    grid[2][x] = 0
    grid[27][x] = 0
for y in range(2, 28):
    grid[y][2] = 0
    grid[y][27] = 0

# Folded corner at top-right (x from 22 to 27, y from 2 to 7)
for d in range(6):
    # erase white card outside the fold
    for x in range(22 + d, 28):
        grid[2 + (5 - d)][x] = None

# Draw folded triangle shadow and flap
for y in range(3, 8):
    for x in range(22, 27):
        if (x - 22) + (y - 3) >= 4:
            grid[y][x] = 7 # light gray fold
for d in range(6):
    grid[2 + d][27 - d] = 0 # fold crease line

# Header bar on the card (teal/cyan, matching project theme)
for y in range(4, 9):
    for x in range(4, 21):
        grid[y][x] = 6 # dark teal header

# Draw "HEX" text in white on the header bar
# H (width 3, x=5..7)
for y in range(5, 8):
    grid[y][5] = 15
    grid[y][7] = 15
grid[6][6] = 15

# E (width 3, x=9..11)
for y in range(5, 8):
    grid[y][9] = 15
grid[5][10] = 15; grid[5][11] = 15
grid[6][10] = 15
grid[7][10] = 15; grid[7][11] = 15

# X (width 3, x=13..15)
grid[5][13] = 15; grid[5][15] = 15
grid[6][14] = 15
grid[7][13] = 15; grid[7][15] = 15

# Hex numbers / binary dump visualization
# Row 1 (y=11..12): Offset in blue (x=4..7), Hex in dark red/black (x=9..25)
for x in (4, 5, 6): grid[11][x] = 4
for x in (9, 10, 12, 13, 15, 16, 18, 19): grid[11][x] = 0
for x in (22, 23, 24, 25): grid[11][x] = 8

# Row 2 (y=14..15):
for x in (4, 5, 6): grid[14][x] = 4
for x in (9, 10, 12, 13, 15, 16, 18, 19): grid[14][x] = 1 # dark red
for x in (22, 23, 24, 25): grid[14][x] = 8

# Row 3 (y=17..18):
for x in (4, 5, 6): grid[17][x] = 4
for x in (9, 10, 12, 13, 15, 16, 18, 19): grid[17][x] = 0
for x in (22, 23, 24, 25): grid[17][x] = 8

# Row 4 (y=20..21):
for x in (4, 5, 6): grid[20][x] = 4
for x in (9, 10, 12, 13, 15, 16, 18, 19): grid[20][x] = 1
for x in (22, 23, 24, 25): grid[20][x] = 8

# Row 5 (y=23..24):
for x in (4, 5, 6): grid[23][x] = 4
for x in (9, 10, 12, 13, 15, 16, 18, 19): grid[23][x] = 0
for x in (22, 23, 24, 25): grid[23][x] = 8

# A magnifying glass over row 3-4 (bottom right quadrant)
# Glass rim at center (20, 20), radius ~5
glass_rim = [
    (18, 16), (19, 16), (20, 16), (21, 16),
    (17, 17), (22, 17),
    (16, 18), (23, 18),
    (16, 19), (23, 19),
    (16, 20), (23, 20),
    (17, 21), (22, 21),
    (18, 22), (19, 22), (20, 22), (21, 22)
]
for px, py in glass_rim:
    grid[py][px] = 0 # black rim

# Lens interior highlight (light cyan / white)
for py in range(17, 22):
    for px in range(17, 23):
        if (px, py) not in glass_rim:
            grid[py][px] = 14 # bright cyan lens glass

# Lens shine
grid[18][18] = 15; grid[18][19] = 15; grid[19][18] = 15

# "01" inside magnifying glass
grid[19][20] = 0; grid[20][20] = 0; grid[21][20] = 0 # '1'

# Glass handle pointing down-right
handle = [
    (22, 23), (23, 23),
    (23, 24), (24, 24),
    (24, 25), (25, 25),
    (25, 26), (26, 26),
    (26, 27), (27, 27)
]
for px, py in handle:
    grid[py][px] = 1 # dark red / wood handle
    if px + 1 < 32: grid[py][px+1] = 0 # handle outline

# Build .ICO file structure
xor_mask = bytearray()
and_mask = bytearray()

for y in range(31, -1, -1): # bottom-up DIB
    row = bytearray(16)
    mrow = bytearray(4)
    for x in range(32):
        p = grid[y][x]
        if p is None:
            mrow[x // 8] |= (0x80 >> (x % 8))
            p = 0
        row[x // 2] |= (p << 4) if (x % 2 == 0) else p
    xor_mask += row
    and_mask += mrow

hdr = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, 4, 0, len(xor_mask) + len(and_mask), 0, 0, 16, 0)
pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in VGA)
img = hdr + pal + xor_mask + and_mask
ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(img), 22) + img

out_path = sys.argv[1] if len(sys.argv) > 1 else 'HEXVIEW.ICO'
with open(out_path, 'wb') as f:
    f.write(ico)
print(f"Generated {out_path} successfully.")
