# XIAO nRF54LM20B Wiki Examples

Example code for the Seeed Studio XIAO nRF54LM20B (and nRF54LM20B Sense), companion to the wiki.

Each example is a self-contained [PlatformIO](https://platformio.org/) project using the Zephyr RTOS framework. Open a folder in VS Code with the PlatformIO extension (or run `pio run` inside it) to build.

## Examples

| Folder | Description |
|---|---|
| `1_getting_start` | GPIO: on-board RGB LED blinking in rotation, USB CDC-ACM serial console, IMU (Sense) readings |
| `2_Mic_demo` | PDM microphone capture over USB serial |
| `3_ble/3_ble_oberver` | BLE observer (passive scanning) |
| `4_imu_demo` | IMU (LSM6DS3TR-C) data acquisition |
| `5_battery_npm1300` | Battery gauge and PMIC (nPM1300) |
| `6_zephyr-epaper/2inch13` | 2.13" e-paper display |
| `6_zephyr-epaper/5inch83` | 5.83" e-paper display |

## Flashing

Flashing uses the MCUboot serial-recovery DFU over USB:

1. Enter DFU mode: hold **B0** (P0.09) → tap **RST** → release **B0**; the board enumerates as VID `2886` PID `0x0013`.
2. Run the project's `flash.sh` (build + sign + upload), or use `nrfutil mcu-manager serial image-upload` directly.

See the comments in each example for details.
