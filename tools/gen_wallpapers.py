#!/usr/bin/env python3
import struct, math, random, os

def make_bmp_24bpp(w, h, pixel_fn):
    row_bytes = ((w * 3 + 3) // 4) * 4
    img_size = row_bytes * h
    file_size = 54 + img_size
    hdr = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54)
    info = struct.pack('<IIIHHIIIIII', 40, w, h, 1, 24, 0, img_size, 2835, 2835, 0, 0)
    data = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            b, g, r = pixel_fn(x, y)
            # clamp 0..255
            b = max(0, min(255, int(b)))
            g = max(0, min(255, int(g)))
            r = max(0, min(255, int(r)))
            row.extend([b, g, r])
        while len(row) < row_bytes:
            row.append(0)
        data.extend(row)
    return hdr + info + bytes(data)

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def lerp(c1, c2, t):
    t = clamp(t, 0.0, 1.0)
    return (
        c1[0] + (c2[0] - c1[0]) * t,
        c1[1] + (c2[1] - c1[1]) * t,
        c1[2] + (c2[2] - c1[2]) * t
    )

# -------------------------------------------------------------
# 1. CYBER95.BMP (640x480) - Synthwave Sunset & Neon Grid
# -------------------------------------------------------------
def gen_cyber95(w=640, h=480):
    horizon_y = 290
    sun_cx, sun_cy = 320, 240
    sun_r = 95
    grid_vp_x, grid_vp_y = 320, horizon_y

    # Precalculate horizontal perspective lines for the floor
    # Exponential spacing from horizon to bottom
    horiz_lines = []
    for i in range(1, 20):
        t = (i / 20.0) ** 2.2
        y = int(horizon_y + t * (h - horizon_y))
        if y < h:
            horiz_lines.append(y)

    def px(x, y):
        # 1. SKY / SUN AREA
        if y <= horizon_y:
            # Gradient colors: top violet -> deep magenta -> hot pink -> warm orange
            t = y / float(horizon_y)
            if t < 0.35:
                sky = lerp((45, 0, 25), (105, 0, 70), t / 0.35)
            elif t < 0.7:
                sky = lerp((105, 0, 70), (25, 0, 180), (t - 0.35) / 0.35) # (B, G, R)
            else:
                sky = lerp((25, 0, 180), (10, 110, 255), (t - 0.7) / 0.3)

            # Check Sun
            dx = x - sun_cx
            dy = y - sun_cy
            dist = math.sqrt(dx * dx + dy * dy)
            if dist <= sun_r:
                # Sun stripes: horizontal blinds on bottom half
                if dy > 0:
                    stripe_idx = int(dy / 8.0)
                    stripe_pos = dy % 8.0
                    gap = min(5.0, stripe_idx * 1.1)
                    if stripe_pos < gap:
                        return sky # transparent gap
                # Sun gradient: top yellow (0, 230, 255) -> bottom magenta (130, 0, 255)
                st = (dy + sun_r) / (2.0 * sun_r)
                sun_col = lerp((0, 230, 255), (140, 10, 255), st)
                return sun_col

            # Subtle horizon mountain silhouettes
            m1 = 28 + int(12 * math.sin(x * 0.02) + 8 * math.sin(x * 0.05 + 1.2))
            if y >= horizon_y - m1:
                return (35, 10, 45) # dark mountain silhouette

            return sky

        # 2. FLOOR GRID AREA
        else:
            fy = y - horizon_y
            fh = h - horizon_y
            t = fy / float(fh)

            # Floor base: dark purple/black gradient
            floor_base = lerp((20, 5, 15), (5, 0, 5), t)

            # Is this on a horizontal line?
            is_hline = any(abs(y - ly) <= 1 for ly in horiz_lines)

            # Perspective vertical grid lines:
            # At y, calculate perspective width
            scale = (y - horizon_y) / float(fh)
            # Distance from center at bottom
            if scale > 0.02:
                norm_x = (x - grid_vp_x) / scale
                # Grid spacing: every 40 px at full scale
                x_mod = abs(norm_x % 38.0)
                is_vline = (x_mod <= 1.2 or x_mod >= 36.8)
            else:
                is_vline = False

            if is_hline or is_vline:
                # Glowing neon line: cyan/turquoise or hot magenta
                glow_int = 0.5 + 0.5 * t
                if is_hline:
                    line_col = lerp((255, 230, 0), (255, 255, 100), t) # cyan glow
                else:
                    line_col = lerp((255, 50, 200), (255, 150, 0), t) # pink/magenta glow
                return (line_col[0] * glow_int, line_col[1] * glow_int, line_col[2] * glow_int)

            return floor_base

    return make_bmp_24bpp(w, h, px)

