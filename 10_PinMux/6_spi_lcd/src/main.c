/*
 * XIAO nRF54LM20B -- Pin Multiplexing demo 6: SPI (GC9A01 240x240 round LCD)
 * ----------------------------------------------------------------------------
 * Port of the wiki "SPI" demo. Drives a GC9A01 240x240 round LCD over spi23
 * (SCK=P1.4/D8, MOSI=P1.6/D10, MISO=P1.5/D9) via the MIPI DBI SPI interface,
 * cycling solid colors then color bars in RGB565.
 *
 * 20B adaptations vs the 20A original:
 *   - self-confirm the mcuboot image at startup;
 *   - console NOT moved to &uart20 (wiki chosen console/rpc-uart/shell-uart
 *     on uart20 and the uart20_default/sleep pinctrl redefinition are dropped
 *     -- LOG output stays on USB CDC);
 *   - spi23 pinctrl (P1.4/P1.6/P1.5) is already defined in the board DTS, so
 *     the wiki's spi23_default/sleep redefinition is dropped; this overlay
 *     only enables spi23 and adds its cs-gpios;
 *   - banner text "nRF54LM20A" -> "nRF54LM20B".
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_pin_multiplexing/#spi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/dfu/mcuboot.h>
#include <string.h>
#include <stdlib.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gc9a01_demo, LOG_LEVEL);

/* GC9A01 Resolution */
#define LCD_W  240
#define LCD_H  240

/* RGB565 Color Definitions */
#define COLOR_BLACK   0x0000U
#define COLOR_WHITE   0xFFFFU
#define COLOR_RED     0xF800U
#define COLOR_GREEN   0x07E0U
#define COLOR_BLUE    0x001FU
#define COLOR_YELLOW  0xFFE0U
#define COLOR_CYAN    0x07FFU
#define COLOR_MAGENTA 0xF81FU
#define COLOR_ORANGE  0xFD20U

/* Fill entire screen using display_write.
 * buf: RGB565 pixel buffer, length = w * h * 2 bytes.
 */
static uint16_t fb[LCD_W * LCD_H];   /* ~115 KB, global to avoid stack overflow */

static void fill_screen(const struct device *dev, uint16_t color)
{
	/* RGB565 Big-endian (Default for GC9A01) */
	uint16_t be = (color >> 8) | (color << 8);
	for (int i = 0; i < LCD_W * LCD_H; i++)
	{
		fb[i] = be;
	}
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(fb),
		.width    = LCD_W,
		.height   = LCD_H,
		.pitch    = LCD_W,
	};
	display_write(dev, 0, 0, &desc, fb);
}

/* Scene 1: Solid color full-screen fill, cycle colors */
static void demo_solid_colors(const struct device *dev)
{
	static const uint16_t colors[] = {
		COLOR_RED, COLOR_GREEN, COLOR_BLUE,
		COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA,
	};
	static const char *names[] = {
		"RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA",
	};
	for (int i = 0; i < ARRAY_SIZE(colors); i++)
	{
		LOG_INF("Solid: %s", names[i]);
		fill_screen(dev, colors[i]);
		k_sleep(K_MSEC(600));
	}
}

/* Scene 2: Color vertical bars (6 bars, 40px width each) */
static void demo_color_bars(const struct device *dev)
{
	static const uint16_t bar_colors[] = {
		COLOR_RED, COLOR_ORANGE, COLOR_YELLOW,
		COLOR_GREEN, COLOR_BLUE, COLOR_MAGENTA,
	};
	static const int BAR_W = LCD_W / ARRAY_SIZE(bar_colors);  /* = 40 */

	for (int y = 0; y < LCD_H; y++)
	{
		for (int x = 0; x < LCD_W; x++)
		{
			uint16_t c = bar_colors[x / BAR_W];
			uint16_t be = (c >> 8) | (c << 8);
			fb[y * LCD_W + x] = be;
		}
	}
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(fb),
		.width    = LCD_W,
		.height   = LCD_H,
		.pitch    = LCD_W,
	};
	display_write(dev, 0, 0, &desc, fb);
	LOG_INF("Color bars");
	k_sleep(K_MSEC(2000));
}

int main(void)
{
	(void)boot_write_img_confirmed();

	LOG_INF("GC9A01 demo — XIAO nRF54LM20B");

	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(dev))
	{
		LOG_ERR("Display device not ready");
		return 0;
	}

	/* Set pixel format: GC9A01 uses RGB565 */
	if (display_set_pixel_format(dev, PIXEL_FORMAT_RGB_565) != 0)
	{
		LOG_ERR("Failed to set pixel format");
		return 0;
	}
	display_blanking_off(dev);
	LOG_INF("Display ready, starting demo loop");

	while (1)
	{
		demo_solid_colors(dev);   /* 6-color cycle ~3.6s */
		demo_color_bars(dev);     /* Color vertical bars ~2s */
	}

	return 0;
}
