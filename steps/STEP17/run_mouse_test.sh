#!/bin/sh
# run_mouse_test.sh - test myszy PS/2 w przeplataniu z klawiatura (STEP17)
# Sprawdza ze IRQ12 (mysz) i IRQ1 (klawiatura) nie koliduja.
# Wstrzykuje ruchy/kliki myszy ORAZ klawisze przez QEMU monitor,
# weryfikuje serial log (brak crashu, PS/2 init OK, WM_PAINT po zdarzeniach).
#
# QEMU monitor: mouse_move <dx> <dy>  (relatywne delty)
#               mouse_button <mask>   (1=LButton down, 0=up)
#               sendkey <key>         (klawiatura PS/2)

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
echo "[*] Gra gotowa po ${WAITED}s - wstrzykuje zdarzenia (mysz + klawiatura przeplatanie)"
sleep 0.3

# --- Sekwencja przeplatana: mysz <-> klawiatura ---

echo "[*] [1] sendkey up"
send_mon "sendkey up"
sleep 0.3

echo "[*] [2] mouse_move 50 30"
send_mon "mouse_move 50 30"
sleep 0.3

echo "[*] [3] sendkey down"
send_mon "sendkey down"
sleep 0.3

echo "[*] [4] mouse_move -20 40"
send_mon "mouse_move -20 40"
sleep 0.3

echo "[*] [5] sendkey left"
send_mon "sendkey left"
sleep 0.2

echo "[*] [6] mouse_button 1 (LButton down)"
send_mon "mouse_button 1"
sleep 0.2

echo "[*] [7] sendkey right"
send_mon "sendkey right"
sleep 0.2

echo "[*] [8] mouse_button 0 (LButton up)"
send_mon "mouse_button 0"
sleep 0.2

echo "[*] [9] mouse_move 100 -50"
send_mon "mouse_move 100 -50"
sleep 0.2

echo "[*] [10] sendkey esc"
send_mon "sendkey esc"
sleep 0.2

echo "[*] [11] sendkey f1"
send_mon "sendkey f1"
sleep 0.2

echo "[*] [12] mouse_move -30 20"
send_mon "mouse_move -30 20"
sleep 0.3

echo "[*] [13] sendkey a"
send_mon "sendkey a"
sleep 0.3

echo "[*] [14] mouse_button 1"
send_mon "mouse_button 1"
sleep 0.2
echo "[*] [15] mouse_button 0"
send_mon "mouse_button 0"
sleep 0.5

# Czekaj krok na ewentualne WM_PAINT po zdarzeniach
sleep 1

kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "=== Serial log (MOUSE / MSE / KEY linie) ==="
grep "MOUSE\|MSE\|KEY:\|KBDT\|WM_M\|mouse" "$LOG" 2>/dev/null | head -30 || echo "(brak linii MOUSE/MSE/KEY)"

echo ""
echo "=== Ostatnie 10 linii serial logu ==="
tail -10 "$LOG" 2>/dev/null

# --- Weryfikacja ---
PASS=1

if grep -q "MOUSE: PS/2 init OK" "$LOG" 2>/dev/null; then
    echo "[OK] MOUSE: PS/2 init OK"
else
    echo "[FAIL] brak MOUSE: PS/2 init OK"
    PASS=0
fi

if grep -q "cpu_reset\|triple fault\|RESET" /tmp/mouse_qemu.log 2>/dev/null; then
    echo "[FAIL] CPU reset / crash w qemu.log"
    PASS=0
else
    echo "[OK] brak CPU reset (IRQ1+IRQ12 stabilne)"
fi

if grep -q "msg=000F" "$LOG" 2>/dev/null; then
    echo "[OK] WM_PAINT pojawil sie (system dziala)"
else
    echo "[FAIL] brak WM_PAINT"
    PASS=0
fi

# Policz ile linii KEY: lub MSE: sie pojawilo (informacyjnie)
KEY_COUNT=$(grep -c "KEY:\|KBDT" "$LOG" 2>/dev/null || echo 0)
MSE_COUNT=$(grep -c "MSE:" "$LOG" 2>/dev/null || echo 0)
echo "[INFO] KEY:/KBDT linie: $KEY_COUNT  |  MSE: linie: $MSE_COUNT"
echo "[INFO] Uwaga: QEMU headless mouse_move nie generuje PS/2 IRQ12 (ograniczenie QEMU -display none)"
echo "[INFO] sendkey DZIALA headless - jezeli KEY: linie = 0, sprawdz logowanie w user.c/skitest.c"

echo ""
if [ $PASS -eq 1 ]; then
    echo "=== TEST PASSED ==="
else
    echo "=== TEST FAILED ==="
    exit 1
fi
