#!/usr/bin/env python3
import struct

VGA = [
    (0,0,0),        # 0: black
    (128,0,0),      # 1: dark red
    (0,128,0),      # 2: dark green
    (128,128,0),    # 3: dark yellow/olive
    (0,0,128),      # 4: dark blue / navy
    (128,0,128),    # 5: dark magenta
    (0,128,128),    # 6: dark cyan / teal
    (192,192,192),  # 7: light gray
    (128,128,128),  # 8: dark gray
    (255,0,0),      # 9: bright red
    (0,255,0),      # 10: bright green
    (255,255,0),    # 11: bright yellow
    (0,0,255),      # 12: bright blue
    (255,0,255),    # 13: bright magenta
    (0,255,255),    # 14: bright cyan
    (255,255,255)   # 15: white
]

grid = [[None for _ in range(32)] for _ in range(32)]

# Draw a stylish notebook / notepad:
# Dimensions: x from 4 to 26, y from 3 to 28
# Drop shadow on right and bottom:
for y in range(5, 30):
    for x in range(6, 28):
        if x >= 25 or y >= 27:
            grid[y][x] = 8 # dark gray shadow

# Notepad page / body:
for y in range(4, 27):
    for x in range(4, 25):
        grid[y][x] = 15 # white page

# Notepad border:
for x in range(4, 25):
    grid[4][x] = 0
    grid[26][x] = 0
for y in range(4, 27):
    grid[y][4] = 0
    grid[y][24] = 0

# Folded corner at bottom-right (dog-ear):
for d in range(4):
    for dx in range(d + 1):
        grid[26 - d][24 - dx] = None # cut corner from body
    grid[26 - d][24 - d] = 0 # diagonal fold line
# Shadow of fold:
grid[25][22] = 7
grid[25][23] = 7
grid[24][23] = 7

# Top header / binding (Navy Blue banner):
for y in range(5, 10):
    for x in range(5, 24):
        grid[y][x] = 4 # Dark blue
# Top header highlight:
for x in range(5, 24):
    grid[5][x] = 12 # Bright blue accent line
    grid[9][x] = 0  # Separator line

# Spiral rings on top:
for sx in [7, 11, 15, 19]:
    grid[3][sx] = 8
    grid[3][sx+1] = 7
    grid[4][sx] = 15
    grid[4][sx+1] = 7
    grid[5][sx] = 0
    grid[5][sx+1] = 8

# Ruled lines of text (light blue / gray):
for ly in [12, 15, 18, 21]:
    for lx in range(7, 22):
        if (lx + ly) % 5 != 0: # broken line to look like words/letters
            grid[ly][lx] = 8

# Red margin line on the left:
for my in range(10, 26):
    grid[my][7] = 9 # Red line

# Add a stylish tilted pencil / quill across bottom right:
# Pencil body: yellow (11), tip: black/wood (0, 3), eraser: red (9)
pencil_pixels = [
    (18, 20, 9), (19, 19, 9),  # eraser
    (20, 18, 7), (21, 17, 7),  # metal band
    (21, 19, 11), (22, 18, 11), (22, 20, 11), (23, 19, 11), # yellow shaft
    (23, 21, 11), (24, 20, 11), (24, 22, 11), (25, 21, 11),
    (25, 23, 3), (26, 22, 3),  # wood tip
    (27, 23, 0)                # lead tip
]
for px, py, col in pencil_pixels:
    if 0 <= px < 32 and 0 <= py < 32:
        grid[py][px] = col

# Build .ICO file structure
xor = bytearray()
andm = bytearray()

for y in range(31, -1, -1): # bottom-up DIB
    row = bytearray(16)
    mrow = bytearray(4)
    for x in range(32):
        p = grid[y][x]
        if p is None:
            mrow[x // 8] |= 0x80 >> (x % 8)
            p = 0
        row[x // 2] |= (p << 4) if x % 2 == 0 else p
    xor += row
    andm += mrow

hdr = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, 4, 0, len(xor) + len(andm), 0, 0, 16, 0)
pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in VGA)
img = hdr + pal + xor + andm
ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(img), 22) + img

with open('notepad/OUR_NOTE.ICO', 'wb') as f:
    f.write(ico)
print("Generated notepad/OUR_NOTE.ICO successfully.")
