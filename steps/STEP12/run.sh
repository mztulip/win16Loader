#!/bin/bash
# run.sh - kill any stale QEMU holding boot.img, then run fresh
OUT=${1:-/tmp/ski_out.txt}

fuser boot.img 2>/dev/null | xargs -r kill -9
sleep 0.3

qemu-system-i386 -fda boot.img -boot a -m 16 \
    -no-reboot -no-shutdown \
    -serial file:"$OUT" \
    -display none \
    2>/tmp/qemu.log

echo "--- serial output: $OUT ---"
cat "$OUT"
