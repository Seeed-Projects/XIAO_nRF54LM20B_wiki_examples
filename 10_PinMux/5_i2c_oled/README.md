# PinMux demo 5 — I²C（SSD1306 OLED）

对照 [M20A wiki · I²C](https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#i2c)。

## 功能
i2c22（P1.3 SDA / P1.7 SCL）驱动 128×64 SSD1306 OLED，用字符帧缓冲（cfb）居中显示
`Hello XIAO` / `nRF54LM20B`，每秒刷新。

## 硬件
| 信号 | 引脚 | XIAO D# |
|---|---|---|
| I²C SDA | P1.3 | D4 |
| I²C SCL | P1.7 | D5 |

SSD1306 模块地址 0x3C，3.3V 供电，SDA/SCL 接 D4/D5，共地。

## 20B 适配
- `i2c22` 的 pinctrl（P1.3/P1.7）已在板级 DTS，overlay 只补 `status="okay"` + ssd1306 节点，不重复定义 pinctrl。
- 不动 console，LOG 走 USB CDC。
- 第二行文字 `nRF54LM20A` → `nRF54LM20B`。

## 编译 / 烧录
```
cd 5_i2c_oled
pio run
./flash.sh        # IMGVER 默认 0.0.134
# 按 RST
```

## 验证
- OLED 屏幕居中显示两行文字。
- USB CDC（115200）：`Initialized display`、`Display resolution: 128x64`、`Selected 8x8 font idx: N`。
