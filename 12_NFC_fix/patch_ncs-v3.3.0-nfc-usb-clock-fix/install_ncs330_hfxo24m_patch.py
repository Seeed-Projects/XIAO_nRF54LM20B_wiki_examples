#!/usr/bin/env python3
"""Install the nRF54LM20 USB x NFC clock fix into a local NCS v3.3.0 SDK.

Bug summary: on NCS v3.3.0, enabling USB starts the HFCLK24M clock domain,
which implicitly starts HFXO without telling the clock driver; an NFC
application requesting HFXO afterwards never gets its clock-started callback,
so USB and NFC cannot coexist.  Fixed upstream in NCS v3.4.2 (Zephyr ab802f3
/ sdk-zephyr ca5c120c).  This script backports the same fix to NCS v3.3.0 by
editing one file in place -- no git required.  Details:
hfxo24m-clock-bug-mechanism.md in this directory.

Requirements: Python 3 only.  (The .patch file shipped alongside is for
reference / manual `git apply` users; the script does not need it.)

Usage:

    python install_ncs330_hfxo24m_patch.py <NCS-v3.3.0-SDK-root>             # install
    python install_ncs330_hfxo24m_patch.py <NCS-v3.3.0-SDK-root> --revert    # uninstall

<SDK-root> is the directory containing zephyr/ and nrf/.  On Windows you can
drag the SDK folder into the terminal to fill in its path.  (Running the
script from inside the SDK root without an argument also works.)

Rebuild your application with a pristine build directory afterwards
(e.g. "west build -p always ...").
"""

import os
import sys

TARGET = os.path.join("zephyr", "drivers", "clock_control", "clock_control_nrf.c")

ORIGINAL = """\
#if NRF_CLOCK_HAS_HFCLK24M
static void hfclk24m_start(void)
{
	nrfx_clock_start(NRF_CLOCK_DOMAIN_HFCLK24M);
}

static void hfclk24m_stop(void)
{
	nrfx_clock_stop(NRF_CLOCK_DOMAIN_HFCLK24M);
}
#endif
"""

PATCHED = """\
#if NRF_CLOCK_HAS_HFCLK24M
static struct onoff_client hfclk_for_24m_cli;

static void hfclk24m_start(void)
{
	int ret;
	struct onoff_manager *mgr =
		z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);

	/* Even though HFXO24M automatically starts HFXO, it is much easier
	 * to manage from software perspective if XO is explicitly requested.
	 */
	sys_notify_init_spinwait(&hfclk_for_24m_cli.notify);
	ret = onoff_request(mgr, &hfclk_for_24m_cli);
	__ASSERT(ret >= 0, "Failed to request HFCLK for HFCLK24M: %d", ret);

	nrfx_clock_start(NRF_CLOCK_DOMAIN_HFCLK24M);
}

static void hfclk24m_stop(void)
{
	(void)onoff_cancel_or_release(
		z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF),
		&hfclk_for_24m_cli);

	nrfx_clock_stop(NRF_CLOCK_DOMAIN_HFCLK24M);
}
#endif
"""


def main():
    revert = "--revert" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--revert"]

    # Primary: explicit SDK root argument.  Fallback: the current directory,
    # for users who cd into the SDK root themselves.
    root = os.path.abspath(args[0]) if args else os.getcwd()
    target = os.path.join(root, TARGET)

    if not os.path.isfile(target):
        sys.exit(
            "ERROR: %s not found under %s.\n\n"
            "Usage: python %s <NCS-v3.3.0-SDK-root>%s\n"
            "<SDK-root> is the directory containing zephyr/ and nrf/.\n"
            "Common locations:\n"
            "  Windows: C:\\Users\\<you>\\ncs\\v3.3.0\n"
            "  macOS:   ~/ncs/v3.3.0\n"
            "  Linux:   ~/ncs/v3.3.0"
            % (TARGET, root, os.path.basename(__file__),
               " --revert" if revert else ""))

    with open(target, encoding="utf-8", newline="") as f:
        content = f.read()

    nl = "\r\n" if "\r\n" in content else "\n"
    original = ORIGINAL.replace("\n", nl)
    patched = PATCHED.replace("\n", nl)

    if "--revert" in sys.argv:
        if patched not in content:
            print("Patch not present, nothing to revert.")
            return
        content = content.replace(patched, original, 1)
        with open(target, "w", encoding="utf-8", newline="") as f:
            f.write(content)
        print("Patch reverted: %s" % target)
        return

    if patched in content:
        print("Patch already installed: %s" % target)
        return

    if original not in content:
        sys.exit(
            "ERROR: expected original code not found in %s\n"
            "(the SDK is probably not a pristine NCS v3.3.0, or the file "
            "was already modified).  Nothing was changed." % target)

    content = content.replace(original, patched, 1)
    with open(target, "w", encoding="utf-8", newline="") as f:
        f.write(content)

    print("Patch installed: %s" % target)
    print("")
    print("Next steps:")
    print("  - rebuild with a pristine build directory (west build -p always ...)")
    print("  - revert with: python %s --revert" % os.path.basename(__file__))


if __name__ == "__main__":
    main()