# -------------------------------------------------------------
# 2. HORIZON.BMP (640x480) - Minimalist Twilight Mountain Landscape
# -------------------------------------------------------------
def gen_horizon(w=640, h=480):
    # Stars map
    random.seed(42)
    stars = {}
    for _ in range(120):
        sx = random.randint(0, w - 1)
        sy = random.randint(0, 260)
        bright = random.choice([160, 210, 255])
        stars[(sx, sy)] = bright

    def px(x, y):
        # Sky: from deep dark navy (top) to soft teal-aquamarine to warm dusk at horizon
        if y < 310:
            t = y / 310.0
            if t < 0.6:
                col = lerp((38, 18, 8), (70, 45, 18), t / 0.6) # Deep midnight to navy
            else:
                col = lerp((70, 45, 18), (120, 110, 45), (t - 0.6) / 0.4) # navy to dusk teal

            # Stars
            if (x, y) in stars:
                b = stars[(x, y)]
                return (b, b, b)

            # Crescent moon at (520, 75)
            dx = x - 520
            dy = y - 75
            d = math.sqrt(dx * dx + dy * dy)
            if d < 18:
                # Cutout for crescent
                dx2 = x - 526
                dy2 = y - 73
                d2 = math.sqrt(dx2 * dx2 + dy2 * dy2)
                if d2 >= 16:
                    return (230, 245, 255)

            # Mountain layer 1 (distant light slate blue)
            m1 = int(75 * math.sin(x * 0.007 + 0.5) + 40 * math.sin(x * 0.018 + 2.0) + 15 * math.sin(x * 0.04))
            if y >= 250 - m1:
                return (85, 60, 40)

            # Mountain layer 2 (mid dark slate)
            m2 = int(90 * math.sin(x * 0.009 + 2.4) + 35 * math.sin(x * 0.025 + 0.8) + 12 * math.cos(x * 0.05))
            if y >= 285 - m2:
                return (55, 38, 24)

            # Mountain layer 3 (foreground jagged peaks)
            m3 = int(60 * math.sin(x * 0.013 + 4.1) + 25 * math.cos(x * 0.03 + 1.2))
            if y >= 310 - m3:
                return (35, 22, 14)

            return col

        # Water / lake reflection at bottom (y >= 310)
        else:
            wy = y - 310
            t = wy / 170.0
            # Ripples
            ripple = int(math.sin(x * 0.08 + wy * 0.3) * 3)
            lake_col = lerp((40, 25, 16), (20, 12, 8), t)
            # Add soft reflection of sky
            if ripple > 1:
                lake_col = (lake_col[0] + 15, lake_col[1] + 20, lake_col[2] + 25)
            return lake_col

    return make_bmp_24bpp(w, h, px)

# -------------------------------------------------------------
# 3. NEBULA.BMP (640x480) - Deep Space Cosmic Galaxy
# -------------------------------------------------------------
def gen_nebula(w=640, h=480):
    random.seed(99)
    stars = {}
    for _ in range(250):
        sx = random.randint(0, w - 1)
        sy = random.randint(0, h - 1)
        stars[(sx, sy)] = random.choice([(180, 180, 255), (255, 255, 255), (255, 210, 180)])

    # Bright cluster stars with cross glow
    big_stars = [(150, 120), (450, 210), (320, 360), (510, 390)]

    def px(x, y):
        # Cosmic swirling dust formula using layered sine waves
        nx = x * 0.007
        ny = y * 0.007
        v1 = math.sin(nx * 1.5 + math.cos(ny * 2.0))
        v2 = math.cos(ny * 1.3 - math.sin(nx * 2.2))
        v3 = math.sin(nx * 2.5 + ny * 2.5)
        plasma = (v1 + v2 + v3 + 3.0) / 6.0 # 0.0 .. 1.0

        # Galaxy core center at (360, 220)
        dx = (x - 360) / 220.0
        dy = (y - 220) / 140.0
        dist = math.sqrt(dx * dx + dy * dy)
        glow = math.exp(-dist * 1.6)

        # Color palette:
        # Base deep dark void (8, 4, 12)
        # Plasma clouds: vibrant purple, royal blue and electric cyan
        r = 12 + glow * 140 * plasma + plasma * 55
        g = 6 + glow * 70 * (plasma ** 1.5) + plasma * 40 * math.sin(nx * 4)
        b = 24 + glow * 210 + plasma * 110

        # Big stars with diffraction spikes
        for bx, by in big_stars:
            bdx = abs(x - bx)
            bdy = abs(y - by)
            if bdx == 0 and bdy <= 8:
                r += 200 - bdy * 20; g += 220 - bdy * 20; b += 255
            elif bdy == 0 and bdx <= 8:
                r += 200 - bdx * 20; g += 220 - bdx * 20; b += 255
            elif bdx <= 2 and bdy <= 2:
                r += 220; g += 240; b += 255

        # Background stars
        if (x, y) in stars:
            sc = stars[(x, y)]
            r += sc[2] * 0.8
            g += sc[1] * 0.8
            b += sc[0] * 0.8

        return (b, g, r)

    return make_bmp_24bpp(w, h, px)

