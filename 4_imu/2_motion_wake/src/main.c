/*
 * XIAO nRF54LM20B IMU motion wake Demo
 * ----------------------------------------------------------------------------
 *  Bring up the 6-axis IMU (LSM6DS3TR-C) driver and configure the IMU interrupt
 *  pin (INT1 = P0.06) as a Wakeup Source for the main core:
 *
 *  - On power-up the user indicator LED (alias led0 = blue) lights up, then
 *    turns off and the board enters an ultra-low-power sleep state (the main
 *    thread blocks on a semaphore while the idle thread executes WFI /
 *    System ON idle, woken by GPIO DETECT).
 *  - When a shake of the XIAO is detected, the IMU's wake-up (motion) detection
 *    asserts INT1 high, waking nRF54LM20B via a rising edge on P0.06.
 *  - After waking, one shake is recorded, "Hello XIAO nRF54LM20B" is printed to
 *    the serial port, the latched interrupt source in the IMU is cleared
 *    (WAKE_UP_SRC is read so INT1 goes low and can trigger again), and the
 *    board re-enters sleep.
 *
 *  Implementation notes:
 *  - The lsm6dsl Zephyr driver only supports SENSOR_TRIG_DATA_READY, not
 *    wake-up triggers, so shake detection does not use the sensor trigger API.
 *    Instead, main() uses bare I2C to directly configure the wake-up function
 *    registers of the LSM6DS3TR-C and routes the interrupt to INT1 (= P0.06).
 *  - CONFIG_LSM6DSL_TRIGGER is off (not enabled in prj.conf), so the driver
 *    does not claim irq-gpios; P0.06 is configured by the application itself as
 *    a GPIO rising-edge interrupt. nRF GPIO automatically enables SENSE when
 *    an interrupt is enabled, and the DETECT signal can wake the SoC from
 *    WFI / System ON idle -- this is the "IMU interrupt pin as main-core
 *    Wakeup Source" mechanism.
 *  - The USB CDC-ACM console is auto-initialized at boot by the board
 *    (CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y); zephyr,console = cdc_acm_uart is
 *    ready before main(). USB stays enumerated during sleep (idle does not
 *    disable USBHS), so printk can be used immediately after waking.
 *
 * Board devicetree (in the framework package):
 *   - led0/blue_led: common-anode RGB, GPIO_ACTIVE_LOW
 *   - lsm6ds3tr_c@6a (i2c30, compatible st,lsm6dsl), irq-gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>
 *   - imu_vdd = PMIC nPM1300 LDO1 (app.overlay adds regulator-boot-on, powered at boot)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

/* ============================ Board resource bindings ============================ */

/* User indicator LED: alias led0 = blue LED (GPIO_ACTIVE_LOW, active low) */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, over USBHS, ready at boot) */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* IMU: LSM6DS3TR-C, alias imu0 / node lsm6ds3tr_c.
 * Only present on the Sense variant; if absent it is not a build error, and
 * device_is_ready() is checked at runtime. */
#if DT_NODE_EXISTS(DT_NODELABEL(lsm6ds3tr_c))
static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(imu0));
#else
static const struct device *const imu = NULL;
#endif

/* I2C bus the IMU sits on (i2c30, nordic,nrf-twim) and device address 0x6a --
 * used for bare I2C access to the wake-up registers. DT_BUS() returns the
 * parent bus; DT_REG_ADDR() returns reg = 0x6a. */
#if DT_NODE_EXISTS(DT_NODELABEL(lsm6ds3tr_c))
static const struct device *const i2c = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(lsm6ds3tr_c)));
#define IMU_I2C_ADDR DT_REG_ADDR(DT_NODELABEL(lsm6ds3tr_c))
#else
static const struct device *const i2c = NULL;
#define IMU_I2C_ADDR 0x6a
#endif

