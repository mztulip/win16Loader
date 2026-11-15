#!/usr/bin/env python3
"""
gen_charmap_ico.py - Generates a 32x32 16-color Windows icon (CHARMAP.ICO)
representing the classic Character Map (a character grid sheet and a magnified 'A').
"""
import struct, os

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

# Draw a base card/sheet from (2, 2) to (29, 29)
# Shadow on bottom and right
for y in range(5, 30):
    for x in range(5, 30):
        grid[y][x] = 8 # dark gray drop shadow

# Main table card: (2, 2) to (27, 27)
for y in range(2, 28):
    for x in range(2, 28):
        grid[y][x] = 15 # white paper / card

# Card border
for x in range(2, 28):
    grid[2][x] = 0 # top border
    grid[27][x] = 0 # bottom border
for y in range(2, 28):
    grid[y][2] = 0 # left border
    grid[y][27] = 0 # right border

# Header bar of table card (like a title bar)
for y in range(3, 7):
    for x in range(3, 27):
        grid[y][x] = 4 # dark blue title bar

# Draw grid lines on the table card:
# vertical lines
for vx in (8, 14, 20):
    for y in range(7, 27):
        grid[y][vx] = 7 # light gray grid line
# horizontal lines
for hy in (12, 17, 22):
    for x in range(3, 27):
        grid[hy][x] = 7 # light gray grid line

# Add some small glyphs in the background cells:
# 'B' in cell (3..7, 7..11)
for y in range(8, 12):
    grid[y][4] = 0
grid[8][5] = 0; grid[8][6] = 0
grid[10][5] = 0; grid[10][6] = 0
grid[11][5] = 0; grid[11][6] = 0
grid[9][7] = 0; grid[11][7] = 0

# 'C' in cell (9..13, 7..11)
for y in range(8, 12):
    grid[y][10] = 0
grid[8][11] = 0; grid[8][12] = 0
grid[11][11] = 0; grid[11][12] = 0

# '1' in cell (15..19, 7..11)
for y in range(8, 12):
    grid[y][17] = 0
grid[8][16] = 0

# Draw a large pop-up / magnified character box in the lower-right / center:
# Box from (11, 11) to (29, 29)
# Shadow for magnified key
for y in range(13, 30):
    for x in range(13, 30):
        grid[y][x] = 0 # black shadow

# Key body: (11, 11) to (27, 27)
for y in range(11, 28):
    for x in range(11, 28):
        grid[y][x] = 7 # light gray 3D button face

# 3D bevel:
for x in range(11, 28):
    grid[11][x] = 15 # top highlight white
    grid[27][x] = 8  # bottom shadow dark gray
for y in range(11, 28):
    grid[y][11] = 15 # left highlight white
    grid[y][27] = 8  # right shadow dark gray

# Inner area of magnified key: (13, 13) to (25, 25)
for y in range(13, 26):
    for x in range(13, 26):
        grid[y][x] = 15 # crisp white display

# Outer border of key:
for x in range(11, 28):
    grid[11][x] = 0
    grid[27][x] = 0
for y in range(11, 28):
    grid[y][11] = 0
    grid[y][27] = 0

# Draw a bold blue 'A' inside the magnified key (size ~10x9):
# Coordinates around (15..23, 14..23)
a_pixels = [
    # Top of 'A'
    (18, 14), (19, 14), (20, 14),
    (17, 15), (18, 15), (19, 15), (20, 15), (21, 15),
    # Upper sides
    (16, 16), (17, 16), (21, 16), (22, 16),
    (16, 17), (17, 17), (21, 17), (22, 17),
    (15, 18), (16, 18), (22, 18), (23, 18),
    # Crossbar
    (15, 19), (16, 19), (17, 19), (18, 19), (19, 19), (20, 19), (21, 19), (22, 19), (23, 19),
    (15, 20), (16, 20), (17, 20), (18, 20), (19, 20), (20, 20), (21, 20), (22, 20), (23, 20),
    # Legs
    (14, 21), (15, 21), (23, 21), (24, 21),
    (14, 22), (15, 22), (23, 22), (24, 22),
    (14, 23), (15, 23), (16, 23), (22, 23), (23, 23), (24, 23),
    (13, 24), (14, 24), (15, 24), (16, 24), (22, 24), (23, 24), (24, 24), (25, 24),
]

for px, py in a_pixels:
    grid[py][px] = 4 # navy blue

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

out_path = 'example_apps/charmap/CHARMAP.ICO' if os.path.exists('example_apps/charmap') else 'CHARMAP.ICO'
with open(out_path, 'wb') as f:
    f.write(ico)
print(f"Generated {out_path} successfully.")
