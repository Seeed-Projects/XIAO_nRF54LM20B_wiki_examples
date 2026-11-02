# PinMux 烧录报错日志 · DFU 1200-baud 软触发失败

> 归档时间：2026-10-28
> 触发场景：PinMux demo 2 (pwm_servo) 烧录，`nrfutil image-upload` Timeout
> 影响范围：demo 2~6（及任何用精简 Zephyr demo + mcuboot 的工程）

## 测试环境

| 项 | 值 |
|---|---|
| 主机 | macOS 26.5.2 (Build 25F84), Darwin 25.5.0 arm64 (Apple M2) |
| PlatformIO Core | 6.2.0 |
| Zephyr | 4.4.0 (framework-zephyr-nrf54lm20 PIO 包) |
| Python | 3.11.7 (`~/.platformio/penv/.zephyr-4.4.0/bin/python`) |
| nrfutil | 8.2.0 (c910332, 2026-04-21, aarch64-apple-darwin) |
| imgtool (mcuboot) | 2.4.0 |
| 签名方式 | ed25519 + `--pure`, header 0x800, align 16, slot-size 0x1BE000 |
| 签名 key | `.../framework-zephyr-nrf54lm20/_pio/bootloader/mcuboot/root-ed25519.pem` |
| 板子 | Seeed XIAO nRF54LM20B (nRF54LM20B, cortex-m33, mcuboot) |
| 板定义 | `~/.platformio/platforms/SeeedStudio/boards/seeed-xiao-nrf54lm20b.json` |
| 串口 | `/dev/cu.usbmodem1101` |
| 板定义 CDC | `app_vidpid=2886:8013`, `loader_vidpids=2886:0013`, `use_1200bps_touch=true` |
| demo 镜像 | v0.0.131 (pwm_servo), `firmware.signed.bin` 83713 B, Flash 3.7%/RAM 3.2% |
| self-confirm | `CONFIG_BOOTLOADER_MCUBOOT=y` + img_manager + `boot_write_img_confirmed()` |

## 报错原文

`flash.sh` 步骤 1/3 build、2/3 sign 均成功，3/3 upload 失败：

`/tmp/nrfutil_upload.json`：
```json
{"type":"task_begin","data":{"task":{"id":"...","description":"/dev/cu.usbmodem1101","name":"mcu-upload-binary-file","data":null}}}
{"type":"task_end","data":{"task":{"id":"...","description":"/dev/cu.usbmodem1101","name":"mcu-upload-binary-file","data":null},
 "message":"Upload image failed: Timeout occured while sending or receiving data\n",
 "result":"fail","error":{"code":1,"description":"Timeout occured while sending or receiving data\n"}}}
```

手动软触发验证（pyserial 1200 baud touch，DTR/RTS low）：
```
before: /dev/cu.usbmodem1101
touch:  opened 1200 + DTR/RTS low + closed OK
after:  /dev/cu.usbmodem1101   ← 端口不重枚举、VID:PID 不变 → touch 无效
```

## 根因

**不是 touch 命令错，是应用固件不响应 1200-baud。**

1. Zephyr「1200-baud → 自动 reboot 进 bootloader」是 **BOSSA 专用机制**：
   `Kconfig.zephyr:1180` `BOOTLOADER_BOSSA_DEVICE_NAME depends on BOOTLOADER_BOSSA && CDC_ACM_DTE_RATE_CALLBACK_SUPPORT`
2. 20B 用 **mcuboot**，demo1 实测 `.config`：`CONFIG_BOOTLOADER_MCUBOOT=y`、`# CONFIG_BOOTLOADER_BOSSA is not set`、`CDC_ACM_DTE_RATE_CALLBACK_SUPPORT` 未启用。
3. → CDC ACM 驱动不监听 1200 line coding → 应用不 reboot → mcuboot serial recovery 等不到握手 → 60s Timeout。
4. 板定义 `use_1200bps_touch=true` 只让 PIO **发** touch（`uf2upload.py: trigger_bootloader_1200baud()`），应用侧不响应则无效。

补充：mcuboot 端本身支持 `BOOT_SERIAL_WAIT_FOR_DFU`（`_pio/bootloader/mcuboot/boot/zephyr/main.c:613`），缺的是应用侧 1200 回调。

## 原始证据（命令 + 真实 stdout）