/* IMU interrupt pin: irq-gpios = <&gpio0 6 GPIO_ACTIVE_HIGH> = P0.06, rising edge.
 * When the IMU detects motion it drives INT1 high -> P0.06 rising edge -> wakes SoC. */
#if DT_NODE_EXISTS(DT_NODELABEL(lsm6ds3tr_c))
static const struct gpio_dt_spec imu_irq = GPIO_DT_SPEC_GET(DT_NODELABEL(lsm6ds3tr_c),
							   irq_gpios);
#else
static const struct gpio_dt_spec imu_irq;
#endif

/* IMU supply: PMIC nPM1300 LDO1 (imu_vdd). Degrades gracefully if absent. */
#if DT_NODE_EXISTS(DT_NODELABEL(imu_vdd))
static const struct device *const imu_vdd = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#else
static const struct device *const imu_vdd = NULL;
#endif

/* ============================ LSM6DS3TR-C registers (raw addresses) ============================ */
/* The driver-private header lsm6dsl.h is not on the application include path,
 * so register addresses are self-contained here. All addresses and bit
 * definitions match the ST LSM6DS3TR-C / LSM6DSL datasheet. */

#define REG_CTRL3_C		0x12U  /* BDU / IF_INC / BOOT and other system control */
#define REG_CTRL1_XL		0x10U  /* Accelerometer ODR[7:4] + FS[3:2] */
#define REG_CTRL2_G		0x11U  /* Gyroscope ODR/FS (0 = power-down) */
#define REG_TAP_CFG		0x58U  /* Master interrupt enable / axis enable / latching */
#define REG_WAKE_UP_THS		0x5BU  /* wake-up threshold WK_THS[5:0] */
#define REG_WAKE_UP_DUR		0x5CU  /* wake-up duration WAKE_DUR[6:5] */
#define REG_MD1_CFG		0x5EU  /* Interrupt selection routed to INT1 */
#define REG_WAKE_UP_SRC		0x1BU  /* wake-up interrupt source (read clears latch, drives INT1 low) */

/* TAP_CFG bits */
#define TAP_CFG_IE		BIT7    /* INTERRUPTS_ENABLE: master enable for embedded-function interrupts */
#define TAP_CFG_XYZ_EN		(BIT3 | BIT2 | BIT1)  /* X/Y/Z wake-up enable */
#define TAP_CFG_LIR		BIT0    /* Latched interrupt (INT1 stays high until SRC is read) */
#define TAP_CFG_VAL		(TAP_CFG_IE | TAP_CFG_XYZ_EN | TAP_CFG_LIR) /* = 0x8F */

/* CTRL1_XL: ODR_XL=52Hz(0x03<<4) | FS_XL=+-4g(0x02<<2) = 0x30 | 0x08 = 0x38 */
#define CTRL1_XL_VAL		0x38U

/* WAKE_UP_THS: WK_THS=8 -> at +-4g 1 LSB = 62.5mg, threshold ~= 500mg (a shake far exceeds this) */
#define WK_THS_VAL		0x08U

/* MD1_CFG: INT1_WU(bit5) = route wake-up interrupt to INT1 */
#define MD1_CFG_INT1_WU		BIT5
#define MD1_CFG_VAL		MD1_CFG_INT1_WU /* = 0x20 */

/* CTRL3_C: BDU(bit6)=1 + IF_INC(bit2)=1 = 0x44 */
#define CTRL3_C_VAL		0x44U

/* Helper macro: BITn (some toolchains' <zephyr/sys/util.h> already define BIT; avoid redefinition here) */
#ifndef BIT0
#define BIT0 0x01U
#define BIT1 0x02U
#define BIT2 0x04U
#define BIT3 0x08U
#define BIT4 0x10U
#define BIT5 0x20U
#define BIT6 0x40U
#define BIT7 0x80U
#endif

/* ============================ Bare I2C register read/write ============================ */

static int imu_reg_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };

	if (i2c == NULL || !device_is_ready(i2c)) {
		return -ENODEV;
	}
	return i2c_write(i2c, buf, sizeof(buf), IMU_I2C_ADDR);
}

