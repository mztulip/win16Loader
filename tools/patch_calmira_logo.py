#!/usr/bin/env python3
"""patch_calmira_logo.py - podmienia logo Windows w przycisku Start w CALMIRA.EXE
na ikone procesora i486 z nozkami (Loader16 i486).
"""
import sys, os, struct

def create_i486_tile():
    # 16x16 grid of 4-bit palette indices:
    # 13: background (transparent)
    # 0: black
    # 7: dark gray
    # 8: light gray / silver
    # 11: gold / yellow
    # 15: white
    # 4: dark navy / ceramic
    # 1: maroon
    B = 13 # transparent background
    G = 11 # gold
    S = 8  # silver
    K = 0  # black
    D = 7  # dark gray
    C = 4  # ceramic body (dark navy / ceramic)
    W = 15 # white

    # 16 rows, each 16 pixels (y=0 top to y=15 bottom)
    # y=0: empty
    # y=1..2: top pins
    # y=3..12: chip body with left/right pins
    # y=13..14: bottom pins
    # y=15: empty

    rows = [
        # y=0
        [B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B],
        # y=1 (top pins)
        [B, B, B, G, B, G, B, G, B, G, B, G, B, B, B, B],
        # y=2 (top pins)
        [B, B, B, G, B, G, B, G, B, G, B, G, B, B, B, B],
        # y=3 (chip top border)
        [B, B, G, D, D, D, D, D, D, D, D, D, D, G, B, B],
        # y=4 (pin 1 marker)
        [B, G, B, D, G, C, C, C, C, C, C, C, K, B, G, B],
        # y=5
        [B, G, B, D, C, C, C, C, C, C, C, C, K, B, G, B],
        # y=6 (text '4 8 6' row 1)
        # '4': x=4,6 ; '8': x=7,8,9 ; '6': x=10,11,12
        [B, B, B, D, G, C, G, G, G, G, G, G, G, B, B, B],
        # y=7 (text '4 8 6' row 2)
        [B, G, B, D, G, C, G, G, C, G, G, C, C, B, G, B],
        # y=8 (text '4 8 6' row 3)
        [B, G, B, D, G, G, G, G, G, G, G, G, G, B, G, B],
        # y=9 (text '4 8 6' row 4)
        [B, B, B, D, C, C, G, G, C, G, G, C, G, B, B, B],
        # y=10 (text '4 8 6' row 5)
        [B, G, B, D, C, C, G, G, G, G, G, G, G, B, G, B],
        # y=11
        [B, G, B, D, C, C, C, C, C, C, C, C, K, B, G, B],
        # y=12 (chip bottom border)
        [B, B, G, K, K, K, K, K, K, K, K, K, K, G, B, B],
        # y=13 (bottom pins)
        [B, B, B, G, B, G, B, G, B, G, B, G, B, B, B, B],
        # y=14 (bottom pins)
        [B, B, B, G, B, G, B, G, B, G, B, G, B, B, B, B],
        # y=15
        [B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B],
    ]
    return rows

def set_icon_pixel(data, off, x, y, color):
    # color: None (transparent) or palette index 0..15
    xor_start = off + 40 + 64
    and_start = xor_start + 32 * 16
    y_bmp = 31 - y
    and_off = and_start + y_bmp * 4 + x // 8
    xor_off = xor_start + y_bmp * 16 + x // 2
    shift = 7 - (x % 8)
    if color is None:
        data[and_off] |= (1 << shift)
        if x % 2 == 0: data[xor_off] &= 0x0f
        else: data[xor_off] &= 0xf0
    else:
        data[and_off] &= ~(1 << shift)
        if x % 2 == 0: data[xor_off] = (data[xor_off] & 0x0f) | ((color & 0x0f) << 4)
        else: data[xor_off] = (data[xor_off] & 0xf0) | (color & 0x0f)

