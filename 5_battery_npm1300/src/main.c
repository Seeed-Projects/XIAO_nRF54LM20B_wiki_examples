/*
 * XIAO nRF54LM20B battery level detection demo
 * ----------------------------------------------------------------------------
 *  Bring up the nPM1300 PMIC I2C interface (board-level pmic_i2c is a
 *  gpio-i2c bit-banged bus on P1.18/P1.17, 100kHz; PMIC device address 0x6b),
 *  and read the Li-ion battery voltage and charge level:
 *
 *  - The current battery voltage and charge percentage are printed to the
 *    serial console in a loop, e.g.:
 *        Battery: 85%  (Vbat 4.12V)
 *    Once the PC opens the serial port (DTR asserted), output occurs once
 *    per second.
 *
 *  Implementation notes:
 *  - The nPM1300 has no coulomb counter / fuel gauge. The Zephyr
 *    npm13xx_charger sensor driver only exposes voltage
 *    (SENSOR_CHAN_GAUGE_VOLTAGE), current, and temperature channels; it does
 *    not provide a SoC percentage. The charge percentage here is therefore
 *    derived from the measured battery voltage via a piecewise-linear
 *    "4.2V Li-ion OCV -> SoC" table (a voltage-estimation method that reads
 *    low under load; for demonstration only).
 *  - Each loop iteration calls sensor_sample_fetch(), which triggers a VBAT
 *    ADC task inside the driver and reads back the result, so the value
 *    obtained is the instantaneous terminal voltage.
 *  - The USB CDC-ACM console is auto-initialized at boot by the board
 *    (CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y); zephyr,console = cdc_acm_uart is
 *    ready before main() runs.
 *
 *  Board-level devicetree (in the framework package):
 *    - pmic_i2c: gpio-i2c (P1.18 SDA / P1.17 SCL, 100kHz)
 *    - pmic: npm1300@6b (compatible nordic,npm1300)
 *    - pmic_charger: charger (compatible nordic,npm1300-charger,
 *      term-microvolt=4.2V, charging-enable -- 4.2V full-charge Li-ion cell)
 *    - led0 = blue LED (common-anode RGB, GPIO_ACTIVE_LOW)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

/* ============================ Board resource bindings ============================ */

/* User LED: alias led0 = blue LED (GPIO_ACTIVE_LOW, active low) -- used as heartbeat */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, over USBHS, ready at boot) */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* nPM1300 charger/battery sensor: node pmic_charger (compatible nordic,npm1300-charger).
 * The driver CONFIG_NPM13XX_CHARGER is enabled by default when this DT node exists. */
static const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));

/* ============================ Voltage -> SoC percentage ============================ */
/*
 * Piecewise-linear "open-circuit voltage (OCV) -> SoC" table for a 4.2V
 * Li-ion cell (mV, %). The nPM1300 has no coulomb counter, so voltage
 * estimation is the only option; terminal voltage sags under load, so the
 * estimate is conservative. Table values follow a typical 4.2V Li-ion
 * discharge curve; for demonstration, not precision metering.
 */
static const struct {
	int32_t mv;
	uint8_t soc;
} ocv_table[] = {
	{ 3000,  0  }, { 3200,  1  }, { 3400,  5  }, { 3500, 10  },
	{ 3550, 15  }, { 3600, 20  }, { 3650, 30  }, { 3700, 40  },
	{ 3750, 50  }, { 3800, 58  }, { 3850, 65  }, { 3900, 72  },
	{ 3950, 78  }, { 4000, 84  }, { 4050, 89  }, { 4100, 94  },
	{ 4150, 97  }, { 4200, 100 }, { 4400, 100 },
};

static uint8_t voltage_to_soc(int32_t mv)
{
	int n = ARRAY_SIZE(ocv_table);

	if (mv <= ocv_table[0].mv) {
		return 0;
	}
	if (mv >= ocv_table[n - 1].mv) {
		return 100;
	}

	for (int i = 1; i < n; i++) {
		if (mv <= ocv_table[i].mv) {
			/* Linear interpolation between two points */
			int32_t mv0 = ocv_table[i - 1].mv;
			int32_t mv1 = ocv_table[i].mv;
			int32_t s0 = ocv_table[i - 1].soc;
			int32_t s1 = ocv_table[i].soc;
			return (uint8_t)(s0 + (s1 - s0) * (mv - mv0) / (mv1 - mv0));
		}
	}
	return 100;
}

/* ============================ Main flow ============================ */

int main(void)
{
	bool led_on = false;
	uint32_t dtr = 0U;

	/* 1) Blue LED: configure as output, initially off */
	if (!gpio_is_ready_dt(&led)) {
		printk("LED not ready\r\n");
		return 0;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	/* 2) Battery charger sensor ready check (depends on nPM1300 mfd + gpio-i2c bus) */
	if (!device_is_ready(charger)) {
		printk("pmic_charger not ready (nPM1300 I2C not brought up?)\r\n");
		return 0;
	}

	printk("Battery demo ready. Open this port to start reporting.\r\n");

	/*
	 * Main loop: every 1 second
	 *  - Toggle the blue LED (~0.5Hz heartbeat, indicates running)
	 *  - After the PC opens the serial port (DTR asserted): trigger the VBAT
	 *    ADC, read the voltage, convert to SoC and print
	 */
	while (1) {
		led_on = !led_on;
		gpio_pin_set_dt(&led, led_on ? 1 : 0);

		if (cons != NULL && device_is_ready(cons) &&
		    uart_line_ctrl_get(cons, UART_LINE_CTRL_DTR, &dtr) == 0 &&
		    dtr != 0U) {
			struct sensor_value val;
			int ret;

			/* Trigger one VBAT/current/temperature ADC measurement */
			ret = sensor_sample_fetch(charger);
			if (ret) {
				printk("charger: sample_fetch failed (%d)\r\n", ret);
				k_sleep(K_SECONDS(1));
				continue;
			}

			ret = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE,
						 &val);
			if (ret) {
				printk("charger: voltage get failed (%d)\r\n", ret);
				k_sleep(K_SECONDS(1));
				continue;
			}

			/* val is in volts (val1 whole volts, val2 microvolts); convert to millivolts as integer */
			int32_t mv = val.val1 * 1000 + val.val2 / 1000;
			uint8_t soc = voltage_to_soc(mv);

			printk("Battery: %u%%  (Vbat %d.%02dV)\r\n",
			       soc, mv / 1000, (mv % 1000) / 10);
		}

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
