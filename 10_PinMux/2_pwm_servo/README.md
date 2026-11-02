# PinMux demo 2 — PWM (舵机)

对照 [M20A wiki · PWM](https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#pwm)。

## 功能
PWM20 通道 0 输出 50 Hz 舵机信号，在 P1.0 / D0 上驱动标准 hobby servo，0°↔180° 来回扫描，角度实时打印到串口。

## 硬件
| 信号 | 引脚 | XIAO D# |
|---|---|---|
| Servo PWM | P1.0 | D0 |

舵机电源单独供（5V），信号线接 D0，共地。

## 20B 适配
- 删 wiki 的 `chosen{console=&uart20}` 和整个 uart20 配置：输出走 **USB CDC**，本 demo 不用 uart20。
- 板级 `pwm20` 默认绑 P1.22-24（RGB LED），overlay 把通道 0 改到 **P1.0**（新增 `servo_pwm20_default`，覆盖 `pinctrl-0`）。
- `pwm_set()` 显式传 `PWM_POLARITY_NORMAL`，不受板级 RGB 共阳极反相影响。

## 编译 / 烧录
```
cd 2_pwm_servo
pio run
./flash.sh        # IMGVER 默认 0.0.131
# 按 RST，开 cu.usbmodem* @115200
```

## 验证
串口持续打印 `Angle: N deg`，舵机来回转动。
