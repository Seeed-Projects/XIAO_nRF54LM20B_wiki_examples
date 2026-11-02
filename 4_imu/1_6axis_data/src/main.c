/*
 * XIAO nRF54LM20B Sense -- IMU demo 1: 6-axis data stream (simplest case)
 * ----------------------------------------------------------------------------
 * The minimal way to drive the onboard 6-axis IMU (LSM6DS3TR-C):
 *   power on -> the driver brings the IMU up at 104 Hz / +/-4g / +/-2000dps
 *   (all set in prj.conf) -> main() just fetches and prints. No button, no
 *   menu, no setup steps: open the USB CDC serial port, shake the board,
 *   and watch ax/ay/az (g) and gx/gy/gz (dps) change in real time.
 *
 * The whole app is one loop:
 *     sensor_sample_fetch()          read a fresh sample
 *     sensor_channel_get(ACCEL_XYZ)  accelerometer, 3 values
 *     sensor_channel_get(GYRO_XYZ)   gyroscope, 3 values
 *
 * Only two app-side things to know:
 *   - app.overlay turns on the IMU power rail at boot (imu_vdd
 *     regulator-boot-on), so the driver's WHO_AM_I check passes;
 *   - ODR / full-scale live in prj.conf (CONFIG_LSM6DSL_ACCEL_ODR etc.),
 *     applied by the driver at init -- no runtime sensor_attr_set() here.
 *
 * The Zephyr sensor API returns SI units (m/s^2 and rad/s); we convert to
 * g and dps for output so the numbers match the datasheet intuition
 * (at rest az ~= +1.00 g, gyro ~= 0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <math.h>

/* IMU: LSM6DS3TR-C, board alias imu0 (compatible st,lsm6dsl) */
static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(imu0));

/* Print rate: 10 lines/s is readable and still shows every shake */
#define PRINT_INTERVAL_MS 100

/* unit conversions for readable output (sensor API is SI) */
#define MS2_TO_G   (1.0 / 9.80665)
#define RAD_TO_DPS 57.29578

int main(void)
{
	printk("\r\nXIAO nRF54LM20B 6-axis IMU (LSM6DS3TR-C, 104 Hz)\r\n");
	printk("ax/ay/az in g, gx/gy/gz in dps -- shake me\r\n\r\n");

	if (!device_is_ready(imu)) {
		printk("[IMU] not ready -- check power overlay / Sense variant\r\n");
		return 0;
	}

	struct sensor_value a[3], g[3];
	unsigned int n = 0;

	while (1) {
		if (sensor_sample_fetch(imu) == 0 &&
		    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, a) == 0 &&
		    sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, g) == 0) {

			double ax = sensor_value_to_double(&a[0]) * MS2_TO_G;
			double ay = sensor_value_to_double(&a[1]) * MS2_TO_G;
			double az = sensor_value_to_double(&a[2]) * MS2_TO_G;
			double amag = sqrt(ax * ax + ay * ay + az * az);

			printk("[%06u] ax=%6.2f ay=%6.2f az=%6.2f g  "
			       "gx=%7.1f gy=%7.1f gz=%7.1f dps  |a|=%5.2f g\r\n",
			       ++n, ax, ay, az,
			       sensor_value_to_double(&g[0]) * RAD_TO_DPS,
			       sensor_value_to_double(&g[1]) * RAD_TO_DPS,
			       sensor_value_to_double(&g[2]) * RAD_TO_DPS,
			       amag);
		}

		k_sleep(K_MSEC(PRINT_INTERVAL_MS));
	}
	return 0;
}
