#!/usr/bin/env bash
# Build + sign + flash the XIAO nRF54LM20B in one shot
# Usage: ./flash.sh
#
# NOTE: the 1200 bps soft trigger does NOT enter DFU on this board (the
#       current app does not implement the boot-mode handover).
#       Before running this script, put the board into DFU/serial-recovery
#       mode manually:
#         hold the B0 button (P0.09) -> tap RST -> release B0
#       Then run this script.
# The image version must be incremented on every re-flash, otherwise
# mcuboot rejects it as "same version".
set -e

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/1_getting_start"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
PORT=/dev/cu.usbmodem1101
# Increment this version on every re-flash
IMGVER="${IMGVER:-0.0.11}"

echo "==> 1/3 build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (make sure the board is in DFU mode)"
NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 60 --json 2>&1 | grep -E "task_end|result" | tail -1

echo "==> Done. Tap RST to reset the board, then check the LED and serial output."