# -------------------------------------------------------------
# 4. CIRCUITS.BMP (64x64, Seamless Tile) - Motherboard Tech
# -------------------------------------------------------------
def gen_circuits(s=64):
    # Fixed procedural PCB traces designed to tile seamlessly (modulo 64)
    # Background: dark matte graphite
    bg = (24, 20, 16)
    trace_col = (150, 160, 0) # Teal/Cyan (B, G, R)
    trace_hi = (210, 230, 20)
    via_col = (40, 180, 220)  # Gold/Copper

    def px(x, y):
        # Traces along specific coordinates modulo 64
        # Horizontal lines:
        h_lines = [10, 26, 42, 58]
        v_lines = [14, 30, 46, 62]

        is_trace = False
        is_hi = False

        # Segment 1: x from 0 to 64 along y=10
        if y == 10 or y == 42:
            is_trace = True
            is_hi = (y == 10)
        # Segment 2: vertical from y=10 to y=42 along x=30
        if (x == 30 and 10 <= y <= 42) or (x == 62 and (y >= 42 or y <= 10)):
            is_trace = True
        # Diagonal 45-deg corner: from (14, 26) to (26, 38)
        if (x - 14) == (y - 26) and 14 <= x <= 26:
            is_trace = True
        if (x - 46) == (y - 10) and 46 <= x <= 58:
            is_trace = True

        # Vias (circular pads) at junctions
        vias = [(10, 10), (30, 10), (30, 42), (58, 22), (46, 10), (14, 26), (26, 38)]
        for vx, vy in vias:
            d = (x - vx) ** 2 + (y - vy) ** 2
            if d <= 5:
                if d <= 1:
                    return (10, 10, 10) # center hole
                return via_col

        if is_trace:
            return trace_hi if is_hi else trace_col

        # Subtle noise / grid texture
        if (x % 16 == 0) or (y % 16 == 0):
            return (32, 26, 20)

        return bg

    return make_bmp_24bpp(s, s, px)

# -------------------------------------------------------------
# 5. HEXAGONS.BMP (48x48, Seamless Tile) - Carbon Fiber Honeycomb
# -------------------------------------------------------------
def gen_hexagons(s=48):
    def px(x, y):
        # Hex pattern with 2 centers per tile: (12, 12) and (36, 36)
        # Using isometric hex distance
        cx1, cy1 = 12, 12
        cx2, cy2 = 36, 36
        cx3, cy3 = 36, 12
        cx4, cy4 = 12, 36

        min_d = 999.0
        for cx, cy in [(cx1, cy1), (cx2, cy2), (cx3, cy3), (cx4, cy4)]:
            dx = abs(x - cx)
            dy = abs(y - cy)
            # Hex metric
            d = max(dx * 0.866 + dy * 0.5, dy)
            if d < min_d:
                min_d = d

        # Border at d ~ 9.0 .. 11.0
        if 9.5 <= min_d <= 11.5:
            # Highlight top-left, shadow bottom-right
            if (x + y) % 24 < 12:
                return (85, 75, 65) # highlight
            else:
                return (25, 20, 15) # shadow
        elif min_d < 9.5:
            # Inside hexagon: dark carbon with subtle glowing cyan center
            if min_d < 2.0:
                return (220, 180, 0) # Glowing cyan dot (B, G, R)
            shade = int(35 - min_d * 2)
            return (shade + 8, shade + 4, shade)
        else:
            return (18, 15, 12)

    return make_bmp_24bpp(s, s, px)

# -------------------------------------------------------------
# 6. RETROWV.BMP (64x64, Seamless Tile) - 3D Isometric Weave / Cubes
# -------------------------------------------------------------
def gen_retrowv(s=64):
    def px(x, y):
        # 32x32 block repeating 4 times in 64x64
        lx = x % 32
        ly = y % 32

        # Isometric cube faces:
        # Top face (diamond): ly from 0 to 16
        # Left face: lx < 16, ly >= 16
        # Right face: lx >= 16, ly >= 16
        dx = abs(lx - 16)
        if ly < 16:
            if dx <= ly:
                # Top face (Light slate teal)
                border = (dx == ly or ly == 15)
                return (160, 150, 80) if not border else (200, 190, 120)
            else:
                # Surrounding area: Deep slate
                return (65, 50, 30)
        else:
            if lx < 16:
                # Left face (Medium teal)
                return (110, 95, 45) if (lx != 0 and ly != 31) else (140, 125, 65)
            else:
                # Right face (Dark teal)
                return (80, 65, 30) if (lx != 31 and ly != 31) else (120, 100, 50)

    return make_bmp_24bpp(s, s, px)

def main():
    os.makedirs("windir", exist_ok=True)
    wallpapers = [
        ("windir/CYBER95.BMP", gen_cyber95(640, 480)),
        ("windir/HORIZON.BMP", gen_horizon(640, 480)),
        ("windir/NEBULA.BMP",  gen_nebula(640, 480)),
        ("windir/CIRCUITS.BMP", gen_circuits(64)),
        ("windir/HEXAGONS.BMP", gen_hexagons(48)),
        ("windir/RETROWV.BMP",  gen_retrowv(64)),
    ]
    for path, data in wallpapers:
        with open(path, "wb") as f:
            f.write(data)
        print(f"Generated {path}: {len(data)} bytes")

if __name__ == "__main__":
    main()
