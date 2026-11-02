/*
 * XIAO nRF54LM20B -- Pin Multiplexing demo 2: PWM (servo on P1.0)
 * ----------------------------------------------------------------------------
 * Port of the wiki "PWM" demo. A standard hobby servo is driven from PWM20
 * channel 0 on P1.0 / D0 at 50 Hz (20 ms period, 0.5-2.5 ms pulse -> 0-180 deg).
 * The angle sweeps 0->180 deg then back 180->0 deg, printed to the console.
 *
 * 20B adaptations vs the 20A original:
 *   - self-confirm the mcuboot image at startup;
 *   - \r\n line endings over the default USB CDC console;
 *   - console NOT moved to &uart20 -- output stays on USB CDC (cu.usbmodem*),
 *     so the wiki "chosen { zephyr,console = &uart20 }" and the whole uart20
 *     pinctrl block are dropped (this demo does not use uart20 at all);
 *   - the overlay only re-maps PWM20 channel 0 from its board default
 *     (P1.22, the red RGB LED) to P1.0 / D0 for the servo.
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#pwm
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

/* Standard servo: 50 Hz period, 0.5-2.5 ms pulse for 0-180 deg */
#define SERVO_PERIOD_NS  PWM_MSEC(20)
#define SERVO_MIN_NS      PWM_USEC(500)
#define SERVO_MAX_NS      PWM_USEC(2500)
#define SERVO_CHANNEL     0

/* 30 ms/step -> ~6 deg/step; full sweep takes ~5.4 s each way */
#define STEP_FWD_MS 30
#define STEP_RET_MS 30

static const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm20));

static int set_angle(int degrees)
{
	uint32_t pulse = SERVO_MIN_NS +
		(uint32_t)((uint64_t)degrees *
			   (SERVO_MAX_NS - SERVO_MIN_NS) / 180U);

	return pwm_set(pwm_dev, SERVO_CHANNEL, SERVO_PERIOD_NS, pulse,
		       PWM_POLARITY_NORMAL);
}

int main(void)
{
	(void)boot_write_img_confirmed();

	printk("\r\n=== boot ===\r\n");

	if (!device_is_ready(pwm_dev)) {
		printk("Error: PWM device not ready\r\n");
		return -1;
	}
	printk("Servo control started on P1.0\r\n");

	while (1) {
		/* 0 deg -> 180 deg */
		for (int a = 0; a <= 180; a++) {
			if (set_angle(a) != 0) {
				printk("PWM error at %d deg\r\n", a);
			} else {
				printk("Angle: %d deg\r\n", a);
			}
			k_msleep(STEP_FWD_MS);
		}

		/* 180 deg -> 0 deg */
		for (int a = 180; a >= 0; a--) {
			if (set_angle(a) != 0) {
				printk("PWM error at %d deg\r\n", a);
			} else {
				printk("Angle: %d deg\r\n", a);
			}
			k_msleep(STEP_RET_MS);
		}
	}

	return 0;
}
