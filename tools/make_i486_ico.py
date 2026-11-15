#!/usr/bin/env python3
"""make_i486_ico.py - generuje ikone LOADER16 / i486 w formacie .ICO (32x32 i 16x16 16-kolorow).
Przedstawia procesor i486 z nozkami (PGA/QFP) i zlotym napisem i486 / LOADER16.
"""
import struct, os
from PIL import Image, ImageDraw

def create_i486_image(size=32):
    # Standard 16-color VGA palette
    vga_palette = [
        0, 0, 0,        # 0: Black
        128, 0, 0,      # 1: Dark Red / Maroon
        0, 128, 0,      # 2: Green
        128, 128, 0,    # 3: Olive / Dark Gold
        0, 0, 128,      # 4: Navy / Ceramic
        128, 0, 128,    # 5: Purple
        0, 128, 128,    # 6: Teal
        192, 192, 192,  # 7: Silver / Light Gray
        128, 128, 128,  # 8: Gray
        255, 0, 0,      # 9: Bright Red
        0, 255, 0,      # 10: Lime
        255, 255, 0,    # 11: Yellow / Gold
        0, 0, 255,      # 12: Blue
        255, 0, 255,    # 13: Magenta (Transparent in icon mask)
        0, 255, 255,    # 14: Cyan
        255, 255, 255,  # 15: White
    ]
    # pad to 256 colors
    while len(vga_palette) < 768:
        vga_palette.extend([0, 0, 0])

    im = Image.new('P', (size, size), 13) # 13 = transparent
    im.putpalette(vga_palette)
    draw = ImageDraw.Draw(im)

    if size == 32:
        gold = 11
        dark_gold = 3
        black = 0
        gray = 8
        silver = 7
        white = 15
        ceramic = 4 # dark navy
        teal = 6

        # 1. Pins on 4 sides (length 4 px)
        # Top pins (y=1..4)
        for x in range(6, 26, 2):
            draw.line((x, 1, x, 4), fill=gold)
            draw.point((x + 1, 3), fill=dark_gold)
        # Bottom pins (y=27..30)
        for x in range(6, 26, 2):
            draw.line((x, 27, x, 30), fill=gold)
            draw.point((x + 1, 28), fill=dark_gold)
        # Left pins (x=1..4)
        for y in range(6, 26, 2):
            draw.line((1, y, 4, y), fill=gold)
            draw.point((3, y + 1), fill=dark_gold)
        # Right pins (x=27..30)
        for y in range(6, 26, 2):
            draw.line((27, y, 30, y), fill=gold)
            draw.point((28, y + 1), fill=dark_gold)

        # 2. Ceramic Package Body (x=5..26, y=5..26) -> 22x22 px
        draw.rectangle((5, 5, 26, 26), fill=black)
        # Beveled edges
        draw.line((5, 5, 26, 5), fill=silver)
        draw.line((5, 5, 5, 26), fill=silver)
        draw.line((6, 26, 26, 26), fill=gray)
        draw.line((26, 6, 26, 26), fill=gray)

        # Ceramic substrate (x=6..25, y=6..25)
        draw.rectangle((6, 6, 25, 25), fill=ceramic)

        # Pin 1 chamfer / gold notch dot at (7, 7)
        draw.rectangle((7, 7, 8, 8), fill=gold)

        # 3. Inner Heatspreader / Die (x=9..22, y=9..22) -> 14x14 px
        draw.rectangle((9, 9, 22, 22), fill=black)
        draw.rectangle((10, 10, 21, 21), fill=teal)
        draw.line((10, 10, 21, 10), fill=silver)
        draw.line((10, 10, 10, 21), fill=silver)

        # 4. Text: 'i486' on line 12..16, 'L16' on line 18..20
        # Draw 'i' at (11, 12..16)
        draw.point((11, 12), fill=gold)
        draw.line((11, 14, 11, 16), fill=gold)

        # Draw '4' at x=13..15
        draw.line((13, 12, 13, 14), fill=gold)
        draw.point((14, 14), fill=gold)
        draw.line((15, 12, 15, 16), fill=gold)

        # Draw '8' at x=17..19
        draw.rectangle((17, 12, 19, 16), outline=gold)
        draw.point((18, 14), fill=gold)

        # Draw '6' at x=20..22 wait, inside 21: let's put '86'
        # Let's adjust '486':
        # '4': x=12..14
        # '8': x=15..17
        # '6': x=18..20
        draw.rectangle((10, 10, 21, 21), fill=teal) # redraw inner
        draw.point((7, 7), fill=gold); draw.point((8, 7), fill=gold)

        # 'i': x=10
        draw.point((10, 12), fill=white)
        draw.line((10, 14, 10, 16), fill=white)

        # '4': x=12..14
        draw.line((12, 12, 12, 14), fill=gold)
        draw.point((13, 14), fill=gold)
        draw.line((14, 12, 14, 16), fill=gold)

        # '8': x=16..18
        draw.rectangle((16, 12, 18, 16), outline=gold)
        draw.point((17, 14), fill=gold)

        # '6': x=20..22 wait x=19..21
        draw.line((19, 12, 19, 16), fill=gold)
        draw.line((19, 12, 21, 12), fill=gold)
        draw.rectangle((19, 14, 21, 16), outline=gold)

        # Micro-text 'LOADER' at y=18..19
        for x in [11, 13, 15, 17, 19]:
            draw.point((x, 19), fill=white)
    else:
        # 16x16 icon
        from patch_calmira_logo import create_i486_tile
        tile_rows = create_i486_tile()
        for y in range(16):
            for x in range(16):
                im.putpixel((x, y), tile_rows[y][x])

    return im

