# NCS v3.3.0 NFC x USB Clock Patch — Usage (English)

## The problem

On NCS v3.3.0 and nRF54LM20 (XIAO nRF54LM20B / 20A V2), USB and NFC cannot
coexist: enabling USB implicitly starts the HFXO crystal without the clock
driver recording it, and a later NFC request for HFXO never receives its
clock-started callback, so NFC stops working. The defect is **fixed in NCS
v3.4.2** (upstream Zephyr `ab802f3`). This patch backports the same 22-line
fix to v3.3.0.

## Files

| File | Purpose |
|---|---|
| `install_ncs330_hfxo24m_patch.py` | Installer (Windows/macOS/Linux) |

## Install

1. Open any terminal with Python 3 and run the script with the SDK root as
   its argument

   ```bash
   python install_ncs330_hfxo24m_patch.py C:\Users\<you>\ncs\v3.3.0    # Windows
   # python3 install_ncs330_hfxo24m_patch.py ~/ncs/v3.3.0              # macOS / Linux
   ```
   `Patch installed: ...` means success.

## Revert

```bash
python install_ncs330_hfxo24m_patch.py <SDK-root> --revert
```

## Troubleshooting

* **`python` not found on Windows**: install Python.
* **`Patch already installed`**: the patch is already applied, nothing to do.
* **`expected original code not found`**: the SDK is not a pristine v3.3.0,
  or the file was modified. Restore the file and retry.
* **Patch applied but no effect**: rebuild pristine.
