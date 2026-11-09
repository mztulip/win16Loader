#!/usr/bin/env python3
"""
Generate DOSPRMPT.PIF - Windows 3.1 standard Program Information File (545 bytes).
Configured for COMMAND.COM, full-screen execution.
"""
import struct
import os

def create_dosprmpt_pif(output_path):
    pif = bytearray(545)

    # Offset 0x02: Title (30 bytes)
    title = b"FreeDOS Prompt\x00"
    pif[0x02:0x02+len(title)] = title

    # Offset 0x20: Max memory in KB (640 = 0x0280)
    struct.pack_into("<H", pif, 0x20, 640)
    # Offset 0x22: Min memory in KB (128 = 0x0080)
    struct.pack_into("<H", pif, 0x22, 128)

    # Offset 0x24: Program file name (64 bytes)
    prog = b"COMMAND.COM\x00"
    pif[0x24:0x24+len(prog)] = prog

    # Offset 0x66: Default working directory (64 bytes)
    defdir = b"C:\\\x00"
    pif[0x66:0x66+len(defdir)] = defdir

    # Offset 0xA6: Command line parameters (64 bytes)
    params = b"\x00"
    pif[0xA6:0xA6+len(params)] = params

    # Offset 0xE6: Screen exchange (0 = text mode / normal)
    pif[0xE6] = 0
    # Offset 0xE7: Initial pages
    pif[0xE7] = 1

    # Offset 0xEC: Pointer to 386 extension (offset 0x171 = 369)
    struct.pack_into("<H", pif, 0xEC, 0x0171)

    # Offset 0x171: 386 extension header
    sig = b"MICROSOFT PIFEX\x00"
    pif[0x171:0x171+16] = sig
    struct.pack_into("<H", pif, 0x171+16, 0xFFFF) # Next extension pointer
    struct.pack_into("<H", pif, 0x171+18, 0x0187) # Data offset
    struct.pack_into("<H", pif, 0x171+20, 0x0090) # Data size

    # Data at 0x187: 386 params
    # 0x187: Max memory (KB)
    struct.pack_into("<H", pif, 0x187, 640)
    # 0x189: Min memory (KB)
    struct.pack_into("<H", pif, 0x189, 128)
    # 0x193: Flags (bit 3 = Full Screen, bit 4 = Close on exit)
    # Flag 0x0008 = Fullscreen
    struct.pack_into("<I", pif, 0x197, 0x00000008)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(pif)
    print(f"Generated {output_path} ({len(pif)} bytes)")

if __name__ == "__main__":
    out1 = "win16Loader/windir/DOSPRMPT.PIF"
    create_dosprmpt_pif(out1)
    out2 = "win31_apps/english_calmira/calmii33/DOSPRMPT.PIF"
    if os.path.isdir(os.path.dirname(out2)):
        create_dosprmpt_pif(out2)
