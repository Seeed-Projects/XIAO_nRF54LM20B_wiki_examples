/*
 * XIAO nRF54LM20B -- Pin Multiplexing demo 5: I2C (SSD1306 OLED)
 * ----------------------------------------------------------------------------
 * Port of the wiki "I²C" demo. Drives a 128x64 SSD1306 OLED on i2c22
 * (SDA=P1.3/D4, SCL=P1.7/D5) via the character framebuffer, printing
 * "Hello XIAO" / "nRF54LM20B" centered.
 *
 * 20B adaptations vs the 20A original:
 *   - self-confirm the mcuboot image at startup;
 *   - \r\n line endings on LOG output (USB CDC console, untouched);
 *   - i2c22 pinctrl (P1.3/P1.7) is already in the board DTS, so the overlay
 *     only enables i2c22 and adds the ssd1306 node (no pinctrl redefinition);
 *   - line-2 text updated to "nRF54LM20B".
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#i2c
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/dfu/mcuboot.h>
#include <stdio.h>
#include <string.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main_app, LOG_LEVEL);

#define CFB_MAX_FONT_INDEX  42

/* Display content */
#define LINE1_TEXT  "Hello XIAO"
#define LINE2_TEXT  "nRF54LM20B"
#define TOTAL_LINES 2

static int display_init(const struct device **dev)
{
	*dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(*dev))
	{
		LOG_ERR("Device not ready");
		return -1;
	}
	if (display_set_pixel_format(*dev, PIXEL_FORMAT_MONO10) != 0)
	{
		if (display_set_pixel_format(*dev, PIXEL_FORMAT_MONO01) != 0)
		{
			LOG_ERR("Failed to set required pixel format");
			return -1;
		}
	}
	LOG_INF("Initialized display");
	return 0;
}

static int framebuffer_setup(const struct device *dev)
{
	if (cfb_framebuffer_init(dev))
	{
		LOG_ERR("Framebuffer initialization failed!");
		return -1;
	}
	cfb_framebuffer_clear(dev, false);
	display_blanking_off(dev);
	return 0;
}

static int select_font(const struct device *dev, uint8_t *font_width, uint8_t *font_height)
{
	int chosen_font_idx = -1;
	uint8_t current_font_width, current_font_height;

	for (int idx = 0; idx < CFB_MAX_FONT_INDEX; idx++)
	{
		if (cfb_get_font_size(dev, idx, &current_font_width, &current_font_height) != 0)
		{
			continue;
		}
		if (current_font_width == 0 || current_font_height == 0)
		{
			continue;
		}
		if (current_font_width == 8 && current_font_height == 8)
		{
			chosen_font_idx = idx;
			*font_width  = current_font_width;
			*font_height = current_font_height;
			cfb_framebuffer_set_font(dev, chosen_font_idx);
			LOG_INF("Selected 8x8 font idx: %d", chosen_font_idx);
			break;
		}
		if (chosen_font_idx == -1)
		{
			chosen_font_idx = idx;
			*font_width  = current_font_width;
			*font_height = current_font_height;
			cfb_framebuffer_set_font(dev, chosen_font_idx);
			LOG_INF("Fallback font idx: %d, size: %dx%d",
				chosen_font_idx, *font_width, *font_height);
		}
	}

	if (chosen_font_idx == -1)
	{
		LOG_ERR("No suitable font found!");
		return -1;
	}
	return 0;
}

static void print_text_centered(const struct device *dev,
				const char *text, int line_index, int total_lines,
				uint8_t font_width, uint8_t font_height,
				uint16_t x_res, uint16_t y_res)
{
	int text_len = (int)strlen(text);
	int pixel_x = (x_res - text_len * font_width) / 2;
	int block_height = total_lines * font_height;
	int pixel_y = (y_res - block_height) / 2 + line_index * font_height;

	if (pixel_x < 0) pixel_x = 0;
	if (pixel_y < 0) pixel_y = 0;

	if (cfb_print(dev, text, pixel_x, pixel_y))
	{
		LOG_ERR("Failed to print \"%s\" at (%d, %d)", text, pixel_x, pixel_y);
	}
}

int main(void)
{
	(void)boot_write_img_confirmed();

	const struct device *dev;
	uint8_t font_width  = 0;
	uint8_t font_height = 0;

	if (display_init(&dev)                          != 0) return 0;
	if (framebuffer_setup(dev)                      != 0) return 0;
	if (select_font(dev, &font_width, &font_height) != 0) return 0;

	uint16_t x_res = cfb_get_display_parameter(dev, CFB_DISPLAY_WIDTH);
	uint16_t y_res = cfb_get_display_parameter(dev, CFB_DISPLAY_HEIGHT);
	LOG_INF("Display resolution: %dx%d", x_res, y_res);

	cfb_set_kerning(dev, 0);

	while (1)
	{
		cfb_framebuffer_clear(dev, false);
		print_text_centered(dev, LINE1_TEXT, 0, TOTAL_LINES,
				    font_width, font_height, x_res, y_res);
		print_text_centered(dev, LINE2_TEXT, 1, TOTAL_LINES,
				    font_width, font_height, x_res, y_res);
		cfb_framebuffer_finalize(dev);
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
