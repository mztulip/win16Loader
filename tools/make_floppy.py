#!/usr/bin/env python3
"""
Creates a 1.44MB FAT12 test floppy image (floppy.img)
containing sample text files to open and test under Calmira/FreeDOS.
"""
import os
import subprocess
import sys

def main():
    out_img = sys.argv[1] if len(sys.argv) > 1 else "floppy.img"

    if os.path.exists(out_img):
        os.remove(out_img)

    # 1440 KB FAT12 floppy
    subprocess.run(["mkfs.fat", "-C", out_img, "1440", "-n", "TESTFLOPPY"], check=True)

    test_files = {
        "README.TXT": (
            "========================================================\r\n"
            " Witaj w systemie Calmira II na FreeDOS!\r\n"
            " Dyskietka testowa w napedzie A:\r\n"
            " Autor modyfikacji i srodowiska: mztulip\r\n"
            "========================================================\r\n\r\n"
            "System dziala bezposrednio z dysku twardego C:, a naped A:\r\n"
            "udostepnia pliki testowe i dokumentacje.\r\n\r\n"
            "Programy na tym systemie zostaly skompilowane za pomoca:\r\n"
            "- Free Pascal Compiler (FPC 3.3.1 dla i8086-win16)\r\n"
            "- Open Watcom C/C++\r\n\r\n"
            "Linki do stron autorow i darmowego oprogramowania:\r\n"
            "- Calmira II (powloka Windows 3.1): http://www.calmira.org/\r\n"
            "- FreeDOS (wolny system DOS):       https://www.freedos.org/\r\n"
            "- Free Pascal (kompilator Pascal):  https://www.freepascal.org/\r\n"
            "- SkiFree (strona Chrisa Piriha):   https://ski.ihoc.net/\r\n"
            "- Open Watcom (kompilator C/C++):   https://openwatcom.org/\r\n\r\n"
            "Wszystkie pliki mozesz otwierac i edytowac w programie Notepad\r\n"
            "(C:\\WINDOWS\\NOTEPAD.EXE) lub bezposrednio klikajac dwukrotnie!\r\n"
        ),
        "LINKI.TXT": (
            "========================================================\r\n"
            " KATALOG LINKOW DO DARMOWEGO OPROGRAMOWANIA\r\n"
            " Idea: wolny soft, dokumentacja i edukacja\r\n"
            " Autor: mztulip\r\n"
            "========================================================\r\n\r\n"
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
            "5. OPEN WATCOM\r\n"
            "   Strona: https://openwatcom.org/\r\n"
            "   Otwarty kompilator C/C++ dla DOS, Win16, OS/2 i Win32.\r\n"
        ),
        "TEST.TXT": (
            "Plik testowy: A:\\TEST.TXT\r\n"
            "Srodowisko: FreeDOS + Calmira II (dysk C:)\r\n"
            "Edytor: Notepad (autor: mztulip, kompilacja: Free Pascal)\r\n"
            "Status: Gotowy do edycji i zapisu!\r\n"
        ),
        "NOTATKI.TXT": (
            "Notatki projektowe:\r\n"
            "1. Start systemu FreeDOS bezposrednio z dysku C:\r\n"
            "2. Uruchomienie powloki Calmira II z C:\\CALMIRA\r\n"
            "3. Dostep do dyskietki testowej A:\r\n"
            "4. Gra SkiFree dostepna w katalogu C:\\SKIFREE\r\n"
            "5. Wspierajmy darmowe i otwarte oprogramowanie!\r\n"
        ),
    }

    tmp_dir = "/tmp/floppy_files"
    os.makedirs(tmp_dir, exist_ok=True)

    for fname, content in test_files.items():
        fpath = os.path.join(tmp_dir, fname)
        with open(fpath, "wb") as f:
            f.write(content.encode("cp852"))
        subprocess.run(["mcopy", "-oi", out_img, fpath, f"::{fname}"], check=True)

    print(f"[OK] Floppy image created: {out_img}")

if __name__ == "__main__":
    main()
