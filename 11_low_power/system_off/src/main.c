/*
 * XIAO nRF54LM20B Low Power Demo — System OFF (ultra-low-power)
 * ----------------------------------------------------------------------------
 * Ported from the Seeed XIAO nRF54LM20A "low_power" wiki page (mode 2).
 *
 * Behaviour:
 *   - on boot the reset cause is read and printed (first boot vs GPIO/low-power
 *     wake), so waking the board is visible on the serial console;
 *   - the RGB LED is blinked green once to confirm the mode, then released;
 *   - sw0 (BOOT, P0.09) is configured as a GPIO wake source;
 *   - the external flash (PY25Q128HA on &spi00) and its SPI bus are suspended,
 *     and the SPI pins are driven to defined low-leakage states;
 *   - the console is suspended and sys_poweroff() is called → System OFF.
 *
 * Wake by pressing BOOT (P0.09). System state is not retained, so wake equals
 * a fresh boot: main() runs again, prints the wake reset cause, blinks green,
 * and goes back to System OFF. Lab current target ~3-5 uA on battery.
 *
 * Board differences vs nRF54LM20A (accounted for here):
 *   - external flash is PY25Q128HA (128 Mbit), node label py25q128 (not py25q64);
 *     pins are identical: CS=gpio2.5 HOLD=gpio2.0 SCK=gpio2.1 MOSI=gpio2.2
 *     WP=gpio2.3 MISO=gpio2.4.
 *   - there is no dedicated &power_en node; only LDO1 (imu_vdd) has
 *     regulator-boot-on, deleted in the overlay.
 *
 * USB CDC-ACM console is auto-initialised by the board at boot; the app suspends
 * it before poweroff (and it re-initialises on the wake reboot).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>

/* Boot button sw0 (P0.09, active-low with pull-up) — GPIO wake source. */
static const struct gpio_dt_spec sw0 =
	GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static struct gpio_callback sw0_cb;

/* RGB LED is common-anode (active-low): green = gpio1.24. Blink once at boot. */
static const struct gpio_dt_spec green_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);
static const struct gpio_dt_spec red_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec blue_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);

/* External flash PY25Q128HA on &spi00 (board dtsi). Pin map verified:
 *   CS# gpio2.5  HOLD# gpio2.0  WP# gpio2.3  SCK gpio2.1  MOSI gpio2.2  MISO gpio2.4
 */
static const struct device *const flash_gpio =
	DEVICE_DT_GET(DT_NODELABEL(gpio2));

static void sw0_handler(const struct device *port, struct gpio_callback *cb,
			gpio_port_pins_t pins)
{
	/* Intentionally empty: the IRQ only has to wake the SoC from System OFF.
	 * On wake the chip reboots and main() runs from the top — no callback
	 * path is ever taken.
	 */
}

static void print_reset_cause(void)
{
	uint32_t cause = 0;
	int rc = hwinfo_get_reset_cause(&cause);

	if (rc != 0) {
		printk("Reset cause: read failed (err %d)\n", rc);
		return;
	}
	if (cause == 0) {
		printk("Reset cause: none (first boot / fresh power-on)\n");
		return;
	}

	printk("Reset cause: 0x%08x", cause);
	if (cause & RESET_PIN)            printk(" [PIN]");
	if (cause & RESET_LOW_POWER_WAKE) printk(" [LOW_POWER_WAKE]");
	if (cause & RESET_CLOCK)          printk(" [CLOCK]");
	if (cause & RESET_DEBUG)          printk(" [DEBUG]");
	if (cause & RESET_SOFTWARE)       printk(" [SOFTWARE]");
	if (cause & RESET_WATCHDOG)       printk(" [WATCHDOG]");
	if (cause & RESET_POR)            printk(" [POR]");
	if (cause & RESET_BROWNOUT)       printk(" [BROWNOUT]");
	if (cause & RESET_HARDWARE)       printk(" [HARDWARE]");
	printk("\n");
}

static void led_boot_pulse(void)
{
	if (!device_is_ready(green_led.port)) {
		printk("green LED not ready\n");
		return;
	}
	gpio_pin_configure_dt(&green_led, GPIO_OUTPUT);
	gpio_pin_set_dt(&green_led, 1);   /* lit */
	k_msleep(400);
	gpio_pin_set_dt(&green_led, 0);   /* off before release below */
}

static void configure_gpio_wakeup(void)
{
	if (!device_is_ready(sw0.port)) {
		printk("sw0 GPIO not ready\n");
		return;
	}
	gpio_pin_configure_dt(&sw0, GPIO_INPUT);
	gpio_init_callback(&sw0_cb, sw0_handler, BIT(sw0.pin));
	gpio_add_callback(sw0.port, &sw0_cb);
	/* Active-low button: a press holds the line low, so level-low triggers
	 * the DETECT signal that wakes the SoC from System OFF. */
	gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_LEVEL_LOW);
	printk("Wake source: sw0 (P0.09) configured\n");
}

