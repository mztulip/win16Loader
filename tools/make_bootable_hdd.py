#!/usr/bin/env python3
"""
Creates a bootable hard disk image (C:) with FreeDOS, Windows 16-bit runtime,
and Calmira II.
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

def run(cmd, **kwargs):
    kwargs.setdefault("check", True)
    return subprocess.run(cmd, **kwargs)

def main():
    parser = argparse.ArgumentParser(description="Create bootable FreeDOS+Calmira C: HDD image")
    parser.add_argument("--out", default="calmira.img", help="Output HDD image path")
    parser.add_argument("--size", type=int, default=64, help="Image size in MB (default: 64)")
    parser.add_argument("--fdboot", default="FD14-FloppyEdition/144m/x86BOOT.img", help="FreeDOS boot floppy")
    parser.add_argument("--calmira-dir", default="../win31_apps/english_calmira/calmii33", help="Calmira directory")
    parser.add_argument("--wine-fonts", default="fonts", help="Fonts directory")
    parser.add_argument("--user-bin", default="user/USER.EXE", help="USER.EXE path")
    parser.add_argument("--gdi-bin", default="gdi/GDI.EXE", help="GDI.EXE path")
    parser.add_argument("--notepad-exe", default="example_apps/notepad/NOTEPAD.exe", help="NOTEPAD.EXE path")
    parser.add_argument("--paint-exe", default="example_apps/paint/PAINT.EXE", help="PAINT.EXE path")
    parser.add_argument("--mario-dir", default="../games/mario", help="Mario game directory")
    args = parser.parse_args()

    out_img = args.out
    if os.path.exists(out_img):
        os.remove(out_img)

    size_mb = args.size
    print(f"Creating {size_mb}MB disk image: {out_img}...")
    run(["truncate", "-s", f"{size_mb}M", out_img])

    # 1. Partition with sfdisk (start=2048 = 1MB alignment, type=6 = FAT16, bootable)
    print("Partitioning with sfdisk...")
    run(["sfdisk", "-q", out_img], input=b"start=2048, type=6, bootable\n")

    # 2. Format partition 1 as FAT16
    print("Formatting FAT16 filesystem...")
    num_blocks = (size_mb * 1024 * 1024 - 2048 * 512) // 1024
    run(["mkfs.fat", "-F", "16", "-n", "CALMIRA", "--offset", "2048", out_img, str(num_blocks)],
        stdout=subprocess.DEVNULL)

    # 3. Write FreeDOS FAT16 VBR and patch geometry
    print("Writing FreeDOS FAT16 VBR...")
    with tempfile.NamedTemporaryFile() as tmp:
        with open(out_img, "rb") as f:
            f.seek(2048 * 512)
            vbr = bytearray(f.read(512))
        tmp.write(vbr)
        tmp.flush()

        run(["ms-sys", "-f", "--fat16free", tmp.name], stdout=subprocess.DEVNULL)

        with open(tmp.name, "rb") as f:
            new_vbr = bytearray(f.read(512))

        # Patch hidden_sectors to 2048 (offset 0x1C, 4 bytes uint32)
        struct.pack_into("<I", new_vbr, 0x1C, 2048)
        # Patch drive number to 0x80 (offset 0x24, 1 byte uint8)
        new_vbr[0x24] = 0x80

        with open(out_img, "r+b") as f:
            f.seek(2048 * 512)
            f.write(new_vbr)

    # 4. Write Windows 7 LBA MBR to sector 0
    print("Writing MBR boot code...")
    run(["ms-sys", "-f", "--mbr7", out_img], stdout=subprocess.DEVNULL)

    mpart = f"{out_img}@@1M"

    # 5. Extract and copy FreeDOS system files
    print("Copying FreeDOS system files...")
    run(["mmd", "-i", mpart, "::FREEDOS"], check=False)
    run(["mmd", "-i", mpart, "::FREEDOS/BIN"], check=False)

    with tempfile.TemporaryDirectory() as tmp_fd:
        # Extract files from FDBOOT: KERNEL.SYS is in root, COMMAND.COM/COUNTRY.SYS are in /freedos/bin/
        kernel_tmp = os.path.join(tmp_fd, "KERNEL.SYS")
        run(["mcopy", "-oi", args.fdboot, "::KERNEL.SYS", kernel_tmp])
        run(["mcopy", "-oi", mpart, kernel_tmp, "::KERNEL.SYS"])

        cmd_tmp = os.path.join(tmp_fd, "COMMAND.COM")
        run(["mcopy", "-oi", args.fdboot, "::FREEDOS/BIN/COMMAND.COM", cmd_tmp])
        run(["mcopy", "-oi", mpart, cmd_tmp, "::COMMAND.COM"])
        run(["mcopy", "-oi", mpart, cmd_tmp, "::FREEDOS/BIN/COMMAND.COM"])

        country_tmp = os.path.join(tmp_fd, "COUNTRY.SYS")
        run(["mcopy", "-oi", args.fdboot, "::FREEDOS/BIN/COUNTRY.SYS", country_tmp])
        run(["mcopy", "-oi", mpart, country_tmp, "::COUNTRY.SYS"])
        run(["mcopy", "-oi", mpart, country_tmp, "::FREEDOS/BIN/COUNTRY.SYS"])

    # 6. Create FDCONFIG.SYS and FDAUTO.BAT
    print("Writing FDCONFIG.SYS and FDAUTO.BAT...")
    fdconfig = (
        "!LASTDRIVE=Z\r\n"
        "!BUFFERS=10\r\n"
        "!FILES=40\r\n"
        "SHELL=C:\\FREEDOS\\BIN\\COMMAND.COM C:\\FREEDOS\\BIN /E:512 /P=C:\\FDAUTO.BAT\r\n"
    )
    fdauto = (
        "@echo off\r\n"
        "SET PATH=C:\\WINDOWS;C:\\CALMIRA;C:\\FREEDOS\\BIN\r\n"
        "SET PROMPT=\r\n"
        "C:\r\n"
        "cd \\CALMIRA\r\n"
        "WIN CALMIRA.EXE\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(fdconfig)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::FDCONFIG.SYS"])

    with tempfile.NamedTemporaryFile("w") as f:
        f.write(fdauto)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::FDAUTO.BAT"])

    # 7. Copy Windows files to C:\WINDOWS
    print("Copying Windows files to ::WINDOWS...")
    run(["mmd", "-i", mpart, "::WINDOWS"], check=False)

    if not os.path.exists("windir/FREEDOS.ICO") and os.path.exists("tools/make_freedos_ico.py"):
        run(["python3", "tools/make_freedos_ico.py"])
    if not os.path.exists("windir/DOSPRMPT.PIF") and os.path.exists("tools/make_pif.py"):
        run(["python3", "tools/make_pif.py"])

    win_files = [
        ("loader.exe", "LOADER.EXE"),
        ("kernel.exe", "KERNEL.EXE"),
        (args.user_bin, "USER.EXE"),
        (args.gdi_bin, "GDI.EXE"),
        ("WIN16API.DB", "WIN16API.DB"),
        ("windir/WIN.INI", "WIN.INI"),
        ("windir/SYSTEM.INI", "SYSTEM.INI"),
        ("windir/DOSPRMPT.PIF", "DOSPRMPT.PIF"),
        ("windir/FREEDOS.ICO", "FREEDOS.ICO"),
        ("control.exe", "CONTROL.EXE"),
    ]
    notepad_candidates = [
        args.notepad_exe,
        "example_apps/notepad/NOTEPAD.exe",
        "win16_example_app/NOTEPAD.exe",
        "win16_example_app/notepad/NOTEPAD.exe",
        "../notepad/NOTEPAD.exe",
    ]
    for c in notepad_candidates:
        if c and os.path.exists(c):
            win_files.append((c, "NOTEPAD.EXE"))
            break

    paint_candidates = [
        args.paint_exe,
        "example_apps/paint/PAINT.EXE",
        "example_apps/paint/PAINT.exe",
    ]
    for c in paint_candidates:
        if c and os.path.exists(c):
            win_files.append((c, "PBRUSH.EXE"))
            break

    for src, dst in win_files:
        run(["mcopy", "-oi", mpart, src, f"::WINDOWS/{dst}"])

    # DLLs
    dlls = glob.glob("dlls/*.DLL") + glob.glob("dlls/*.DRV")
    for dll in dlls:
        dst = os.path.basename(dll).upper()
        run(["mcopy", "-oi", mpart, dll, f"::WINDOWS/{dst}"])

    # Fonts
    font_names = ["sserife", "sserifb", "vgasys", "vgafix", "coure", "smalle"]
    for fnt in font_names:
        src = os.path.join(args.wine_fonts, f"{fnt}.fon")
        dst = f"{fnt.upper()}.FON"
        if os.path.exists(src):
            run(["mcopy", "-oi", mpart, src, f"::WINDOWS/{dst}"])

    # C:\WINDOWS\WIN.BAT
    win_bat = (
        "@echo off\r\n"
        ":again\r\n"
        "C:\\WINDOWS\\LOADER.EXE %1 %2 %3 %4 %5\r\n"
        "rem kod 42 = ExitWindows(EW_RESTARTWINDOWS): uruchom od nowa\r\n"
        "if errorlevel 42 goto again\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(win_bat)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::WINDOWS/WIN.BAT"])

    # 8. Copy Calmira files and subdirectories (e.g. TRAY with quicklaunch icons)
    print("Copying Calmira files to ::CALMIRA...")
    run(["mmd", "-i", mpart, "::CALMIRA"], check=False)
    for cfile in glob.glob(f"{args.calmira_dir}/*"):
        if os.path.isfile(cfile):
            run(["mcopy", "-oi", mpart, cfile, "::CALMIRA/"])
        elif os.path.isdir(cfile):
            subname = os.path.basename(cfile).upper()
            run(["mmd", "-i", mpart, f"::CALMIRA/{subname}"], check=False)
            for subfile in glob.glob(f"{cfile}/*"):
                if os.path.isfile(subfile):
                    run(["mcopy", "-oi", mpart, subfile, f"::CALMIRA/{subname}/"])

    run(["mcopy", "-oi", mpart, "windir/DOSPRMPT.PIF", "::CALMIRA/DOSPRMPT.PIF"])
    run(["mcopy", "-oi", mpart, "windir/FREEDOS.ICO", "::CALMIRA/FREEDOS.ICO"])

    # 9. Create C:\SKIFREE with SKI.EXE and README.TXT
    print("Setting up C:\\SKIFREE and documentation...")
    run(["mmd", "-i", mpart, "::SKIFREE"], check=False)
    if os.path.exists("SKIFREE/SKI.EXE"):
        run(["mcopy", "-oi", mpart, "SKIFREE/SKI.EXE", "::SKIFREE/SKI.EXE"])

    skifree_readme = (
        "======================================================================\r\n"
        "               SKIFREE DLA WINDOWS (WIN16 / CALMIRA)\r\n"
        "======================================================================\r\n\r\n"
        "Autor oryginalny: Chris Pirih\r\n"
        "Oficjalna strona autora: https://ski.ihoc.net/\r\n\r\n"
        "O GRZE SKIFREE:\r\n"
        "---------------\r\n"
        "SkiFree to jedna z najbardziej znanych i kultowych gier dla srodowiska\r\n"
        "Microsoft Windows. Zostala stworzona przez Chrisa Piriha w 1991 roku,\r\n"
        "poczatkowo na wlasny uzytek, a nastepnie wlaczona do oficjalnego pakietu\r\n"
        "Microsoft Windows Entertainment Pack (WEP 3).\r\n\r\n"
        "Gra stala sie legenda dzieki wspanialej dynamice, humorowi oraz\r\n"
        "niezapomnianemu potworowi snieznemu (Abominable Snow Monster / Yeti),\r\n"
        "ktory pojawia sie po przekroczeniu 2000 metrow zjazdu i goni narciarza!\r\n\r\n"
        "ZASADY I STEROWANIE:\r\n"
        "--------------------\r\n"
        "- Strzalka w lewo / 4:  Skret w lewo\r\n"
        "- Strzalka w prawo / 6: Skret w prawo\r\n"
        "- Strzalka w dol / 2:   Zjazd wprost w dol (przyspieszenie)\r\n"
        "- Klawisz F:            Maksymalna predkosc (Fast!)\r\n"
        "- Klawisz F3:           Rozpoczecie nowej gry (Restart)\r\n"
        "- Mysz:                 Mozna rowniez sterowac ruchem kursora myszy!\r\n\r\n"
        "PRZESZKODY:\r\n"
        "-----------\r\n"
        "Omijaj drzewa, skaly, pnie, slupy wyciagu krzeselkowego oraz innych\r\n"
        "narciarzy i snowboardzistow. Wybijaj sie z muld (moguls) i wykonuj\r\n"
        "efektowne skoki narciarskie!\r\n\r\n"
        "WIECEJ INFORMACJI:\r\n"
        "------------------\r\n"
        "Odwiedz oficjalna strone autora Chrisa Piriha:\r\n"
        "https://ski.ihoc.net/\r\n\r\n"
        "Srodowisko uruchomieniowe przygotowane przez: mztulip\r\n"
        "Dziala na FreeDOS + Calmira II (Win16Loader).\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(skifree_readme)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::SKIFREE/README.TXT"])

    # 10. Create C:\MARIO with MARIO.EXE, MARIO.CFG and README.TXT
    print("Setting up C:\\MARIO and documentation...")
    run(["mmd", "-i", mpart, "::MARIO"], check=False)
    mario_dir = args.mario_dir
    if not os.path.isdir(mario_dir):
        mario_dir = "/home/tulip/Dokumenty/Projekty_moje/osproject/games/mario"

    if os.path.exists(f"{mario_dir}/MARIO.EXE"):
        run(["mcopy", "-oi", mpart, f"{mario_dir}/MARIO.EXE", "::MARIO/MARIO.EXE"])
    if os.path.exists(f"{mario_dir}/MARIO.CFG"):
        run(["mcopy", "-oi", mpart, f"{mario_dir}/MARIO.CFG", "::MARIO/MARIO.CFG"])

    mario_readme = (
        "======================================================================\r\n"
        "               MARIO (MARIO & LUIGI) DLA DOS / CALMIRA\r\n"
        "======================================================================\r\n\r\n"
        "Autor: Mike Wiering (Wiering Software)\r\n"
        "Oficjalna strona projektu: https://wieringsoftware.nl/mario/index.html\r\n\r\n"
        "O GRZE MARIO (1994):\r\n"
        "--------------------\r\n"
        "Klasyczny klon gry Super Mario Bros. stworzony w 1994 roku przez\r\n"
        "holenderskiego programiste Mike'a Wieringa na komputery PC z DOS-em.\r\n\r\n"
        "Gra powstala jako cwiczenie z programowania grafiki VGA w jezyku\r\n"
        "Borland Turbo Pascal, w celu osiagniecia plynnego przewijania ekranu\r\n"
        "(parallax scrolling) na procesorze 486 25 MHz w trybie 320x200 (page flipping).\r\n\r\n"
        "HISTORIA I KOD ZRODLOWY:\r\n"
        "------------------------\r\n"
        "Wczesna, niedokonczona wersja demonstracyjna gry (z 4 poziomami) wyciekla\r\n"
        "w latach 90. i stala sie hitem na gieldach komputerowych i plytach CD\r\n"
        "ze shareware. Wielu graczy pamietalo ja jako gre, ktorej nie dalo sie\r\n"
        "ukonczyc z powodu braku kolejnych etapow w wersji testowej.\r\n\r\n"
        "Po latach autor udostepnil oficjalna, pelna wersje (6 poziomow, wybor\r\n"
        "postaci Mario lub Luigi) wraz z pelnym kodem zrodlowym w Turbo Pascalu\r\n"
        "na swojej stronie: https://wieringsoftware.nl/mario/index.html\r\n\r\n"
        "Silnik ten posluzyl autorowi pozniej do stworzenia kultowej gry\r\n"
        "'Charlie the Duck'.\r\n\r\n"
        "STEROWANIE:\r\n"
        "-----------\r\n"
        "- Strzalki w lewo / w prawo:  Poruszanie sie\r\n"
        "- Strzalka w dol:             Kucanie / wchodzenie do rur\r\n"
        "- Strzalka w gore / Spacja / Alt / Ctrl / Shift: Skok\r\n"
        "- Po zakonczeniu gry powrot do Calmiry nastepuje automatycznie!\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(mario_readme)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::MARIO/README.TXT"])

    # 11. Root documentation and links: C:\README.TXT and C:\LINKI.TXT
    root_readme = (
        "======================================================================\r\n"
        "     SYSTEM CALMIRA II NA FREEDOS - DOKUMENTACJA I WOLNY SOFT\r\n"
        "======================================================================\r\n\r\n"
        "Autor modyfikacji i integracji: mztulip\r\n"
        "Projekt Win16Loader: https://github.com/mztulip/win16Loader\r\n\r\n"
        "System bazowy: FreeDOS (https://www.freedos.org/)\r\n"
        "Powloka graficzna: Calmira II 3.3 (http://www.calmira.org/)\r\n"
        "Edytor tekstu: Notepad (skompilowany w Free Pascalu dla i8086-win16)\r\n"
        "Kompilatory: Free Pascal (https://www.freepascal.org/) & Open Watcom\r\n\r\n"
        "STRUKTURA DYSKU C:\r\n"
        "------------------\r\n"
        "C:\\WINDOWS\\       - Srodowisko uruchomieniowe Win16 (Win16Loader, DLL, fonty)\r\n"
        "C:\\WINDOWS\\NOTEPAD.EXE - Edytor tekstu (autor: mztulip, FPC)\r\n"
        "C:\\WINDOWS\\PBRUSH.EXE  - PaintCraft Studio (grafika i rysowanie)\r\n"
        "C:\\CALMIRA\\       - Powloka graficzna Calmira II\r\n"
        "C:\\FREEDOS\\BIN\\   - Narzedzia i powloka systemu FreeDOS\r\n"
        "C:\\SKIFREE\\       - Kultowa gra narciarska SkiFree (Chris Pirih)\r\n"
        "C:\\MARIO\\         - Klasyczna gra Mario (Mike Wiering, 1994, Turbo Pascal)\r\n"
        "A:\\               - Stacja dyskietek z plikami testowymi i dokumentacja\r\n\r\n"
        "LINKI DO PROJEKTOW I DARMOWEGO OPROGRAMOWANIA:\r\n"
        "----------------------------------------------\r\n"
        "1. Projekt Win16Loader (kod zrodlowy, dokumentacja, loader):\r\n"
        "   https://github.com/mztulip/win16Loader\r\n\r\n"
        "2. Calmira II Shell:\r\n"
        "   http://www.calmira.org/\r\n"
        "   Otwarta powloka graficzna w stylu Windows 95 dla Win16 (GNU GPL).\r\n\r\n"
        "3. FreeDOS Project:\r\n"
        "   https://www.freedos.org/\r\n"
        "   Calkowicie wolny system operacyjny DOS.\r\n\r\n"
        "4. Free Pascal Compiler (FPC):\r\n"
        "   https://www.freepascal.org/\r\n"
        "   Wieloplatformowy kompilator Object Pascal ze wsparciem dla Win16.\r\n\r\n"
        "5. SkiFree (Chris Pirih):\r\n"
        "   https://ski.ihoc.net/\r\n"
        "   Oficjalna strona autora legendarnej gry narciarskiej.\r\n\r\n"
        "6. Mario & Luigi (Mike Wiering / Wiering Software):\r\n"
        "   https://wieringsoftware.nl/mario/index.html\r\n"
        "   Kultowy klon Super Mario Bros. z 1994 roku z pelnym kodem w Turbo Pascalu.\r\n\r\n"
        "7. Open Watcom:\r\n"
        "   https://openwatcom.org/\r\n"
        "   Otwarty kompilator C/C++ uzyty do zbudowania modulu loadera i KERNEL.\r\n\r\n"
        "8. Wine Project:\r\n"
        "   https://www.winehq.org/\r\n"
        "   Zrodlo fontow rastrowych oraz specyfikacji Win16 API.\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(root_readme)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::README.TXT"])

    root_linki = (
        "======================================================================\r\n"
        "          KATALOG LINKOW DO DARMOWEGO OPROGRAMOWANIA\r\n"
        "          Idea: wolny soft, dokumentacja i edukacja\r\n"
        "          Autor srodowiska: mztulip\r\n"
        "======================================================================\r\n\r\n"
        "1. CALMIRA II\r\n"
        "   Strona: http://www.calmira.org/\r\n"
        "   Nowoczesna powloka graficzna w stylu Windows 95 dla Windows 3.1,\r\n"
        "   zbudowana w Delphi 1.0 / Free Pascal, na licencji GPL.\r\n\r\n"
        "2. FREEDOS PROJECT\r\n"
        "   Strona: https://www.freedos.org/\r\n"
        "   Calkowicie wolny, darmowy i zgodny z MS-DOS system operacyjny.\r\n\r\n"
        "3. FREE PASCAL COMPILER (FPC)\r\n"
        "   Strona: https://www.freepascal.org/\r\n"
        "   Potezny, wieloplatformowy kompilator jezyka Pascal/Object Pascal,\r\n"
        "   wspierajacy m.in. 16-bitowy tryb Windows (i8086-win16).\r\n\r\n"
        "4. SKIFREE - CHRIS PIRIH\r\n"
        "   Strona autora: https://ski.ihoc.net/\r\n"
        "   Kultowa gra narciarska z 1991 roku z potworem snieznym Yeti.\r\n\r\n"
        "5. MARIO & LUIGI - MIKE WIERING\r\n"
        "   Strona autora: https://wieringsoftware.nl/mario/index.html\r\n"
        "   Klasyczny klon Super Mario Bros. z 1994 roku z kodem zrodlowym.\r\n\r\n"
        "6. OPEN WATCOM\r\n"
        "   Strona: https://openwatcom.org/\r\n"
        "   Otwarty kompilator C/C++ dla DOS, Win16, OS/2 i Win32.\r\n\r\n"
        "7. WINE\r\n"
        "   Strona: https://www.winehq.org/\r\n"
        "   Warstwa kompatybilnosci Windows dla systemow UNIX/Linux.\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(root_linki)
        f.flush()
        run(["mcopy", "-oi", mpart, f.name, "::LINKI.TXT"])

    print(f"[OK] Bootable C: HDD image created successfully: {out_img}")

if __name__ == "__main__":
    main()
