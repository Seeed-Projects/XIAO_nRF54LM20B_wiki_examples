#!/usr/bin/env bash
# One-click build + sign + flash for XIAO nRF54LM20B (BLE demo: Observer)
# Usage: ./flash.sh   (or PORT=/dev/cu.usbmodem1101 ./flash.sh)
#
# Enter DFU: hold B0 (P0.09) -> RST -> release B0. (1200-baud soft touch does
# NOT work on these Zephyr+mcuboot demos.)
# Version must be higher than the image in slot0. Override with IMGVER=...
set -e

PROJ="$(cd "$(dirname "$0")" && pwd)"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"

if [ -z "$PORT" ]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"
  if [ -z "$PORT" ]; then
    echo "ERROR: no /dev/cu.usbmodem* port found — is the board in DFU mode?" >&2
    exit 1
  fi
  echo "==> auto-detected serial port: $PORT"
fi

# (BLE: observer 0.0.146 [enhanced: name parse + dedup], advertising 0.0.141,
#  central_gatt 0.0.142, cs initiator 0.0.143 / reflector 0.0.144)
IMGVER="${IMGVER:-0.0.146}"

echo "==> 1/3 build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (make sure the board is in DFU)"
if NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 60 --json 2>&1 | tee /tmp/nrfutil_upload.json | \
  grep -q '"task_end".*"success"'; then
  echo "==> upload verified: task_end = success"
else
  echo "ERROR: upload did not report task_end success -- see /tmp/nrfutil_upload.json" >&2
  echo "       (likely the board is in app mode, not DFU. B0+RST to enter DFU.)" >&2
  exit 1
fi

echo "==> Done. NOW RESET THE BOARD BY PRESSING THE RST BUTTON ONCE."
echo "    DO NOT run 'nrfutil mcu-manager serial reset' (soft reset kills USB CDC)."
echo "    The board passively scans surrounding advertisers."
echo "    LED: blue slow = scanning, green flash = device found, red = error."
echo "    LOG on cu.usbmodem* (115200)."
