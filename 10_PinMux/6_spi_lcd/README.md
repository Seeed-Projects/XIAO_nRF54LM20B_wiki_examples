# PinMux demo 6 — SPI (GC9A01 round display)

Companion to the [M20A wiki · SPI](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/#spi).

## Function
spi23 drives a GC9A01 240×240 round LCD via the MIPI DBI SPI interface, RGB565: first cycles through 6 solid
full-screen colors, then draws 6 vertical color bars, looping back and forth.

## Hardware
| Signal | Pin | XIAO D# |
|---|---|---|
| SPI SCK | P1.4 | D8 |
| SPI MISO | P1.5 | D9 |
| SPI MOSI | P1.6 | D10 |
| LCD CS | P1.31 | D1 |
| LCD DC | P1.29 | D3 |

GC9A01 module on 3.3 V supply, BLK to 3.3 V or PWM, common ground.

## 20B adaptations
- Removed the wiki's `chosen{console=&uart20}` + `nordic,rpc-uart` + `shell-uart` and the entire `&uart20` + `uart20_default/sleep` redefinition: LOG goes through USB CDC.
- The `spi23` pinctrl (P1.4/1.6/1.5) is already in the board-level DTS; removed the wiki's `spi23_default/sleep` redefinition, the overlay only adds `status="okay"` + `cs-gpios=<&gpio1 31>`.
- Kept the mipi_dbi node (`dc-gpios=<&gpio1 29>`) + gc9x01x child node, `chosen{display=&gc9x01x}`.
- Banner text changed from `nRF54LM20A` → `nRF54LM20B`.

## Build / Flash
```
cd 6_spi_lcd
pio run
./flash.sh        # IMGVER defaults to 0.0.135
# Press RST
```

## Verification
- Screen: 6 solid colors cycling (~0.6 s each) → 6 vertical color bars (~2 s), looping.
- USB CDC (115200): `GC9A01 demo`, `Display ready, starting demo loop`, `Solid: RED…`, `Color bars`.
