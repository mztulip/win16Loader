#!/usr/bin/env python3
"""
Generate FREEDOS.ICO - True 16-color uncompressed Windows 3.1 compatible DIB ICO.
"""
import struct
from PIL import Image, ImageDraw

def create_freedos_image():
    # 32x32 RGBA
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Standard 16-color palette friendly colors
    # 0: Transparent
    # 1: Black / Navy outline: (0, 0, 128) or (0, 0, 0)
    # 9: Light Blue / Cyan: (0, 128, 255) / (0, 255, 255)
    # 11: Cyan: (0, 255, 255)
    # 15: Bright White: (255, 255, 255)
    # 7: Light Gray: (192, 192, 192)
    # 1: Blue: (0, 0, 255)

    c_outline = (0, 0, 128, 255)       # Navy
    c_body = (0, 128, 255, 255)        # Light Blue / FreeDOS Blue
    c_highlight = (0, 255, 255, 255)   # Cyan
    c_belly = (192, 220, 255, 255)     # Very light soft blue
    c_white = (255, 255, 255, 255)
    c_eye_pupil = (0, 0, 0, 255)
    c_fin = (0, 100, 220, 255)
    c_bubble = (192, 255, 255, 255)

    # Tail fin (left)
    draw.polygon([(3, 9), (10, 14), (7, 16), (10, 18), (3, 23), (5, 16)], fill=c_fin, outline=c_outline)
    draw.line([(4, 11), (8, 15)], fill=c_highlight, width=1)

    # Dorsal fin (top)
    draw.polygon([(14, 4), (20, 6), (23, 10), (13, 10)], fill=c_fin, outline=c_outline)
    draw.line([(16, 6), (20, 9)], fill=c_highlight, width=1)

    # Ventral fin (bottom)
    draw.polygon([(14, 24), (17, 28), (20, 25), (17, 23)], fill=c_fin, outline=c_outline)

    # Main Body (oval / fish shape)
    draw.ellipse([7, 9, 28, 25], fill=c_body, outline=c_outline)

    # Belly arc
    draw.chord([8, 14, 27, 25], start=0, end=180, fill=c_belly)
    draw.arc([8, 14, 27, 25], start=0, end=180, fill=c_outline)

    # Body highlight
    draw.arc([9, 10, 26, 23], start=210, end=290, fill=c_highlight, width=1)

    # Big friendly eye
    draw.ellipse([19, 10, 26, 17], fill=c_white, outline=c_outline)
    draw.ellipse([22, 12, 25, 15], fill=c_eye_pupil)
    draw.point((23, 12), fill=c_white)

    # Smile / Mouth
    draw.arc([24, 17, 28, 21], start=45, end=170, fill=c_outline, width=1)
    draw.point((27, 18), fill=c_outline)

    # Pectoral Fin (side fin)
    draw.polygon([(13, 16), (18, 18), (17, 22), (12, 19)], fill=c_fin, outline=c_outline)
    draw.line([(14, 17), (17, 19)], fill=c_highlight, width=1)

    # Cheerful Bubbles in front of fish
    draw.ellipse([27, 6, 29, 8], fill=c_bubble, outline=c_outline)
    draw.point((28, 6), fill=c_white)

    draw.ellipse([29, 2, 30, 3], fill=c_bubble, outline=c_outline)

    return img

def save_win16_ico(img, filepath):
    # Fixed 16-color palette (B, G, R, 0)
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
        (255, 128, 0, 0),     # 12: Light Blue / FreeDOS Blue (B=255, G=128, R=0)
        (255, 0, 255, 0),     # 13: Bright Magenta
        (255, 255, 0, 0),     # 14: Cyan (B=255, G=255, R=0)
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

    # Process from bottom row to top row (standard DIB order)
    for y in reversed(range(height)):
        xor_row = bytearray(16)  # 32 pixels * 4 bits = 16 bytes
        and_row = bytearray(4)   # 32 pixels * 1 bit = 4 bytes

        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            if a < 128:
                # Transparent pixel
                and_row[x // 8] |= (1 << (7 - (x % 8)))
                color_idx = 0
            else:
                # Opaque pixel
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

    with open(filepath, "wb") as f:
        f.write(ico_header)
        f.write(ico_entry)
        f.write(image_data)

if __name__ == "__main__":
    import os
    img = create_freedos_image()
    img.save("/tmp/freedos_preview.png")
    out_ico = "win16Loader/windir/FREEDOS.ICO"
    os.makedirs(os.path.dirname(out_ico), exist_ok=True)
    save_win16_ico(img, out_ico)
    # Also save to calmii33 if exists
    calmira_ico = "win31_apps/english_calmira/calmii33/FREEDOS.ICO"
    if os.path.isdir(os.path.dirname(calmira_ico)):
        save_win16_ico(img, calmira_ico)
    print(f"Generated {out_ico} (pure Win16 DIB ICO, size={os.path.getsize(out_ico)}B)")
