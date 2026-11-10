#!/bin/bash
# Poll for MCUboot recovery via cu port, then auto-upload + reset (all on cu).
PORT=/dev/cu.usbmodem11101
BIN=/Users/li/coprocessor_1/build/coprocessor_1/zephyr/zephyr.signed.bin
echo "Waiting for board to enter recovery (hold BOOT + tap RESET)..."
for i in $(seq 1 240); do
  out=$(nrfutil mcu-manager serial image-list --serial-port "$PORT" --timeout 4 2>/dev/null | grep -c "slot:")
  if [ "$out" -gt 0 ] 2>/dev/null; then
    echo "[$i] Recovery detected! Waiting for port to settle..."
    sleep 2
    echo "Uploading..."
    nrfutil mcu-manager serial image-upload --serial-port "$PORT" --timeout 180 --firmware "$BIN" 2>&1 | tail -5
    echo "Upload done. Verifying..."
    nrfutil mcu-manager serial image-list --serial-port "$PORT" --timeout 8 2>&1 | grep -E "slot:|hash:" | head -6
    echo "Resetting..."
    nrfutil mcu-manager serial reset --serial-port "$PORT" --timeout 30 2>&1 | tail -2
    echo "ALL DONE"
    exit 0
  fi
  printf "."
  sleep 1
done
echo ""
echo "Timed out waiting for recovery."
exit 1
