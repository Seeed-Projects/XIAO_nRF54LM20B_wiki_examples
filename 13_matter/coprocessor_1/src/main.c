/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>

#if defined(CONFIG_RCP_SAMPLE_HCI)
#include "rcp_hci.h"
#endif

LOG_MODULE_REGISTER(coprocessor_sample, CONFIG_OT_COPROCESSOR_LOG_LEVEL);

/* DIAGNOSTIC: report the ot-uart binding + readiness so we can tell from the
 * console port whether the firmware is alive and which CDC instance ot-uart is.
 */
static const struct device *const ot_uart_diag =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_ot_uart));

#define WELCOME_TEXT                                                           \
	"\n\r"                                                                 \
	"\n\r"                                                                 \
	"=========================================================\n\r"        \
	"OpenThread Coprocessor application is now running on NCS.\n\r"        \
	"=========================================================\n\r"

int main(void)
{
	LOG_INF(WELCOME_TEXT);

	LOG_INF("ot-uart device: %s, ready=%d",
		ot_uart_diag->name, device_is_ready(ot_uart_diag));

#if defined(CONFIG_RCP_SAMPLE_HCI)
	run_hci();
#endif

	/* RCP mode: after openthread_sys_init() calls otPlatUartEnable() the
	 * Spinel NCP runs in the OpenThread workqueue/IRQ context. main() has
	 * nothing to do — sleep forever so the CDC stream carries only Spinel
	 * HDLC frames, with no heartbeat text that could clutter OTBR's
	 * HDLC decoder. */
	k_sleep(K_FOREVER);

	return 0;
}
