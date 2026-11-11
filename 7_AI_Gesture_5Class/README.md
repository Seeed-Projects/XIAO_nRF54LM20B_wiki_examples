# XIAO nRF54LM20B Sense — 5-class IMU gesture recognition (Axon NPU) porting & deployment log

> This document records the full process of deploying a Nordic Edge AI model in a PlatformIO + Zephyr
> project, and can serve as a draft for writing a tutorial. All numbers are real build/measurement results.

---

## 1. Overall architecture

```
Edge AI Studio (cloud training)
        │  export deployment package: model C code + prj_example.conf
        ▼
src/nrf_edgeai_generated/nrf_edgeai_user_model.c   ← generated artifact, not modified
        │  called by the app
        ▼
src/main.c + src/imu_lsm6dsl.c                     ← hand-written application
        │
        ▼
PlatformIO + Zephyr 4.4.0 + sdk-edge-ai v3 (Axon NPU runtime)
```

| Item | Description |
|---|---|
| Board | XIAO nRF54LM20B **Sense** (on-board LSM6DS3TR-C 6-axis IMU, nPM1300 PMIC) |
| Model | Edge AI Lab solution 96964 |
| Classes | 5: IDLE / SWIPE_LEFT / SWIPE_RIGHT / ROTATION_CW / ROTATION_CCW |
| Input | 6 channels (ax, ay, az, gx, gy, gz), FLOAT32, **micro-SI units** |
| Window | window = 200, shift = 33 (78 features) |
| Network | Dense 64 → 32 → 16 → Softmax 5, runs on the Axon NPU |
| Runtime | sdk-edge-ai v3.0.0-6 (`~/XIAO-EdgeAI/tools/sdk-edge-ai`, registered as a Zephyr external module via the `XIAO_EDGE_AI_DIR` environment variable; the framework's bundled v2 is not used) |

---

## 2. Model side (pure porting, zero modification)

1. Export the deployment package from Edge AI Studio; place `nrf_edgeai_user_model.c/.h` into `src/nrf_edgeai_generated/`.
2. Copy two lines from the package's `prj_example.conf` into the project's `zephyr/prj.conf` (the Axon buffer sizes must match the model):

```ini
CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE=80
CONFIG_NRF_AXON_PSUM_BUFFER_SIZE=0
```

**Hard constraints (a tutorial must emphasize these):**

- Model weights, feature algorithm, and input normalization logic **must never be modified**;
- The input channel order and units must exactly match the training CSV — get this wrong and the model is useless;
- Confidence (max probability) ≠ real accuracy — do not conflate the two.

---

## 3. Application code (all hand-written)

### `src/main.c` responsibility chain

1. **Startup self-check**: call real APIs like `nrf_edgeai_input_window_size` / `model_outputs_num` /
   `is_runtime_compatible` to verify the model geometry (6 channels, window 200, 5 outputs);
   on mismatch, report and halt — never fail silently.
2. **100 Hz non-blocking sampling**: semaphore-driven — collect IMU data as soon as it arrives, no sleep polling.
3. **Unit conversion**: acceleration → **µm/s²**, gyroscope → **µrad/s** (× 1000000.0f),
   aligned with the micro-SI units of the training CSV.
4. **Sliding window fill**: window = 200, shift = 33, feeding data into `nrf_edgeai_input_window`.
5. **Inference + output**: call the real `nrf_edgeai` API; output 5 classes + confidence +
   `k_cycle_get_32()` measured inference time (**measured 1.25 ms/inf**).
6. **Interaction**: on a recognized gesture, blink the LED once (k_timer one-shot); serial output is throttled to 3 s
   (`CONFIG_GESTURE_DEMO_REPORT_INTERVAL_MS`); `CONFIG_GESTURE_DEMO_DEBUG`
   enables raw inference data; the banner waits for DTR to be raised before printing.
7. **MCUboot self-confirm**: call `boot_write_img_confirmed()` at startup,
   otherwise the uploaded test image will be rolled back by the bootloader on reboot.

### `src/imu_lsm6dsl.c`

LSM6DS3TR-C driver (`st,lsm6dsl` compatible layer, via the board's `imu0` alias + bit-bang I2C).

---

## 4. Two platform-level integration pitfalls (the hardest part of the port)

### Pitfall 1: PlatformIO's Zephyr build never runs ninja

PIO's build flow is: cmake configure → read the source manifest from the **CMake file API** →
compile with SCons itself. **Products of `add_custom_command()` in a module's CMakeLists are never generated.**

sdk-edge-ai v3's banner (`edge_ai_banner.c`) depends on `edge_ai_version.h` /
`edge_ai_commit.h`, both produced by custom commands → pio builds always fail to compile
(a direct cmake + ninja works fine, which is highly misleading).

**Fix** (generate during the configure stage in `zephyr/CMakeLists.txt`, see the "generate
version/commit headers" block):

- `edge_ai_version.h`: use `execute_process` to call Zephyr's bundled
  `gen_version_h.cmake` (identical to the module's original call);
- `edge_ai_commit.h`: the upstream script is missing locally, so reproduce it in NCS upstream
  format, taking the module repo's real git short hash (`git rev-parse HEAD`, truncated to 12 chars).

> Lesson: whenever you integrate a Zephyr module with custom commands, you must patch in
> generation at the configure stage; hand-dropping files into the build directory does not work
> (the build directory is wiped and rebuilt).

### Pitfall 2: MDK double definition, Axon NPU header fails to compile

When `SOC_NRF54LM20B` is selected, because Kconfig has `select SOC_COMPATIBLE_NRF54LM20A`,
**both NRF54LM20A_XXAA and NRF54LM20B_XXAA get defined**. `<mdk/nrf.h>` hits the A branch first,
so the LM20B-specific AXONS NPU definitions in the module's `nrf_axon_platform_zephyr.c`
(`NRF_AXONS_Type`, `AXONS_ENABLE_EN_*`, etc.) are all missing, and compilation fails.

**Fix**: source-file-level properties do not cross module scope, so use a **target-level property** —
swap this single .c out of the zephyr target's `SOURCES` property and replace it with a generated wrapper:

```c
/* nrf_axon_platform_wrapper.c (generated by configure_file) */
#include "<project>/zephyr/mdk_lm20b_compat.h"   /* just one line: #undef NRF54LM20A_XXAA */
#include "<sdk-edge-ai>/lib/axon/platform/src/zephyr/nrf_axon_platform_zephyr.c"
```

**Not a single line of SDK source was touched** — only this one compilation unit had its chip-header selection swapped.

### Minor pitfall 3: generated model header uses C11 `static_assert`

newlib needs to see `<assert.h>` first before it defines it:

```cmake
target_compile_options(app PRIVATE "-include" "assert.h")
```

---

## 5. devicetree (`zephyr/app.overlay`)

Three lines are enough:

```dts
&imu_vdd { regulator-boot-on; };        /* nPM1300 LDO1 powers the IMU, on at boot */
&lsm6ds3tr_c {                          /* deferred probe; app calls device_init() after the power rail stabilizes */
    zephyr,deferred-init;
    status = "okay";
};
&axon { status = "okay"; };             /* enable the NPU node */
```

---

## 6. Flashing chain (macOS, one-shot via `./flash.sh`)

The board ships with a **MCUboot + firmware loader (BOOT_FIRMWARE_LOADER)** architecture,
different from a normal MCUboot DFU: slot1 holds the factory USB MCUmgr loader;
when you hold B0 + reset, MCUboot chains into it to receive firmware and write it to slot0.

| Partition | Offset | Size | Contents |
|---|---|---|---|
| mcuboot | 0x0 | 24 KB | factory bootloader (**leave it alone**) |
| slot0 (image-0) | 0x6000 | 1784 KB (0x1BE000) | **application image (flash target)** |
| slot1 (image-1) | 0x1c4000 | 116 KB | factory USB MCUmgr loader |
| storage | 0x1e1000 | 16 KB | settings storage |

### Three-step flow (fully automated by `flash.sh`)

```bash
PORT=/dev/cu.usbmodem1101 ./flash.sh
```

1. **`pio run`** builds (the `XIAO_EDGE_AI_DIR` env var points to the v3 module);
2. **imgtool signing**: uses the framework's bundled `root-ed25519.pem` + `--pure`
   (this board's MCUboot verification mode requires IMAGE_TLV_PURE) +
   `--header-size 0x800 --align 16 --slot-size 0x1BE000` (strictly matching the partition table) +
   `--version` **must be increasing**;
3. **Upload**: put the board into DFU (hold **B0 (P0.09) → tap RST once → release B0**),
   `nrfutil mcu-manager serial image-upload` uploads → `serial reset` resets and boots.

### Three pitfalls a tutorial must cover

- **1200 bps soft-trigger into DFU does not work** (requires app cooperation, not implemented on this board) — you must press the buttons manually;
- **Version number not increasing → mcuboot rejects** ("same version");
- **After flashing, USB CDC does not appear** (occasional "serial disappears"): just tap RST once —
  once the app runs, CDC enumerates naturally as `XIAO_NRF54LM20B`.

---

## 7. Verification results (real measured data)

```
*** XIAO M20B Edge AI Gesture Demo ***
Board: XIAO nRF54LM20B Sense
Model: XIAO_M20B_Gesture_5Class (solution 96964)
Runtime: Nordic Edge AI / Axon NPU
[AI] Model initialized
[IMU] Sampling rate: 100 Hz (6ch, micro-SI, window 200 shift 33)
[AXON] NPU initialized
[AI] Gesture: ROTATION_CCW
[AI] Confidence: 0.62
[AI] Inference time: 1.248 ms
```

| Metric | Value |
|---|---|
| Flash usage | 140,744 B / 2,084,864 B (**6.8%**) |
| RAM usage | 27,592 B / 524,288 B (**5.3%**) |
| Inference time | ~1.25 ms/inf (Axon NPU) |
| USB CDC stability | 10 minutes continuous online, no dropouts |
| Serial | 115200 baud, banner prints after DTR is raised |

### Gesture test posture (per PRD)

Hold the board **face toward you, USB port pointing down**, and perform in turn:

- swipe left (SWIPE_LEFT) / swipe right (SWIPE_RIGHT)
- rotate wrist clockwise (ROTATION_CW) / rotate wrist counter-clockwise (ROTATION_CCW)
- hold still (IDLE)

Just watch the serial output:

```bash
screen /dev/cu.usbmodem1101 115200   # exit: Ctrl+A then K
```

### Not verified yet

- A quantitative confusion-matrix test of 20 trials per class (100 total) has not been run;
- **Confidence is not real accuracy** — it only reflects how certain the model is about its own judgment.

---

## Appendix: file listing

| File | Role |
|---|---|
| `src/main.c` | Application main logic (sampling / sliding window / inference / LED / serial) |
| `src/imu_lsm6dsl.c` | LSM6DS3TR-C IMU driver |
| `src/nrf_edgeai_generated/` | Edge AI Studio generated artifacts (unmodified) |
| `zephyr/CMakeLists.txt` | Module integration + configure-stage header generation + MDK wrapper |
| `zephyr/mdk_lm20b_compat.h` + `nrf_axon_platform_wrapper.c.in` | LM20B MDK compatibility wrapper |
| `zephyr/app.overlay` | IMU power / deferred probe / NPU node |
| `zephyr/prj.conf` / `zephyr/Kconfig` | Application configuration |
| `platformio.ini` | PIO project definition |
| `flash.sh` | one-shot build + sign + upload script |
