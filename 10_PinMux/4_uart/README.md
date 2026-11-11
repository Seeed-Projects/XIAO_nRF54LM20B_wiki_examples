# PinMux demo 4 — UART (uart21 loopback)

Companion to the [M20A wiki · UART](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/#uart).

## Function
UART21 (P1.8 TX / P1.9 RX) in polling mode: echoes back each received line and sends a heartbeat every 5 s.
`LOG_INF` goes through **USB CDC** (`cu.usbmodem*`), while uart21 carries the echo traffic — the two paths are separate and do not interfere.

## Hardware
| Signal | Pin | XIAO D# | Wiring |
|---|---|---|---|
| UART21 TX | P1.8 | D6 | → USB-TTL RX |
| UART21 RX | P1.9 | D7 | ← USB-TTL TX |
| GND | — | GND | common ground |

Use a 3.3 V USB-TTL adapter. The board's own USB CDC stays connected to the PC for LOG output.

## 20B adaptations
- Removed `&uart20` from the wiki overlay: console is not moved to uart20, LOG goes through the default USB CDC.
- The `&uart21` pinctrl (P1.8/P1.9) is already defined in the board-level DTS; the overlay only adds `status="okay"`.
- Polling mode: do not enable async on uart21, otherwise `uart_poll_in()` returns `-ENOTSUP`.

## Build / Flash
```
cd 4_uart
pio run
./flash.sh        # IMGVER defaults to 0.0.133
# Press RST
```

## Verification
- USB CDC (115200): `UART21 device ready` + `Heartbeat #N sent`.
- USB-TTL terminal (115200, P1.8/P1.9): typed characters are echoed back as lines, with a `[Heartbeat #N]` line every 5 s.
