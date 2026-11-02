# PinMux demo 4 — UART (uart21 回环)

对照 [M20A wiki · UART](https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#uart)。

## 功能
UART21（P1.8 TX / P1.9 RX）轮询模式：收到的行原样回显，每 5 s 发一次心跳。
`LOG_INF` 走 **USB CDC**（cu.usbmodem\*），uart21 上是 echo 流量 —— 两路分开，互不干扰。

## 硬件
| 信号 | 引脚 | XIAO D# | 接法 |
|---|---|---|---|
| UART21 TX | P1.8 | D6 | → USB-TTL RX |
| UART21 RX | P1.9 | D7 | ← USB-TTL TX |
| GND | — | GND | 共地 |

USB-TTL 用 3.3V 电平。板子本身的 USB CDC 同时接电脑看 LOG。

## 20B 适配
- 删 wiki overlay 里的 `&uart20`：console 不挪到 uart20，LOG 走默认 USB CDC。
- `&uart21` 的 pinctrl（P1.8/P1.9）已在板级 DTS 定义，overlay 只补 `status="okay"`。
- 轮询模式：不要给 uart21 开 async，否则 `uart_poll_in()` 返回 `-ENOTSUP`。

## 编译 / 烧录
```
cd 4_uart
pio run
./flash.sh        # IMGVER 默认 0.0.133
# 按 RST
```

## 验证
- USB CDC（115200）：`UART21 device ready` + `Heartbeat #N sent`。
- USB-TTL 终端（115200，P1.8/P1.9）：输入字符回显成行，每 5 s 一次 `[Heartbeat #N]`。
