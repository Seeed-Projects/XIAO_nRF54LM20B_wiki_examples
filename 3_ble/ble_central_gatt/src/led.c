/*
 * RGB LED status indicator implementation. See led.h for the state vocabulary.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec led_blue =
	GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);

static volatile enum led_state cur_state = LED_OFF;

/* Drive the three channels with logical on/off (1 = lit). */
static inline void rgb_set(int r, int g, int b)
{
	gpio_pin_set_dt(&led_red, r);
	gpio_pin_set_dt(&led_green, g);
	gpio_pin_set_dt(&led_blue, b);
}

/* Decode a state into a lit colour and a blink period (period == 0 => steady). */
static void decode(enum led_state s, int *r, int *g, int *b,
		   uint32_t *on_ms, uint32_t *period_ms)
{
	switch (s) {
	case LED_OFF:
		*r = 0; *g = 0; *b = 0;
		*on_ms = 0; *period_ms = 0;
		break;
	case LED_BLUE_SLOW:
		*r = 0; *g = 0; *b = 1;
		*on_ms = 250; *period_ms = 1000;
		break;
	case LED_BLUE_FAST:
		*r = 0; *g = 0; *b = 1;
		*on_ms = 80; *period_ms = 160;
		break;
	case LED_BLUE_ON:
		*r = 0; *g = 0; *b = 1;
		*on_ms = 0; *period_ms = 0;
		break;
	case LED_GREEN_ON:
		*r = 0; *g = 1; *b = 0;
		*on_ms = 0; *period_ms = 0;
		break;
	case LED_GREEN_FAST:
		*r = 0; *g = 1; *b = 0;
		*on_ms = 80; *period_ms = 160;
		break;
	case LED_RED_ON:
		*r = 1; *g = 0; *b = 0;
		*on_ms = 0; *period_ms = 0;
		break;
	case LED_RED_SLOW:
		*r = 1; *g = 0; *b = 0;
		*on_ms = 250; *period_ms = 1000;
		break;
	case LED_YELLOW_ON:
		*r = 1; *g = 1; *b = 0;
		*on_ms = 0; *period_ms = 0;
		break;
	default:
		*r = 0; *g = 0; *b = 0;
		*on_ms = 0; *period_ms = 0;
		break;
	}
}

static void led_thread(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	if (!device_is_ready(led_red.port) ||
	    !device_is_ready(led_green.port) ||
	    !device_is_ready(led_blue.port)) {
		return;
	}

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT);
	rgb_set(0, 0, 0);

	while (1) {
		int r, g, b;
		uint32_t on_ms, period_ms;

		decode(cur_state, &r, &g, &b, &on_ms, &period_ms);

		if (period_ms == 0U) {
			/* Steady state: hold the colour, re-check every 100 ms. */
			rgb_set(r, g, b);
			k_sleep(K_MSEC(100));
			continue;
		}

		rgb_set(r, g, b);
		k_msleep(on_ms);
		rgb_set(0, 0, 0);
		k_msleep(period_ms - on_ms);
	}
}

void led_init(void)
{
	cur_state = LED_OFF;
}

void led_set_state(enum led_state s)
{
	cur_state = s;
}

#define LED_THREAD_STACK 512
#define LED_THREAD_PRIO   0

K_THREAD_DEFINE(led_tid, LED_THREAD_STACK, led_thread, NULL, NULL, NULL,
		LED_THREAD_PRIO, 0, 0);
