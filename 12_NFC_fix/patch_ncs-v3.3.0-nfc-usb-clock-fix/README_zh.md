# NCS v3.3.0 NFC×USB 时钟补丁 — 使用方法（中文）

## 问题

NCS v3.3.0 在 nRF54LM20（XIAO nRF54LM20B / 20A V2）上，USB 与 NFC 不能共存：
USB 启动时会隐式拉起 HFXO 晶振而不被时钟驱动记录，NFC 随后申请 HFXO 收不到
"时钟已启动"回调，NFC 功能失效。该缺陷在 **NCS v3.4.2 已官方修复**（上游
Zephyr `ab802f3`）。本补丁把同样的 22 行修复移植到 v3.3.0。

## 文件

| 文件 | 说明 |
|---|---|
| `install_ncs330_hfxo24m_patch.py` | 安装脚本（Windows/macOS/Linux 通用） |

## 安装步骤

1. 打开任意有 Python 3 的终端，把 SDK 根目录作为参数运行脚本

   ```bash
   python install_ncs330_hfxo24m_patch.py C:\Users\<你>\ncs\v3.3.0    # Windows
   # python3 install_ncs330_hfxo24m_patch.py ~/ncs/v3.3.0             # macOS / Linux
   ```
   看到 `Patch installed: ...` 即成功。

## 回退

```bash
python install_ncs330_hfxo24m_patch.py <SDK根目录> --revert
```

## 常见问题

* **Windows 找不到 `python`**：请安装python
* **`Patch already installed`**：已打过补丁，无需重复操作。
* **`expected original code not found`**：SDK 不是纯净的 v3.3.0，或该文件已被
  修改过。恢复该文件后重试。
* **装完补丁没生效**：pristine 重编。
