# 8_AI_Wakeword — "okay nordic" 唤醒词检测（Axon NPU）

在 XIAO nRF54LM20B Sense 上运行 Nordic Edge AI Lab 训练的 **okay_nordic 唤醒词模型**，
推理跑在 nRF54LM20B 的 **Axon NPU** 上，音频来自板载 PDM 麦克风。

## 模型

| 项 | 值 |
|---|---|
| Edge AI Lab Solution | `okay_nordic`（唤醒词 wakeword） |
| 输入 | int16 单声道 PCM，16 kHz |
| 窗口 | 160 采样（10 ms），步进 160，流式 |
| 特征 | 40 维 mel 频谱（Edge AI runtime 内部提取，固件只喂原始 PCM） |
| 输出 | 1 类：唤醒词出现概率 |
| 推理 | Axon NPU（`nrf_edgeai_run_inference_axon_audiomels`），runtime v3 |

模型文件在 `src/nrf_edgeai_generated/`，**生成物一行未改**。

## 数据通路（与官方 sdk-edge-ai `applications/ww_kws` 示例同构）

```
PDM2 mic (pdm20) → 160 采样/块 (10 ms)
      → nrf_edgeai_feed_inputs() 喂原始 int16 PCM
      → nrf_edgeai_run_inference()  (Axon NPU)
      → 概率阈值 + 滑窗去抖 (12/20 窗口)
      → 绿色 LED 闪 300 ms + 串口打印
```

mic 块大小刻意等于模型窗口（都是 160 采样），每次 `dmic_read()` 恰好完成一个窗口。

## 工程结构

```
├── src/main.c                       ← 应用（采集 + 喂入 + 去抖 + 反馈）
├── src/nrf_edgeai_generated/        ← Edge AI Lab 生成的模型（零修改）
├── zephyr/CMakeLists.txt            ← PIO/MDK 兼容层（见 7_AI_Gesture_5Class 的踩坑记录）
├── zephyr/prj.conf                  ← Edge AI + Axon(6048) + AUDIO_DMIC + MCUboot
├── zephyr/app.overlay               ← imu_vdd boot-on + pdm20 + axon 使能
├── zephyr/Kconfig                   ← 阈值/去抖可调参数
└── flash.sh                         ← 编译 + 签名 + 上传
```

## 编译

```bash
cd 8_AI_Wakeword
XIAO_EDGE_AI_DIR=~/XIAO-EdgeAI/tools/sdk-edge-ai pio run
```

（需要本地 sdk-edge-ai **v3**，模型生成的 runtime 版本号是 3。）

## 烧录

板子进 DFU：**按住 B0 (P0.09) → 点一下 RST → 松开 B0**，然后：

```bash
./flash.sh                 # 串口自动检测；重复烧录时 IMGVER=0.0.101 ./flash.sh 递增版本
```

## 验证

```bash
screen /dev/cu.usbmodem1101 115200   # 退出 Ctrl+A K
```

对着板子说 **"okay nordic"**：

```
*** XIAO M20B Edge AI Wakeword Demo ***
[AI] Edge AI runtime 3.0.0, model flash/ROM size xxx bytes
[MIC] Sampling rate: 16000 Hz (int16 mono, window 160 = 10 ms)
[STAT] 500 inferences, avg xxx us, peak prob 0.03
[AI] Wakeword detected: "okay_nordic"
[AI] Probability: 0.94
```

每 5 秒一条 `[STAT]`：推理次数、平均耗时、峰值概率——**调阈值就看着 peak prob 调**。

## 调参（zephyr/Kconfig → prj.conf 里覆盖）

| 参数 | 默认 | 说明 |
|---|---|---|
| `WW_WAKE_PROBABILITY_THRESHOLD_MILLI` | 800 | 单窗口判定为正的概率阈值（官方 DK 示例用 990，换 XIAO 的 PDM 麦放宽到 800） |
| `WW_WAKE_HISTORY_SIZE` | 20 | 去抖滑窗长度（每窗 10 ms） |
| `WW_WAKE_COUNT_THRESHOLD` | 12 | 滑窗内超阈值窗口数 ≥ 此值才确认 |
| `WW_WAKE_DEBUG` | n | 每 200 ms 打印原始概率 |

> 模型训练用的大概率是 nRF54 DK 的麦克风，XIAO 的 PDM 麦频响不同，灵敏度/阈值可能需要按实测微调（看 `[STAT]` 的 peak prob）。

## 依赖

- PlatformIO + SeeedStudio platform（板卡 `seeed-xiao-nrf54lm20b`）
- sdk-edge-ai v3（`~/XIAO-EdgeAI/tools/sdk-edge-ai`）
- 实测占用：Flash 197,100 B / 1784 KB（10.8%），RAM 37,568 B / 511 KB（7.2%）
