#!/bin/sh
# run_skitest.sh - uruchamia QEMU z boot_skitest.img, czeka na WM_PAINT,
# weryfikuje na serialu ze rendering pipeline sie zainicjowal poprawnie.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$SCRIPT_DIR/boot_skitest.img"
LOG=/tmp/skitest_serial.log
MON_PORT=55559

rm -f "$LOG"

echo "[*] Uruchamiam QEMU z boot_skitest.img..."
qemu-system-i386 \
    -fda "$IMG" -boot a -m 16 \
    -no-reboot -no-shutdown \
    -serial file:"$LOG" \
    -monitor tcp:127.0.0.1:$MON_PORT,server,nowait \
    -display none \
    -d guest_errors -D /tmp/skitest_qemu.log \
    &
QEMU_PID=$!
echo "[*] QEMU PID=$QEMU_PID"

# Czekaj az WM_PAINT (msg=000F) pojawi sie w logu - do 40 sekund
echo "[*] Czekam na WM_PAINT (msg=000F)..."
WAITED=0
while [ $WAITED -lt 40 ]; do
    if grep -q "msg=000F" "$LOG" 2>/dev/null; then
        break
    fi
    sleep 1
    WAITED=$((WAITED+1))
done

if ! grep -q "msg=000F" "$LOG" 2>/dev/null; then
    echo "[FAIL] Timeout - WM_PAINT nie pojawil sie w ciagu 40s"
    tail -20 "$LOG" 2>/dev/null || true
    kill $QEMU_PID 2>/dev/null || true
    exit 1
fi

echo "[*] WM_PAINT received po ${WAITED}s - rendering pipeline dziala"
# Daj chwile na kilka klatek
sleep 2

kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

# Sprawdz brak CPU reset (crash)
if grep -q "cpu_reset\|triple fault\|RESET" /tmp/skitest_qemu.log 2>/dev/null; then
    echo "[FAIL] CPU reset / crash wykryty w qemu.log"
    PASS=0
else
    PASS=1
fi

echo ""
echo "=== Weryfikacja serial log ==="

check() {
    PATTERN="$1"
    DESC="$2"
    if grep -q "$PATTERN" "$LOG" 2>/dev/null; then
        echo "[OK] $DESC"
    else
        echo "[FAIL] brak: $DESC"
        PASS=0
    fi
}

check "Ladowanie bitmap SKI.EXE"    "bitmapy zaladowane (86 RT_BITMAP)"
check "USER: RegisterClass"          "RegisterClass OK"
check "USER: CreateWindow -> WM_CREATE" "CreateWindow + WM_CREATE"
check "GDI: CreateCompatDC DC=0002" "CreateCompatDC DC 2"
check "GDI: CreateCompatDC DC=0006" "CreateCompatDC DC 6 (wszystkie 5)"
check "GDI: CreateCompatBitmap 105x55" "CreateCompatBitmap tytul 105x55"
check "GDI: CreateCompatBitmap 26x32"  "CreateCompatBitmap gondola 26x32"
check "msg=000F"                     "WM_PAINT dispatched (petla gry)"

echo ""
if [ $PASS -eq 1 ]; then
    echo "=== TEST PASSED ==="
else
    echo "=== TEST FAILED ==="
    exit 1
fi