static int imu_reg_read(uint8_t reg, uint8_t *val)
{
	if (i2c == NULL || !device_is_ready(i2c)) {
		return -ENODEV;
	}
	return i2c_write_read(i2c, IMU_I2C_ADDR, &reg, 1, val, 1);
}

/* ============================ IMU wake-up configuration ============================ */
/*
 * Configure the LSM6DS3TR-C wake-up (motion/shake) detection:
 *  - Accelerometer +-4g @ 52Hz; gyroscope powered down to save energy
 *  - Enable X/Y/Z wake-up, latched interrupt
 *  - Threshold ~500mg, triggers after 1 sample (most sensitive, easy to fire on a shake)
 *  - Route wake-up interrupt to INT1 (= P0.06)
 * After configuration, read WAKE_UP_SRC once to clear any latch from boot
 * (avoids a false trigger and drives INT1 low).
 */
static int imu_wake_config(void)
{
	int rc;

	/* 0) Power rail LDO1 on (regulator-boot-on in app.overlay already enabled
	 *    it at boot; this idempotent call increments refcnt harmlessly). */
	if (imu_vdd != NULL && device_is_ready(imu_vdd)) {
		(void)regulator_enable(imu_vdd);
	}

	/* 1) Rely on the SW_RESET / WHO_AM_I validation already performed by the
	 *    driver init stage to confirm the device is online. If the driver is
	 *    not bound (imu == NULL) it doesn't matter -- we go straight to bare I2C. */
	(void)imu;

	if (i2c == NULL || !device_is_ready(i2c)) {
		printk("IMU: i2c bus not ready\r\n");
		return -ENODEV;
	}

	/* 2) System control: BDU + IF_INC (single-byte writes don't depend on
	 *    IF_INC, but keep the standard value) */
	rc = imu_reg_write(REG_CTRL3_C, CTRL3_C_VAL);
	if (rc) {
		printk("IMU: write CTRL3_C failed (%d)\r\n", rc);
		return rc;
	}

	/* 3) Accelerometer: ODR 52Hz / +-4g -- the wake-up engine requires the accelerometer on */
	rc = imu_reg_write(REG_CTRL1_XL, CTRL1_XL_VAL);
	if (rc) {
		printk("IMU: write CTRL1_XL failed (%d)\r\n", rc);
		return rc;
	}

	/* 4) Gyroscope powered down to save energy */
	rc = imu_reg_write(REG_CTRL2_G, 0x00U);
	if (rc) {
		printk("IMU: write CTRL2_G failed (%d)\r\n", rc);
		return rc;
	}

	/* 5) Master interrupt enable + XYZ wake-up + latching */
	rc = imu_reg_write(REG_TAP_CFG, TAP_CFG_VAL);
	if (rc) {
		printk("IMU: write TAP_CFG failed (%d)\r\n", rc);
		return rc;
	}

	/* 6) wake-up threshold */
	rc = imu_reg_write(REG_WAKE_UP_THS, WK_THS_VAL);
	if (rc) {
		printk("IMU: write WAKE_UP_THS failed (%d)\r\n", rc);
		return rc;
	}

	/* 7) wake-up duration = 0 (triggers after 1 sample); not used for sleep-mode detection */
	rc = imu_reg_write(REG_WAKE_UP_DUR, 0x00U);
	if (rc) {
		printk("IMU: write WAKE_UP_DUR failed (%d)\r\n", rc);
		return rc;
	}

	/* 8) Route wake-up interrupt to INT1 (P0.06) */
	rc = imu_reg_write(REG_MD1_CFG, MD1_CFG_VAL);
	if (rc) {
		printk("IMU: write MD1_CFG failed (%d)\r\n", rc);
		return rc;
	}

	/* 9) Wait for the HP/slope filter to settle, avoiding a boot transient being mistaken for motion */
	k_msleep(50);

	/* 10) Clear any latched interrupt source so INT1 goes low, readying for the next (rising-edge) trigger */
	uint8_t src = 0;
	(void)imu_reg_read(REG_WAKE_UP_SRC, &src);
	printk("IMU wake-up armed (WAKE_UP_SRC=0x%02x, INT1=P0.06)\r\n", src);

	return 0;
}

