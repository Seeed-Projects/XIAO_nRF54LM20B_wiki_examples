/*
 * XIAO nRF54LM20B -- Pin Multiplexing demo 1: Digital GPIO
 * ----------------------------------------------------------------------------
 * Port of the official XIAO nRF54LM20A wiki "数字引脚" demo to the 20B:
 *   Grove Button (P1.0 / D0)  -> input,  active-high (onboard pull-down)
 *   Grove Buzzer (P1.31 / D1) -> output, HIGH = on
 *   Button pressed  => Buzzer ON
 *   Button released => Buzzer OFF
 *
 * NOTE: requires an ACTIVE buzzer (built-in oscillator). A passive piezo
 *       buzzer must be driven with PWM -- use demo 2 (pwm_servo) instead.
 *
 * 20B adaptations vs the 20A original:
 *   - main() self-confirms the mcuboot image at startup (repo convention);
 *   - \r\n line endings (printk over the board's default USB CDC console);
 *   - no overlay needed: gpio1 is a SoC-level node and P1.0/P1.31 are both
 *     broken out on the 20B connector (D0/D1), so no pinctrl is required
 *     for plain GPIO (GPIO does not use the pinctrl subsystem on nRF54L).
 *
 * The wiki original pointed the console at &uart20 (physical pins
 * P1.11/P1.10); that is dropped here so output stays on USB CDC, matching
 * every other project in this repo (cu.usbmodem*).
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#digital
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

#define BUTTON_PIN 0   /* P1.0  - Grove Button  - D0 */
#define BUZZER_PIN 31  /* P1.31 - Grove Buzzer - D1 */

int main(void)
{
	(void)boot_write_img_confirmed();

	const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

	printk("\r\n=== Grove Button + Buzzer (Digital Mode) ===\r\n");

	if (!device_is_ready(gpio1_dev)) {
		printk("ERROR: gpio1 device is not ready!\r\n");
		return -1;
	}
	printk("gpio1 device ready: %s\r\n", gpio1_dev->name);

	/* Grove Button has an onboard pull-down; HIGH = pressed */
	gpio_pin_configure(gpio1_dev, BUTTON_PIN, GPIO_INPUT);
	/* Buzzer: HIGH = on (start off) */
	gpio_pin_configure(gpio1_dev, BUZZER_PIN, GPIO_OUTPUT_INACTIVE);

	printk("Button:  P1.%d (input)\r\n", BUTTON_PIN);
	printk("Buzzer:  P1.%d (output)\r\n", BUZZER_PIN);
	printk("Press the button to turn on the buzzer...\r\n");

	int last_val = 0;

	while (1) {
		int val = gpio_pin_get(gpio1_dev, BUTTON_PIN);

		if (val >= 0) {
			gpio_pin_set(gpio1_dev, BUZZER_PIN, val);

			/* Only print on state change to avoid log spam */
			if (val != last_val) {
				printk("Button %s -> Buzzer %s\r\n",
				       val ? "PRESSED " : "released",
				       val ? "ON" : "OFF");
				last_val = val;
			}
		}

		k_msleep(10);
	}

	return 0;
}
