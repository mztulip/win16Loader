#!/bin/sh
# test_kbd.sh - uruchamia QEMU z boot_kbd.img, czeka na "KBDT: ready",
# wstrzykuje klawisze przez QEMU monitor (sendkey), weryfikuje serial log.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$SCRIPT_DIR/boot_kbd.img"
LOG=/tmp/kbd_serial.log
MON_PORT=55556
QEMU_PID_FILE=/tmp/kbd_qemu.pid

# Cleanup
rm -f "$LOG" "$QEMU_PID_FILE"

send_mon() {
    printf '%s\r\n' "$1" | nc -w 1 -c 127.0.0.1 $MON_PORT 2>/dev/null
}

echo "[*] Uruchamiam QEMU z boot_kbd.img..."
qemu-system-i386 \
    -fda "$IMG" -boot a -m 16 \
    -no-reboot -no-shutdown \
    -serial file:"$LOG" \
    -monitor tcp:127.0.0.1:$MON_PORT,server,nowait \
    -display none \
    -d guest_errors -D /tmp/kbd_qemu.log \
    &
QEMU_PID=$!
echo $QEMU_PID > "$QEMU_PID_FILE"
echo "[*] QEMU PID=$QEMU_PID, monitor port=$MON_PORT"

# Czekaj az KBDT: ready pojawi sie w logu
echo "[*] Czekam na KBDT: ready..."
WAITED=0
while [ $WAITED -lt 60 ]; do
    if grep -q "KBDT: ready" "$LOG" 2>/dev/null; then
        break
    fi
    sleep 1
    WAITED=$((WAITED+1))
done

if ! grep -q "KBDT: ready" "$LOG" 2>/dev/null; then
    echo "[FAIL] Timeout - KBDT: ready nie pojawilo sie w ciagu 60s"
    cat "$LOG" 2>/dev/null || true
    kill $QEMU_PID 2>/dev/null || true
    exit 1
fi
echo "[*] KBDT: ready - wstrzykuje klawisze przez QEMU monitor sendkey"
sleep 0.5

# Wyslij klawisze: Up Down Left Right F1 Escape
for KEY in up down left right f1 esc; do
    echo "[*] sendkey $KEY"
    send_mon "sendkey $KEY"
    sleep 0.5
done

echo "[*] Czekam na KBDT: done..."
WAITED=0
while [ $WAITED -lt 15 ]; do
    if grep -q "KBDT: done" "$LOG" 2>/dev/null; then
        break
    fi
    sleep 1
    WAITED=$((WAITED+1))
done

kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "=== Serial log (KBDT linie) ==="
grep "KBDT" "$LOG" || echo "(brak linii KBDT)"
echo ""

# Weryfikacja
PASS=1
check_vk() {
    VK="$1"
    NAME="$2"
    if grep -q "KBDT VK=0x$VK" "$LOG"; then
        echo "[OK] VK=0x$VK ($NAME)"
    else
        echo "[FAIL] brak VK=0x$VK ($NAME)"
        PASS=0
    fi
}

check_vk "26" "Up"
check_vk "28" "Down"
check_vk "25" "Left"
check_vk "27" "Right"
check_vk "70" "F1"
check_vk "1B" "Escape"

if grep -q "KBDT: done" "$LOG"; then
    echo "[OK] KBDT: done"
else
    echo "[FAIL] brak KBDT: done"
    PASS=0
fi

echo ""
if [ $PASS -eq 1 ]; then
    echo "=== TEST PASSED ==="
else
    echo "=== TEST FAILED ==="
    exit 1
fi