### EVIDENCE 1 — nrfutil upload 报错原文（应用模式，Timeout）
命令：`nrfutil mcu-manager serial image-upload --firmware firmware.signed.bin --serial-port /dev/cu.usbmodem1101 --timeout 60 --json`
`/tmp/nrfutil_upload.json`：
```json
{"type":"task_begin",...,"description":"/dev/cu.usbmodem1101","name":"mcu-upload-binary-file"}
{"type":"task_end",...,"message":"Upload image failed: Timeout occured while sending or receiving data\n","result":"fail","error":{"code":1,"description":"Timeout occured while sending or receiving data\n"}}
Error: Timeout occured while sending or receiving data
```

### EVIDENCE 2 — 手动 1200 baud touch 前后端口不变（软触发无效）
```
--- before ---            /dev/cu.usbmodem1101
touch: opened 1200 + DTR/RTS low + closed OK
--- wait 3s ---
--- after ---             /dev/cu.usbmodem1101   ← 未重枚举
```

### EVIDENCE 3 — demo1 `.config`（BOSSA 关、DTE_RATE 回调未启用）
`platforms/10_PinMux/1_digital_gpio/.pio/build/seeed-xiao-nrf54lm20b/zephyr/.config`：
```
69:CONFIG_BOOTLOADER_MCUBOOT=y
150:CONFIG_REBOOT=y
1742:# CONFIG_BOOTLOADER_BOSSA is not set
```
（`CDC_ACM_DTE_RATE_CALLBACK_SUPPORT` 不在 .config 中 = 未启用）

### EVIDENCE 4 — Zephyr Kconfig：1200 回调绑 BOSSA
`framework-zephyr-nrf54lm20/Kconfig.zephyr`：
```
1178:config BOOTLOADER_BOSSA_DEVICE_NAME
1179-    string "BOSSA CDC ACM device name"
1180-    depends on BOOTLOADER_BOSSA && CDC_ACM_DTE_RATE_CALLBACK_SUPPORT
1181-    default "CDC_ACM_0"
```

### EVIDENCE 5 — 板定义声明 1200 touch + VID:PID
`seeed-xiao-nrf54lm20b.json` `upload` 段：
```json
{"protocol":"nrfutil-mcumgr","use_1200bps_touch":true,"wait_for_upload_port":true,
 "cdc":{"app_vidpid":"2886:8013","loader_vidpids":["2886:0013"]}}
```

### EVIDENCE 6 — USB 产品名/VID:PID 区分 app vs DFU
命令：`ioreg -rc IOUSBHostDevice` 解析 `/dev/cu.usbmodem1101`
- 应用模式（报错时）：`USB Product Name="XIAO_NRF54LM20B"`, VID:PID=`2886:8013`
- DFU 模式（进 DFU 后）：`USB Product Name="CDC ACM serial backend"`, `idVendor=10374(0x2886) idProduct=19(0x13)` → `2886:0013`

### EVIDENCE 7 — 硬触发进 DFU 后 upload 成功（复测）
命令：同 EVIDENCE 1，板子已 B0+RST 进 DFU（2886:0013）
`/tmp/nrfutil_upload_demo2.json`：
```json
{"type":"task_end",...,"message":"Uploaded .../2_pwm_servo/.../firmware.signed.bin","result":"success","error":null}
```
进度峰值 `progressPercentage:99`。结论：构建链无误，唯一变量是板子是否在 DFU。

## 证据文件
- demo1 `.config`：`platforms/10_PinMux/1_digital_gpio/.pio/build/seeed-xiao-nrf54lm20b/zephyr/.config`
- 板级 Kconfig：`.../xiao_nrf54lm20b/Kconfig.defconfig`（`_APP_CDC_VIDPID=2886:8013` 注释）
- PIO touch 实现：`.../SeeedStudio/builder/tools/uf2upload.py:204 trigger_bootloader_1200baud()`
- 上传原始 JSON：`/tmp/nrfutil_upload.json`

## 对策
- **烧录前硬触发进 DFU**：按住 B0(P0.09) → 按一下 RST → 松 B0。loader 枚举 VID:PID `2886:0013`。
- 烧完手动按 RST（勿用 `nrfutil mcu-manager serial reset`，软重置杀 USB CDC，见 memory `nrf54lm20b-flash-serial-disappear`）。
- 想软触发一劳永逸：prj.conf 加 `CONFIG_CDC_ACM_DTE_RATE_CALLBACK_SUPPORT=y` + 注册 dte_rate 回调（rate==1200 → `sys_reboot`）。6 个 demo 都要改，不值得。

## 关联 memory
- `nrf54lm20b-1200baud-touch-mcuboot` — 本日志的 memory 摘要
- `nrf54lm20b-flash-serial-disappear` — 烧完勿用 serial reset
- `nrf54lm20b-mac-flashing-setup` — 工具链命令
