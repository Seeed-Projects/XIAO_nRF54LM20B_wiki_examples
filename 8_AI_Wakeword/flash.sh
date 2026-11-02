#!/usr/bin/env bash
# Build + sign + flash the XIAO nRF54LM20B wakeword demo in one shot
# Usage: ./flash.sh   (or PORT=/dev/cu.usbmodemXXXX ./flash.sh)
#
# NOTE: to enter DFU/serial-recovery mode, EITHER
#   - hold the B0 button (P0.09) -> tap RST -> release B0, OR
#   - soft-trigger it from the running app (the [xiao_dfu] module supports
#     the Arduino-style 1200 bps open/close; the board needs a reset right
#     after the trigger for it to take effect).
# The image version must be incremented on every re-flash, otherwise
# mcuboot rejects it as "same version".
set -e

PROJ="/Users/li/Desktop/XIAO_nRF54LM20B_example/platforms/8_AI_Wakeword"
BUILD="$PROJ/.pio/build/seeed-xiao-nrf54lm20b"
PY="$HOME/.platformio/penv/.zephyr-4.4.0/bin/python"
IMG="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/scripts/imgtool.py"
KEY="$HOME/.platformio/packages/framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem"
NRFUTIL="$HOME/.platformio/nrfutil/nrfutil-8.2.0-aarch64-apple-darwin/nrfutil-aarch64-apple-darwin-8.2.0/data/bin/nrfutil"
# Auto-detect the DFU serial port unless PORT is given (the CDC name changes
# between normal app mode and serial-recovery mode).
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"
  if [ -z "$PORT" ]; then
    echo "ERROR: no /dev/cu.usbmodem* port found — is the board in DFU mode?" >&2
    exit 1
  fi
  echo "==> auto-detected serial port: $PORT"
fi
# Increment this version on every re-flash (other projects on this board have
# used 0.0.2 .. 0.0.13; override with IMGVER=0.0.N ./flash.sh)
IMGVER="${IMGVER:-0.0.100}"

echo "==> 1/3 build (sdk-edge-ai v3 from ~/XIAO-EdgeAI/tools/sdk-edge-ai)"
XIAO_EDGE_AI_DIR="$HOME/XIAO-EdgeAI/tools/sdk-edge-ai" pio run -d "$PROJ"

echo "==> 2/3 imgtool sign (ed25519 + pure, version $IMGVER)"
"$PY" "$IMG" sign \
  --key "$KEY" --pure \
  --header-size 0x800 --align 16 \
  --version "$IMGVER" --slot-size 0x1BE000 \
  "$BUILD/firmware.bin" "$BUILD/firmware.signed.bin"

echo "==> 3/3 nrfutil mcu-manager upload (make sure the board is in DFU mode)"
NRFUTIL_HOME="$HOME/.platformio/nrfutil" "$NRFUTIL" mcu-manager serial image-upload \
  --firmware "$BUILD/firmware.signed.bin" \
  --serial-port "$PORT" --timeout 120 --json 2>&1 | grep '"task_end"' | tail -1

echo "==> Done. Tap RST to reset the board, then check the LED and serial output."
