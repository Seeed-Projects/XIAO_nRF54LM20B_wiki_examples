# XIAO nRF54LM20B — NFC: Writable NDEF Message

> Source on GitHub: **[12_NFC/writable_ndef_msg_3](https://github.com/Seeed-Projects/XIAO_nRF54LM20B_wiki_examples/tree/main/12_NFC/writable_ndef_msg_3)**
> · repo: [Seeed-Projects/XIAO_nRF54LM20B_wiki_examples](https://github.com/Seeed-Projects/XIAO_nRF54LM20B_wiki_examples)

This is the Nordic Connect SDK **[writable NDEF message](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/samples/nfc/writable_ndef_msg/README.html)** sample (NFC Type 4 Tag, read-write emulation), built for the XIAO nRF54LM20B. It exposes an NDEF message over the on-chip NFCT peripheral that a phone or NFC reader can **read and overwrite**; updates are persisted to flash.

The 20A and 20B variants are **byte-for-byte identical at the digital/configuration layer** — they share `nrf54lm20_a_b.dtsi`, the same NFCT peripheral (`d6000` / IRQ 214), and the same `P1.01 / P1.02` NFC pins. The sample's `sample.yaml` lists both `nrf54lm20dk/nrf54lm20a/cpuapp` and `nrf54lm20dk/nrf54lm20b/cpuapp`, so "reference 20A" simply means: the same sample, unchanged. See the **Read/write journey** below for what is *different* about 20B.

## Requirements

| Item | Notes |
|---|---|
| Board | XIAO nRF54LM20B / 20B Sense |
| Toolchain | nRF Connect SDK (NCS) v3.3.0 + `west` (this is an **NCS sample**, not a PlatformIO project — no `platformio.ini`) |
| NFC reader | A smartphone or tablet with NFC Tools (or equivalent) |
| **Antenna** | **External** — see the journey below; the bare XIAO 20B board has no on-board NFC coil |

## Overview

On startup the sample initializes the NFC Type 4 Tag and loads the NDEF message from a file in flash. If the file does not exist, a default **URI record** (`http://www.nordicsemi.com`) is generated. The T4T library runs in **read-write emulation**: read and update procedures are handled internally, and any overwritten message is written back to the NDEF file in flash and survives reboots.

## User interface

The sample uses `dk_buttons_and_leds`. On a Nordic DK the LEDs mean:

| LED | Meaning |
|---|---|
| LED 1 | NFC field present |
| LED 2 | NDEF message was updated (written) |
| LED 4 | NDEF message was read |
| Button 1 (hold at boot) | Restore the default NDEF message |

> The XIAO nRF54LM20B exposes only the RGB LED (and one button), not four discrete LEDs like a DK. The `dk_buttons_and_leds` abstraction maps onto whatever LEDs the board definition declares, so not all three indicators may be visible simultaneously. Treat the serial/NFC-reader behavior as the primary feedback path on XIAO.

## Building and running

This is an NCS sample — build with `west`, not PlatformIO:

```bash
# XIAO board target (requires the Seeed platform board definitions in BOARD_ROOT)
west build -b xiao_nrf54lm20b/nrf54lm20b/cpuapp writable_ndef_msg_3

# …or the Nordic DK target (also listed in sample.yaml)
west build -b nrf54lm20dk/nrf54lm20b/cpuapp writable_ndef_msg_3
```

Flash the signed image through MCUboot serial-recovery DFU: hold **B0 (P0.09) → tap RST → release B0**, then

```bash
nrfutil mcu-manager serial image-upload
nrfutil mcu-manager serial reset
```

## Testing

1. Power the board, then touch the NFC antenna with a phone/tablet.
2. The phone tries to open `http://www.nordicsemi.com` in a browser.
3. Overwrite the tag with your own NDEF message using NFC Tools (or equivalent).
4. Restart the board and touch the antenna again — the new message is served from flash.

## Read/write journey (20B-specific)

Getting NFC read/write actually working on the XIAO 20B took two detours past the stock sample. Documenting them here so the next person doesn't repeat them.

### 1. NCS v3.3.0 — NFC and USB cannot coexist

On NCS v3.3.0, enabling USB implicitly starts the HFXO crystal without the clock driver recording it; a later NFC request for HFXO never receives its clock-started callback, so NFC silently stops. This is **fixed upstream in NCS v3.4.2** (Zephyr `ab802f3`). For v3.3.0 we backport the same 22-line fix:

→ **[12_NFC_fix/patch_ncs-v3.3.0-nfc-usb-clock-fix](../12_NFC_fix/patch_ncs-v3.3.0-nfc-usb-clock-fix/README_en.md)** — install with `python install_ncs330_hfxo24m_patch.py <NCS-root>`.

### 2. The bare board does not scan — root cause is analog/RF, not firmware

Even with the clock patch applied, the stock sample **does not scan on the bare XIAO 20B board**. This was confirmed to be **not** a firmware/driver/library/config issue:

- The NFCT pad config (`PADCONFIG` reset value `0x1` = antenna mode) is identical on 20A and 20B.
- `&nfct { status = "okay"; }` is the only board-level DTS touch on both boards.
- The same `libnfc_t4t` + `nrfx_nfct.c` are linked on both.

The difference is on the **analog layer**. Per the schematic (`XIAO_nRF54LM20B_V1.01_SCH`), the NFC pins route like this:

```
P1.01/NFC1 ── R39(0Ω) ──┬── TP19  (test point)
                        └── C25(300pF) ── GND
P1.02/NFC2 ── R40(0Ω) ──┬── TP20  (test point)
                        └── C26(300pF) ── GND
```

So: full electrical connection + 300 pF match capacitors + TP19/TP20 test points, but **no on-board NFC coil**, and the pins are *not* broken out to the 28-pin header. You must solder an external antenna to **TP19/TP20**.

**Tuning caveat (the usual trap):** the on-board 300 pF (C25/C26) is sized for a bare coil antenna. If you instead wire a **pre-tuned commercial NFC tag** (which has its own internal matching capacitance) to TP19/TP20, the tag's capacitance goes in parallel with the 300 pF, the total blows past the match, the tank detunes away from 13.56 MHz, and it still won't scan. Options:

- **Bare coil antenna** → solder to TP19/TP20, keep C25/C26 (the 300 pF is the intended match; L ≈ 4.6 µH for 13.56 MHz).
- **Pre-tuned tag** → remove C25/C26 and let the tag use its own matching network.

### 3. How to confirm you have RF output (don't be fooled by software)

`AT+NFC:TEST=START → RUNNING,NDEF_LEN=70` only proves the NDEF bytes were encoded in software — it does **not** mean RF is coming out of the antenna. To actually confirm the analog layer, read the NFCT registers while a field is active:

```bash
nrfjprog --memrd 0xD66D4 --n 4   # PADCONFIG, expect 0x1
nrfjprog --memrd 0xD6404 --n 4   # ERRORSTATUS, check error bits when activated
```

## Dependencies

NCS libraries: `nfc_ndef`, `nfc_ndef_msg`, `nfc_ndef_record`, `nfc_ndef_uri_rec`, `nfc_ndef_uri_msg`, `nfc_t4t_ndef_file`, and the Type 4 Tag library from `sdk-nrfxlib`. Zephyr: NVS, ZMS, GPIO. See the sample's `prj.conf` for the full Kconfig set.

## Files

| Path | Role |
|---|---|
| `writable_ndef_msg_3/src/main.c` | Application: T4T init, NDEF encode, field/read/write events |
| `writable_ndef_msg_3/src/ndef_file_m.[ch]` | NDEF file persistence in flash |
| `writable_ndef_msg_3/prj.conf` | Kconfig (NFC T4T + NDEF + flash + DK library) |
| `writable_ndef_msg_3/boards/*` | Per-DK overlays (nRF5340, nRF54H20 power mgmt) |
| `writable_ndef_msg_3/README.rst` | Upstream NCS sample documentation (verbatim) |