def build_ico(path_out):
    im32 = create_i486_image(32)
    im16 = create_i486_image(16)

    def img_to_icon_bytes(im, w, h):
        # DIB BITMAPINFOHEADER + palette + XOR mask (4bpp) + AND mask (1bpp)
        # Note: biHeight in ICON DIB is 2 * h!
        biSize = 40
        biWidth = w
        biHeight = h * 2
        biPlanes = 1
        biBitCount = 4
        biCompression = 0
        biSizeImage = 0
        biXPels = 0
        biYPels = 0
        biClrUsed = 16
        biClrImportant = 16

        hdr = struct.pack('<IIIHHIIIIII',
            biSize, biWidth, biHeight, biPlanes, biBitCount,
            biCompression, biSizeImage, biXPels, biYPels,
            biClrUsed, biClrImportant
        )

        pal = im.getpalette()[:64] # 16 RGBQUAD entries
        pal_bytes = bytearray()
        for i in range(16):
            r, g, b = pal[i*3], pal[i*3+1], pal[i*3+2]
            pal_bytes.extend([b, g, r, 0])

        xor_stride = ((w * 4 + 31) // 32) * 4
        and_stride = ((w + 31) // 32) * 4

        xor_bits = bytearray(xor_stride * h)
        and_bits = bytearray(and_stride * h)

        for y in range(h):
            # bottom-up
            y_bmp = h - 1 - y
            for x in range(w):
                p = im.getpixel((x, y))
                is_trans = (p == 13)

                byte_xor = y_bmp * xor_stride + (x // 2)
                if x % 2 == 0:
                    xor_bits[byte_xor] |= ((0 if is_trans else p) & 0x0f) << 4
                else:
                    xor_bits[byte_xor] |= ((0 if is_trans else p) & 0x0f)

                if is_trans:
                    byte_and = y_bmp * and_stride + (x // 8)
                    bit = 7 - (x % 8)
                    and_bits[byte_and] |= (1 << bit)

        dib_data = hdr + bytes(pal_bytes) + bytes(xor_bits) + bytes(and_bits)
        return dib_data

    dib32 = img_to_icon_bytes(im32, 32, 32)
    dib16 = img_to_icon_bytes(im16, 16, 16)

    # ICONDIR (6 bytes) + 2 * ICONDIRENTRY (16 bytes each)
    idReserved = 0
    idType = 1 # ICON
    idCount = 2
    icondir = struct.pack('<HHH', idReserved, idType, idCount)

    off1 = 6 + 16 * 2
    off2 = off1 + len(dib32)

    entry32 = struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(dib32), off1)
    entry16 = struct.pack('<BBBBHHII', 16, 16, 16, 0, 1, 4, len(dib16), off2)

    with open(path_out, 'wb') as f:
        f.write(icondir + entry32 + entry16 + dib32 + dib16)
    print(f'Wrote icon: {path_out} ({os.path.getsize(path_out)} bytes)')

if __name__ == '__main__':
    os.makedirs('windir', exist_ok=True)
    build_ico('windir/LOADER16.ICO')
    build_ico('windir/I486.ICO')
