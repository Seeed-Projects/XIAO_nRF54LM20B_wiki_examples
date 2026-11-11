/*
 * XIAO nRF54LM20B -- Pin Multiplexing demo 3: Analog (ADC on P1.0 / AIN0)
 * ----------------------------------------------------------------------------
 * Port of the wiki "Analog" demo. Reads SAADC channel 0 (P1.0 / D0 = AIN0)
 * and prints the raw 16-bit sample every 500 ms.
 *
 * 20B adaptations vs the 20A original:
 *   - self-confirm the mcuboot image at startup;
 *   - \r\n line endings over the default USB CDC console;
 *   - console NOT moved to &uart20 (wiki "chosen { console=&uart20 }" and
 *     the uart20 pinctrl block are dropped -- this demo does not use uart20);
 *   - the ADC spec is still declared under /zephyr,user via io-channels
 *     = <&adc 0>, exactly as the wiki original. Channel 0 maps to AIN0,
 *     which on nRF54LM20 is P1.0 / D0 (same SoC dtsi on 20A and 20B).
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#analog
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

static const struct adc_dt_spec adc_ch =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int main(void)
{
	(void)boot_write_img_confirmed();

	int ret;

	if (!adc_is_ready_dt(&adc_ch)) {
		printk("ADC not ready\r\n");
		return -1;
	}

	ret = adc_channel_setup_dt(&adc_ch);
	if (ret < 0) {
		printk("ADC channel setup failed: %d\r\n", ret);
		return ret;
	}

	int16_t buf;
	struct adc_sequence seq = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	adc_sequence_init_dt(&adc_ch, &seq);

	printk("ADC reading on P1.0 / AIN0 every 500 ms...\r\n");

	while (1) {
		ret = adc_read_dt(&adc_ch, &seq);
		if (ret < 0) {
			printk("ADC read error: %d\r\n", ret);
		} else {
			printk("ADC raw: %d\r\n", buf);
		}

		k_msleep(500);
	}

	return 0;
}
