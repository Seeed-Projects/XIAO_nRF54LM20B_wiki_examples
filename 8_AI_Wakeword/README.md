# 8_AI_Wakeword — "okay nordic" wakeword detection (Axon NPU)

Runs the **okay_nordic wakeword model** trained in Nordic Edge AI Lab on the XIAO nRF54LM20B Sense,
with inference on the nRF54LM20B's **Axon NPU** and audio from the on-board PDM microphone.

## Model

| Item | Value |
|---|---|
| Edge AI Lab Solution | `okay_nordic` (wakeword) |
| Input | int16 mono PCM, 16 kHz |
| Window | 160 samples (10 ms), step 160, streaming |
| Features | 40-dim mel spectrogram (extracted internally by the Edge AI runtime; firmware feeds only raw PCM) |
| Output | 1 class: probability of the wakeword |
| Inference | Axon NPU (`nrf_edgeai_run_inference_axon_audiomels`), runtime v3 |

The model files live in `src/nrf_edgeai_generated/` — **the generated artifact is unmodified**.

## Data path (structurally identical to the official sdk-edge-ai `applications/ww_kws` example)

```
PDM2 mic (pdm20) → 160 samples/block (10 ms)
      → nrf_edgeai_feed_inputs() feeds raw int16 PCM
      → nrf_edgeai_run_inference()  (Axon NPU)
      → probability threshold + sliding-window debounce (12/20 window)
      → green LED blinks 300 ms + serial print
```

The mic block size is deliberately equal to the model window (both 160 samples), so each `dmic_read()` completes exactly one window.

## Project structure

```
├── src/main.c                       ← application (capture + feed + debounce + feedback)
├── src/nrf_edgeai_generated/        ← Edge AI Lab generated model (zero modification)
├── zephyr/CMakeLists.txt            ← PIO/MDK compatibility layer (see the pitfall notes in 7_AI_Gesture_5Class)
├── zephyr/prj.conf                  ← Edge AI + Axon(6048) + AUDIO_DMIC + MCUboot
├── zephyr/app.overlay               ← imu_vdd boot-on + pdm20 + axon enable
├── zephyr/Kconfig                   ← tunable threshold / debounce parameters
└── flash.sh                         ← build + sign + upload
```

## Build

```bash
cd 8_AI_Wakeword
XIAO_EDGE_AI_DIR=~/XIAO-EdgeAI/tools/sdk-edge-ai pio run
```

(Requires a local sdk-edge-ai **v3**; the model's generated runtime version is 3.)

## Flash

Put the board into DFU: **hold B0 (P0.09) → tap RST once → release B0**, then:

```bash
./flash.sh                 # auto-detects the serial port; for repeated flashes, bump the version with IMGVER=0.0.101 ./flash.sh
```

## Verification

```bash
screen /dev/cu.usbmodem1101 115200   # exit Ctrl+A K
```

Say **"okay nordic"** toward the board:

```
*** XIAO M20B Edge AI Wakeword Demo ***
[AI] Edge AI runtime 3.0.0, model flash/ROM size xxx bytes
[MIC] Sampling rate: 16000 Hz (int16 mono, window 160 = 10 ms)
[STAT] 500 inferences, avg xxx us, peak prob 0.03
[AI] Wakeword detected: "okay_nordic"
[AI] Probability: 0.94
```

A `[STAT]` line every 5 seconds: inference count, average time, peak probability — **tune the threshold by watching peak prob**.

## Tuning (zephyr/Kconfig → override in prj.conf)

| Parameter | Default | Description |
|---|---|---|
| `WW_WAKE_PROBABILITY_THRESHOLD_MILLI` | 800 | per-window probability threshold to count as a positive (official DK example uses 990; relaxed to 800 for the XIAO's PDM mic) |
| `WW_WAKE_HISTORY_SIZE` | 20 | debounce sliding-window length (10 ms per window) |
| `WW_WAKE_COUNT_THRESHOLD` | 12 | confirm only when the number of above-threshold windows in the sliding window ≥ this value |
| `WW_WAKE_DEBUG` | n | print raw probability every 200 ms |

> The model was very likely trained with an nRF54 DK microphone; the XIAO's PDM mic has a different frequency response, so sensitivity/threshold may need fine-tuning based on measurement (watch peak prob in `[STAT]`).

## Dependencies

- PlatformIO + SeeedStudio platform (board `seeed-xiao-nrf54lm20b`)
- sdk-edge-ai v3 (`~/XIAO-EdgeAI/tools/sdk-edge-ai`)
- Measured footprint: Flash 197,100 B / 1784 KB (10.8%), RAM 37,568 B / 511 KB (7.2%)