def patch_fileicon(data, off):
    # Clear center flag area to white (color 15)
    for y in range(10, 24):
        for x in range(5, 24):
            set_icon_pixel(data, off, x, y, 15)

    NAVY = 4
    GOLD = 11
    BLACK = 0

    # Chip body: 12x10 from x=9..20, y=12..21
    for cy in range(12, 22):
        for cx in range(9, 21):
            c = BLACK if (cx in (9, 20) or cy in (12, 21)) else NAVY
            set_icon_pixel(data, off, cx, cy, c)

    # Pins
    for px in (11, 13, 15, 17, 19):
        set_icon_pixel(data, off, px, 11, GOLD)
        set_icon_pixel(data, off, px, 22, GOLD)
    for py in (14, 16, 18, 20):
        set_icon_pixel(data, off, 8, py, GOLD)
        set_icon_pixel(data, off, 21, py, GOLD)

    # Pin 1 marker
    set_icon_pixel(data, off, 10, 13, GOLD)

    # Text '486' in gold
    for p in [(11,15),(11,16),(11,17),(12,17),(13,15),(13,16),(13,17),(13,18),(13,19)]:
        set_icon_pixel(data, off, p[0], p[1], GOLD)
    for p in [(14,15),(15,15),(16,15),(14,16),(16,16),(14,17),(15,17),(16,17),(14,18),(16,18),(14,19),(15,19),(16,19)]:
        set_icon_pixel(data, off, p[0], p[1], GOLD)
    for p in [(17,15),(18,15),(19,15),(17,16),(17,17),(18,17),(19,17),(17,18),(19,18),(17,19),(18,19),(19,19)]:
        set_icon_pixel(data, off, p[0], p[1], GOLD)

def patch_tinyfileicon(data, off):
    # Clear center flag area to white (color 15)
    for y in range(5, 12):
        for x in range(2, 10):
            set_icon_pixel(data, off, x, y, 15)

    NAVY = 4
    GOLD = 11
    BLACK = 0

    # Mini chip body at x=3..8, y=6..10
    for cy in range(6, 11):
        for cx in range(3, 9):
            c = BLACK if (cx in (3, 8) or cy in (6, 10)) else NAVY
            set_icon_pixel(data, off, cx, cy, c)

    # Pins
    for px in (4, 6):
        set_icon_pixel(data, off, px, 5, GOLD)
        set_icon_pixel(data, off, px, 11, GOLD)
    for py in (7, 9):
        set_icon_pixel(data, off, 2, py, GOLD)
        set_icon_pixel(data, off, 9, py, GOLD)

    # Pin 1 marker
    set_icon_pixel(data, off, 4, 7, GOLD)

    # Center mark
    for p in [(5,8),(6,8),(7,8),(5,9),(7,9)]:
        set_icon_pixel(data, off, p[0], p[1], GOLD)

