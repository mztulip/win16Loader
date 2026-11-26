#!/usr/bin/env python3
"""
Generate MARIO.ICO - 32x32 16-color uncompressed Windows 3.1 compatible DIB ICO.
Based directly on the Mario character sprite from Mike Wiering's MARIO.EXE (1994).
"""
import os
import struct
from PIL import Image

# Exact 19x19 Mario sprite extracted from MARIO.EXE title screen (320x200 VGA)
SPRITE = [
    "        .......    ",
    "     ...:::--:=.   ",
    "   ..:::::-+*-:=   ",
    "  .:::::=......... ",
    " .:::=.............",
    " :@=....@.@.@      ",
    "=@+@=.=@bb.b.-@@@  ",
    "=@--@..=b---+++++@ ",
    ".=@@-.=--.@@@@@@@= ",
    "  ...----......... ",
    "  ..=@@@@=.....    ",
    "   =::=assssa      ",
    "  =:::assh11ha     ",
    "  a3**3shh144h.    ",
    "  a3*3.sssh41h.    ",
    "  a333.ssssss.     ",
    "   ==66=......     ",
    "   .=77788=78.     ",
    "   .====66.==.     ",
]

# Custom 16-color RGB palette matching Mario's exact VGA colors
PALETTE_MAP = {
    ".": (0, (0, 0, 0)),          # Black outline / eyes / mustache
    ":": (1, (232, 0, 0)),        # Mario red (cap & shirt)
    "*": (2, (255, 255, 255)),    # White (glove & cap highlight)
    "3": (3, (192, 192, 223)),    # Light grey-white (glove shade)
    "a": (4, (111, 111, 127)),    # Dark grey (glove outline/shadow)
    "+": (5, (255, 192, 144)),    # Light skin highlight
    "-": (6, (248, 159, 111)),    # Main skin tone
    "b": (7, (223, 136, 103)),    # Warm skin mid-shade
    "@": (8, (184, 104, 80)),     # Dark skin / ear shade
    "=": (9, (135, 71, 56)),      # Dark brown (hair / brim / boots)
    "6": (10, (144, 95, 63)),     # Mid brown (boots)
    "7": (11, (175, 119, 79)),    # Warm brown (boots)
    "8": (12, (231, 167, 111)),   # Tan highlight (boots)
    "s": (13, (56, 112, 143)),    # Dark blue (overalls)
    "h": (14, (80, 148, 180)),    # Mid blue (overalls)
    "1": (15, (112, 180, 216)),   # Light blue (overalls)
    "4": (15, (112, 180, 216)),   # Cyan-blue highlight (overalls, merged with 1)
}

# Build ordered 16-color palette (index 0..15)
PALETTE = [None] * 16
for ch, (idx, rgb) in PALETTE_MAP.items():
    if PALETTE[idx] is None:
        PALETTE[idx] = rgb


def create_mario_image():
    sw = len(SPRITE[0])
    sh = len(SPRITE)
    src = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    for y, row in enumerate(SPRITE):
        for x, ch in enumerate(row):
            if ch != " ":
                _, (r, g, b) = PALETTE_MAP[ch]
                src.putpixel((x, y), (r, g, b, 255))

    # Scale cleanly to 30x30 with nearest-neighbor to preserve crisp pixel art
    scaled = src.resize((30, 30), Image.NEAREST)

    # Center on 32x32 transparent canvas (slightly right so desktop shortcut arrow doesn't obscure Mario)
    canvas = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    canvas.paste(scaled, (2, 1))
    return canvas


def save_win16_ico(img, filepath):
    width, height = 32, 32
    rgb_to_idx = {rgb: i for i, rgb in enumerate(PALETTE)}

    xor_rows = []
    and_rows = []
    for y in reversed(range(height)):
        xor_row = bytearray(16)
        and_row = bytearray(4)
        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            if a < 128:
                and_row[x // 8] |= 1 << (7 - (x % 8))
                color_idx = 0
            else:
                color_idx = rgb_to_idx[(r, g, b)]
            if x % 2 == 0:
                xor_row[x // 2] |= (color_idx << 4)
            else:
                xor_row[x // 2] |= (color_idx & 0x0F)
        xor_rows.append(bytes(xor_row))
        and_rows.append(bytes(and_row))

    xor_data = b"".join(xor_rows)
    and_data = b"".join(and_rows)
    palette_bytes = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in PALETTE)

    dib_header = struct.pack(
        "<IIIHHIIIIII",
        40,
        32,
        64,
        1,
        4,
        0,
        len(xor_data) + len(and_data),
        0,
        0,
        16,
        16,
    )
    image_data = dib_header + palette_bytes + xor_data + and_data
    ico_header = struct.pack("<HHH", 0, 1, 1)
    ico_entry = struct.pack(
        "<BBBBHHII",
        32,
        32,
        16,
        0,
        1,
        4,
        len(image_data),
        22,
    )

    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as f:
        f.write(ico_header)
        f.write(ico_entry)
        f.write(image_data)


if __name__ == "__main__":
    img = create_mario_image()
    save_win16_ico(img, "windir/MARIO.ICO")
    print(f"Generated windir/MARIO.ICO ({os.path.getsize('windir/MARIO.ICO')} B)")
