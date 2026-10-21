#!/usr/bin/env bash
# One-shot build + sign + flash script for the XIAO nRF54LM20B.
# Usage: ./flash.sh
#
# Note: the 1200bps soft-trigger cannot enter DFU on this board
#       (the current application does not hand off boot-mode).
#       Before running this script, manually put the board into DFU/serial
#       recovery mode first:
#         press and hold B0 (P0.09) -> tap RST -> release B0
#       Then run this script once the board is in DFU.
# The version number must be incremented on every re-flash, otherwise
# mcuboot rejects it as the "same version".
set -e

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/5_battery_npm1300"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
PORT="${PORT:-/dev/cu.usbmodem1101}"
# Increment this version number on every re-flash
# (getting_start 0.0.12, mic 0.0.13, imu 0.0.14)
IMGVER="${IMGVER:-0.0.15}"

echo "==> 1/3 Build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (ensure the board is in DFU)"
NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 60 --json 2>&1 | grep -E "task_end|result" | tail -1

echo "==> Done. Reset the board (tap RST) and check the LED and serial output."