def patch_calmira_exe(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    # 1. Patch DFM StartButton Glyph embedded in Form definition (delphi TSpeedButton)
    dfm_patched = False
    pos = data.find(b'StartButton')
    while pos != -1:
        glyph = data.find(b'Glyph.Data', pos, pos + 500)
        if glyph != -1:
            bm = data.find(b'BM', glyph, glyph + 50)
            if bm != -1:
                sig, size, res1, res2, b_off = struct.unpack_from('<2sIIII', data, bm)
                hdr_sz, w, h, pl, bpp = struct.unpack_from('<IIIHH', data, bm + 14)
                if w == 16 and h == 16 and bpp == 4:
                    print(f'Found StartButton DFM glyph BMP at {hex(bm)} ({w}x{h})')
                    bits_start = bm + 54 + 64
                    tile_rows = create_i486_tile()
                    for y_screen in range(16):
                        y_bmp = 15 - y_screen
                        row_start = bits_start + y_bmp * 8
                        pix = tile_rows[y_screen]
                        for byte_idx in range(8):
                            p1 = pix[byte_idx * 2]
                            p2 = pix[byte_idx * 2 + 1]
                            if p1 == 13: p1 = 7
                            if p2 == 13: p2 = 7
                            data[row_start + byte_idx] = ((p1 & 0x0f) << 4) | (p2 & 0x0f)
                    dfm_patched = True
        pos = data.find(b'StartButton', pos + 1)

    if dfm_patched:
        print('Successfully patched StartButton DFM Glyph!')
    else:
        print('Warning: StartButton DFM Glyph not found!')

    # Find NE header
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    ne = e_lfanew
    rto = struct.unpack_from('<H', data, ne + 0x24)[0] + ne
    align = struct.unpack_from('<H', data, rto)[0]

    # Find resource STARTBMPS, ICONs, and GROUP_ICONs
    p = rto + 2
    target_off = None
    target_len = None
    icons_by_id = {}
    group_icons = {}

    while True:
        type_id = struct.unpack_from('<H', data, p)[0]
        if type_id == 0: break
        count = struct.unpack_from('<H', data, p + 2)[0]
        raw_type = type_id & 0x7fff
        p += 8
        for i in range(count):
            off = struct.unpack_from('<H', data, p)[0] << align
            length = struct.unpack_from('<H', data, p + 2)[0] << align
            id_field = struct.unpack_from('<H', data, p + 6)[0]
            if not (id_field & 0x8000):
                str_off = rto + id_field
                slen = data[str_off]
                name = data[str_off+1:str_off+1+slen].decode('latin1', 'replace').upper()
                if raw_type == 2 and name == 'STARTBMPS':
                    target_off = off
                    target_len = length
                elif raw_type == 14 and length >= 20:
                    icon_id = struct.unpack_from('<H', data, off + 6 + 12)[0]
                    group_icons[name] = icon_id
            elif raw_type == 3:
                icons_by_id[id_field & 0x7fff] = off
            p += 12

    if target_off is None:
        sys.exit('STARTBMPS resource not found in ' + path)

    print(f'Found STARTBMPS at offset {hex(target_off)} (length {target_len})')
    # Check DIB header: 40 bytes BITMAPINFOHEADER
    w, h, bpp = struct.unpack_from('<II2xH', data, target_off + 4)
    if w != 272 or h != 16 or bpp != 4:
        sys.exit(f'Unexpected STARTBMPS geometry: {w}x{h} {bpp}bpp')

    bits_off = target_off + 40 + 16 * 4 # header + 16 RGBQUAD palette entries
    row_stride = ((w * bpp + 31) // 32) * 4 # 136 bytes

    tile_rows = create_i486_tile() # y=0 (top) to y=15 (bottom)

    # In BMP, rows are stored bottom-to-top:
    # row 0 in BMP file is y=15 (bottom)
    # row 15 in BMP file is y=0 (top)
    for y_screen in range(16):
        y_bmp = 15 - y_screen
        row_start = bits_off + y_bmp * row_stride
        # Tile 1 is at x=16..31 (bytes 8..15)
        # Tile 15 is at x=240..255 (bytes 120..127) - this is the actual Start button flag!
        pix = tile_rows[y_screen]
        for byte_idx in range(8):
            p1 = pix[byte_idx * 2] & 0x0f
            p2 = pix[byte_idx * 2 + 1] & 0x0f
            val = (p1 << 4) | p2
            data[row_start + 8 + byte_idx] = val
            data[row_start + 120 + byte_idx] = val

    # 3. Patch FILEICON and TINYFILEICON (resolved via RT_GROUP_ICON, fallback to 6 / 31)
    fileicon_id = group_icons.get('FILEICON', 6)
    tinyfileicon_id = group_icons.get('TINYFILEICON', 31)
    if fileicon_id in icons_by_id:
        patch_fileicon(data, icons_by_id[fileicon_id])
        print(f'Successfully patched FILEICON (Icon #{fileicon_id}) at {hex(icons_by_id[fileicon_id])} with i486 document logo!')
    if tinyfileicon_id in icons_by_id:
        patch_tinyfileicon(data, icons_by_id[tinyfileicon_id])
        print(f'Successfully patched TINYFILEICON (Icon #{tinyfileicon_id}) at {hex(icons_by_id[tinyfileicon_id])} with i486 mini document logo!')

    with open(path, 'wb') as f:
        f.write(data)
    print(f'Successfully patched i486 CPU logo into {path}!')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: patch_calmira_logo.py <path_to_CALMIRA.EXE>')
        sys.exit(1)
    patch_calmira_exe(sys.argv[1])
