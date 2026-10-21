#!/usr/bin/env bash
# One-shot script: build + sign + flash the XIAO nRF54LM20B
# Usage: ./flash.sh
#
# Note: this board's 1200bps soft trigger cannot enter DFU (the current app does
#       not implement boot-mode handoff). Before running this script, manually
#       put the board into DFU/serial recovery mode:
#         hold the B0 button (P0.09) -> press RST once -> release B0
#       then run this script.
# The version number must be incremented on each reflash, otherwise mcuboot
# rejects it as "same version".
set -e

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/3_ble/3_ble_oberver"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
# The port name changes with USB plug/unplug and position; confirm the actual
# port with `ls -1 /dev/cu.usbmodem*` before flashing.
# Override with PORT=/dev/cu.usbmodem101 ./flash.sh.
PORT="${PORT:-/dev/cu.usbmodem1101}"
# Increment this version number on each reflash
# (getting_start 0.0.12, mic 0.0.13, imu 0.0.14, battery 0.0.16)
IMGVER="${IMGVER:-0.0.17}"

echo "==> 1/3 build"
pio run

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (confirm the board is in DFU)"
NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 60 --json 2>&1 | grep -E "task_end|result" | tail -1

echo "==> Done. Reset the board (press RST once) and check the LED and serial port."
