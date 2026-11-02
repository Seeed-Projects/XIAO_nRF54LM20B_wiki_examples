#!/usr/bin/env bash
# One-click build + sign + flash for XIAO nRF54LM20B (IMU demo 3: electronic ocean)
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

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/4_imu/3_electronic_ocean"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-8.2.0-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
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
#  6axis 0.0.15 broken / 0.0.16 fixed,
#  ocean 0.0.110/111/112 flashed 2026-10-27 -- 0.0.112 runs OK when the
#  board is reset by pressing RST; nrfutil "serial reset" kills USB, see
#  复盘-烧录后串口消失.md)
IMGVER="${IMGVER:-0.0.113}"

echo "==> 1/3 build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (make sure the board is in DFU)"
# Full --json output kept visible; only "success" in task_end counts as OK
# (a truncated/filtered line is NOT proof the upload worked).
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
echo "    Then watch the LED ocean on /dev/cu.usbmodem1201."
