#!/usr/bin/env python3
"""tools/make_release.py - Tworzy gotowa paczke dystrybucyjna (.zip) do release'u
Zawiera obrazy calmira.img, floppy.img, skrypty run.sh / run.bat oraz czytelne README.txt.
"""
import os, sys, shutil, zipfile, argparse, subprocess

README_TEMPLATE = """===============================================================================
                       win16Loader v{version} - Release
            Minimal Win16 Compatible Loader for i486 on FreeDOS
===============================================================================

Copyright (c) 2026 win16Loader Contributors
License: GNU Lesser General Public License v2.1 or later (LGPL-2.1-or-later)
GitHub: https://github.com/mztulip/win16Loader

-------------------------------------------------------------------------------
1. QUICK START (HOW TO RUN)
-------------------------------------------------------------------------------

Requirements:
  - QEMU (qemu-system-i386) installed on your system.
    Linux (Debian/Ubuntu):  sudo apt install qemu-system-x86
    Linux (Arch):           sudo pacman -S qemu-system-x86
    Linux (Fedora):         sudo dnf install qemu-system-x86
    macOS (Homebrew):       brew install qemu
    Windows:                https://www.qemu.org/download/

Run on Linux / macOS:
  Execute run.sh or paste the following into your terminal:

    qemu-system-i386 -vga std,retrace=precise \\
        -drive file=floppy.img,if=floppy,format=raw \\
        -drive file=calmira.img,if=ide,format=raw \\
        -boot c -m 16

Run on Windows (Command Prompt / PowerShell):
  Double click run.bat or execute:

    qemu-system-i386.exe -vga std,retrace=precise ^
        -drive file=floppy.img,if=floppy,format=raw ^
        -drive file=calmira.img,if=ide,format=raw ^
        -boot c -m 16

QEMU Controls & Tips:
  - Press Ctrl + Alt + G (or Ctrl + Alt on older QEMU) to release the mouse cursor.
  - Press Ctrl + Alt + F to toggle fullscreen mode.

-------------------------------------------------------------------------------
2. WHAT IS INCLUDED
-------------------------------------------------------------------------------

* FreeDOS 1.4 kernel & DOS environment booting from FAT16 HDD (C:).
* win16Loader 32-bit Protected Mode kernel, GDI, USER, and system DLLs:
    C:\\WINDOWS\\LOADER.EXE
    C:\\WINDOWS\\KERNEL.EXE
    C:\\WINDOWS\\USER.EXE
    C:\\WINDOWS\\GDI.EXE
    C:\\WINDOWS\\SHELL.DLL, KEYBOARD.DRV, TOOLHELP.DLL, COMMDLG.DLL, etc.
* Calmira II 3.3 Graphical Shell (Delphi 1, 16-bit Win16):
    - Desktop with retro synthwave wallpapers and icons
    - Taskbar, Clock, and QuickLaunch tray
    - Start Menu branded with custom Loader16 i486 CPU chip
    - Calmira Explorer file manager (C:\\CALMIRA\\EXPLORER.EXE)
* Applications installed on C:\\:
    - PaintCraft Studio (C:\\WINDOWS\\PBRUSH.EXE):
        Modern Win16 paint application supporting 1/4/8/24-bit BMP loading/saving,
        canvas resize dialog (Ctrl+E), in-place interactive text tool,
        brush/pencil/lines/rectangles/circles/flood fill, viewport scrollbars,
        and clipboard cut/copy/paste.
    - Notepad (C:\\WINDOWS\\NOTEPAD.EXE):
        Text editor with open/save dialogs, scrollbars, and full editing.
    - SkiFree (C:\\SKIFREE\\SKI.EXE):
        Original 1991 Win16 classic ski game by Chris Pirih.
    - Mario (C:\\MARIO\\MARIO.EXE):
        Classic DOS platformer with sound, PC speaker, and VGA graphics.
    - FreeDOS Command Prompt (Full screen DOS prompt with FreeDOS icon).
    - Control Panel (C:\\WINDOWS\\CONTROL.EXE):
        Desktop wallpaper and pattern selector with dynamic BMP detection.

-------------------------------------------------------------------------------
3. SHUTDOWN & RESTART
-------------------------------------------------------------------------------

From Calmira Start Menu:
  - Click "Start" -> "Shut Down..." -> "Restart the computer":
      Fully restarts the virtual machine.
  - Click "Start" -> "Shut Down..." -> "Restart Windows":
      Restarts the Win16 graphical session cleanly.
  - Click "Start" -> "Shut Down..." -> "Shut down the computer":
      Cleanly halts the system.
"""

