/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO nRF54LM20B BLE Observer Demo (ported from NCS SDK samples/bluetooth/observer)
 * ----------------------------------------------------------------------------
 *  Replicates the XIAO nRF54L15 Bluetooth usage: starts BLE scanning and passively
 *  listens to surrounding advertising packets, printing the discovered device
 *  address, RSSI, advertising type and AD data length on the USB CDC serial port:
 *        Device found: xx:xx:xx:xx:xx:xx (RSSI -xx), type N, AD data len N
 *
 *  Implementation: bt_enable(NULL) initializes the Bluetooth subsystem, then
 *  observer_start() registers scan callbacks and calls bt_le_scan_start (active
 *  scan with duplicate filtering). After main returns, scanning is continuously
 *  driven by the BT subsystem thread; callbacks print from the BT RX context.
 *
 *  The Bluetooth stack is single-core: host + Zephyr software controller
 *  (BT_LL_SW_SPLIT); the controller HAL drives the nRF54LM20B RADIO peripheral
 *  via radio_nrf54lx.
 *
 *  The USB CDC-ACM console is auto-initialized at boot by the board
 *  (CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y), so zephyr,console = cdc_acm_uart is
 *  ready before main(); the application does not need to create a usbd context.
 */

#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>

int observer_start(void);

int main(void)
{
	int err;

	printk("Starting Observer Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	(void)observer_start();

	printk("Exiting %s thread.\n", __func__);
	return 0;
}
