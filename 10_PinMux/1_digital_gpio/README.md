# PinMux demo 1 — Digital GPIO

Companion to the [M20A wiki · Digital](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/#digital).

## Function
Grove Button (P1.0 / D0) as input, Grove Buzzer (P1.31 / D1) as output: beep while pressed, silent when released.

## Hardware
| Signal | Pin | XIAO D# | Grove |
|---|---|---|---|
| Button | P1.0 | D0 | Grove Button (on-board pull-down, HIGH = pressed) |
| Buzzer | P1.31 | D1 | **Active** buzzer (built-in oscillator, HIGH = beep) |

> ⚠️ Must be an **active** buzzer. A passive piezo element needs PWM drive — use demo 2 instead.

## 20B adaptations
- Removed `zephyr,console=&uart20` from the wiki overlay: output goes through the board-default **USB CDC** (`cu.usbmodem*`), no USB-TTL needed.
- GPIO does not go through pinctrl; P1.0/P1.31 are usable by board default, so the overlay is left empty.
- Added mcuboot self-confirm.

## Build / Flash
```
cd 1_digital_gpio
pio run                 # build
./flash.sh              # build + sign + upload (put the board into DFU first)
# After flashing, press RST once, then open cu.usbmodem* @115200
```

## Verification
Press the button → serial prints `Button PRESSED -> Buzzer ON` and the buzzer beeps; release → `Button released -> Buzzer OFF`.
