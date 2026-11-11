# XIAO nRF54LM20B Wiki Examples

Companion example code for the **Seeed Studio XIAO nRF54LM20B** and **XIAO nRF54LM20B Sense**, paired with the [wiki](https://wiki.seeedstudio.com/xiao_nrf54lm20b_pin_multiplexing/).

Each example is self-contained. Most are [PlatformIO](https://platformio.org/) projects built on the **Zephyr RTOS** framework; a few (`12_NFC`, `13_matter`) are stock Nordic Connect SDK (NCS / `west`) samples lightly adapted for the board.

## Hardware

| Variant | Key hardware |
|---|---|
| XIAO nRF54LM20B | nRF54LM20B SoC, USB-C, RGB LED, B0 button (P0.09), RST |
| XIAO nRF54LM20B **Sense** | adds on-board LSM6DS3TR-C 6-axis IMU, PDM microphone, nPM1300 PMIC |

The board ships with a **MCUboot + firmware loader** setup. Application images are flashed over USB through MCUboot serial-recovery DFU (not a standard BOSSA-style bootloader).

## Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI) with the SeeedStudio platform installed — board id `seeed-xiao-nrf54lm20b`.
- For `12_NFC` / `13_matter`: [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop) + NCS toolchain (`west`, `nrfutil`).
- `nrfutil` for signing/uploading images (`pip install nrfutil` or the standalone build).

## Examples

| Folder | Description |
|---|---|
| `1_getting_start` | GPIO: on-board RGB LED blinking in rotation, USB CDC-ACM serial console, IMU (Sense) readings |
| `2_Mic_demo` | PDM microphone capture streamed over USB serial |
| `3_ble/ble_advertising` | BLE advertising |
| `3_ble/ble_central_gatt` | BLE central / GATT client |
| `3_ble/ble_channel_sounding` | BLE channel sounding |
| `3_ble/ble_oberver` | BLE observer (passive scanning) |
| `4_imu/1_6axis_data` | LSM6DS3TR-C 6-axis data acquisition |
| `4_imu/2_motion_wake` | IMU motion wake |
| `4_imu/3_electronic_ocean` | IMU "electronic ocean" effect |
| `5_battery_npm1300` | Battery gauge and PMIC (nPM1300) |
| `6_zephyr-epaper/2inch13` | 2.13" e-paper display |
| `6_zephyr-epaper/5inch83` | 5.83" e-paper display |
| `7_AI_Gesture_5Class` | 5-class IMU gesture recognition on the Axon NPU |
| `8_AI_Wakeword` | "okay nordic" wakeword detection on the Axon NPU |
| `9_RTC/1_rtc_systemoff` | RTC wake from system-off |
| `10_PinMux/1_digital_gpio` | Pin muxing — digital GPIO (button + active buzzer) |
| `10_PinMux/2_pwm_servo` | Pin muxing — PWM (hobby servo) |
| `10_PinMux/3_adc` | Pin muxing — analog (SAADC on P1.0 / AIN0) |
| `10_PinMux/4_uart` | Pin muxing — UART (uart21 loopback) |
| `10_PinMux/5_i2c_oled` | Pin muxing — I²C (SSD1306 OLED) |
| `10_PinMux/6_spi_lcd` | Pin muxing — SPI (GC9A01 round LCD) |
| `11_low_power/system_off` | System-off low power |
| `11_low_power/system_on_sleep` | System-on sleep |
| `12_NFC/writable_ndef_msg_3` | NFC writable NDEF message (NCS sample) |
| `12_NFC_fix/patch_ncs-v3.3.0-nfc-usb-clock-fix` | NCS v3.3.0 patch fixing the NFC USB clock issue |
| `13_matter/template_1` | Matter template (NCS sample) |
| `13_matter/coprocessor_1` | Thread co-processor (NCS sample) |

Open a folder in VS Code with the PlatformIO extension, or run `pio run` inside it, to build.

## Flashing

Enter DFU / serial-recovery mode first:

1. Hold **B0** (P0.09) → tap **RST** once → release **B0**.
2. The board enumerates as USB VID `2886` PID `0x0013` (MCUboot serial recovery).

> The 1200-bps "soft touch" used by Arduino/BOSSA boards **does not** enter DFU on this board — the application does not implement that handover. Use the physical buttons above.

Then either:

- **PlatformIO examples** — run the project's `flash.sh` (build + sign + upload in one step), or use the PlatformIO "Upload" task.
- **NCS examples** — build with `west build -b seeed-xiao-nrf54lm20b`, then upload with `nrfutil mcu-manager serial image-upload` and reset with `nrfutil mcu-manager serial reset`.

Notes:
- Images must be **signed** (MCUboot verifies signatures). `flash.sh` handles this with imgtool using the framework's bundled `root-ed25519.pem` and `--pure`.
- The image **version must be incremented** on every re-flash, otherwise MCUboot rejects it as "same version". Pass e.g. `IMGVER=0.0.131 ./flash.sh`.
- After flashing, tap **RST** once if the USB CDC port does not enumerate immediately — the application brings up CDC on boot.

See the comments and README inside each example for wiring, pin assignments, and verification steps.
