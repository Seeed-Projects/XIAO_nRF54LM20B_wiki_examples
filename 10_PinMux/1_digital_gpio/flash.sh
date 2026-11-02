#!/usr/bin/env bash
# One-click build + sign + flash for XIAO nRF54LM20B (PinMux demo 1: Digital GPIO)
# Usage: ./flash.sh   (or PORT=/dev/cu.usbmodem1101 ./flash.sh)
#
# Enter DFU either by
#   (a) hold B0 (P0.09) -> press RST once -> release B0  (verified entry method); or
#   (b) from a running app: open the CDC port at 1200 baud and close it again.
# The version number must be higher than the image currently in slot0,
# otherwise mcuboot rejects it as "same version". Override with IMGVER=...
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

# Increment on every re-flash.
# (ledger: wakeword 0.0.100/101, ocean 0.0.110-112, RTC 0.0.120/121,
#  PinMux demos 0.0.130-135 -- demo1=digital, demo2=pwm, demo3=adc,
#  demo4=uart, demo5=i2c_oled, demo6=spi_lcd)
IMGVER="${IMGVER:-0.0.130}"

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
  exit 1
fi

echo "==> Done. NOW RESET THE BOARD BY PRESSING THE RST BUTTON ONCE."
echo "    DO NOT run 'nrfutil mcu-manager serial reset' -- the soft reset"
echo "    leaves USB CDC dead on this board (3/3 failures 2026-10-27)."
echo "    Then open the CDC port (115200) and watch the output."
