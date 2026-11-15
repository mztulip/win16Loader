#!/usr/bin/env python3
"""
gen_paint_ico.py - Generates a 32x32 16-color Windows icon (PAINT.ICO)
representing an artist palette and paint brush.
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

# Draw wooden palette: an oval/bean shape
# Center roughly at (14, 16), rx=11, ry=12
for y in range(4, 28):
    for x in range(3, 26):
        # Oval equation with indent on the right
        dx = (x - 14) / 10.5
        dy = (y - 16) / 11.5
        dist2 = dx * dx + dy * dy
        # Cutout / thumb hole around (20, 20)
        th_dx = (x - 20) / 2.5
        th_dy = (y - 19) / 3.0
        thumb_hole = (th_dx * th_dx + th_dy * th_dy) < 1.0

        if dist2 <= 1.0 and not thumb_hole:
            # Drop shadow / 3D edge
            if dist2 > 0.88 or y >= 26 or x >= 24:
                grid[y][x] = 3 # dark yellow/olive shadow edge
            else:
                grid[y][x] = 11 # warm yellow/wood body

# Palette outline
for y in range(3, 29):
    for x in range(2, 27):
        if grid[y][x] is not None:
            # check neighbors
            for ny, nx in [(y-1,x), (y+1,x), (y,x-1), (y,x+1)]:
                if 0 <= ny < 32 and 0 <= nx < 32:
                    if grid[ny][nx] is None:
                        # set outline
                        pass

# Add crisp dark outline for thumb hole
for y in range(16, 23):
    for x in range(18, 23):
        th_dx = (x - 20) / 2.5
        th_dy = (y - 19) / 3.0
        if 0.8 <= (th_dx*th_dx + th_dy*th_dy) <= 1.3:
            if grid[y][x] is not None:
                grid[y][x] = 8 # dark gray rim

# Add Paint Splats / Color Blobs on Palette:
# 1. Bright Red blob around (7, 10)
for y in range(9, 13):
    for x in range(6, 10):
        if (x-7.5)**2 + (y-10.5)**2 <= 2.2:
            grid[y][x] = 9
grid[10][7] = 15 # shine

# 2. Bright Blue blob around (8, 16)
for y in range(15, 19):
    for x in range(6, 10):
        if (x-7.5)**2 + (y-16.5)**2 <= 2.2:
            grid[y][x] = 12
grid[16][7] = 15

# 3. Bright Green blob around (10, 22)
for y in range(21, 25):
    for x in range(9, 13):
        if (x-10.5)**2 + (y-22.5)**2 <= 2.2:
            grid[y][x] = 10
grid[22][10] = 15

# 4. Bright Cyan blob around (12, 8)
for y in range(7, 11):
    for x in range(11, 15):
        if (x-12.5)**2 + (y-8.5)**2 <= 2.0:
            grid[y][x] = 14

# 5. Bright Magenta blob around (17, 9)
for y in range(8, 12):
    for x in range(16, 20):
        if (x-17.5)**2 + (y-9.5)**2 <= 2.0:
            grid[y][x] = 13

# 6. Dark/shadow edge around palette
for y in range(3, 29):
    for x in range(2, 27):
        if grid[y][x] == 11 or grid[y][x] == 3:
            # If neighbor is empty outside, draw dark border (0)
            is_edge = False
            for dy, dx in [(-1,0),(1,0),(0,-1),(0,1)]:
                ny, nx = y + dy, x + dx
                if ny < 0 or ny >= 32 or nx < 0 or nx >= 32 or grid[ny][nx] is None:
                    is_edge = True
                    break
            if is_edge:
                grid[y][x] = 0

# Now draw Paintbrush diagonally across the top right down to the canvas
# Handle: from (29, 3) down to (17, 18)
brush_line = [
    # Handle end
    (29, 3, 1), (28, 4, 1), (28, 3, 1), (27, 4, 1),
    (27, 5, 3), (26, 5, 3), (26, 6, 3), (25, 6, 3),
    (25, 7, 3), (24, 7, 3), (24, 8, 3), (23, 8, 3),
    (23, 9, 3), (22, 9, 3), (22, 10, 3), (21, 10, 3),
    # Metal Ferrule (silver/light gray & white)
    (21, 11, 7), (20, 11, 15), (20, 12, 7), (19, 12, 15),
    (19, 13, 8), (18, 13, 7),
    # Bristles (dark with bright red/crimson tip dipping into paint!)
    (18, 14, 8), (17, 14, 0), (17, 15, 8), (16, 15, 0),
    (16, 16, 1), (15, 16, 9), (15, 17, 9), (14, 17, 9), (14, 18, 15)
]

for px, py, col in brush_line:
    if 0 <= px < 32 and 0 <= py < 32:
        grid[py][px] = col

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

out_path = 'example_apps/paint/PAINT.ICO' if os.path.exists('example_apps/paint') else 'PAINT.ICO'
with open(out_path, 'wb') as f:
    f.write(ico)
print(f"Generated {out_path} successfully.")