RUN_SH = """#!/bin/sh
# win16Loader QEMU launcher for Linux / macOS
qemu-system-i386 -vga std,retrace=precise \\
    -drive file=floppy.img,if=floppy,format=raw \\
    -drive file=calmira.img,if=ide,format=raw \\
    -boot c -m 16 "$@"
"""

RUN_BAT = """@echo off
rem win16Loader QEMU launcher for Windows
qemu-system-i386.exe -vga std,retrace=precise -drive file=floppy.img,if=floppy,format=raw -drive file=calmira.img,if=ide,format=raw -boot c -m 16 %*
"""

def main():
    parser = argparse.ArgumentParser(description="Create win16Loader release zip")
    parser.add_argument("--version", default="0.1", help="Release version (default: 0.1)")
    parser.add_argument("--dist-dir", default="dist", help="Output directory (default: dist)")
    parser.add_argument("--calmira-img", default="calmira.img", help="Path to calmira.img")
    parser.add_argument("--floppy-img", default="floppy.img", help="Path to floppy.img")
    args = parser.parse_args()

    v = args.version
    if not v.startswith("v"):
        tag_v = f"v{v}"
    else:
        tag_v = v
        v = v[1:]

    os.makedirs(args.dist_dir, exist_ok=True)
    pkg_name = f"win16Loader-{tag_v}"
    pkg_dir = os.path.join(args.dist_dir, pkg_name)
    if os.path.exists(pkg_dir):
        shutil.rmtree(pkg_dir)
    os.makedirs(pkg_dir, exist_ok=True)

    if not os.path.exists(args.calmira_img):
        sys.exit(f"Error: {args.calmira_img} not found. Run 'make calmira.img' first.")
    if not os.path.exists(args.floppy_img):
        sys.exit(f"Error: {args.floppy_img} not found. Run 'make floppy.img' first.")

    print(f"Copying disk images into {pkg_dir}...")
    shutil.copy2(args.calmira_img, os.path.join(pkg_dir, "calmira.img"))
    shutil.copy2(args.floppy_img, os.path.join(pkg_dir, "floppy.img"))

    readme_path = os.path.join(pkg_dir, "README.txt")
    with open(readme_path, "w", encoding="utf-8") as f:
        f.write(README_TEMPLATE.format(version=v))

    run_sh_path = os.path.join(pkg_dir, "run.sh")
    with open(run_sh_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(RUN_SH)
    os.chmod(run_sh_path, 0o755)

    run_bat_path = os.path.join(pkg_dir, "run.bat")
    with open(run_bat_path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(RUN_BAT)

    # Copy LICENSE if exists
    if os.path.exists("LICENSE"):
        shutil.copy2("LICENSE", os.path.join(pkg_dir, "LICENSE.txt"))

    # Create ZIP archive
    zip_path = os.path.join(args.dist_dir, f"{pkg_name}.zip")
    if os.path.exists(zip_path):
        os.unlink(zip_path)

    print(f"Compressing {zip_path}...")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, dirs, files in os.walk(pkg_dir):
            for file in files:
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, args.dist_dir)
                zf.write(full_path, arcname=rel_path)

    zip_size_mb = os.path.getsize(zip_path) / (1024 * 1024)
    print(f"[OK] Release package created successfully!")
    print(f"     Archive: {zip_path} ({zip_size_mb:.2f} MB)")
    print(f"     Folder:  {pkg_dir}")

if __name__ == "__main__":
    main()
