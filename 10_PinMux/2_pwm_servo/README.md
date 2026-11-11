# PinMux demo 2 — PWM (Servo)

Companion to the [M20A wiki · PWM](https://wiki.seeedstudio.com/xiao_nrf54lm20a_pin_multiplexing/#pwm).

## Function
PWM20 channel 0 outputs a 50 Hz servo signal on P1.0 / D0 to drive a standard hobby servo, sweeping back and forth between 0°↔180°. The angle is printed to the serial console in real time.

## Hardware
| Signal | Pin | XIAO D# |
|---|---|---|
| Servo PWM | P1.0 | D0 |

Power the servo separately (5 V), connect its signal line to D0, and share ground.

## 20B adaptations
- Removed the wiki's `chosen{console=&uart20}` and the entire uart20 config: output goes through **USB CDC**; this demo does not use uart20.
- The board-level `pwm20` is bound by default to P1.22–24 (RGB LED); the overlay moves channel 0 to **P1.0** (adds `servo_pwm20_default`, overriding `pinctrl-0`).
- `pwm_set()` explicitly passes `PWM_POLARITY_NORMAL`, so it is unaffected by the board-level RGB common-anode inversion.

## Build / Flash
```
cd 2_pwm_servo
pio run
./flash.sh        # IMGVER defaults to 0.0.131
# Press RST, open cu.usbmodem* @115200
```

## Verification
The serial console keeps printing `Angle: N deg` while the servo sweeps back and forth.
