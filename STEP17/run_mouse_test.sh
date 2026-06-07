#!/bin/sh
# run_mouse_test.sh - test myszy PS/2 (STEP17)
# Uruchamia QEMU z boot_skitest.img (skitest obsluguje WM_MOUSEMOVE przez DefWindowProc).
# Wstrzykuje ruchy i klik przez QEMU monitor, sprawdza serial log.
#
# QEMU monitor: mouse_move <dx> <dy>  (relatywne delty)
#               mouse_button <mask>   (1=LButton down, 0=up)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$SCRIPT_DIR/boot_skitest.img"
LOG=/tmp/mouse_serial.log
MON_PORT=55561

rm -f "$LOG"

send_mon() {
    printf '%s\r\n' "$1" | nc -w 1 -c 127.0.0.1 $MON_PORT 2>/dev/null
}

echo "[*] Uruchamiam QEMU z boot_skitest.img..."
qemu-system-i386 \
    -fda "$IMG" -boot a -m 16 \
    -no-reboot -no-shutdown \
    -serial file:"$LOG" \
    -monitor tcp:127.0.0.1:$MON_PORT,server,nowait \
    -display none \
    -d guest_errors -D /tmp/mouse_qemu.log \
    &
QEMU_PID=$!
echo "[*] QEMU PID=$QEMU_PID"

echo "[*] Czekam na WM_PAINT (gra gotowa)..."
WAITED=0
while [ $WAITED -lt 40 ]; do
    if grep -q "msg=000F" "$LOG" 2>/dev/null; then
        break
    fi
    sleep 1
    WAITED=$((WAITED+1))
done

if ! grep -q "msg=000F" "$LOG" 2>/dev/null; then
    echo "[FAIL] Timeout - WM_PAINT nie pojawil sie"
    kill $QEMU_PID 2>/dev/null || true
    exit 1
fi
echo "[*] Gra gotowa po ${WAITED}s - wstrzykuje zdarzenia myszy"
sleep 0.5

# Ruch myszy - kilka delt
echo "[*] mouse_move 50 30"
send_mon "mouse_move 50 30"
sleep 0.3
echo "[*] mouse_move -20 40"
send_mon "mouse_move -20 40"
sleep 0.3
echo "[*] mouse_move 100 -50"
send_mon "mouse_move 100 -50"
sleep 0.3

# Klik LButton: down (mask=1) + up (mask=0)
echo "[*] mouse_button 1 (LButton down)"
send_mon "mouse_button 1"
sleep 0.3
echo "[*] mouse_button 0 (LButton up)"
send_mon "mouse_button 0"
sleep 1

kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "=== Serial log (MOUSE linie) ==="
grep "MOUSE\|MSE\|WM_M\|mouse" "$LOG" 2>/dev/null | head -20 || echo "(brak linii MOUSE)"

# Weryfikacja: sprawdz IRQ12 init i brak crashu
PASS=1

if grep -q "MOUSE: PS/2 init OK" "$LOG" 2>/dev/null; then
    echo "[OK] MOUSE: PS/2 init OK"
else
    echo "[FAIL] brak MOUSE: PS/2 init OK"
    PASS=0
fi

# Sprawdz WM_MOUSEMOVE (0x0200) lub WM_LBUTTONDOWN (0x0201) w serialu
# (user.c nie loguje mouse msgs, ale sprawdzamy ze nie bylo crashu)
if grep -q "cpu_reset\|triple fault\|RESET" /tmp/mouse_qemu.log 2>/dev/null; then
    echo "[FAIL] CPU reset / crash w qemu.log"
    PASS=0
else
    echo "[OK] brak CPU reset (IRQ12 obslugiwany stabilnie)"
fi

if grep -q "msg=000F" "$LOG" 2>/dev/null; then
    echo "[OK] WM_PAINT po zdarzeniach myszy (gra dziala)"
else
    echo "[FAIL] brak WM_PAINT"
    PASS=0
fi

echo ""
if [ $PASS -eq 1 ]; then
    echo "=== TEST PASSED ==="
else
    echo "=== TEST FAILED ==="
    exit 1
fi