static void release_led_gpios(void)
{
	/* Disconnect the RGB LED pins so they cannot leak through the LED. */
	if (device_is_ready(red_led.port))
		gpio_pin_configure(red_led.port, red_led.pin, GPIO_DISCONNECTED);
	if (device_is_ready(green_led.port))
		gpio_pin_configure(green_led.port, green_led.pin, GPIO_DISCONNECTED);
	if (device_is_ready(blue_led.port))
		gpio_pin_configure(blue_led.port, blue_led.pin, GPIO_DISCONNECTED);
}

static void configure_spi_pins_for_system_off(void)
{
	if (!device_is_ready(flash_gpio)) {
		printk("flash GPIO port not ready\n");
		return;
	}
	/* Drive the external-flash SPI pins to defined, low-leakage states so
	 * the PY25Q128HA can reach its deep power-down current. */
	gpio_pin_configure(flash_gpio, 5, GPIO_OUTPUT_HIGH); /* CS#   high: deselected */
	gpio_pin_configure(flash_gpio, 0, GPIO_OUTPUT_HIGH); /* HOLD# high: not held */
	gpio_pin_configure(flash_gpio, 3, GPIO_OUTPUT_HIGH); /* WP#   high: not protected */
	gpio_pin_configure(flash_gpio, 1, GPIO_OUTPUT_LOW);  /* SCK   low */
	gpio_pin_configure(flash_gpio, 2, GPIO_OUTPUT_LOW);  /* MOSI  low */
	gpio_pin_configure(flash_gpio, 4, GPIO_INPUT | GPIO_PULL_DOWN); /* MISO pd */
}

static void suspend_external_flash(void)
{
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(py25q128));
	const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(spi00));

	if (device_is_ready(flash)) {
		int rc = pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);
		printk("Suspend flash: %d\n", rc);
	}
	if (device_is_ready(spi)) {
		int rc = pm_device_action_run(spi, PM_DEVICE_ACTION_SUSPEND);
		printk("Suspend spi00: %d\n", rc);
	}
	configure_spi_pins_for_system_off();
}

static void suspend_console_best_effort(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (device_is_ready(console)) {
		int rc = pm_device_action_run(console, PM_DEVICE_ACTION_SUSPEND);
		printk("Suspend console: %d\n", rc);
	}
}

/* The XIAO nRF54LM20B console is USB CDC, so the usbhs controller + USB PHY
 * stay alive after suspending just the CDC function. Suspend the controller too
 * so the USB transceiver cannot leak while the SoC is in System OFF. */
static void suspend_usb_controller(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(usbhs))
	const struct device *usb = DEVICE_DT_GET(DT_NODELABEL(usbhs));

	if (device_is_ready(usb)) {
		int rc = pm_device_action_run(usb, PM_DEVICE_ACTION_SUSPEND);
		printk("Suspend usbhs: %d\n", rc);
	}
#endif
}

/* Turn OFF the two nPM1300 LDOs over the bit-bang I²C bus before poweroff.
 *
 * The board dtsi exposes only LDO1 (imu_vdd / dmic_vdd → IMU + PDM mic rail);
 * LDO2 (header_ldo → the empty "LDO2_3V3" header/test rail) is added in
 * app.overlay. Both LDOs power up in the nPM1300 OTP-enabled state, and deleting
 * regulator-boot-on only stops the driver from *forcing* them on — it does not
 * write the enable register. An explicit regulator_disable() is required to
 * actually deassert the LDO switch and shed the rail load (IMU deep power-down,
 * mic bias, LDO quiescent) in System OFF.
 *
 * BUCK1/BUCK2 (VSYS_3V3 → SoC VDD + flash) are NOT touched: they must stay up
 * to keep the SoC's always-on DETECT wake domain biased while off.
 */
static void disable_pmic_ldos(void)
{
	const struct device *ldo1 = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
	const struct device *ldo2 = DEVICE_DT_GET(DT_NODELABEL(header_ldo));

	if (device_is_ready(ldo1)) {
		int rc = regulator_disable(ldo1);
		printk("Disable LDO1 (imu/mic rail): %d\n", rc);
	} else {
		printk("LDO1 not ready, skipping\n");
	}
	if (device_is_ready(ldo2)) {
		int rc = regulator_disable(ldo2);
		printk("Disable LDO2 (header rail): %d\n", rc);
	} else {
		printk("LDO2 not ready, skipping\n");
	}
}

int main(void)
{
	printk("Starting Low Power Demo — System OFF\n");

	print_reset_cause();
	led_boot_pulse();

	configure_gpio_wakeup();
	release_led_gpios();
	suspend_external_flash();
	disable_pmic_ldos();

	/* Let the last printk bytes drain before we yank the console. */
	k_msleep(30);

	printk("Entering System OFF — press BOOT (P0.09) to wake\n");
	suspend_console_best_effort();
	suspend_usb_controller();

	(void)hwinfo_clear_reset_cause();
	sys_poweroff();

	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
