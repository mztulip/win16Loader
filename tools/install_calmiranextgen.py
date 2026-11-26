#!/usr/bin/env python3
r"""
Installs Calmira NextGen into an existing bootable HDD image (copy of calmira.img)
and configures FDAUTO.BAT to launch C:\NEXTGEN\NEXTGEN.EXE.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import zipfile


def run(cmd, **kwargs):
    kwargs.setdefault("check", True)
    return subprocess.run(cmd, **kwargs)


def copy_tree_to_img(src_dir, mpart, base_dst="::NEXTGEN"):
    run(["mmd", "-i", mpart, base_dst], check=False)
    for root, dirs, files in os.walk(src_dir):
        dirs.sort()
        files.sort()
        rel = os.path.relpath(root, src_dir)
        dst_dir = base_dst if rel == "." else f"{base_dst}/{rel.upper()}"
        if rel != ".":
            run(["mmd", "-i", mpart, dst_dir], check=False)
        for fname in files:
            fpath = os.path.join(root, fname)
            run(["mcopy", "-o", "-D", "o", "-i", mpart, fpath, f"{dst_dir}/"])


def main():
    parser = argparse.ArgumentParser(description="Install Calmira NextGen into HDD image")
    parser.add_argument("--img", required=True, help="Target HDD image (copied from calmira.img)")
    parser.add_argument("--src", required=True, help="Calmira NextGen directory or SETUP.EXE")
    args = parser.parse_args()

    src = args.src
    if not os.path.exists(src):
        sys.exit(f"ERROR: Calmira NextGen source not found: {src}")

    mpart = f"{args.img}@@1M"

    setup_exe = src if os.path.isfile(src) else os.path.join(src, "SETUP.EXE")
    nextgen_exe = os.path.join(src, "NEXTGEN.EXE") if os.path.isdir(src) else None

    if os.path.isfile(setup_exe) and zipfile.is_zipfile(setup_exe):
        print(f"Extracting Calmira NextGen from {setup_exe}...")
        with tempfile.TemporaryDirectory(prefix="nextgen_") as tmpdir:
            with zipfile.ZipFile(setup_exe, "r") as zf:
                zf.extractall(tmpdir)
            copy_tree_to_img(tmpdir, mpart)
    elif nextgen_exe and os.path.isfile(nextgen_exe):
        print(f"Copying Calmira NextGen from {src}...")
        copy_tree_to_img(src, mpart)
    else:
        sys.exit(f"ERROR: Neither SETUP.EXE (zip) nor NEXTGEN.EXE found in: {src}")

    fdauto = (
        "@echo off\r\n"
        "SET PATH=C:\\WINDOWS;C:\\NEXTGEN;C:\\FREEDOS\\BIN\r\n"
        "SET PROMPT=\r\n"
        "C:\r\n"
        "cd \\NEXTGEN\r\n"
        "WIN NEXTGEN.EXE\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(fdauto)
        f.flush()
        run(["mcopy", "-o", "-i", mpart, f.name, "::FDAUTO.BAT"])

    print(f"[OK] Calmira NextGen installed into {args.img}")


if __name__ == "__main__":
    main()
