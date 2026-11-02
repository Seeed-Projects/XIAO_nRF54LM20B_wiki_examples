#!/usr/bin/env bash
# One-click build + sign + flash for XIAO nRF54LM20B (IMU demo 1: 6-axis data)
# Usage: ./flash.sh   (or PORT=/dev/cu.usbmodem1101 ./flash.sh)
#
# Note: to enter DFU you can either
#   (a) soft-trigger it from the running app (verified 2026-10-27): open the
#       CDC port at 1200 baud and close it again (see below), the board
#       re-enumerates as "CDC ACM serial backend"; or
#   (b) hold the B0 button (P0.09) -> press RST once -> release B0.
# Soft trigger (python -c "import serial,time; s=serial.Serial('/dev/cu.usbmodem1201',1200); time.sleep(0.5); s.close()")
# The version number must be higher than the image currently in slot0,
# otherwise mcuboot rejects it as "same version".
set -e

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/4_imu/1_6axis_data"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
# Auto-detect the DFU serial port unless PORT is given (the CDC name changes
# between normal app mode and serial-recovery mode and between USB ports).
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"
  if [ -z "$PORT" ]; then
    echo "ERROR: no /dev/cu.usbmodem* port found — is the board in DFU mode?" >&2
    exit 1
  fi
  echo "==> auto-detected serial port: $PORT"
fi
# Increment this version on every re-flash
# (ledger: getting_start 0.0.12, mic 0.0.13+0.0.17, motion_wake 0.0.14,
#  6axis 0.0.15 broken / 0.0.16 fixed)
IMGVER="${IMGVER:-0.0.18}"

echo "==> 1/3 build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (make sure the board is in DFU)"
NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 60 --json 2>&1 | grep -E "task_end|result" | tail -1

echo "==> Done. Reset the board (press RST once) and check the serial output."
