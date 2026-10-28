#!/usr/bin/env python3
"""gen_testres.py - generuje TESTRES.NE: minimalne NE z zasobami testowymi.

Zasoby:
  RT_STRING (0x8006) blok 1 (ID=1, stringi 0-15):
    ID 1 -> "Hello Test"
    ID 3 -> "World RC"
  RT_STRING (0x8006) blok 2 (ID=2, stringi 16-31):
    ID 17 -> "ABCDE"
  RT_BITMAP (0x8002) ID 1:
    16 znanych bajtow: DE AD BE EF 01..0C
  RT_MENU   (0x8004) ID 1:
    16 bajtow: 10..1F
  RT_ACCEL  (0x8009) ID 1:
    16 bajtow: 20..2F

Uklad pliku:
  0x000: MZ stub (64 B, e_lfanew=64)
  0x040: NE header (38 B, ne_rsrctab=38)
  0x066: tablica zasobow (96 B)
  0x0D0: dane zasobow (align=16)
"""
import struct
import os

ALIGN_SHIFT = 4
ALIGN = 1 << ALIGN_SHIFT  # 16

def align_up(n, a=ALIGN):
    return (n + a - 1) & ~(a - 1)

# --- dane zasobow ---------------------------------------------------------

def make_string_block(strings):
    """16 Pascalowych stringow (string = [len_byte, chars...])"""
    data = bytearray()
    for i in range(16):
        s = strings.get(i, '')
        b = s.encode('ascii')
        data.append(len(b))
        data.extend(b)
    # wyrownaj do wielokrotnosci ALIGN
    while len(data) % ALIGN != 0:
        data.append(0)
    return bytes(data)

block1 = make_string_block({1: 'Hello Test', 3: 'World RC'})   # 34 B -> 48 B
block2 = make_string_block({1: 'ABCDE'})                        # 21 B -> 32 B

bitmap1 = bytes([0xDE,0xAD,0xBE,0xEF, 0x01,0x02,0x03,0x04,
                 0x05,0x06,0x07,0x08, 0x09,0x0A,0x0B,0x0C])   # 16 B
menu1   = bytes(range(0x10, 0x20))                              # 16 B
accel1  = bytes(range(0x20, 0x30))                              # 16 B

# typ -> lista (id, dane)
TYPE_GROUPS = [
    (0x8006, [(1, block1), (2, block2)]),  # RT_STRING
    (0x8002, [(1, bitmap1)]),               # RT_BITMAP
    (0x8004, [(1, menu1)]),                 # RT_MENU
    (0x8009, [(1, accel1)]),               # RT_ACCEL
]

# --- oblicz uklad -----------------------------------------------------------
NE_OFF      = 64
NE_SIZE     = 38                      # sizeof(RC_NE)
RSCTAB_OFF  = NE_OFF + NE_SIZE        # 102

# rozmiar tablicy zasobow
rsctab_size = 2                       # rscAlignShift
for _, res_list in TYPE_GROUPS:
    rsctab_size += 8                  # naglowek bloku
    rsctab_size += len(res_list) * 12 # wpisy zasobow (6 x WORD)
rsctab_size += 2                      # znacznik konca

DATA_OFF = align_up(RSCTAB_OFF + rsctab_size)

# offsety poszczegolnych zasobow
flat_resources = [(t, rid, d) for t, rl in TYPE_GROUPS for rid, d in rl]
res_offsets = []
cur = DATA_OFF
for _, _, d in flat_resources:
    res_offsets.append(cur)
    cur = align_up(cur + len(d))
TOTAL = cur

# --- buduj plik -------------------------------------------------------------
buf = bytearray(TOTAL)

# MZ stub
struct.pack_into('<H', buf, 0, 0x5A4D)        # e_magic 'MZ'
struct.pack_into('<L', buf, 60, NE_OFF)        # e_lfanew

# NE header (ne_rsrctab = offset tablicy zasobow od poczatku NE)
struct.pack_into('<H', buf, NE_OFF + 0,  0x454E)  # ne_magic 'NE'
struct.pack_into('<H', buf, NE_OFF + 36, RSCTAB_OFF - NE_OFF)  # ne_rsrctab = 38

# tablica zasobow
pos = RSCTAB_OFF
struct.pack_into('<H', buf, pos, ALIGN_SHIFT); pos += 2   # rscAlignShift

res_idx = 0
for type_id, res_list in TYPE_GROUPS:
    struct.pack_into('<H', buf, pos, type_id);       pos += 2
    struct.pack_into('<H', buf, pos, len(res_list)); pos += 2
    struct.pack_into('<L', buf, pos, 0);             pos += 4  # rscProc
    for rid, d in res_list:
        file_off   = res_offsets[res_idx]
        rn_offset  = file_off >> ALIGN_SHIFT
        rn_length  = len(d)  >> ALIGN_SHIFT
        struct.pack_into('<H', buf, pos, rn_offset);      pos += 2
        struct.pack_into('<H', buf, pos, rn_length);      pos += 2
        struct.pack_into('<H', buf, pos, 0);              pos += 2  # rnFlags
        struct.pack_into('<H', buf, pos, rid | 0x8000);   pos += 2  # rnID (numeric)
        struct.pack_into('<H', buf, pos, 0);              pos += 2  # rnHandle
        struct.pack_into('<H', buf, pos, 0);              pos += 2  # rnUsage
        res_idx += 1

struct.pack_into('<H', buf, pos, 0)  # koniec tablicy

# dane zasobow
for i, (_, _, d) in enumerate(flat_resources):
    off = res_offsets[i]
    buf[off:off+len(d)] = d

# --- zapisz -----------------------------------------------------------------
outpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'TESTRES.NE')
with open(outpath, 'wb') as f:
    f.write(buf)

print(f"Wygenerowano {outpath} ({TOTAL} bajtow)")
print(f"  NE header  : off=0x{NE_OFF:03X}")
print(f"  rsctab     : off=0x{RSCTAB_OFF:03X}, size={rsctab_size}")
print(f"  dane start : off=0x{DATA_OFF:03X}")
for i, (t, rid, d) in enumerate(flat_resources):
    print(f"  typ=0x{t:04X} id={rid}: off=0x{res_offsets[i]:03X} size={len(d)}")
