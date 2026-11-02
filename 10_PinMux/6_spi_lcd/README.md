# PinMux demo 6 — SPI（GC9A01 圆屏）

对照 [M20A wiki · SPI](https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#spi)。

## 功能
spi23 驱动 GC9A01 240×240 圆形 LCD，通过 MIPI DBI SPI 接口，RGB565：先循环 6 种纯色满屏，
再画 6 条竖向彩条，循环往复。

## 硬件
| 信号 | 引脚 | XIAO D# |
|---|---|---|
| SPI SCK | P1.4 | D8 |
| SPI MISO | P1.5 | D9 |
| SPI MOSI | P1.6 | D10 |
| LCD CS | P1.31 | D1 |
| LCD DC | P1.29 | D3 |

GC9A01 模块 3.3V 供电，BLK 接 3.3V 或 PWM，共地。

## 20B 适配
- 删 wiki 的 `chosen{console=&uart20}` + `nordic,rpc-uart` + `shell-uart` 和整个 `&uart20` + `uart20_default/sleep` 重定义：LOG 走 USB CDC。
- `spi23` 的 pinctrl（P1.4/1.6/1.5）已在板级 DTS，删 wiki 的 `spi23_default/sleep` 重定义，overlay 只补 `status="okay"` + `cs-gpios=<&gpio1 31>`。
- 保留 mipi_dbi 节点（`dc-gpios=<&gpio1 29>`）+ gc9x01x 子节点，`chosen{display=&gc9x01x}`。
- banner 文字 `nRF54LM20A` → `nRF54LM20B`。

## 编译 / 烧录
```
cd 6_spi_lcd
pio run
./flash.sh        # IMGVER 默认 0.0.135
# 按 RST
```

## 验证
- 屏幕：6 种纯色循环（每种 ~0.6 s）→ 6 条竖彩条（~2 s），循环。
- USB CDC（115200）：`GC9A01 demo`、`Display ready, starting demo loop`、`Solid: RED…`、`Color bars`。
