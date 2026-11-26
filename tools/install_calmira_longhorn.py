#!/usr/bin/env python3
r"""
Installs Calmira Longhorn into an existing bootable HDD image (copy of calmira.img)
and configures FDAUTO.BAT to launch C:\LONGHORN\CALMIRA.EXE.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def run(cmd, **kwargs):
    kwargs.setdefault("check", True)
    return subprocess.run(cmd, **kwargs)


def copy_tree_to_img(src_dir, mpart, base_dst="::LONGHORN"):
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
    parser = argparse.ArgumentParser(description="Install Calmira Longhorn into HDD image")
    parser.add_argument("--img", required=True, help="Target HDD image (copied from calmira.img)")
    parser.add_argument("--src", required=True, help="Calmira Longhorn CALMIRA directory")
    args = parser.parse_args()

    src = args.src
    if not os.path.isdir(src):
        sys.exit(f"ERROR: Calmira Longhorn directory not found: {src}")

    calmira_exe = os.path.join(src, "CALMIRA.EXE")
    if not os.path.isfile(calmira_exe):
        sys.exit(f"ERROR: CALMIRA.EXE not found in: {src}")

    mpart = f"{args.img}@@1M"

    print(f"Copying Calmira Longhorn from {src} to ::LONGHORN...")
    copy_tree_to_img(src, mpart, base_dst="::LONGHORN")

    fdauto = (
        "@echo off\r\n"
        "SET PATH=C:\\WINDOWS;C:\\LONGHORN;C:\\FREEDOS\\BIN\r\n"
        "SET PROMPT=\r\n"
        "C:\r\n"
        "cd \\LONGHORN\r\n"
        "WIN CALMIRA.EXE\r\n"
    )
    with tempfile.NamedTemporaryFile("w") as f:
        f.write(fdauto)
        f.flush()
        run(["mcopy", "-o", "-i", mpart, f.name, "::FDAUTO.BAT"])

    print(f"[OK] Calmira Longhorn installed into {args.img}")


if __name__ == "__main__":
    main()
