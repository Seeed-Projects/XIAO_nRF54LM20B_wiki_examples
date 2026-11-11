# PinMux demo 5 — I²C (SSD1306 OLED)

Companion to the [M20A wiki · I²C](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/#i2c).

## Function
i2c22 (P1.3 SDA / P1.7 SCL) drives a 128×64 SSD1306 OLED, using a character frame buffer (cfb) to center
`Hello XIAO` / `nRF54LM20B`, refreshing once per second.

## Hardware
| Signal | Pin | XIAO D# |
|---|---|---|
| I²C SDA | P1.3 | D4 |
| I²C SCL | P1.7 | D5 |

SSD1306 module address 0x3C, 3.3 V supply, SDA/SCL to D4/D5, common ground.

## 20B adaptations
- The `i2c22` pinctrl (P1.3/P1.7) is already in the board-level DTS; the overlay only adds `status="okay"` + the ssd1306 node, without redefining pinctrl.
- Console left untouched; LOG goes through USB CDC.
- Second line of text changed from `nRF54LM20A` → `nRF54LM20B`.

## Build / Flash
```
cd 5_i2c_oled
pio run
./flash.sh        # IMGVER defaults to 0.0.134
# Press RST
```

## Verification
- The OLED shows two centered lines of text.
- USB CDC (115200): `Initialized display`, `Display resolution: 128x64`, `Selected 8x8 font idx: N`.