/* ============================ Wake source (GPIO interrupt) ============================ */

static struct k_sem wake_sem;
static struct gpio_callback irq_cb;

/* P0.06 rising-edge ISR: the IMU detected motion -> give the semaphore to
 * wake the main thread. No I2C operations in the ISR (the bus clock may be
 * gated in sleep); latch clearing is deferred to the main thread. */
static void imu_irq_handler(const struct device *port, struct gpio_callback *cb,
			    gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&wake_sem);
}

static int imu_irq_setup(void)
{
	int rc;

	if (!gpio_is_ready_dt(&imu_irq)) {
		printk("IMU: irq gpio(P0.06) not ready\r\n");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&imu_irq, GPIO_INPUT);
	if (rc) {
		printk("IMU: irq pin config failed (%d)\r\n", rc);
		return rc;
	}

	gpio_init_callback(&irq_cb, imu_irq_handler, BIT(imu_irq.pin));
	rc = gpio_add_callback(imu_irq.port, &irq_cb);
	if (rc) {
		printk("IMU: gpio_add_callback failed (%d)\r\n", rc);
		return rc;
	}

	/* Rising-edge interrupt: nRF GPIO automatically enables SENSE when an
	 * interrupt is enabled, and DETECT can wake the SoC from WFI / System ON
	 * idle -- i.e. P0.06 acts as the main-core Wakeup Source. */
	rc = gpio_pin_interrupt_configure_dt(&imu_irq, GPIO_INT_EDGE_RISING);
	if (rc) {
		printk("IMU: irq interrupt config failed (%d)\r\n", rc);
		return rc;
	}

	return 0;
}

/* ============================ Main flow ============================ */

int main(void)
{
	unsigned int shake_count = 0U;
	int rc;

	/* 1) Blue LED: configure as output; light on power-up */
	if (!gpio_is_ready_dt(&led)) {
		printk("LED not ready\r\n");
		return 0;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&led, 1);   /* on */

	/* 2) Configure IMU wake-up (before turning the LED off, so LED state can be observed on failure) */
	k_sem_init(&wake_sem, 0, 1);
	rc = imu_wake_config();

	/* 3) Configure P0.06 as a wake source (GPIO rising-edge interrupt) */
	if (rc == 0) {
		(void)imu_irq_setup();
	}

	/* 4) Turn the indicator LED off and enter sleep state */
	k_msleep(200);
	gpio_pin_set_dt(&led, 0);    /* off */
	printk("Sleeping... shake to wake.\r\n");

	/*
	 * Main loop: block on the semaphore -> idle executes WFI (ultra-low-power
	 * sleep state). Woken by a rising edge on P0.06: clear the IMU latch
	 * (read WAKE_UP_SRC so INT1 goes low and can trigger again) -> record one
	 * shake -> blink the blue LED as feedback -> print to serial -> re-sleep.
	 */
	while (1) {
		k_sem_take(&wake_sem, K_FOREVER);

		/* Clear the latched interrupt source: read WAKE_UP_SRC so INT1 goes
		 * low, allowing the next shake to produce another rising edge */
		uint8_t src = 0;
		(void)imu_reg_read(REG_WAKE_UP_SRC, &src);

		shake_count++;

		/* Briefly light the blue LED as wake feedback, then turn off and resume sleep */
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);

		if (cons != NULL && device_is_ready(cons)) {
			printk("Hello XIAO nRF54LM20B  [shake #%u, src=0x%02x]\r\n",
			       shake_count, src);
		}
	}

	return 0;
}
