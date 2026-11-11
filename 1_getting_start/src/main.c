/*
 * XIAO nRF54LM20A getting-started
 * ----------------------------------------------------------------------------
 *  1. On power-up the user RGB LED (common anode, red/green/blue channels)
 *     blinks in rotation: red 500ms -> off 500ms -> green 500ms -> off 500ms
 *     -> blue 500ms -> off 500ms, repeating.
 *  2. Once attached to a PC, the USBHS (CDC-ACM) serial port prints once:
 *     "Hello, XIAO nRF54LM20A".
 *  3. Sense variant: the same output is followed by current raw IMU
 *     (LSM6DS3TR-C) readings.
 *
 * The USB CDC-ACM console is added by the `cdc-acm-console` snippet (-S on the
 * west build line): it appends a cdc_acm_uart node under &usbhs and repoints
 * zephyr,console at it, and enables CDC_ACM_SERIAL_INITIALIZE_AT_BOOT so the
 * USB device and CDC ACM UART are up before main() runs. The application
 * therefore does not need to - and must not - build a second usbd context
 * manually (that would conflict with the auto backend and break enumeration).
 *
 * Board-level devicetree (in the framework package):
 *   - red_led/green_led/blue_led: common-anode RGB, GPIO_ACTIVE_LOW
 *     (led0 alias = blue_led)
 *   - zephyr_udc0 = &usbhs (vregusb enabled); cdc_acm_uart added by the snippet
 *   - imu0 = lsm6ds3tr_c@6a (i2c30), imu_vdd = PMIC nPM1300 LDO1
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/printk.h>

/* ============================ Board resource bindings ============================ */

/* User RGB LED: board-level red_led/blue_led/green_led (common anode,
 * GPIO_ACTIVE_LOW - driven low to light up). The led0 alias is blue_led;
 * use the three nodelabels directly to get all three channels. */
static const struct gpio_dt_spec rgb[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios),
};

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, on USBHS, ready at boot) */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* IMU: LSM6DS3TR-C, alias imu0 (present on the Sense variant only; a missing
 * node is not a build error - checked at runtime with device_is_ready()). */
#if DT_NODE_EXISTS(DT_ALIAS(imu0))
static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(imu0));
#else
static const struct device *const imu = NULL;
#endif

/* IMU power rail: LDO1 of the PMIC nPM1300 (imu_vdd). Same graceful fallback
 * if the node does not exist. */
#if DT_NODE_EXISTS(DT_NODELABEL(imu_vdd))
static const struct device *const imu_vdd = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#else
static const struct device *const imu_vdd = NULL;
#endif

/* ============================ IMU helpers ============================ */

static void imu_init(void)
{
	/*
	 * Power up the IMU supply rail LDO1 first. regulator-boot-on already
	 * enabled it once during kernel init; the regulator_enable() here is
	 * just an idempotent safety net (bumps the refcount, harmless).
	 * Done before the device_is_ready(imu) check, which is the logical order.
	 */
	if (imu_vdd != NULL && device_is_ready(imu_vdd)) {
		(void)regulator_enable(imu_vdd);
	}

	if (imu == NULL || !device_is_ready(imu)) {
		return;
	}

	/* After lsm6dsl init the ODR may still be power-down; set 52 Hz so
	 * valid data is produced. */
	struct sensor_value odr = { .val1 = 52, .val2 = 0 };
	(void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	(void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
}

/* Print the raw IMU readings once (accel m/s^2, gyro rad/s; the val1.val2
 * pair of each sensor_value). */
static void imu_print(void)
{
	if (imu == NULL || !device_is_ready(imu)) {
		printk("IMU: not available\r\n");
		return;
	}

	struct sensor_value acc[3];
	struct sensor_value gyr[3];
	int ret;

	ret = sensor_sample_fetch(imu);
	if (ret) {
		printk("IMU: sample_fetch failed (%d)\r\n", ret);
		return;
	}

	ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, acc);
	if (ret) {
		printk("IMU: accel channel_get failed (%d)\r\n", ret);
		return;
	}
	ret = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyr);
	if (ret) {
		printk("IMU: gyro channel_get failed (%d)\r\n", ret);
		return;
	}

	printk("IMU accel[m/s^2] ax=%d.%06d ay=%d.%06d az=%d.%06d\r\n",
	       acc[0].val1, acc[0].val2, acc[1].val1, acc[1].val2,
	       acc[2].val1, acc[2].val2);
	printk("IMU gyro[rad/s]  gx=%d.%06d gy=%d.%06d gz=%d.%06d\r\n",
	       gyr[0].val1, gyr[0].val2, gyr[1].val1, gyr[1].val2,
	       gyr[2].val1, gyr[2].val2);
}

/* ============================ Main flow ============================ */

int main(void)
{
	int color = 0;
	bool led_on = false;
	bool greeted = false;

	/* 1) RGB LED: configure all three channels as outputs, initially off
	 *    (with GPIO_ACTIVE_LOW, inactive = high level). */
	for (size_t i = 0; i < ARRAY_SIZE(rgb); i++) {
		if (!gpio_is_ready_dt(&rgb[i])) {
			return 0;
		}
		gpio_pin_configure_dt(&rgb[i], GPIO_OUTPUT_INACTIVE);
	}

	/* 2) IMU (Sense variant): power up + set sampling frequency */
	imu_init();

	/* 3) USB CDC console is already initialized by the board; nothing to do. */

	/*
	 * Main loop: toggle the LED every 500 ms => 1 Hz blinking, and switch
	 * to the next color on every rising (off -> on) edge (red -> green ->
	 * blue rotation); poll DTR at the same rate and print the Hello line
	 * (+IMU) only once, after the PC opens the serial port.
	 */
	while (1) {
		led_on = !led_on;
		if (led_on) {
			/* Advance to the next color on each off -> on edge */
			color = (color + 1) % (int)ARRAY_SIZE(rgb);
		}
		for (size_t i = 0; i < ARRAY_SIZE(rgb); i++) {
			gpio_pin_set_dt(&rgb[i], (led_on && (int)i == color) ? 1 : 0);
		}

		if (!greeted && cons != NULL && device_is_ready(cons)) {
			uint32_t dtr = 0U;

			if (uart_line_ctrl_get(cons, UART_LINE_CTRL_DTR, &dtr) == 0
			    && dtr != 0U) {
				/* Let the host finish port setup before printing */
				k_msleep(50);
				printk("Hello, XIAO nRF54LM20A\r\n");
				imu_print();
				greeted = true;
			}
		}

		k_sleep(K_MSEC(500));
	}

	return 0;
}
