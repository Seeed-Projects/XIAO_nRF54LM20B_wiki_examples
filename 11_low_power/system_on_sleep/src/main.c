/*
 * XIAO nRF54LM20B Low Power Demo — System ON Sleep
 * ----------------------------------------------------------------------------
 * Ported from the Seeed XIAO nRF54LM20A "low_power" wiki page (mode 1).
 *
 * Behaviour:
 *   - the BLE controller advertises non-connectably as "XIAO_LP_ON" once per
 *     second (1 s interval);
 *   - the application main loop calls k_sleep(): the CPU clock is gated, RAM and
 *     peripheral state are retained, while the BLE controller keeps advertising
 *     autonomously (System ON sleep);
 *   - LDO1 (IMU + PDM microphone rail) is released via the overlay to cut the
 *     standby load of those unused analog parts;
 *   - the RGB LED is blinked once at boot to confirm the mode visually, then
 *     turned off so it draws nothing while the CPU sleeps.
 *
 * Verify with a phone: nRF Connect scans and sees "XIAO_LP_ON" advertising at
 * ~1 Hz while the board draws only the controller + regulator leakage current.
 *
 * Single-core BLE stack: host + Zephyr software controller (BT_LL_SW_SPLIT)
 * driving the nRF54LM20B RADIO via radio_nrf54lx (controller enabled through
 * the app.overlay bt_hci_controller node, see prj.conf notes). USB CDC-ACM
 * console is auto-initialised by the board, so printk lands on cu.usbmodem*
 * with no extra usbd setup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/gpio.h>

#define DEVICE_NAME      CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN  (sizeof(DEVICE_NAME) - 1)

/* Advertising interval: 1 s = 1000 ms / 0.625 ms = 1600 (0x0640). */
#define ADV_INTERVAL_1S  0x0640

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* RGB LED is common-anode (active-low): blue = gpio1.23. Blink once at boot,
 * then turn off so it draws nothing during sleep. We deliberately do NOT use
 * the shared led.c blink thread here — a ready background thread would keep
 * the CPU from entering System ON sleep and would leave an LED lit.
 */
static const struct gpio_dt_spec blue_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);

static void led_boot_pulse(void)
{
	if (!device_is_ready(blue_led.port)) {
		printk("blue LED not ready\n");
		return;
	}
	gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT);
	gpio_pin_set_dt(&blue_led, 1);   /* lit (active-low: 1 = on) */
	k_msleep(500);
	gpio_pin_set_dt(&blue_led, 0);   /* off — draw nothing during sleep */
}

/* Non-connectable advertiser at a fixed 1 s interval. */
static const struct bt_le_adv_param adv_param =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_NONE, ADV_INTERVAL_1S,
			     ADV_INTERVAL_1S, NULL);

static void bt_ready(int err)
{
	char addr_s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t addr = {0};
	size_t count = 1;

	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	bt_id_get(&addr, &count);
	bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
	printk("Advertising as %s (non-connectable, 1 Hz, name \"%s\")\n",
	       addr_s, DEVICE_NAME);
	printk("Entering System ON sleep; BLE controller advertises autonomously\n");
}

int main(void)
{
	int err;

	printk("Starting Low Power Demo — System ON Sleep\n");

	led_boot_pulse();

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	/* The CPU sleeps here every iteration; the BLE controller keeps
	 * advertising on its own. No heartbeat print — output would wake the
	 * CPU and defeat the demo.
	 */
	while (1) {
		k_sleep(K_SECONDS(10));
	}
	return 0;
}
