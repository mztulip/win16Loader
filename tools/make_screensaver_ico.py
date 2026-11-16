#!/usr/bin/env python3
"""
Generate SCREENSV.ICO - True 16-color uncompressed Windows 3.1 compatible DIB ICO.
Retro CRT Monitor displaying the bouncing shape screensaver ("The Tim Saver 3000").
"""
import struct, os
from PIL import Image, ImageDraw

def create_screensaver_image():
    # 32x32 RGBA
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 16-color palette colors
    c_black      = (0, 0, 0, 255)
    c_white      = (255, 255, 255, 255)
    c_light_gray = (192, 192, 192, 255)
    c_dark_gray  = (128, 128, 128, 255)
    c_yellow     = (255, 255, 0, 255)
    c_cyan       = (0, 255, 255, 255)
    c_green      = (0, 255, 0, 255)
    c_magenta    = (255, 0, 255, 255)
    c_red        = (255, 0, 0, 255)
    c_navy       = (0, 0, 128, 255)
    c_dark_cyan  = (0, 128, 128, 255)

    # 1. Monitor Base / Stand (bottom foot)
    # Foot plate: [8, 27] to [23, 29]
    draw.rectangle([8, 27, 23, 29], fill=c_light_gray, outline=c_black)
    draw.line([(8, 27), (23, 27)], fill=c_white)
    draw.line([(8, 29), (23, 29)], fill=c_dark_gray)

    # Stand neck: [13, 23] to [18, 26]
    draw.rectangle([13, 23, 18, 26], fill=c_dark_gray, outline=c_black)
    draw.line([(13, 23), (13, 26)], fill=c_light_gray)

    # 2. Monitor Chassis (CRT bezel)
    # Outer box: [2, 2] to [29, 23]
    draw.rectangle([2, 2, 29, 23], fill=c_light_gray, outline=c_black)
    # 3D Highlight on top and left outer edges
    draw.line([(3, 3), (28, 3)], fill=c_white)
    draw.line([(3, 3), (3, 22)], fill=c_white)
    # 3D Shadow on bottom and right outer edges
    draw.line([(4, 22), (28, 22)], fill=c_dark_gray)
    draw.line([(28, 4), (28, 22)], fill=c_dark_gray)

    # Bottom bezel detail / buttons:
    # Power LED: green dot
    draw.point((26, 21), fill=c_green)
    # Small contrast/brightness dials
    draw.point((22, 21), fill=c_dark_gray)
    draw.point((24, 21), fill=c_dark_gray)

    # 3. CRT Screen Area (recessed into bezel)
    # Recessed inner border: [5, 4] to [26, 19]
    draw.rectangle([5, 4, 26, 19], fill=c_black, outline=c_black)
    # Inner bezel shadow
    draw.line([(5, 4), (26, 4)], fill=c_dark_gray)
    draw.line([(5, 4), (5, 19)], fill=c_dark_gray)
    draw.line([(6, 19), (26, 19)], fill=c_white)
    draw.line([(26, 5), (26, 19)], fill=c_white)

    # Screen glass background: [6, 5] to [25, 18]
    draw.rectangle([6, 5, 25, 18], fill=c_black)

    # 4. Screensaver Graphics (bouncing rainbow shape with motion trails!)
    # Trail 1 (oldest ghost): small magenta dot at (9, 15)
    draw.point((9, 15), fill=c_magenta)

    # Trail 2: cyan mini-diamond at (12, 13)
    draw.polygon([(12, 12), (13, 13), (12, 14), (11, 13)], fill=c_dark_cyan, outline=c_cyan)

    # Trail 3: bright green diamond at (15, 11)
    draw.polygon([(15, 9), (17, 11), (15, 13), (13, 11)], fill=c_green, outline=c_white)

    # Trail streak line (motion path connecting the shapes)
    draw.line([(9, 15), (12, 13)], fill=c_navy)
    draw.line([(12, 13), (15, 11)], fill=c_dark_cyan)
    draw.line([(15, 11), (19, 9)], fill=c_green)

    # Main Shape: Bright Golden-Yellow Bouncing Diamond / Star at (19, 9)
    draw.polygon([(19, 6), (23, 9), (19, 12), (15, 9)], fill=c_yellow, outline=c_red)
    # Center sparkle
    draw.point((19, 9), fill=c_white)
    draw.point((19, 8), fill=c_white)

    # Background twinkle stars on the dark CRT glass
    draw.point((8, 8), fill=c_white)
    draw.point((23, 15), fill=c_yellow)
    draw.point((10, 7), fill=c_cyan)
    draw.point((18, 16), fill=c_magenta)

    # CRT Glass corner reflection (glare on curved glass)
    draw.line([(7, 6), (9, 6)], fill=c_dark_gray)
    draw.point((7, 7), fill=c_dark_gray)

    return img

