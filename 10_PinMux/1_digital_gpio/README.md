# PinMux demo 1 — 数字引脚 (Digital GPIO)

对照 [M20A wiki · 数字引脚](https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#digital)。

## 功能
Grove Button (P1.0 / D0) 作输入，Grove Buzzer (P1.31 / D1) 作输出：按下响，松开停。

## 硬件
| 信号 | 引脚 | XIAO D# | Grove |
|---|---|---|---|
| Button | P1.0 | D0 | Grove Button（板载下拉，高电平=按下）|
| Buzzer | P1.31 | D1 | **有源**蜂鸣器（内置振荡，HIGH=响）|

> ⚠️ 必须是**有源**蜂鸣器。无源压电片需要 PWM 驱动，请改用 demo 2。

## 20B 适配
- 删除 wiki overlay 里 `zephyr,console=&uart20`：输出走板级默认 **USB CDC**（`cu.usbmodem*`），不用接 USB-TTL。
- GPIO 不经 pinctrl，P1.0/P1.31 板级默认可用，overlay 留空。
- 加 mcuboot self-confirm。

## 编译 / 烧录
```
cd 1_digital_gpio
pio run                 # 编译
./flash.sh              # 构建+签名+上传（板子先进 DFU）
# 烧完按一次 RST，再开 cu.usbmodem* @115200
```

## 验证
按按钮 → 串口打印 `Button PRESSED -> Buzzer ON`，蜂鸣器响；松开 → `Button released -> Buzzer OFF`。