def save_win16_ico(img, filepath):
    # Standard 16-color palette (B, G, R, 0)
    palette = [
        (0, 0, 0, 0),         # 0: Black
        (128, 0, 0, 0),       # 1: Dark Red
        (0, 128, 0, 0),       # 2: Dark Green
        (128, 128, 0, 0),     # 3: Dark Yellow
        (0, 0, 128, 0),       # 4: Dark Blue (Navy)
        (128, 0, 128, 0),     # 5: Dark Magenta
        (0, 128, 128, 0),     # 6: Dark Cyan
        (192, 192, 192, 0),   # 7: Light Gray
        (128, 128, 128, 0),   # 8: Medium Gray
        (255, 0, 0, 0),       # 9: Bright Red
        (0, 255, 0, 0),       # 10: Bright Green
        (255, 255, 0, 0),     # 11: Bright Yellow
        (255, 128, 0, 0),     # 12: Light Blue / Sky
        (255, 0, 255, 0),     # 13: Bright Magenta
        (255, 255, 0, 0),     # 14: Cyan
        (255, 255, 255, 0),   # 15: White
    ]

    def find_closest(r, g, b):
        best_idx = 0
        best_dist = 1e9
        for i, (pb, pg, pr, _) in enumerate(palette):
            d = (r - pr)**2 + (g - pg)**2 + (b - pb)**2
            if d < best_dist:
                best_dist = d
                best_idx = i
        return best_idx

    width, height = 32, 32
    xor_rows = []
    and_rows = []

    # Bottom to top row
    for y in reversed(range(height)):
        xor_row = bytearray(16)
        and_row = bytearray(4)

        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            if a < 128:
                and_row[x // 8] |= (1 << (7 - (x % 8)))
                color_idx = 0
            else:
                color_idx = find_closest(r, g, b)

            if x % 2 == 0:
                xor_row[x // 2] |= (color_idx << 4)
            else:
                xor_row[x // 2] |= (color_idx & 0x0F)

        xor_rows.append(bytes(xor_row))
        and_rows.append(bytes(and_row))

    xor_data = b"".join(xor_rows)
    and_data = b"".join(and_rows)

    palette_bytes = b"".join(struct.pack("<BBBB", b, g, r, 0) for b, g, r, _ in palette)

    dib_header = struct.pack(
        "<IIIHHIIIIII",
        40,                 # biSize
        32,                 # biWidth
        64,                 # biHeight (double for XOR + AND)
        1,                  # biPlanes
        4,                  # biBitCount
        0,                  # biCompression (BI_RGB)
        len(xor_data) + len(and_data), # biSizeImage
        0, 0,               # biXPelsPerMeter, biYPelsPerMeter
        16,                 # biClrUsed
        16                  # biClrImportant
    )

    image_data = dib_header + palette_bytes + xor_data + and_data

    ico_header = struct.pack("<HHH", 0, 1, 1)
    ico_entry = struct.pack(
        "<BBBBHHII",
        32,                 # bWidth
        32,                 # bHeight
        16,                 # bColorCount
        0,                  # bReserved
        1,                  # wPlanes
        4,                  # wBitCount
        len(image_data),    # dwBytesInRes
        22                  # dwImageOffset (6 + 16)
    )

    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as f:
        f.write(ico_header)
        f.write(ico_entry)
        f.write(image_data)

if __name__ == "__main__":
    img = create_screensaver_image()
    # Save to windir and example_apps/win31-screensaver/src/
    save_win16_ico(img, "windir/SCREENSV.ICO")
    save_win16_ico(img, "example_apps/win31-screensaver/src/win31ss.ico")
    print(f"Generated windir/SCREENSV.ICO ({os.path.getsize('windir/SCREENSV.ICO')} B)")
